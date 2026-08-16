#include "display_ui.h"
#include "config.h"
#include <time.h>

DisplayUI::~DisplayUI() {
    if (_canvas) {
        _canvas->deleteSprite();
        delete _canvas;
        _canvas = nullptr;
    }
}

void DisplayUI::begin() {
    auto cfg = M5.config();
    M5.begin(cfg);
    
    M5.Display.wakeup();
    M5.Display.setBrightness(128);
    M5.Display.setRotation(_rotation);

    if (_canvas == nullptr) {
        _canvas = new M5Canvas(&M5.Display);
        _canvas->setColorDepth(16);
        _canvas->createSprite(M5.Display.width(), M5.Display.height());
    }

    drawStatusMessage("System Starting...");
}

void DisplayUI::toggleRotation() {
    _rotation = (_rotation == 1) ? 3 : 1;
    M5.Display.setRotation(_rotation);
    if (_canvas != nullptr) {
        _canvas->deleteSprite();
        _canvas->createSprite(M5.Display.width(), M5.Display.height());
    }
}

void DisplayUI::toggleBeep() {
    _beepEnabled = !_beepEnabled;
}

void DisplayUI::showIpPopup(uint32_t durationMs) {
    _popupUntilMs = millis() + durationMs;
}

uint16_t DisplayUI::getPowerColor(int32_t watt) {
    if (_canvas == nullptr) return 0;
    int limitPhase = AMPERE_LIMIT_WATT / 2;
    float ratio = (float)watt / (float)limitPhase;

    if (ratio >= 0.90f) return _canvas->color565(239, 68, 68);   // 赤 (90%以上)
    if (ratio >= 0.75f) return _canvas->color565(249, 115, 22);  // オレンジ (75%以上)
    if (ratio >= 0.55f) return _canvas->color565(234, 179, 8);   // 黄 (55%以上)
    return _canvas->color565(52, 211, 153);                      // 緑 (通常)
}

void DisplayUI::drawStatusMessage(const char* msg) {
    if (_canvas == nullptr) return;

    _canvas->fillScreen(TFT_BLACK);
    _canvas->setTextColor(TFT_WHITE);
    _canvas->setTextSize(1.2);
    _canvas->setTextDatum(MC_DATUM);
    _canvas->drawString("M5B-ROUTE POWER", _canvas->width() / 2, 25);
    _canvas->setTextColor(TFT_CYAN);
    _canvas->drawString(msg, _canvas->width() / 2, _canvas->height() / 2 + 10);
    _canvas->pushSprite(0, 0);
}

void DisplayUI::update(BrouteState state, const PowerData& data, double monthlyKWh, const char* periodStr, bool wifiConnected, const char* ipStr) {
    if (_canvas == nullptr) return;

    uint32_t now = millis();
    if (now - _lastRenderMs < 80) return;
    _lastRenderMs = now;

    int w = _canvas->width();  // 240
    int h = _canvas->height(); // 135

    bool isPopupActive = (now < _popupUntilMs);

    // 1. ポップアップ表示モード（BtnA 押下時：画面全体をすっきりオーバーレイ）
    if (isPopupActive) {
        _canvas->fillScreen(_canvas->color565(15, 23, 42)); // 背景一括クリア
        _canvas->drawRoundRect(4, 4, w - 8, h - 8, 8, _canvas->color565(56, 189, 248)); // シアン枠線

        time_t nowTime;
        time(&nowTime);
        struct tm timeinfo;
        char timeStr[16];
        char dateStr[24];

        if (localtime_r(&nowTime, &timeinfo) && timeinfo.tm_year >= (2020 - 1900)) {
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            const char* wdayStr[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
            snprintf(dateStr, sizeof(dateStr), "%02d/%02d (%s)", 
                     timeinfo.tm_mon + 1, timeinfo.tm_mday, wdayStr[timeinfo.tm_wday]);
        } else {
            snprintf(timeStr, sizeof(timeStr), "--:--:--");
            snprintf(dateStr, sizeof(dateStr), "Syncing NTP...");
        }

        // ① 上部：左に日付・曜日、右に現在時刻
        _canvas->setTextDatum(TL_DATUM);
        _canvas->setTextColor(_canvas->color565(148, 163, 184));
        _canvas->setTextSize(1.0);
        _canvas->drawString(dateStr, 14, 12);

        _canvas->setTextDatum(TR_DATUM);
        _canvas->setTextColor(_canvas->color565(56, 189, 248));
        _canvas->setTextSize(1.0);
        _canvas->drawString(timeStr, w - 14, 12);

        // ② 中央：今月の積算電力量（特大数字）
        _canvas->setTextDatum(MC_DATUM);
        _canvas->setTextColor(TFT_WHITE);
        _canvas->setTextSize(3.2);
        char kwhMainStr[24];
        snprintf(kwhMainStr, sizeof(kwhMainStr), "%.1f", monthlyKWh);
        
        int numW = _canvas->textWidth(kwhMainStr);
        int centerX = w / 2 - 12;
        _canvas->drawString(kwhMainStr, centerX, 52);

        // 単位「kWh」
        _canvas->setTextDatum(BL_DATUM);
        _canvas->setTextColor(_canvas->color565(167, 139, 250));
        _canvas->setTextSize(1.5);
        _canvas->drawString("kWh", centerX + (numW / 2) + 4, 62);

        // ③ 中央下：期間
        _canvas->setTextDatum(MC_DATUM);
        _canvas->setTextColor(_canvas->color565(148, 163, 184));
        _canvas->setTextSize(1.0);
        char periodLabel[32];
        snprintf(periodLabel, sizeof(periodLabel), "Date range: %s", periodStr ? periodStr : "23rd - 22nd");
        _canvas->drawString(periodLabel, w / 2, 84);

        // ④ 最下部：Web URL（ベゼル枠に被らない安全なY座標）
        _canvas->setTextDatum(MC_DATUM);
        _canvas->setTextColor(_canvas->color565(52, 211, 153));
        _canvas->setTextSize(1.0);
        char urlStr[48];
        snprintf(urlStr, sizeof(urlStr), "http://%s", (wifiConnected && ipStr) ? ipStr : "Connecting...");
        _canvas->drawString(urlStr, w / 2, 110);

        _canvas->pushSprite(0, 0);
        return;
    }

    // 2. メイン通常表示モード
    float loadRatio = (data.validInstantaneous) ? ((float)data.instantaneousWatt / (float)AMPERE_LIMIT_WATT) : 0.0f;
    int loadPercent = (int)(loadRatio * 100.0f);

    int32_t wattR = data.validCurrent ? data.wattRPhase : (data.instantaneousWatt / 2);
    int32_t wattT = data.validCurrent ? data.wattTPhase : (data.instantaneousWatt - wattR);

    int phaseLimit = AMPERE_LIMIT_WATT / 2;
    bool phaseRHigh = (wattR >= phaseLimit * 0.85f);
    bool phaseTHigh = (wattT >= phaseLimit * 0.85f);

    bool isDanger  = (loadRatio >= 0.90f || wattR >= phaseLimit * 0.95f || wattT >= phaseLimit * 0.95f);
    bool isWarning = (!isDanger && (loadRatio >= 0.78f || phaseRHigh || phaseTHigh));
    bool isCaution = (!isDanger && !isWarning && (loadRatio >= 0.65f));

    if (isDanger && _beepEnabled && (now - _lastBeepMs > 1000)) {
        _lastBeepMs = now;
        M5.Speaker.tone(2800, 250);
    } else if (isWarning && _beepEnabled && (now - _lastBeepMs > 2500)) {
        _lastBeepMs = now;
        M5.Speaker.tone(2000, 150);
    }

    if (isDanger && (now / 350) % 2 == 0) {
        _canvas->fillScreen(_canvas->color565(140, 15, 15));
    } else {
        _canvas->fillScreen(TFT_BLACK);
    }

    if (state == BrouteState::CONNECTED && data.validInstantaneous) {
        int splitX = 132;

        // 全体電力
        uint16_t mainColor = TFT_WHITE;
        if (isDanger)       mainColor = _canvas->color565(255, 70, 70);
        else if (isWarning) mainColor = _canvas->color565(251, 146, 60);
        else if (isCaution) mainColor = _canvas->color565(250, 204, 21);

        _canvas->setTextColor(mainColor);
        _canvas->setTextSize(3.8);
        _canvas->setTextDatum(MR_DATUM);
        
        char mainWattStr[16];
        snprintf(mainWattStr, sizeof(mainWattStr), "%d", data.instantaneousWatt);
        
        int textW = _canvas->textWidth(mainWattStr);
        int startX = (splitX / 2) + (textW / 2) - 10;
        
        _canvas->drawString(mainWattStr, startX, h / 2 - 4);

        _canvas->setTextDatum(BL_DATUM);
        uint16_t unitColor = isDanger ? _canvas->color565(255, 80, 80) : 
                             isWarning ? _canvas->color565(251, 146, 60) : _canvas->color565(52, 211, 153);
        _canvas->setTextColor(unitColor);
        _canvas->setTextSize(2.2);
        _canvas->drawString("w", startX + 4, h / 2 + 16);

        // 警告バッジ
        if (isDanger) {
            _canvas->setTextDatum(TC_DATUM);
            _canvas->setTextColor(TFT_WHITE, _canvas->color565(220, 38, 38));
            _canvas->setTextSize(1.0);
            char warnStr[32];
            snprintf(warnStr, sizeof(warnStr), " !! OVER %d%% !! ", loadPercent);
            _canvas->drawString(warnStr, splitX / 2, 4);
        } else if (isWarning) {
            _canvas->setTextDatum(TC_DATUM);
            _canvas->setTextColor(TFT_BLACK, _canvas->color565(249, 115, 22));
            _canvas->setTextSize(1.0);
            char warnStr[32];
            snprintf(warnStr, sizeof(warnStr), " ! WARN %d%% ! ", loadPercent);
            _canvas->drawString(warnStr, splitX / 2, 4);
        } else if (isCaution) {
            _canvas->setTextDatum(TC_DATUM);
            _canvas->setTextColor(TFT_BLACK, _canvas->color565(234, 179, 8));
            _canvas->setTextSize(1.0);
            char warnStr[32];
            snprintf(warnStr, sizeof(warnStr), " CAUTION %d%% ", loadPercent);
            _canvas->drawString(warnStr, splitX / 2, 4);
        }

        // セパレータ
        _canvas->drawFastVLine(splitX, 8, h - 26, _canvas->color565(50, 60, 80));

        // R相 / T相 (右エリア)
        int rightHalfH = (h - 18) / 2;

        // R相
        uint16_t colorR = getPowerColor(wattR);
        _canvas->setTextDatum(ML_DATUM);
        _canvas->setTextColor(phaseRHigh ? _canvas->color565(239, 68, 68) : _canvas->color565(180, 200, 240));
        _canvas->setTextSize(2.0);
        _canvas->drawString("R", splitX + 10, rightHalfH / 2);

        _canvas->setTextDatum(MR_DATUM);
        _canvas->setTextColor(colorR);
        _canvas->setTextSize(2.8);
        char rStr[16];
        snprintf(rStr, sizeof(rStr), "%d", wattR);
        _canvas->drawString(rStr, w - 24, rightHalfH / 2);

        _canvas->setTextDatum(BL_DATUM);
        _canvas->setTextSize(1.5);
        _canvas->drawString("w", w - 20, rightHalfH / 2 + 10);

        _canvas->drawFastHLine(splitX + 6, rightHalfH, w - splitX - 12, _canvas->color565(50, 60, 80));

        // T相
        uint16_t colorT = getPowerColor(wattT);
        _canvas->setTextDatum(ML_DATUM);
        _canvas->setTextColor(phaseTHigh ? _canvas->color565(239, 68, 68) : _canvas->color565(180, 200, 240));
        _canvas->setTextSize(2.0);
        _canvas->drawString("T", splitX + 10, rightHalfH + (rightHalfH / 2));

        _canvas->setTextDatum(MR_DATUM);
        _canvas->setTextColor(colorT);
        _canvas->setTextSize(2.8);
        char tStr[16];
        snprintf(tStr, sizeof(tStr), "%d", wattT);
        _canvas->drawString(tStr, w - 24, rightHalfH + (rightHalfH / 2));

        _canvas->setTextDatum(BL_DATUM);
        _canvas->setTextSize(1.5);
        _canvas->drawString("w", w - 20, rightHalfH + (rightHalfH / 2) + 10);

        // 最下部 (IP & 今月積算)
        _canvas->setTextDatum(BL_DATUM);
        _canvas->setTextColor(_canvas->color565(110, 130, 160));
        _canvas->setTextSize(1.0);
        char ipBuf[32];
        snprintf(ipBuf, sizeof(ipBuf), "IP: %s", (wifiConnected && ipStr) ? ipStr : "---");
        _canvas->drawString(ipBuf, 6, h - 2);

        _canvas->setTextDatum(BR_DATUM);
        _canvas->setTextColor(_canvas->color565(167, 139, 250));
        char monthBuf[32];
        snprintf(monthBuf, sizeof(monthBuf), "Month: %.1fkWh", monthlyKWh);
        _canvas->drawString(monthBuf, w - 6, h - 2);

    } else {
        _canvas->setTextDatum(MC_DATUM);
        _canvas->setTextColor(_canvas->color565(241, 196, 15));
        _canvas->setTextSize(1.3);
        
        const char* statusStr = "Connecting B-Route...";
        if (state == BrouteState::SCANNING) statusStr = "Scanning Smart Meter...";
        else if (state == BrouteState::JOINING) statusStr = "PANA Authenticating...";
        else if (state == BrouteState::FAILED) statusStr = "Auth Failed (Retrying)";

        _canvas->drawString(statusStr, w / 2, h / 2 - 8);

        _canvas->setTextDatum(BC_DATUM);
        _canvas->setTextColor(_canvas->color565(140, 160, 200));
        _canvas->setTextSize(1.0);
        char ipBuf[40];
        snprintf(ipBuf, sizeof(ipBuf), "Web: http://%s", (wifiConnected && ipStr) ? ipStr : "Connecting WiFi...");
        _canvas->drawString(ipBuf, w / 2, h - 4);
    }

    _canvas->pushSprite(0, 0);
}
