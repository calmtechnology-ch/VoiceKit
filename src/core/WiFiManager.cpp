#include "WiFiManager.h"
#include <esp_sntp.h>
#include <time.h>

static std::atomic<bool> g_sntpReceived{false};

bool WiFiManager::begin(const char* ssid, const char* pass) {
    _ssid = ssid;
    _pass = pass;
    _connected = false;
    _timeSynced = false;
    _ntpSyncing = false;
    _lastWiFiAttempt = millis();

    sntp_set_time_sync_notification_cb([](struct timeval*) {
        g_sntpReceived.store(true);
    });

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    
    if (_ssid && strlen(_ssid) > 0) {
        WiFi.begin(_ssid, _pass);
    }
    
    return true;
}

void WiFiManager::startNtp() {
    g_sntpReceived.store(false);
    _timeSynced = false;
    _ntpSyncing = true;
    _ntpStart = millis();
    Serial.println("[WiFi] Lancement synchronisation SNTP...");
    configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
}

void WiFiManager::reconnect() {
    _lastWiFiAttempt = millis();
    WiFi.disconnect();
    if (_ssid && strlen(_ssid) > 0) {
        WiFi.begin(_ssid, _pass);
    }
}

void WiFiManager::tick() {
    const uint32_t now = millis();
    const bool isWifiConnected = (WiFi.status() == WL_CONNECTED);

    // Changement d'etat de connexion Wi-Fi
    if (isWifiConnected != _connected) {
        _connected = isWifiConnected;
        if (_connected) {
            Serial.printf("[WiFi] Connecte ! IP : %s, RSSI : %d dBm\n", 
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            startNtp();
        } else {
            Serial.println("[WiFi] Connexion perdue.");
            _timeSynced = false;
            _ntpSyncing = false;
            g_sntpReceived.store(false);
        }
    }

    if (!_connected) {
        // Tentative de reconnexion périodique
        if (now - _lastWiFiAttempt >= WIFI_RECONNECT_INTERVAL_MS) {
            _lastWiFiAttempt = now;
            Serial.println("[WiFi] Tentative de reconnexion...");
            WiFi.reconnect();
        }
    } else if (!_timeSynced) {
        // En attente de synchronisation NTP
        if (_ntpSyncing && (g_sntpReceived.load() || time(nullptr) >= NTP_MIN_VALID_TIMESTAMP)) {
            _timeSynced = true;
            _ntpSyncing = false;
            Serial.printf("[WiFi] Horloge NTP synchronisee ! Timestamp : %ld\n", (long)time(nullptr));
        } else if (_ntpSyncing && (now - _ntpStart >= NTP_TIMEOUT_MS)) {
            _ntpSyncing = false;
            _ntpRetry = now;
            Serial.println("[WiFi] Delai NTP depasse, nouvel essai dans 30s.");
        } else if (!_ntpSyncing && (now - _ntpRetry >= NTP_RETRY_INTERVAL_MS)) {
            startNtp();
        }
    }
}

String WiFiManager::getIP() const {
    if (_connected) {
        return WiFi.localIP().toString();
    }
    return String("0.0.0.0");
}

int WiFiManager::getRSSI() const {
    if (_connected) {
        return WiFi.RSSI();
    }
    return -100;
}
