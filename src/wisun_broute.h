#ifndef WISUN_BROUTE_H
#define WISUN_BROUTE_H

#include <Arduino.h>
#include <Preferences.h>
#include "echonet_lite.h"

enum class BrouteState {
    INIT,
    SET_PWD,
    SET_ID,
    SET_OPT,      // SKSREG SA1 0
    SCANNING,
    JOINING,
    REAUTH,       // EVENT 29
    CONNECTED,
    FAILED
};

class WiSunBroute {
public:
    WiSunBroute(HardwareSerial& serial);
    
    void begin(int rxPin, int txPin, uint32_t baud);
    void update();

    bool requestPowerData();
    BrouteState getState() const { return _state; }
    const char* getStateString() const;
    
    const PowerData& getLatestData() const { return _latestData; }
    uint32_t getLastSuccessTime() const { return _lastSuccessMs; }

    bool isFastBoot() const { return _hasCache; }

private:
    HardwareSerial& _serial;
    Preferences _prefs;
    BrouteState _state = BrouteState::INIT;
    
    String _channel;
    String _panId;
    String _macAddr;
    String _ipv6Addr;
    bool _hasCache = false;
    
    PowerData _latestData = {};
    uint32_t _stateTimer = 0;
    uint32_t _lastSuccessMs = 0;
    uint32_t _lastRequestMs = 0;
    int _consecutiveFailures = 0;
    uint16_t _tid = 1;
    
    String _rxBuffer;

    void sendCommand(const String& cmd);
    void processLine(const String& line);
    void handleErxudp(const String& line);
    void restartConnection();
    
    // NVS キャッシュ管理 (再起動時のスキャン省略・超高速接続)
    void loadCache();
    void saveCache();
    void clearCache();
};

#endif // WISUN_BROUTE_H
