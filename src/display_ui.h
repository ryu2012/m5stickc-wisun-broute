#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <M5Unified.h>
#include "wisun_broute.h"

class DisplayUI {
public:
    DisplayUI() = default;
    ~DisplayUI();

    void begin();
    void update(BrouteState state, const PowerData& data, double monthlyKWh, const char* periodStr, bool wifiConnected, const char* ipStr);
    void drawStatusMessage(const char* msg);
    void toggleRotation();
    void toggleBeep();
    void showIpPopup(uint32_t durationMs = 4000);

    bool isBeepEnabled() const { return _beepEnabled; }

private:
    M5Canvas* _canvas = nullptr;
    uint32_t _lastRenderMs = 0;
    int _rotation = 1;
    bool _beepEnabled = true;
    uint32_t _lastBeepMs = 0;
    uint32_t _popupUntilMs = 0;

    uint16_t getPowerColor(int32_t watt);
};

#endif // DISPLAY_UI_H
