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

    if (ratio >= 0.95f) return _canvas->color565(239, 68, 68);   // 赤 (95%以上 危険)
    if (ratio >= 0.85f) return _canvas->color565(249, 115, 22);  // オレンジ (85%以上 警告)
    if (ratio >= 0.75f) return _canvas->color565(250, 204, 21);  // 黄 (75%以上 注意)
    return _canvas->color565(52, 211, 153);                      // 緑 (通常・安全)
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

    // 1. ポップアップ表示モード（BtnA 押下時）
    if (isPopupActive) {
        _canvas->fillScreen(_canvas->color565(15, 23, 42));
        _canvas->drawRoundRect(4, 4, w - 8, h - 8, 8, _canvas->color565(56, 189, 248));

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

        // ④ 最下部：Web URL
        _canvas->setTextDatum(MC_DATUM);
        _canvas->setTextColor(_canvas->color565(52, 211, 153));
        _canvas->setTextSize(1.0);
        char urlStr[48];
        snprintf(urlStr, sizeof(urlStr), "http://%s", (wifiConnected && ipStr) ? ipStr : "Connecting...");
        _canvas->drawString(urlStr, w / 2, 110);

        _canvas->pushSprite(0, 0);
        return;
    }

    // 2. メイン通常表示モード (相別強調アラート設計)
    float loadRatio = (data.validInstantaneous) ? ((float)data.instantaneousWatt / (float)AMPERE_LIMIT_WATT) : 0.0f;
    int loadPercent = (int)(loadRatio * 100.0f);

    int32_t wattR = data.validCurrent ? data.wattRPhase : (data.instantaneousWatt / 2);
    int32_t wattT = data.validCurrent ? data.wattTPhase : (data.instantaneousWatt - wattR);

    int phaseLimit = AMPERE_LIMIT_WATT / 2;
    float ratioR = (float)wattR / (float)phaseLimit;
    float ratioT = (float)wattT / (float)phaseLimit;

    // 危険レベル判定
    bool isDangerR  = (ratioR >= 0.95f);
    bool isWarningR = (!isDangerR && ratioR >= 0.85f);
    bool isCautionR = (!isDangerR && !isWarningR && ratioR >= 0.75f);

    bool isDangerT  = (ratioT >= 0.95f);
    bool isWarningT = (!isDangerT && ratioT >= 0.85f);
    bool isCautionT = (!isDangerT && !isWarningT && ratioT >= 0.75f);

    bool isDangerTotal  = (loadRatio >= 0.95f);
    bool isWarningTotal = (!isDangerTotal && loadRatio >= 0.85f);
    bool isCautionTotal = (!isDangerTotal && !isWarningTotal && loadRatio >= 0.75f);

    bool isDanger  = (isDangerTotal || isDangerR || isDangerT);
    bool isWarning = (!isDanger && (isWarningTotal || isWarningR || isWarningT));
    bool isCaution = (!isDanger && !isWarning && (isCautionTotal || isCautionR || isCautionT));

    // ブザー制御
    if (isDanger && _beepEnabled && (now - _lastBeepMs > 1000)) {
        _lastBeepMs = now;
        M5.Speaker.tone(2800, 250);
    } else if (isWarning && _beepEnabled && (now - _lastBeepMs > 3000)) {
        _lastBeepMs = now;
        M5.Speaker.tone(2000, 120);
    }

    // 背景描画（全体が危険なときは全体赤点滅）
    if (isDangerTotal && (now / 350) % 2 == 0) {
        _canvas->fillScreen(_canvas->color565(120, 15, 15));
    } else {
        _canvas->fillScreen(TFT_BLACK);
    }

    if (state == BrouteState::CONNECTED && data.validInstantaneous) {
        int splitX = 126;
        bool hasAlert = (isDanger || isWarning || isCaution);

        // ----------------------------------------------------
        // A. 外枠カラーフレーム
        // ----------------------------------------------------
        if (isDanger) {
            _canvas->drawRect(0, 0, w, h, _canvas->color565(239, 68, 68));
            _canvas->drawRect(1, 1, w - 2, h - 2, _canvas->color565(239, 68, 68));
        } else if (isWarning) {
            _canvas->drawRect(0, 0, w, h, _canvas->color565(249, 115, 22));
            _canvas->drawRect(1, 1, w - 2, h - 2, _canvas->color565(249, 115, 22));
        } else if (isCaution) {
            _canvas->drawRect(0, 0, w, h, _canvas->color565(250, 204, 21));
            _canvas->drawRect(1, 1, w - 2, h - 2, _canvas->color565(250, 204, 21));
        }

        // ----------------------------------------------------
        // B. 上部フル幅警告バナー帯
        // ----------------------------------------------------
        int topOffset = 0;
        if (hasAlert) {
            topOffset = 18;
            uint16_t bannerBg = isDanger ? _canvas->color565(220, 38, 38) :
                                isWarning ? _canvas->color565(234, 88, 12) :
                                _canvas->color565(202, 138, 4);
            
            uint16_t bannerText = (isCaution && !isDanger && !isWarning) ? TFT_BLACK : TFT_WHITE;

            _canvas->fillRect(2, 2, w - 4, 18, bannerBg);
            _canvas->setTextDatum(MC_DATUM);
            _canvas->setTextColor(bannerText);
            _canvas->setTextSize(1.0);

            char bannerMsg[48];
            if (isDangerR) {
                snprintf(bannerMsg, sizeof(bannerMsg), "!! DANGER: R-PHASE %.1fA OVER !!", (data.validCurrent ? data.currentRPhase : (wattR/100.0f)));
            } else if (isDangerT) {
                snprintf(bannerMsg, sizeof(bannerMsg), "!! DANGER: T-PHASE %.1fA OVER !!", (data.validCurrent ? data.currentTPhase : (wattT/100.0f)));
            } else if (isDangerTotal) {
                snprintf(bannerMsg, sizeof(bannerMsg), "!! DANGER: %dW / %dA (%d%%) !!", data.instantaneousWatt, AMPERE_LIMIT_WATT/100, loadPercent);
            } else if (isWarningR) {
                snprintf(bannerMsg, sizeof(bannerMsg), "! WARN: R-PHASE %.1fA HIGH !", (data.validCurrent ? data.currentRPhase : (wattR/100.0f)));
            } else if (isWarningT) {
                snprintf(bannerMsg, sizeof(bannerMsg), "! WARN: T-PHASE %.1fA HIGH !", (data.validCurrent ? data.currentTPhase : (wattT/100.0f)));
            } else if (isWarningTotal) {
                snprintf(bannerMsg, sizeof(bannerMsg), "! WARNING: %dW / %dA (%d%%) !", data.instantaneousWatt, AMPERE_LIMIT_WATT/100, loadPercent);
            } else if (isCautionR) {
                snprintf(bannerMsg, sizeof(bannerMsg), "CAUTION: R-PHASE %.1fA (75%%)", (data.validCurrent ? data.currentRPhase : (wattR/100.0f)));
            } else if (isCautionT) {
                snprintf(bannerMsg, sizeof(bannerMsg), "CAUTION: T-PHASE %.1fA (75%%)", (data.validCurrent ? data.currentTPhase : (wattT/100.0f)));
            } else {
                snprintf(bannerMsg, sizeof(bannerMsg), "CAUTION: %dW / %dA (%d%%)", data.instantaneousWatt, AMPERE_LIMIT_WATT/100, loadPercent);
            }
            _canvas->drawString(bannerMsg, w / 2, 11);
        }

        // ----------------------------------------------------
        // C. 全体電力 (左エリア)
        // ----------------------------------------------------
        uint16_t mainColor = TFT_WHITE;
        if (isDangerTotal)       mainColor = _canvas->color565(255, 70, 70);
        else if (isWarningTotal) mainColor = _canvas->color565(251, 146, 60);
        else if (isCautionTotal) mainColor = _canvas->color565(250, 204, 21);

        _canvas->setTextColor(mainColor);
        _canvas->setTextSize(3.8);
        _canvas->setTextDatum(MR_DATUM);
        
        char mainWattStr[16];
        snprintf(mainWattStr, sizeof(mainWattStr), "%d", data.instantaneousWatt);
        
        int textW = _canvas->textWidth(mainWattStr);
        int startX = (splitX / 2) + (textW / 2) - 8;
        int centerY = (h + topOffset - 16) / 2;
        
        _canvas->drawString(mainWattStr, startX, centerY);

        // 単位「W」
        _canvas->setTextDatum(BL_DATUM);
        uint16_t unitColor = isDangerTotal ? _canvas->color565(255, 80, 80) : 
                             isWarningTotal ? _canvas->color565(251, 146, 60) : _canvas->color565(52, 211, 153);
        _canvas->setTextColor(unitColor);
        _canvas->setTextSize(2.2);
        _canvas->drawString("w", startX + 4, centerY + 20);

        // ----------------------------------------------------
        // D. セパレータ (縦線)
        // ----------------------------------------------------
        int sepTop = hasAlert ? (topOffset + 4) : 8;
        _canvas->drawFastVLine(splitX, sepTop, h - 22 - sepTop, _canvas->color565(50, 60, 80));

        // ----------------------------------------------------
        // E. R相 / T相 (右エリア: 強調警告カードボックス化)
        // ----------------------------------------------------
        int cardX = splitX + 5;
        int cardW = w - cardX - 5;
        int availableH = h - topOffset - 24;
        int cardH = (availableH - 4) / 2;
        int cardY_R = topOffset + 3;
        int cardY_T = cardY_R + cardH + 4;

        // --- R相カードの描画 ---
        uint16_t bgR = _canvas->color565(18, 24, 38);       // デフォルト背景
        uint16_t borderR = _canvas->color565(40, 50, 70);   // デフォルト枠線
        uint16_t textColR = getPowerColor(wattR);
        uint16_t tagBgR = _canvas->color565(30, 41, 59);
        uint16_t tagTextR = _canvas->color565(56, 189, 248); // 水色

        if (isDangerR) {
            // 危険: 赤点滅または鮮烈な赤背景
            bool blink = ((now / 350) % 2 == 0);
            bgR = blink ? _canvas->color565(220, 38, 38) : _canvas->color565(120, 15, 15);
            borderR = _canvas->color565(255, 100, 100);
            textColR = TFT_WHITE;
            tagBgR = TFT_BLACK;
            tagTextR = _canvas->color565(255, 100, 100);
        } else if (isWarningR) {
            // 警告: 鮮やかなオレンジ枠 ＆ 濃いオレンジ背景
            bgR = _canvas->color565(124, 45, 18);
            borderR = _canvas->color565(249, 115, 22);
            textColR = _canvas->color565(255, 237, 213);
            tagBgR = _canvas->color565(234, 88, 12);
            tagTextR = TFT_WHITE;
        } else if (isCautionR) {
            // 注意: 黄色枠
            bgR = _canvas->color565(66, 48, 10);
            borderR = _canvas->color565(234, 179, 8);
            textColR = _canvas->color565(254, 240, 138);
            tagBgR = _canvas->color565(202, 138, 4);
            tagTextR = TFT_BLACK;
        }

        // R相カード背景＆枠
        _canvas->fillRoundRect(cardX, cardY_R, cardW, cardH, 4, bgR);
        _canvas->drawRoundRect(cardX, cardY_R, cardW, cardH, 4, borderR);
        if (isDangerR || isWarningR) {
            _canvas->drawRoundRect(cardX + 1, cardY_R + 1, cardW - 2, cardH - 2, 4, borderR); // 2重枠で太く
        }

        // R相タグ [ R ]
        _canvas->fillRoundRect(cardX + 4, cardY_R + (cardH / 2) - 10, 18, 20, 3, tagBgR);
        _canvas->setTextDatum(MC_DATUM);
        _canvas->setTextColor(tagTextR);
        _canvas->setTextSize(1.6);
        _canvas->drawString("R", cardX + 13, cardY_R + (cardH / 2));

        // R相数字
        _canvas->setTextDatum(MR_DATUM);
        _canvas->setTextColor(textColR);
        _canvas->setTextSize(2.4);
        char rStr[16];
        snprintf(rStr, sizeof(rStr), "%d", wattR);
        _canvas->drawString(rStr, cardX + cardW - 18, cardY_R + (cardH / 2) - 1);

        // 単位 W
        _canvas->setTextDatum(BL_DATUM);
        _canvas->setTextColor((isDangerR || isWarningR) ? textColR : _canvas->color565(148, 163, 184));
        _canvas->setTextSize(1.2);
        _canvas->drawString("w", cardX + cardW - 15, cardY_R + (cardH / 2) + 7);

        // --- T相カードの描画 ---
        uint16_t bgT = _canvas->color565(18, 24, 38);
        uint16_t borderT = _canvas->color565(40, 50, 70);
        uint16_t textColT = getPowerColor(wattT);
        uint16_t tagBgT = _canvas->color565(30, 41, 59);
        uint16_t tagTextT = _canvas->color565(251, 191, 36); // アンバー

        if (isDangerT) {
            bool blink = ((now / 350) % 2 == 0);
            bgT = blink ? _canvas->color565(220, 38, 38) : _canvas->color565(120, 15, 15);
            borderT = _canvas->color565(255, 100, 100);
            textColT = TFT_WHITE;
            tagBgT = TFT_BLACK;
            tagTextT = _canvas->color565(255, 100, 100);
        } else if (isWarningT) {
            bgT = _canvas->color565(124, 45, 18);
            borderT = _canvas->color565(249, 115, 22);
            textColT = _canvas->color565(255, 237, 213);
            tagBgT = _canvas->color565(234, 88, 12);
            tagTextT = TFT_WHITE;
        } else if (isCautionT) {
            bgT = _canvas->color565(66, 48, 10);
            borderT = _canvas->color565(234, 179, 8);
            textColT = _canvas->color565(254, 240, 138);
            tagBgT = _canvas->color565(202, 138, 4);
            tagTextT = TFT_BLACK;
        }

        // T相カード背景＆枠
        _canvas->fillRoundRect(cardX, cardY_T, cardW, cardH, 4, bgT);
        _canvas->drawRoundRect(cardX, cardY_T, cardW, cardH, 4, borderT);
        if (isDangerT || isWarningT) {
            _canvas->drawRoundRect(cardX + 1, cardY_T + 1, cardW - 2, cardH - 2, 4, borderT); // 2重枠
        }

        // T相タグ [ T ]
        _canvas->fillRoundRect(cardX + 4, cardY_T + (cardH / 2) - 10, 18, 20, 3, tagBgT);
        _canvas->setTextDatum(MC_DATUM);
        _canvas->setTextColor(tagTextT);
        _canvas->setTextSize(1.6);
        _canvas->drawString("T", cardX + 13, cardY_T + (cardH / 2));

        // T相数字
        _canvas->setTextDatum(MR_DATUM);
        _canvas->setTextColor(textColT);
        _canvas->setTextSize(2.4);
        char tStr[16];
        snprintf(tStr, sizeof(tStr), "%d", wattT);
        _canvas->drawString(tStr, cardX + cardW - 18, cardY_T + (cardH / 2) - 1);

        // 単位 W
        _canvas->setTextDatum(BL_DATUM);
        _canvas->setTextColor((isDangerT || isWarningT) ? textColT : _canvas->color565(148, 163, 184));
        _canvas->setTextSize(1.2);
        _canvas->drawString("w", cardX + cardW - 15, cardY_T + (cardH / 2) + 7);

        // ----------------------------------------------------
        // F. 最下部 (IP & 今月積算)
        // ----------------------------------------------------
        _canvas->setTextDatum(BL_DATUM);
        _canvas->setTextColor(_canvas->color565(110, 130, 160));
        _canvas->setTextSize(1.0);
        char ipBuf[32];
        snprintf(ipBuf, sizeof(ipBuf), "IP: %s", (wifiConnected && ipStr) ? ipStr : "---");
        _canvas->drawString(ipBuf, 6, h - 3);

        _canvas->setTextDatum(BR_DATUM);
        _canvas->setTextColor(_canvas->color565(167, 139, 250));
        char monthBuf[32];
        snprintf(monthBuf, sizeof(monthBuf), "Month: %.1fkWh", monthlyKWh);
        _canvas->drawString(monthBuf, w - 6, h - 3);

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
