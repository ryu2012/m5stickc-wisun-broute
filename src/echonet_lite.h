#ifndef ECHONET_LITE_H
#define ECHONET_LITE_H

#include <Arduino.h>

struct PowerData {
    int32_t instantaneousWatt; // 全体瞬時電力 (W) [0xE7]
    int32_t wattRPhase;        // R相 瞬時電力 (W) [0xE8]
    int32_t wattTPhase;        // T相 瞬時電力 (W) [0xE8]
    float currentRPhase;        // R相 瞬時電流 (A)
    float currentTPhase;        // T相 瞬時電流 (A)
    double cumulativeKWh;       // 累積電力量 (kWh) [0xE0]
    
    bool validInstantaneous;
    bool validCurrent;
    bool validCumulative;
    uint32_t timestampMs;
};

class EchonetLite {
public:
    // 瞬時電力(0xE7), 瞬時電流(0xE8), 累積電力(0xE0) の一括取得フレーム生成
    static size_t buildGetCombinedPowerFrame(uint8_t* buffer, uint16_t tid);

    // パケットパース
    static bool parseResponse(const uint8_t* data, size_t length, PowerData& outData);
};

#endif // ECHONET_LITE_H
