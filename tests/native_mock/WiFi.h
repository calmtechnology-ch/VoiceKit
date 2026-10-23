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
    void begin(const char*, const char*) {}
    void reconnect() {}
    void disconnect(bool = false, bool = false) {}

private:
    bool _connected = true;
};

#define WiFi (MockWiFiClass::getInstance())
