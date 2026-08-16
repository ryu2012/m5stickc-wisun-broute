#include "power_history.h"
#include <time.h>

PowerHistory::PowerHistory() {
    for (int i = 0; i < HISTORY_SLOTS; i++) {
        _slots[i] = HistorySlot{};
    }
}

void PowerHistory::begin() {
    configTime(9 * 3600, 0, "ntp.nict.jp", "pool.ntp.org");
}

int PowerHistory::getCurrentSlotIndex(int hour, int minute) const {
    if (hour < 0 || hour > 23) return 0;
    int slot = (hour * 6) + (minute / 10);
    if (slot >= HISTORY_SLOTS) slot = HISTORY_SLOTS - 1;
    return slot;
}

void PowerHistory::addSample(const PowerData& data) {
    if (!data.validInstantaneous) return;

    time_t nowTime;
    time(&nowTime);
    struct tm timeinfo;
    if (!localtime_r(&nowTime, &timeinfo) || timeinfo.tm_year < (2020 - 1900)) {
        return;
    }

    if (_currentDay != -1 && _currentDay != timeinfo.tm_mday) {
        for (int i = 0; i < HISTORY_SLOTS; i++) {
            _slots[i] = HistorySlot{};
        }
    }
    _currentDay = timeinfo.tm_mday;

    int slotIdx = getCurrentSlotIndex(timeinfo.tm_hour, timeinfo.tm_min);

    float ampR = data.validCurrent ? data.currentRPhase : ((data.instantaneousWatt / 2.0f) / 100.0f);
    float ampT = data.validCurrent ? data.currentTPhase : ((data.instantaneousWatt / 2.0f) / 100.0f);
    int32_t wattR = data.validCurrent ? data.wattRPhase : (data.instantaneousWatt / 2);
    int32_t wattT = data.validCurrent ? data.wattTPhase : (data.instantaneousWatt - wattR);

    _slots[slotIdx].recorded = true;
    _slots[slotIdx].watt = data.instantaneousWatt;
    _slots[slotIdx].ampR = ampR;
    _slots[slotIdx].ampT = ampT;
    _slots[slotIdx].wattR = wattR;
    _slots[slotIdx].wattT = wattT;
    if (data.validCumulative) {
        _slots[slotIdx].kwh = data.cumulativeKWh;
    }
}

String PowerHistory::toJson() const {
    String json = "{\"slots\":[";

    for (int i = 0; i < HISTORY_SLOTS; i++) {
        int hour = i / 6;
        int min = (i % 6) * 10;
        char timeLabel[8];
        snprintf(timeLabel, sizeof(timeLabel), "%02d:%02d", hour, min);

        if (i > 0) json += ",";
        json += "{\"time\":\"";
        json += timeLabel;
        json += "\",\"recorded\":";
        json += _slots[i].recorded ? "true" : "false";
        json += ",\"watt\":";
        json += _slots[i].watt;
        json += ",\"amp_r\":";
        json += String(_slots[i].ampR, 1);
        json += ",\"amp_t\":";
        json += String(_slots[i].ampT, 1);
        json += ",\"watt_r\":";
        json += _slots[i].wattR;
        json += ",\"watt_t\":";
        json += _slots[i].wattT;
        json += ",\"kwh\":";
        json += String(_slots[i].kwh, 1);
        json += "}";
    }

    json += "]}";
    return json;
}
