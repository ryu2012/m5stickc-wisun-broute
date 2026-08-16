#include "echonet_lite.h"

size_t EchonetLite::buildGetCombinedPowerFrame(uint8_t* buffer, uint16_t tid) {
    uint8_t frame[] = {
        0x10, 0x81,                                 // EHD1, EHD2
        (uint8_t)(tid >> 8), (uint8_t)(tid & 0xFF), // TID
        0x05, 0xFF, 0x01,                           // SEOJ (コントローラ)
        0x02, 0x88, 0x01,                           // DEOJ (低圧スマート電力量メータ)
        0x62,                                       // ESV (Get)
        0x03,                                       // OPC (3個: 0xE7, 0xE8, 0xE0)
        0xE7, 0x00,                                 // EPC: 瞬時電力計測値 (W)
        0xE8, 0x00,                                 // EPC: 瞬時電流計測値 (R相/T相 A)
        0xE0, 0x00                                  // EPC: 累積電力量 (kWh)
    };
    memcpy(buffer, frame, sizeof(frame));
    return sizeof(frame);
}

bool EchonetLite::parseResponse(const uint8_t* data, size_t length, PowerData& outData) {
    if (length < 12) return false;

    // EHD Check
    if (data[0] != 0x10 || data[1] != 0x81) return false;

    // ESV Check (0x72 = Get_Res)
    if (data[10] != 0x72) return false;

    uint8_t opc = data[11];
    size_t offset = 12;

    for (uint8_t i = 0; i < opc; i++) {
        if (offset + 2 > length) break;
        uint8_t epc = data[offset];
        uint8_t pdc = data[offset + 1];
        offset += 2;

        if (offset + pdc > length) break;

        if (epc == 0xE7 && pdc == 4) { 
            // 瞬時電力計測値 (Signed 32bit W)
            int32_t val = (data[offset] << 24) | (data[offset + 1] << 16) | 
                          (data[offset + 2] << 8) | data[offset + 3];
            outData.instantaneousWatt = val;
            outData.validInstantaneous = true;
            outData.timestampMs = millis();
        } else if (epc == 0xE8 && pdc == 4) {
            // 瞬時電流計測値 (R相 2B, T相 2B: 0.1A単位)
            int16_t r01A = (int16_t)((data[offset] << 8) | data[offset + 1]);
            int16_t t01A = (int16_t)((data[offset + 2] << 8) | data[offset + 3]);
            
            outData.currentRPhase = r01A * 0.1f;
            outData.currentTPhase = t01A * 0.1f;
            
            // 100V換算の概算W数 (0.1A * 10 = W)
            outData.wattRPhase = r01A * 10;
            outData.wattTPhase = t01A * 10;
            outData.validCurrent = true;
        } else if (epc == 0xE0 && pdc == 4) { 
            // 累積電力量 (Unsigned 32bit, 0.1kWh)
            uint32_t val = (data[offset] << 24) | (data[offset + 1] << 16) | 
                           (data[offset + 2] << 8) | data[offset + 3];
            outData.cumulativeKWh = val * 0.1;
            outData.validCumulative = true;
        }

        offset += pdc;
    }

    return outData.validInstantaneous || outData.validCurrent || outData.validCumulative;
}
