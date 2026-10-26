#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include "VoiceHubConfig.h"
#include "VoiceKitConfig.h"

// =============================================================================
// WiFiManager : Gestion de la connectivite Wi-Fi et de la synchronisation SNTP
// VoiceKit v0.5.2 — Fast Connection
// - Connexion rapide facultative au dernier BSSID/canal connu avec repli automatique
// - Reconnexion automatique avec cadence reduite (1.5s) et backoff progressif
// - Synchronisation horaire SNTP optimisee (detection prealable horloge valide)
// - Instrumentation complete des temps de demarrage (wifiConnectMs, sntpSyncMs)
// =============================================================================

class WiFiManager {
public:
    WiFiManager() = default;
    ~WiFiManager() = default;

    bool begin(const char* ssid, const char* pass, bool enableFastConnect = true, uint32_t fastConnectTimeoutMs = 3500);
    void tick();

    bool isConnected() const { return _connected; }
    bool isTimeSynced() const { return _timeSynced; }
    String getIP() const;
    int getRSSI() const;
    void reconnect();

    // Instrumentation et métriques de connexion (v0.5.2)
    uint32_t getWifiConnectMs() const { return _wifiConnectDurationMs; }
    uint32_t getSntpSyncMs() const { return _sntpSyncDurationMs; }
    uint32_t getWifiAttempts() const { return _wifiAttempts; }
    uint32_t getWifiReconnects() const { return _wifiReconnects; }
    bool isFastConnectAttempted() const { return _fastConnectAttempted; }
    bool isFastConnectSuccess() const { return _fastConnectSuccess; }
    bool isSntpSkipped() const { return _sntpSkipped; }
    uint8_t getChannel() const { return _channel; }
    const char* getBSSID() const { return _bssidStr; }

private:
    void startNtp();
    void tryFastConnect();
    void fallbackToNormalScan();
    void saveFastConnectParams(uint8_t channel, const uint8_t* bssid);

    const char* _ssid = nullptr;
    const char* _pass = nullptr;
    bool _enableFastConnect = true;
    uint32_t _fastConnectTimeoutMs = 3500;

    bool _connected = false;
    bool _timeSynced = false;
    bool _ntpSyncing = false;
    bool _sntpSkipped = false;

    // État Fast Connect
    bool _fastConnectAttempted = false;
    bool _fastConnectSuccess = false;
    uint32_t _fastConnectStart = 0;
    uint8_t _channel = 0;
    char _bssidStr[18] = "";

    // Horodatages et durées
    uint32_t _wifiStart = 0;
    uint32_t _wifiConnectDurationMs = 0;
    uint32_t _sntpSyncDurationMs = 0;
    uint32_t _ntpStart = 0;
    uint32_t _ntpRetry = 0;

    // Compteurs et Backoff progressif
    uint32_t _wifiAttempts = 0;
    uint32_t _wifiReconnects = 0;
    uint32_t _lastWiFiAttempt = 0;
    uint32_t _reconnectDelayMs = 1500;
};

