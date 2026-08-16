#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "wisun_broute.h"
#include "power_history.h"
#include "billing_manager.h"
#include "display_ui.h"
#include "web_server.h"

HardwareSerial SerialWisun(2);
WiSunBroute* broute = nullptr;
PowerHistory* powerHistory = nullptr;
BillingManager* billingManager = nullptr;
DisplayUI* displayUI = nullptr;
PowerWebServer* webServer = nullptr;

bool wifiConnected = false;
String ipAddressStr = "0.0.0.0";
uint32_t lastFetchMs = 0;
uint32_t lastWiFiCheckMs = 0;

void setupWiFi() {
    Serial.println("\n--- Connecting WiFi ---");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    if (displayUI) displayUI->drawStatusMessage("Connecting WiFi...");

    // ルーター接続待ち (最大15秒待機)
    uint32_t startMs = millis();
    int count = 0;
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs < 15000)) {
        delay(300);
        M5.update();
        count++;
        if (displayUI && count % 3 == 0) {
            char waitMsg[32];
            snprintf(waitMsg, sizeof(waitMsg), "Connecting WiFi (%ds)...", (int)((millis() - startMs) / 1000));
            displayUI->drawStatusMessage(waitMsg);
        }
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        ipAddressStr = WiFi.localIP().toString();
        Serial.println("\nWiFi Connected! IP: " + ipAddressStr);

        // NTP時刻同期を明示的に開始
        if (displayUI) displayUI->drawStatusMessage("Syncing NTP Time...");
        configTime(9 * 3600, 0, "ntp.nict.jp", "time.google.com");
        
        // 時刻同期完了を最大3秒待機
        time_t nowTime;
        struct tm timeinfo;
        uint32_t ntpStart = millis();
        while (millis() - ntpStart < 3000) {
            time(&nowTime);
            if (localtime_r(&nowTime, &timeinfo) && timeinfo.tm_year >= (2020 - 1900)) {
                Serial.printf("NTP Time Synced: %04d/%02d/%02d %02d:%02d:%02d\n",
                              timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                              timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                break;
            }
            delay(150);
        }
    } else {
        Serial.println("\nWiFi Failed. Starting AP Mode...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        wifiConnected = true;
        ipAddressStr = WiFi.softAPIP().toString();
    }
}

void setup() {
    // 1. デバッグシリアル
    Serial.begin(115200);
    delay(100);
    Serial.println("==================================================");
    Serial.println(" M5StickC Plus + Wi-SUN HAT Rev0.1 (TX:0, RX:36) ");
    Serial.println("==================================================");

    // 2. DisplayUI の動的初期化
    displayUI = new DisplayUI();
    displayUI->begin();
    displayUI->drawStatusMessage("System Starting...");

    // 3. 24時間電力履歴管理
    powerHistory = new PowerHistory();
    powerHistory->begin();

    // 4. 今月検針期間マネージャー初期化
    billingManager = new BillingManager();
    billingManager->begin();

    // 5. Wi-SUNドライバ動的生成
    broute = new WiSunBroute(SerialWisun);

    // 6. Web サーバー動的生成
    webServer = new PowerWebServer(*broute, *powerHistory, *billingManager);

    // 7. Wi-Fi 接続 (15秒待機 & NTP同期)
    setupWiFi();

    // 8. Web サーバー開始
    webServer->begin();

    // 9. BP35A1 Wi-SUN モジュール UART 初期化
    displayUI->drawStatusMessage("Init Wi-SUN...");
    Serial.printf("Initializing BP35A1 UART (TX: %d, RX: %d)...\n", WISUN_TX_PIN, WISUN_RX_PIN);
    broute->begin(WISUN_RX_PIN, WISUN_TX_PIN, WISUN_BAUD);
    delay(200);

    displayUI->drawStatusMessage("Connecting B-Route...");
}

void loop() {
    M5.update();

    if (broute == nullptr || displayUI == nullptr || powerHistory == nullptr || billingManager == nullptr) return;

    // 定期的な Wi-Fi 接続状態の監視と自動復帰
    uint32_t now = millis();
    if (now - lastWiFiCheckMs > 10000) {
        lastWiFiCheckMs = now;
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            ipAddressStr = WiFi.localIP().toString();
        }
    }

    // BP35A1 ステートマシン更新
    broute->update();

    const PowerData& latestData = broute->getLatestData();

    // 24時間履歴に最新データを記録
    powerHistory->addSample(latestData);

    // 今月(23日〜翌22日)の積算電力量を更新・取得
    double totalKWh = latestData.validCumulative ? latestData.cumulativeKWh : 0.0;
    double monthlyKWh = billingManager->updateAndGetMonthlyKWh(totalKWh);
    String periodStr = billingManager->getPeriodString();

    // 5秒毎の定期取得
    if (broute->getState() == BrouteState::CONNECTED) {
        if (now - lastFetchMs > (FETCH_INTERVAL_SEC * 1000)) {
            lastFetchMs = now;
            broute->requestPowerData();
        }
    }

    // BtnA: 手動リフレッシュ & IP・今月積算オーバーレイ表示
    if (M5.BtnA.wasPressed()) {
        Serial.println("[BtnA] Refresh Request & Show IP Popup");
        displayUI->showIpPopup(4000);
        if (broute->getState() == BrouteState::CONNECTED) {
            broute->requestPowerData();
            lastFetchMs = now;
        }
    }

    // BtnB: 画面180度反転
    if (M5.BtnB.wasPressed()) {
        Serial.println("[BtnB] Rotation Toggled");
        displayUI->toggleRotation();
    }

    // 画面更新
    displayUI->update(broute->getState(), latestData, monthlyKWh, periodStr.c_str(), wifiConnected, ipAddressStr.c_str());

    delay(10);
}
