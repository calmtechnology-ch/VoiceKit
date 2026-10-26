#include "WiFiManager.h"
#include <esp_sntp.h>
#include <time.h>

#if defined(ESP32) || defined(ARDUINO)
#include <Preferences.h>
#endif

static std::atomic<bool> g_sntpReceived{false};

bool WiFiManager::begin(const char* ssid, const char* pass, bool enableFastConnect, uint32_t fastConnectTimeoutMs) {
    _ssid = ssid;
    _pass = pass;
    _enableFastConnect = enableFastConnect;
    _fastConnectTimeoutMs = fastConnectTimeoutMs;
    _connected = false;
    _timeSynced = false;
    _ntpSyncing = false;
    _sntpSkipped = false;
    _fastConnectAttempted = false;
    _fastConnectSuccess = false;
    _wifiConnectDurationMs = 0;
    _sntpSyncDurationMs = 0;
    _wifiAttempts = 0;
    _wifiReconnects = 0;
    _channel = 0;
    _bssidStr[0] = '\0';
    _reconnectDelayMs = 1500;
    _lastWiFiAttempt = millis();

    sntp_set_time_sync_notification_cb([](struct timeval*) {
        g_sntpReceived.store(true);
    });

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    if (_ssid && strlen(_ssid) > 0) {
        if (_enableFastConnect) {
            tryFastConnect();
        } else {
            fallbackToNormalScan();
        }
    }

    return true;
}

void WiFiManager::saveFastConnectParams(uint8_t channel, const uint8_t* bssid) {
#if defined(ESP32) || defined(ARDUINO)
    if (_ssid && strlen(_ssid) > 0 && channel > 0 && channel <= 14 && bssid) {
        Preferences prefs;
        if (prefs.begin("vk_wifi", false)) {
            prefs.putString("ssid", _ssid);
            prefs.putUChar("chan", channel);
            prefs.putBytes("bssid", bssid, 6);
            prefs.putUChar("valid", 1);
            prefs.end();
            Serial.printf("[WiFi] Cache BSSID/canal sauvegarde pour \"%s\" (canal %u)\n", _ssid, (unsigned)channel);
        }
    }
#endif
}

void WiFiManager::tryFastConnect() {
#if defined(ESP32) || defined(ARDUINO)
    if (_enableFastConnect && _ssid && strlen(_ssid) > 0) {
        Preferences prefs;
        if (prefs.begin("vk_wifi", true)) {
            uint8_t valid = prefs.getUChar("valid", 0);
            String cachedSsid = prefs.getString("ssid", "");
            uint8_t chan = prefs.getUChar("chan", 0);
            uint8_t bssid[6] = {0};
            size_t bssidLen = prefs.getBytes("bssid", bssid, 6);
            prefs.end();

            if (valid == 1 && cachedSsid == _ssid && chan >= 1 && chan <= 14 && bssidLen == 6) {
                snprintf(_bssidStr, sizeof(_bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                         bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
                _channel = chan;
                _fastConnectAttempted = true;
                _fastConnectSuccess = false;
                _fastConnectStart = millis();
                _wifiStart = millis();
                _wifiAttempts++;
                Serial.printf("[WiFi] Connexion rapide (FastConnect) vers BSSID %s sur canal %u...\n",
                              _bssidStr, (unsigned)chan);
                WiFi.begin(_ssid, _pass, chan, bssid);
                return;
            }
        }
    }
#endif
    fallbackToNormalScan();
}

void WiFiManager::fallbackToNormalScan() {
    _fastConnectAttempted = false;
    _fastConnectSuccess = false;
    _wifiStart = millis();
    _wifiAttempts++;
    Serial.println("[WiFi] Connexion standard (recherche multi-canaux)...");
    WiFi.begin(_ssid, _pass);
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
        if (_enableFastConnect) {
            tryFastConnect();
        } else {
            fallbackToNormalScan();
        }
    }
}

void WiFiManager::tick() {
    const uint32_t now = millis();
    const bool isWifiConnected = (WiFi.status() == WL_CONNECTED);

    // Changement d'etat de connexion Wi-Fi
    if (isWifiConnected != _connected) {
        _connected = isWifiConnected;
        if (_connected) {
            _wifiConnectDurationMs = millis() - _wifiStart;
            _reconnectDelayMs = 1500;

            _channel = WiFi.channel();
            const uint8_t* b = WiFi.BSSID();
            if (b) {
                snprintf(_bssidStr, sizeof(_bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                         b[0], b[1], b[2], b[3], b[4], b[5]);
                saveFastConnectParams(_channel, b);
            }

            if (_fastConnectAttempted) {
                _fastConnectSuccess = true;
                _fastConnectAttempted = false;
                Serial.printf("[WiFi] FastConnect reussi en %u ms ! (BSSID: %s, Canal: %u)\n", 
                              (unsigned)_wifiConnectDurationMs, _bssidStr, (unsigned)_channel);
            } else {
                Serial.printf("[WiFi] Connecte en %u ms ! IP : %s, RSSI : %d dBm (Canal: %u)\n", 
                              (unsigned)_wifiConnectDurationMs, WiFi.localIP().toString().c_str(), WiFi.RSSI(), (unsigned)_channel);
            }

            // Optimisation SNTP v0.5.2 : verification prealable de l'horloge systeme
            time_t nowTime = time(nullptr);
            if (nowTime >= NTP_MIN_VALID_TIMESTAMP) {
                _timeSynced = true;
                _ntpSyncing = false;
                _sntpSkipped = true;
                _sntpSyncDurationMs = 0;
                Serial.printf("[WiFi] Horloge systeme deja valide (%ld) — SNTP sans attente bloquante\n", (long)nowTime);
                configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
            } else {
                _sntpSkipped = false;
                startNtp();
            }
        } else {
            Serial.println("[WiFi] Connexion perdue.");
            _wifiReconnects++;
            _connected = false;
            _timeSynced = false;
            _ntpSyncing = false;
            _fastConnectAttempted = false;
            g_sntpReceived.store(false);
            _lastWiFiAttempt = now;
            _reconnectDelayMs = 1500;
        }
    }

    if (!_connected) {
        // Détection timeout FastConnect -> repli recherche normale
        if (_fastConnectAttempted && (now - _fastConnectStart >= _fastConnectTimeoutMs)) {
            Serial.printf("[WiFi] Delai FastConnect depasse (%u ms) -> repli vers recherche normale...\n",
                          (unsigned)_fastConnectTimeoutMs);
            fallbackToNormalScan();
        } else if (!_fastConnectAttempted && (now - _lastWiFiAttempt >= _reconnectDelayMs)) {
            _lastWiFiAttempt = now;
            if (_reconnectDelayMs < 30000) {
                _reconnectDelayMs = (_reconnectDelayMs * 2 > 30000) ? 30000 : _reconnectDelayMs * 2;
            }
            Serial.printf("[WiFi] Tentative de reconnexion auto (delai suivant : %u ms)...\n",
                          (unsigned)_reconnectDelayMs);
            _wifiAttempts++;
            WiFi.reconnect();
        }
    } else if (!_timeSynced) {
        // En attente de synchronisation NTP
        if (_ntpSyncing && (g_sntpReceived.load() || time(nullptr) >= NTP_MIN_VALID_TIMESTAMP)) {
            _timeSynced = true;
            _ntpSyncing = false;
            _sntpSyncDurationMs = millis() - _ntpStart;
            Serial.printf("[WiFi] Horloge NTP synchronisee en %u ms ! Timestamp : %ld\n",
                          (unsigned)_sntpSyncDurationMs, (long)time(nullptr));
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

