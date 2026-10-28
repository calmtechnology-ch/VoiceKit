#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include "VoiceHubConfig.h"
#include "VoiceKitConfig.h"

#if defined(ESP32) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#endif

/**
 * @brief Machine à états explicite du gestionnaire Wi-Fi (v0.6.1 — Correction C).
 */
enum class WiFiManagerState : uint8_t {
    IDLE = 0,               ///< Non démarré
    FAST_CONNECTING,        ///< Tentative de connexion rapide au BSSID/canal mémorisé
    SCAN_CONNECTING,        ///< Connexion standard (scan multi-canaux) en cours
    CONNECTED_WAIT_SNTP,    ///< Connecté au Wi-Fi, en attente de synchronisation horaire SNTP
    READY,                  ///< Connecté au Wi-Fi et horloge système valide
    DISCONNECTED_BACKOFF    ///< Déconnecté, en attente du délai de backoff avant reconnexion
};

// =============================================================================
// WiFiManager : Gestion de la connectivité Wi-Fi et de la synchronisation SNTP
// VoiceKit v0.5.2 / v0.6.1 — Fast Connection & Repli Asynchrone Non-Bloquant
// - Connexion rapide facultative au dernier BSSID/canal connu avec repli automatique
// - Exécution asynchrone des opérations Wi-Fi bloquantes (fallback scan, reconnect,
//   sauvegarde NVS) via une tâche ouvrière unique persistante sur ESP32 (Correction C)
// - Reconnexion automatique avec cadence réduite (1.5s) et backoff progressif
// - Synchronisation horaire SNTP optimisée (détection préalable horloge valide)
// - Instrumentation complète des temps de démarrage et de durée de tick()
// =============================================================================

class WiFiManager {
public:
    WiFiManager() = default;
    ~WiFiManager();

    bool begin(const char* ssid, const char* pass, bool enableFastConnect = true, uint32_t fastConnectTimeoutMs = 3500);
    void end();
    void tick();

    bool isConnected() const { return _connected; }
    bool isTimeSynced() const { return _timeSynced; }
    WiFiManagerState getState() const { return _state; }
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

    // Instrumentation durées et opérations asynchrones (v0.6.1 — Correction C)
    uint32_t getLastTickDurationUs() const { return _lastTickDurationUs; }
    uint32_t getMaxTickDurationUs() const { return _maxTickDurationUs; }
    uint32_t getAsyncOpsCount() const { return _asyncOpsCount.load(std::memory_order_relaxed); }
    bool isAsyncOpInProgress() const { return _asyncOpInProgress.load(std::memory_order_acquire); }

    // Manipulation explicite du cache FastConnect (tests & diagnostic)
    static void clearFastConnectCache();
    static void setFastConnectCache(const char* ssid, uint8_t channel, const uint8_t bssid[6]);
    static void notifySntpSyncedForTest();
    static void setSimulatedSystemTimeForTest(long timestampSec);

private:
    enum class AsyncCmdType : uint8_t {
        NONE = 0,
        FAST_CONNECT,
        NORMAL_SCAN,
        RECONNECT,
        DISCONNECT_AND_FAST_CONNECT,
        DISCONNECT_AND_NORMAL_SCAN,
        SAVE_NVS_CACHE,
        STOP_WORKER
    };

    struct AsyncCmd {
        AsyncCmdType type = AsyncCmdType::NONE;
        uint8_t channel = 0;
        uint8_t bssid[6] = {0};
    };

    void startNtp();
    void tryFastConnect(bool asyncDispatch);
    void fallbackToNormalScan(bool asyncDispatch);
    void saveFastConnectParams(uint8_t channel, const uint8_t* bssid);
    void loadFastConnectCacheIfNeeded();
    bool dispatchAsyncCmd(const AsyncCmd& cmd);
    void executeCmdSynchronously(const AsyncCmd& cmd);

#if defined(ESP32) || defined(ARDUINO)
    void ensureWorkerTask();
    static void workerTaskEntry(void* arg);
    TaskHandle_t _workerTaskHandle = nullptr;
    QueueHandle_t _cmdQueue = nullptr;
#endif

    char _ssidBuf[64] = "";
    char _passBuf[64] = "";
    const char* _ssid = nullptr;
    const char* _pass = nullptr;
    bool _enableFastConnect = true;
    uint32_t _fastConnectTimeoutMs = 3500;

    WiFiManagerState _state = WiFiManagerState::IDLE;
    bool _connected = false;
    bool _timeSynced = false;
    bool _ntpSyncing = false;
    bool _sntpSkipped = false;

    // État Fast Connect
    bool _fastConnectAttempted = false;
    bool _fastConnectUsedInCurrentAttempt = false;
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
    uint32_t _lastTickDurationUs = 0;
    uint32_t _maxTickDurationUs = 0;

    // Compteurs et Backoff progressif
    uint32_t _wifiAttempts = 0;
    uint32_t _wifiReconnects = 0;
    uint32_t _lastWiFiAttempt = 0;
    uint32_t _reconnectDelayMs = 1500;

    // Synchronisation tâche ouvrière asynchrone (v0.6.1)
    std::atomic<bool> _asyncOpInProgress{false};
    std::atomic<uint32_t> _asyncOpsCount{0};
};
