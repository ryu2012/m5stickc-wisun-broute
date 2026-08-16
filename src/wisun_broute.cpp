#include "wisun_broute.h"
#include "config.h"

WiSunBroute::WiSunBroute(HardwareSerial& serial) : _serial(serial) {}

void WiSunBroute::loadCache() {
    _prefs.begin("wisun_cache", false);
    _channel = _prefs.getString("channel", "");
    _panId = _prefs.getString("pan_id", "");
    _ipv6Addr = _prefs.getString("ipv6", "");
    _macAddr = _prefs.getString("mac", "");

    if (_channel.length() > 0 && _panId.length() > 0 && _ipv6Addr.length() > 0) {
        _hasCache = true;
        Serial.printf("[Meter Cache] Loaded: Ch=%s, PanID=%s, IPv6=%s\n", 
                      _channel.c_str(), _panId.c_str(), _ipv6Addr.c_str());
    } else {
        _hasCache = false;
        Serial.println("[Meter Cache] No cache found. Full scan will be performed.");
    }
}

void WiSunBroute::saveCache() {
    if (_channel.length() > 0 && _panId.length() > 0 && _ipv6Addr.length() > 0) {
        _prefs.putString("channel", _channel);
        _prefs.putString("pan_id", _panId);
        _prefs.putString("ipv6", _ipv6Addr);
        _prefs.putString("mac", _macAddr);
        _hasCache = true;
        Serial.println("[Meter Cache] Connection info successfully saved to Flash NVS.");
    }
}

void WiSunBroute::clearCache() {
    _prefs.clear();
    _hasCache = false;
    _channel = "";
    _panId = "";
    _ipv6Addr = "";
    _macAddr = "";
    Serial.println("[Meter Cache] Cache cleared. Will fallback to full scan.");
}

void WiSunBroute::begin(int rxPin, int txPin, uint32_t baud) {
    _serial.begin(baud, SERIAL_8N1, rxPin, txPin);
    _state = BrouteState::INIT;
    _stateTimer = millis();
    _rxBuffer.reserve(512);

    loadCache();
}

const char* WiSunBroute::getStateString() const {
    switch (_state) {
        case BrouteState::INIT:      return "Initializing...";
        case BrouteState::SET_PWD:   return "Setting Password";
        case BrouteState::SET_ID:    return "Setting ID";
        case BrouteState::SET_OPT:   return "Configuring SA1...";
        case BrouteState::SCANNING:  return "Scanning Meter...";
        case BrouteState::JOINING:   return _hasCache ? "Fast Joining (PANA)..." : "PANA Authenticating...";
        case BrouteState::REAUTH:    return "PANA Re-Authenticating...";
        case BrouteState::CONNECTED: return "Connected (B-Route)";
        case BrouteState::FAILED:    return "Connection Failed";
        default:                     return "Unknown";
    }
}

void WiSunBroute::sendCommand(const String& cmd) {
    _serial.println(cmd);
    Serial.print("[TX] ");
    Serial.println(cmd);
}

void WiSunBroute::restartConnection() {
    Serial.println("[Stability Watchdog] Communication lost. Restarting PANA connection...");
    _state = BrouteState::INIT;
    _stateTimer = millis();
    _consecutiveFailures = 0;
    
    // セッション終了要求
    sendCommand("SKTERM");
    delay(500);
}

void WiSunBroute::update() {
    // 1. UART受信処理
    while (_serial.available()) {
        char c = _serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            if (_rxBuffer.length() > 0) {
                processLine(_rxBuffer);
                _rxBuffer = "";
            }
        } else {
            _rxBuffer += c;
        }
    }

    // 2. ステートマシンのタイムアウト・遷移コントロール
    uint32_t now = millis();
    switch (_state) {
        case BrouteState::INIT:
            if (now - _stateTimer > 1500) {
                sendCommand("SKVER");
                _state = BrouteState::SET_PWD;
                _stateTimer = now;
            }
            break;

        case BrouteState::SET_PWD:
            if (now - _stateTimer > 1000) {
                String cmdPwd = "SKSETPWD C " + String(BROUTE_PASSWORD);
                sendCommand(cmdPwd);
                _state = BrouteState::SET_ID;
                _stateTimer = now;
            }
            break;

        case BrouteState::SET_ID:
            if (now - _stateTimer > 1000) {
                String cmdId = "SKSETRBID " + String(BROUTE_ID);
                sendCommand(cmdId);
                _state = BrouteState::SET_OPT;
                _stateTimer = now;
            }
            break;

        case BrouteState::SET_OPT:
            if (now - _stateTimer > 1000) {
                sendCommand("SKSREG SA1 0");
                _stateTimer = now;

                // ★スマートメーターキャッシュがある場合は SKSCAN をスキップして超高速接続！
                if (_hasCache && _channel.length() > 0 && _panId.length() > 0 && _ipv6Addr.length() > 0) {
                    Serial.println("[Fast Boot] Cache found! Skipping SKSCAN and connecting immediately...");
                    delay(300);
                    sendCommand("SKSREG S2 " + _channel);
                    sendCommand("SKSREG S3 " + _panId);
                    delay(200);
                    sendCommand("SKJOIN " + _ipv6Addr);
                    _state = BrouteState::JOINING;
                } else {
                    _state = BrouteState::SCANNING;
                    delay(800);
                    sendCommand("SKSCAN 2 FFFFFFFF 6");
                }
            }
            break;

        case BrouteState::SCANNING:
            if (now - _stateTimer > 35000) {
                Serial.println("Scan timeout (35s). Retrying scan...");
                _channel = "";
                _panId = "";
                _macAddr = "";
                _ipv6Addr = "";
                sendCommand("SKSCAN 2 FFFFFFFF 6");
                _stateTimer = now;
            }
            break;

        case BrouteState::JOINING:
        case BrouteState::REAUTH:
            // PANA認証・再認証タイムアウト
            if (now - _stateTimer > 60000) {
                Serial.println("PANA Auth timeout. Clearing cache and restarting with full scan...");
                clearCache(); // キャッシュが無効だった可能性があるためクリアして再試行
                restartConnection();
            }
            break;

        case BrouteState::CONNECTED:
            // 45秒間応答が途絶えたら自動リカバリ
            if (_lastSuccessMs > 0 && (now - _lastSuccessMs > 45000)) {
                Serial.printf("[Watchdog] No response for %u ms. Triggering reconnect...\n", 
                              (now - _lastSuccessMs));
                restartConnection();
            }
            break;

        case BrouteState::FAILED:
            if (now - _stateTimer > 8000) {
                clearCache(); // 失敗時はキャッシュをクリアして再スキャン
                _state = BrouteState::INIT;
                _stateTimer = now;
            }
            break;
    }
}

void WiSunBroute::processLine(const String& line) {
    Serial.print("[RX] ");
    Serial.println(line);

    String trimmed = line;
    trimmed.trim();

    // 1. スキャン結果のパース
    if (trimmed.indexOf("Channel:") != -1) {
        _channel = trimmed.substring(trimmed.indexOf(':') + 1);
        _channel.trim();
        Serial.println("--> Found Channel: " + _channel);
    } else if (trimmed.indexOf("Pan ID:") != -1) {
        _panId = trimmed.substring(trimmed.indexOf(':') + 1);
        _panId.trim();
        Serial.println("--> Found Pan ID: " + _panId);
    } else if (trimmed.indexOf("Addr:") != -1) {
        _macAddr = trimmed.substring(trimmed.indexOf(':') + 1);
        _macAddr.trim();
        Serial.println("--> Found MAC Addr: " + _macAddr);
    } 
    // EVENT 22 直後の IPv6 アドレス抽出
    else if (trimmed.startsWith("EVENT 22")) {
        int spaceIdx = trimmed.indexOf(' ', 9);
        if (spaceIdx != -1) {
            String ipCandidate = trimmed.substring(spaceIdx + 1);
            ipCandidate.trim();
            if (ipCandidate.startsWith("FE80") || ipCandidate.startsWith("fe80")) {
                _ipv6Addr = ipCandidate;
                Serial.println("--> Extracted IPv6 Addr from EVENT 22: " + _ipv6Addr);
            }
        }

        if (_ipv6Addr.length() > 0) {
            Serial.println("Starting PANA Join directly with IPv6: " + _ipv6Addr);
            delay(300);
            sendCommand("SKJOIN " + _ipv6Addr);
            _state = BrouteState::JOINING;
            _stateTimer = millis();
        } else if (_macAddr.length() > 0 && _channel.length() > 0 && _panId.length() > 0) {
            Serial.println("Smart Meter Discovered! Setting SKSREG...");
            sendCommand("SKSREG S2 " + _channel);
            sendCommand("SKSREG S3 " + _panId);
            delay(200);
            sendCommand("SKLL64 " + _macAddr);
        } else {
            Serial.println("Smart Meter not found in this scan. Retrying in 3s...");
            delay(3000);
            _channel = "";
            _panId = "";
            _macAddr = "";
            _ipv6Addr = "";
            sendCommand("SKSCAN 2 FFFFFFFF 6");
            _stateTimer = millis();
        }
    } 
    // SKLL64 のレスポンス
    else if (line.length() >= 37 && line.indexOf(':') != -1 && _state == BrouteState::SCANNING && _macAddr.length() > 0) {
        _ipv6Addr = line;
        _ipv6Addr.trim();
        Serial.print("Converted IPv6 Addr: ");
        Serial.println(_ipv6Addr);

        delay(300);
        sendCommand("SKJOIN " + _ipv6Addr);
        _state = BrouteState::JOINING;
        _stateTimer = millis();
    } 
    // EVENT 29: PANA再認証開始 ➔ 送信一時停止
    else if (line.startsWith("EVENT 29")) {
        Serial.println("[PANA] EVENT 29: Session Lifetime Refresh started. Pausing requests...");
        _state = BrouteState::REAUTH;
        _stateTimer = millis();
    }
    // EVENT 25: PANA認証成功 ➔ キャッシュを保存して通信開始！
    else if (line.startsWith("EVENT 25")) {
        Serial.println("[PANA] EVENT 25: PANA Auth Success! Saving cache...");
        saveCache(); // 次回起動用に不揮発性メモリへ保存
        _state = BrouteState::CONNECTED;
        _stateTimer = millis();
        _consecutiveFailures = 0;
        _lastSuccessMs = millis();
        delay(1000);
        requestPowerData();
    }
    // EVENT 24: PANA認証失敗 ➔ キャッシュを破棄して再探索
    else if (line.startsWith("EVENT 24")) {
        Serial.println("[PANA] EVENT 24: PANA Auth Failed! Clearing cache...");
        clearCache();
        _state = BrouteState::FAILED;
        _stateTimer = millis();
    }
    // EVENT 26 / 28: 切断通知
    else if (line.startsWith("EVENT 26") || line.startsWith("EVENT 28")) {
        Serial.println("[PANA] Session Terminated. Triggering reconnect...");
        restartConnection();
    }
    // ECHONET Lite データ受信
    else if (line.startsWith("ERXUDP")) {
        handleErxudp(line);
    }
}

void WiSunBroute::handleErxudp(const String& line) {
    int spaceIdx = 0;
    int lastIdx = 0;
    String tokens[10];
    int tokenCount = 0;

    while ((spaceIdx = line.indexOf(' ', lastIdx)) != -1 && tokenCount < 9) {
        tokens[tokenCount++] = line.substring(lastIdx, spaceIdx);
        lastIdx = spaceIdx + 1;
    }
    if (tokenCount < 9) {
        tokens[tokenCount++] = line.substring(lastIdx);
    }

    if (tokenCount >= 9) {
        String hexData = tokens[8];
        hexData.trim();
        
        size_t len = hexData.length() / 2;
        uint8_t binBuf[256];
        if (len > sizeof(binBuf)) len = sizeof(binBuf);

        for (size_t i = 0; i < len; i++) {
            String byteStr = hexData.substring(i * 2, i * 2 + 2);
            binBuf[i] = (uint8_t) strtol(byteStr.c_str(), NULL, 16);
        }

        if (EchonetLite::parseResponse(binBuf, len, _latestData)) {
            _lastSuccessMs = millis();
            _consecutiveFailures = 0;
            Serial.printf("--> Power Read Success: W=%d (R=%d, T=%d), kWh=%.1f\n", 
                _latestData.instantaneousWatt, _latestData.wattRPhase, _latestData.wattTPhase, _latestData.cumulativeKWh);
        }
    }
}

bool WiSunBroute::requestPowerData() {
    if (_state != BrouteState::CONNECTED || _ipv6Addr.length() == 0) {
        return false;
    }

    uint8_t echonetBuf[64];
    _tid++;
    size_t len = EchonetLite::buildGetCombinedPowerFrame(echonetBuf, _tid);

    char lenBuf[8];
    snprintf(lenBuf, sizeof(lenBuf), "%04X", (unsigned int)len);

    String header = "SKSENDTO 1 " + _ipv6Addr + " 0E1A 1 " + String(lenBuf) + " ";
    
    _serial.print(header);
    _serial.write(echonetBuf, len);
    _serial.print("\r\n");
    _serial.flush();

    _lastRequestMs = millis();
    _consecutiveFailures++;

    Serial.print("[TX SKSENDTO Header] ");
    Serial.print(header);
    Serial.printf("<Binary %d bytes>\n", len);
    return true;
}
