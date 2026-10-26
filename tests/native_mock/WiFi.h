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
    void begin(const char* ssid = nullptr, const char* pass = nullptr, int32_t channel = 0, const uint8_t* bssid = nullptr, bool connect = true) {
        (void)ssid; (void)pass; (void)connect;
        _beginCalls++;
        if (channel > 0 && bssid) _fastConnectCalls++;
    }
    void reconnect() { _reconnectCalls++; }
    void setAutoReconnect(bool) {}
    void disconnect(bool = false, bool = false) {}
    int channel() const { return 6; }
    const uint8_t* BSSID() const { static const uint8_t b[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}; return b; }
    const char* BSSIDstr() const { return "AA:BB:CC:DD:EE:FF"; }

    uint32_t getBeginCalls() const { return _beginCalls; }
    uint32_t getFastConnectCalls() const { return _fastConnectCalls; }
    uint32_t getReconnectCalls() const { return _reconnectCalls; }
    void resetCounters() { _beginCalls = 0; _fastConnectCalls = 0; _reconnectCalls = 0; }

private:
    bool _connected = true;
    uint32_t _beginCalls = 0;
    uint32_t _fastConnectCalls = 0;
    uint32_t _reconnectCalls = 0;
};

#define WiFi (MockWiFiClass::getInstance())
