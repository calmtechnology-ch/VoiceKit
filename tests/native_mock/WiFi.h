#pragma once
#include "Arduino.h"

#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
#define WIFI_STA 1
#define WIFI_OFF 0

struct MockIPAddress {
    String toString() const { return "192.168.1.100"; }
};

class MockWiFiClass {
public:
    static MockWiFiClass& getInstance() {
        static MockWiFiClass s_instance;
        return s_instance;
    }

    int status() const { return _connected ? WL_CONNECTED : WL_DISCONNECTED; }
    int RSSI() const { return -60; }
    void setConnected(bool c) { _connected = c; }
    MockIPAddress localIP() const { return MockIPAddress(); }
    void mode(int) {}
    void persistent(bool p) { _persistent = p; }
    bool isPersistent() const { return _persistent; }
    void begin(const char* ssid = nullptr, const char* pass = nullptr, int32_t channel = 0, const uint8_t* bssid = nullptr, bool connect = true) {
        (void)ssid; (void)pass; (void)connect;
        _beginCalls++;
        if (channel > 0 && bssid) {
            _fastConnectCalls++;
            if (_failFastConnect) {
                _connected = false;
            }
        } else {
            _normalBeginCalls++;
            if (_succeedNormalScan) {
                _connected = true;
            }
        }
    }
    void reconnect() {
        _reconnectCalls++;
        if (_succeedOnReconnect) {
            _connected = true;
        }
    }
    void setAutoReconnect(bool) {}
    void disconnect(bool = false, bool = false) {}
    int channel() const { return _channel; }
    void setChannel(int ch) { _channel = ch; }
    const uint8_t* BSSID() const { return _bssid; }
    void setBSSID(const uint8_t b[6]) { if (b) memcpy(_bssid, b, 6); }
    const char* BSSIDstr() const { return "AA:BB:CC:DD:EE:FF"; }

    void setSimulateFastConnectFailure(bool fail) { _failFastConnect = fail; }
    void setSimulateNormalScanSuccess(bool succeed) { _succeedNormalScan = succeed; }
    void setSimulateReconnectSuccess(bool succeed) { _succeedOnReconnect = succeed; }

    uint32_t getBeginCalls() const { return _beginCalls; }
    uint32_t getFastConnectCalls() const { return _fastConnectCalls; }
    uint32_t getNormalBeginCalls() const { return _normalBeginCalls; }
    uint32_t getReconnectCalls() const { return _reconnectCalls; }
    void resetCounters() {
        _beginCalls = 0;
        _fastConnectCalls = 0;
        _normalBeginCalls = 0;
        _reconnectCalls = 0;
        _failFastConnect = false;
        _succeedNormalScan = false;
        _succeedOnReconnect = false;
        _channel = 6;
        const uint8_t defBssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        memcpy(_bssid, defBssid, 6);
    }

private:
    bool _connected = true;
    bool _persistent = true;
    bool _failFastConnect = false;
    bool _succeedNormalScan = false;
    bool _succeedOnReconnect = false;
    int _channel = 6;
    uint8_t _bssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint32_t _beginCalls = 0;
    uint32_t _fastConnectCalls = 0;
    uint32_t _normalBeginCalls = 0;
    uint32_t _reconnectCalls = 0;
};

#define WiFi (MockWiFiClass::getInstance())
