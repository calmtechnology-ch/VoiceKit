#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include "VoiceHubConfig.h"

// =============================================================================
// WiFiManager : Gestion de la connectivite Wi-Fi et de la synchronisation SNTP
// - Reconnexion automatique avec cadence bornee
// - Synchronisation horaire SNTP obligatoire pour TLS
// - Indication de statut vers le DialogueController et l'UI
// =============================================================================

class WiFiManager {
public:
    WiFiManager() = default;
    ~WiFiManager() = default;

    bool begin(const char* ssid, const char* pass);
    void tick();

    bool isConnected() const { return _connected; }
    bool isTimeSynced() const { return _timeSynced; }
    String getIP() const;
    int getRSSI() const;
    void reconnect();

private:
    void startNtp();

    const char* _ssid = nullptr;
    const char* _pass = nullptr;
    bool _connected = false;
    bool _timeSynced = false;
    bool _ntpSyncing = false;
    uint32_t _lastWiFiAttempt = 0;
    uint32_t _ntpStart = 0;
    uint32_t _ntpRetry = 0;
};
