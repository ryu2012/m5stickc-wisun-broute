#ifndef POWER_HISTORY_H
#define POWER_HISTORY_H

#include <Arduino.h>
#include "echonet_lite.h"

// 24時間を10分単位 (計144スロット) で高密度記録 (Grafana並みの解像度)
constexpr int HISTORY_SLOTS = 144;

struct HistorySlot {
    bool recorded = false;
    int32_t watt = 0;
    float ampR = 0.0f;
    float ampT = 0.0f;
    int32_t wattR = 0;
    int32_t wattT = 0;
    double kwh = 0.0;
};

class PowerHistory {
public:
    PowerHistory();
    void begin();
    void addSample(const PowerData& data);
    
    String toJson() const;

private:
    HistorySlot _slots[HISTORY_SLOTS];
    int _currentDay = -1;

    int getCurrentSlotIndex(int hour, int minute) const;
};

#endif // POWER_HISTORY_H
