#include "WiFiManager.h"
#include <esp_sntp.h>
#include <time.h>
#include <cstring>

#if defined(ESP32) || defined(ARDUINO)
#include <Preferences.h>
#endif

static std::atomic<bool> g_sntpReceived{false};
static long g_simulatedSystemTimeOverride = -1;

static time_t getEffectiveSystemTime() {
    if (g_simulatedSystemTimeOverride >= 0) {
        return static_cast<time_t>(g_simulatedSystemTimeOverride);
    }
    return time(nullptr);
}

namespace {
struct FastConnectCacheData {
    bool valid = false;
    bool loadedFromNvs = false;
    char ssid[64] = "";
    uint8_t channel = 0;
    uint8_t bssid[6] = {0};
};

static FastConnectCacheData s_fastConnectCache;
} // namespace

WiFiManager::~WiFiManager() {
    end();
}

void WiFiManager::clearFastConnectCache() {
    s_fastConnectCache.valid = false;
    s_fastConnectCache.loadedFromNvs = true;
    s_fastConnectCache.ssid[0] = '\0';
    s_fastConnectCache.channel = 0;
    memset(s_fastConnectCache.bssid, 0, 6);
#if defined(ESP32) || defined(ARDUINO)
    Preferences prefs;
    if (prefs.begin("vk_wifi", false)) {
        prefs.clear();
        prefs.end();
    }
#endif
}

void WiFiManager::setFastConnectCache(const char* ssid, uint8_t channel, const uint8_t bssid[6]) {
    if (!ssid || strlen(ssid) == 0 || channel < 1 || channel > 14 || !bssid) {
        return;
    }
    s_fastConnectCache.valid = true;
    s_fastConnectCache.loadedFromNvs = true;
    strncpy(s_fastConnectCache.ssid, ssid, sizeof(s_fastConnectCache.ssid) - 1);
    s_fastConnectCache.ssid[sizeof(s_fastConnectCache.ssid) - 1] = '\0';
    s_fastConnectCache.channel = channel;
    memcpy(s_fastConnectCache.bssid, bssid, 6);
}

void WiFiManager::notifySntpSyncedForTest() {
    g_sntpReceived.store(true);
}

void WiFiManager::setSimulatedSystemTimeForTest(long timestampSec) {
    g_simulatedSystemTimeOverride = timestampSec;
}

void WiFiManager::loadFastConnectCacheIfNeeded() {
    if (s_fastConnectCache.loadedFromNvs) {
        return;
    }
    s_fastConnectCache.loadedFromNvs = true;
#if defined(ESP32) || defined(ARDUINO)
    Preferences prefs;
    if (prefs.begin("vk_wifi", true)) {
        uint8_t valid = prefs.getUChar("valid", 0);
        String cachedSsid = prefs.getString("ssid", "");
        uint8_t chan = prefs.getUChar("chan", 0);
        uint8_t bssid[6] = {0};
        size_t bssidLen = prefs.getBytes("bssid", bssid, 6);
        prefs.end();

        if (valid == 1 && !cachedSsid.isEmpty() && chan >= 1 && chan <= 14 && bssidLen == 6) {
            s_fastConnectCache.valid = true;
            strncpy(s_fastConnectCache.ssid, cachedSsid.c_str(), sizeof(s_fastConnectCache.ssid) - 1);
            s_fastConnectCache.ssid[sizeof(s_fastConnectCache.ssid) - 1] = '\0';
            s_fastConnectCache.channel = chan;
            memcpy(s_fastConnectCache.bssid, bssid, 6);
        }
    }
#endif
}

#if defined(ESP32) || defined(ARDUINO)
void WiFiManager::ensureWorkerTask() {
    if (!_cmdQueue) {
        _cmdQueue = xQueueCreate(4, sizeof(AsyncCmd));
    }
    if (!_workerTaskHandle && _cmdQueue) {
        xTaskCreatePinnedToCore(
            &WiFiManager::workerTaskEntry,
            "vk_wifi_wkr",
            4096,
            this,
            1, // Priorité 1 sur Cœur 0 : ne préempte jamais loopTask (Cœur 1) ni l'audio
            &_workerTaskHandle,
            0  // Épinglé sur le Cœur 0 (cœur protocole Wi-Fi/lwIP)
        );
    }
}

void WiFiManager::workerTaskEntry(void* arg) {
    auto* self = static_cast<WiFiManager*>(arg);
    AsyncCmd cmd;
    while (true) {
        if (xQueueReceive(self->_cmdQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd.type == AsyncCmdType::STOP_WORKER) {
                break;
            }
            self->_asyncOpInProgress.store(true, std::memory_order_release);
            self->executeCmdSynchronously(cmd);
            self->_asyncOpsCount.fetch_add(1, std::memory_order_relaxed);
            self->_asyncOpInProgress.store(false, std::memory_order_release);
        }
    }
    self->_workerTaskHandle = nullptr;
    vTaskDelete(NULL);
}
#endif

void WiFiManager::end() {
#if defined(ESP32) || defined(ARDUINO)
    if (_workerTaskHandle && _cmdQueue) {
        AsyncCmd stopCmd;
        stopCmd.type = AsyncCmdType::STOP_WORKER;
        xQueueSendToFront(_cmdQueue, &stopCmd, pdMS_TO_TICKS(50));
        uint32_t waitCount = 0;
        while (_workerTaskHandle != nullptr && waitCount < 50) {
            vTaskDelay(pdMS_TO_TICKS(2));
            waitCount++;
        }
        if (_workerTaskHandle != nullptr) {
            vTaskDelete(_workerTaskHandle);
            _workerTaskHandle = nullptr;
        }
    }
    if (_cmdQueue) {
        vQueueDelete(_cmdQueue);
        _cmdQueue = nullptr;
    }
#endif
    _asyncOpInProgress.store(false, std::memory_order_release);
    _state = WiFiManagerState::IDLE;
}

void WiFiManager::executeCmdSynchronously(const AsyncCmd& cmd) {
    switch (cmd.type) {
        case AsyncCmdType::FAST_CONNECT:
            WiFi.begin(_ssidBuf, _passBuf, cmd.channel, cmd.bssid);
            break;
        case AsyncCmdType::NORMAL_SCAN:
            WiFi.begin(_ssidBuf, _passBuf);
            break;
        case AsyncCmdType::RECONNECT:
            WiFi.reconnect();
            break;
        case AsyncCmdType::DISCONNECT_AND_FAST_CONNECT:
            WiFi.disconnect();
            WiFi.begin(_ssidBuf, _passBuf, cmd.channel, cmd.bssid);
            break;
        case AsyncCmdType::DISCONNECT_AND_NORMAL_SCAN:
            WiFi.disconnect();
            WiFi.begin(_ssidBuf, _passBuf);
            break;
        case AsyncCmdType::SAVE_NVS_CACHE:
#if defined(ESP32) || defined(ARDUINO)
            if (_ssidBuf[0] != '\0' && cmd.channel >= 1 && cmd.channel <= 14) {
                Preferences prefs;
                if (prefs.begin("vk_wifi", false)) {
                    prefs.putString("ssid", _ssidBuf);
                    prefs.putUChar("chan", cmd.channel);
                    prefs.putBytes("bssid", cmd.bssid, 6);
                    prefs.putUChar("valid", 1);
                    prefs.end();
                    Serial.printf("[WiFi] Cache BSSID/canal sauvegarde pour \"%s\" (canal %u)\n",
                                  _ssidBuf, (unsigned)cmd.channel);
                }
            }
#endif
            break;
        default:
            break;
    }
}

bool WiFiManager::dispatchAsyncCmd(const AsyncCmd& cmd) {
#if defined(ESP32) || defined(ARDUINO)
    ensureWorkerTask();
    if (_cmdQueue && _workerTaskHandle) {
        // Pour une commande de connexion/reconnexion, éviter d'empiler des appels Wi-Fi concurrents
        if (cmd.type != AsyncCmdType::SAVE_NVS_CACHE && _asyncOpInProgress.load(std::memory_order_acquire)) {
            return false;
        }
        if (xQueueSend(_cmdQueue, &cmd, 0) == pdTRUE) {
            return true;
        }
    }
#endif
    executeCmdSynchronously(cmd);
    _asyncOpsCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool WiFiManager::begin(const char* ssid, const char* pass, bool enableFastConnect, uint32_t fastConnectTimeoutMs) {
    if (ssid) {
        strncpy(_ssidBuf, ssid, sizeof(_ssidBuf) - 1);
        _ssidBuf[sizeof(_ssidBuf) - 1] = '\0';
    } else {
        _ssidBuf[0] = '\0';
    }
    if (pass) {
        strncpy(_passBuf, pass, sizeof(_passBuf) - 1);
        _passBuf[sizeof(_passBuf) - 1] = '\0';
    } else {
        _passBuf[0] = '\0';
    }
    _ssid = _ssidBuf;
    _pass = _passBuf;
    _enableFastConnect = enableFastConnect;
    _fastConnectTimeoutMs = fastConnectTimeoutMs;
    _state = WiFiManagerState::IDLE;
    _connected = false;
    _timeSynced = false;
    _ntpSyncing = false;
    _sntpSkipped = false;
    _fastConnectAttempted = false;
    _fastConnectUsedInCurrentAttempt = false;
    _fastConnectSuccess = false;
    _wifiConnectDurationMs = 0;
    _sntpSyncDurationMs = 0;
    _wifiAttempts = 0;
    _wifiReconnects = 0;
    _channel = 0;
    _bssidStr[0] = '\0';
    _reconnectDelayMs = 1500;
    _lastTickDurationUs = 0;
    _maxTickDurationUs = 0;
    _lastWiFiAttempt = millis();

    sntp_set_time_sync_notification_cb([](struct timeval*) {
        g_sntpReceived.store(true);
    });

    // Correction C (v0.6.1) : Désactiver l'écriture flash NVS synchrone automatique dans esp_wifi_set_config
    // car WiFiManager gère déjà son propre cache NVS "vk_wifi" uniquement en cas de changement réel.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

#if defined(ESP32) || defined(ARDUINO)
    ensureWorkerTask();
#endif

    loadFastConnectCacheIfNeeded();

    if (_ssidBuf[0] != '\0') {
        if (_enableFastConnect) {
            tryFastConnect(false);
        } else {
            fallbackToNormalScan(false);
        }
    }

    return true;
}

void WiFiManager::saveFastConnectParams(uint8_t channel, const uint8_t* bssid) {
    if (_ssidBuf[0] == '\0' || channel < 1 || channel > 14 || !bssid) {
        return;
    }
    // Si le cache correspond déjà exactement au SSID, au canal et au BSSID actuels,
    // éviter toute réécriture NVS inutile.
    if (s_fastConnectCache.valid &&
        strcmp(s_fastConnectCache.ssid, _ssidBuf) == 0 &&
        s_fastConnectCache.channel == channel &&
        memcmp(s_fastConnectCache.bssid, bssid, 6) == 0) {
        return;
    }

    s_fastConnectCache.valid = true;
    strncpy(s_fastConnectCache.ssid, _ssidBuf, sizeof(s_fastConnectCache.ssid) - 1);
    s_fastConnectCache.ssid[sizeof(s_fastConnectCache.ssid) - 1] = '\0';
    s_fastConnectCache.channel = channel;
    memcpy(s_fastConnectCache.bssid, bssid, 6);

    AsyncCmd cmd;
    cmd.type = AsyncCmdType::SAVE_NVS_CACHE;
    cmd.channel = channel;
    memcpy(cmd.bssid, bssid, 6);
    dispatchAsyncCmd(cmd);
}

void WiFiManager::tryFastConnect(bool asyncDispatch) {
    loadFastConnectCacheIfNeeded();

    if (_enableFastConnect && _ssidBuf[0] != '\0' &&
        s_fastConnectCache.valid &&
        strcmp(s_fastConnectCache.ssid, _ssidBuf) == 0 &&
        s_fastConnectCache.channel >= 1 && s_fastConnectCache.channel <= 14) {

        const uint8_t* bssid = s_fastConnectCache.bssid;
        uint8_t chan = s_fastConnectCache.channel;
        snprintf(_bssidStr, sizeof(_bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        _channel = chan;
        _fastConnectAttempted = true;
        _fastConnectUsedInCurrentAttempt = true;
        _fastConnectSuccess = false;
        _fastConnectStart = millis();
        _wifiStart = _fastConnectStart;
        _lastWiFiAttempt = _fastConnectStart;
        _wifiAttempts++;
        _state = WiFiManagerState::FAST_CONNECTING;

        Serial.printf("[WiFi] Connexion rapide (FastConnect) vers BSSID %s sur canal %u...\n",
                      _bssidStr, (unsigned)chan);

        AsyncCmd cmd;
        cmd.type = AsyncCmdType::FAST_CONNECT;
        cmd.channel = chan;
        memcpy(cmd.bssid, bssid, 6);
        if (asyncDispatch) {
            dispatchAsyncCmd(cmd);
        } else {
            executeCmdSynchronously(cmd);
        }
        return;
    }

    fallbackToNormalScan(asyncDispatch);
}

void WiFiManager::fallbackToNormalScan(bool asyncDispatch) {
    _fastConnectUsedInCurrentAttempt = false;
    _fastConnectSuccess = false;
    _wifiStart = millis();
    _lastWiFiAttempt = _wifiStart;
    _wifiAttempts++;
    _state = WiFiManagerState::SCAN_CONNECTING;

    Serial.println("[WiFi] Connexion standard (recherche multi-canaux)...");

    AsyncCmd cmd;
    cmd.type = AsyncCmdType::NORMAL_SCAN;
    if (asyncDispatch) {
        dispatchAsyncCmd(cmd);
    } else {
        executeCmdSynchronously(cmd);
    }
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
    if (_ssidBuf[0] == '\0') {
        WiFi.disconnect();
        return;
    }
    loadFastConnectCacheIfNeeded();
    if (_enableFastConnect &&
        s_fastConnectCache.valid &&
        strcmp(s_fastConnectCache.ssid, _ssidBuf) == 0 &&
        s_fastConnectCache.channel >= 1 && s_fastConnectCache.channel <= 14) {

        const uint8_t* bssid = s_fastConnectCache.bssid;
        uint8_t chan = s_fastConnectCache.channel;
        snprintf(_bssidStr, sizeof(_bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        _channel = chan;
        _fastConnectAttempted = true;
        _fastConnectUsedInCurrentAttempt = true;
        _fastConnectSuccess = false;
        _fastConnectStart = millis();
        _wifiStart = _fastConnectStart;
        _wifiAttempts++;
        _state = WiFiManagerState::FAST_CONNECTING;

        AsyncCmd cmd;
        cmd.type = AsyncCmdType::DISCONNECT_AND_FAST_CONNECT;
        cmd.channel = chan;
        memcpy(cmd.bssid, bssid, 6);
        dispatchAsyncCmd(cmd);
    } else {
        _fastConnectUsedInCurrentAttempt = false;
        _fastConnectSuccess = false;
        _wifiStart = millis();
        _wifiAttempts++;
        _state = WiFiManagerState::SCAN_CONNECTING;

        AsyncCmd cmd;
        cmd.type = AsyncCmdType::DISCONNECT_AND_NORMAL_SCAN;
        dispatchAsyncCmd(cmd);
    }
}

void WiFiManager::tick() {
    const uint32_t tickStartUs = micros();
    const uint32_t now = millis();
    const bool isWifiConnected = (WiFi.status() == WL_CONNECTED);

    // Changement d'état de connexion Wi-Fi
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

            if (_fastConnectUsedInCurrentAttempt) {
                _fastConnectSuccess = true;
                _fastConnectUsedInCurrentAttempt = false;
                Serial.printf("[WiFi] FastConnect reussi en %u ms ! (BSSID: %s, Canal: %u)\n",
                              (unsigned)_wifiConnectDurationMs, _bssidStr, (unsigned)_channel);
            } else {
                Serial.printf("[WiFi] Connecte en %u ms ! IP : %s, RSSI : %d dBm (Canal: %u)\n",
                              (unsigned)_wifiConnectDurationMs, WiFi.localIP().toString().c_str(), WiFi.RSSI(), (unsigned)_channel);
            }

            // Optimisation SNTP v0.5.2 : vérification préalable de l'horloge système
            time_t nowTime = getEffectiveSystemTime();
            if (nowTime >= NTP_MIN_VALID_TIMESTAMP) {
                _timeSynced = true;
                _ntpSyncing = false;
                _sntpSkipped = true;
                _sntpSyncDurationMs = 0;
                _state = WiFiManagerState::READY;
                Serial.printf("[WiFi] Horloge systeme deja valide (%ld) — SNTP sans attente bloquante\n", (long)nowTime);
                configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
            } else {
                _sntpSkipped = false;
                _state = WiFiManagerState::CONNECTED_WAIT_SNTP;
                startNtp();
            }
        } else {
            Serial.println("[WiFi] Connexion perdue.");
            _wifiReconnects++;
            _connected = false;
            _timeSynced = false;
            _ntpSyncing = false;
            _fastConnectUsedInCurrentAttempt = false;
            g_sntpReceived.store(false);
            _lastWiFiAttempt = now;
            _reconnectDelayMs = 1500;
            _state = WiFiManagerState::DISCONNECTED_BACKOFF;
        }
    }

    if (!_connected) {
        // Détection timeout FastConnect -> repli asynchrone non-bloquant vers recherche normale (Correction C)
        if (_fastConnectUsedInCurrentAttempt && (now - _fastConnectStart >= _fastConnectTimeoutMs)) {
            Serial.printf("[WiFi] Delai FastConnect depasse (%u ms) -> repli asynchrone vers recherche normale...\n",
                          (unsigned)_fastConnectTimeoutMs);
            fallbackToNormalScan(true);
        } else if (!_fastConnectUsedInCurrentAttempt && (now - _lastWiFiAttempt >= _reconnectDelayMs)) {
            _lastWiFiAttempt = now;
            if (_reconnectDelayMs < 30000) {
                _reconnectDelayMs = (_reconnectDelayMs * 2 > 30000) ? 30000 : _reconnectDelayMs * 2;
            }
            if (!_asyncOpInProgress.load(std::memory_order_acquire)) {
                Serial.printf("[WiFi] Tentative de reconnexion auto (delai suivant : %u ms)...\n",
                              (unsigned)_reconnectDelayMs);
                _wifiAttempts++;
                _state = WiFiManagerState::SCAN_CONNECTING;
                AsyncCmd cmd;
                cmd.type = AsyncCmdType::RECONNECT;
                dispatchAsyncCmd(cmd);
            }
        }
    } else if (!_timeSynced) {
        // En attente de synchronisation NTP
        if (_ntpSyncing && (g_sntpReceived.load() || getEffectiveSystemTime() >= NTP_MIN_VALID_TIMESTAMP)) {
            _timeSynced = true;
            _ntpSyncing = false;
            _sntpSyncDurationMs = millis() - _ntpStart;
            _state = WiFiManagerState::READY;
            Serial.printf("[WiFi] Horloge NTP synchronisee en %u ms ! Timestamp : %ld\n",
                          (unsigned)_sntpSyncDurationMs, (long)getEffectiveSystemTime());
        } else if (_ntpSyncing && (now - _ntpStart >= NTP_TIMEOUT_MS)) {
            _ntpSyncing = false;
            _ntpRetry = now;
            Serial.println("[WiFi] Delai NTP depasse, nouvel essai dans 30s.");
        } else if (!_ntpSyncing && (now - _ntpRetry >= NTP_RETRY_INTERVAL_MS)) {
            startNtp();
        }
    }

    const uint32_t tickDurUs = micros() - tickStartUs;
    _lastTickDurationUs = tickDurUs;
    if (tickDurUs > _maxTickDurationUs) {
        _maxTickDurationUs = tickDurUs;
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
