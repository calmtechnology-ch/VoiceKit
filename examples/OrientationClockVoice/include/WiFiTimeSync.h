#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <functional>

#if __has_include("wifi_config.h")
    #include "wifi_config.h"
#elif __has_include("wifi_config.example.h")
    #include "wifi_config.example.h"
#elif __has_include("wifi_config.h.example")
    #include "wifi_config.h.example"
#else
    #define WIFI_SSID     "VOTRE_SSID_WIFI"
    #define WIFI_PASSWORD "VOTRE_MOT_DE_PASSE"
    #define GEMINI_KEY    "VOTRE_CLE_API_GEMINI"
    #define NTP_SERVER_1  "pool.ntp.org"
    #define NTP_SERVER_2  "time.google.com"
    #define NTP_SERVER_3  "time.nist.gov"
    #define TIMEZONE_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

enum class SyncState {
    IDLE,        ///< Wi-Fi éteint, attente du prochain cycle (6h)
    CONNECTING,  ///< Connexion au point d'accès Wi-Fi
    SYNCING,     ///< Connecté, attente du paquet NTP
    SUCCESS,     ///< Synchronisation réussie
    FAILED       ///< Échec ou timeout
};

/**
 * @class WiFiTimeSync
 * @brief Gestionnaire de synchronisation horaire NTP avec coupure Wi-Fi pour économie d'énergie,
 *        coordonné explicitement avec VoiceKit pour ne jamais couper le Wi-Fi pendant la parole.
 */
class WiFiTimeSync {
public:
    using TimeSyncedCallback = std::function<void(const struct tm& utc_timeinfo)>;

    WiFiTimeSync();

    void init();
    void startSync();
    void update();

    void setOnTimeSynced(TimeSyncedCallback cb) { on_time_synced = cb; }
    bool isWiFiActive() const { return state != SyncState::IDLE; }
    SyncState getState() const { return state; }

    /**
     * @brief Verrouillage de maintien de connexion pour VoiceKit.
     * Empêche formellement WiFiTimeSync d'éteindre le Wi-Fi tant que le service vocal est actif.
     */
    void setKeepAliveLock(bool lock);
    bool isKeepAliveLocked() const { return keep_alive_locked; }

    /**
     * @brief Réveil et activation immédiate du Wi-Fi à la demande (ex: appui tactile pour annonce vocale).
     */
    void requestWiFiForVoice();

private:
    void transitionToIdle(uint32_t duration_ms);
    void transitionToFailed();
    void shutDownWiFi();

    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
    static constexpr uint32_t NTP_TIMEOUT_MS          = 10000;
    static constexpr uint32_t RESYNC_INTERVAL_MS      = 6 * 3600 * 1000UL;
    static constexpr uint32_t SHORT_RETRY_DELAY_MS    = 15 * 60 * 1000UL;
    static constexpr uint32_t LONG_RETRY_DELAY_MS     = 60 * 60 * 1000UL;
    static constexpr uint8_t  MAX_CONSECUTIVE_RETRIES = 3;

    SyncState state;
    uint32_t state_start_ms;
    uint32_t idle_start_ms;
    uint32_t idle_duration_ms;
    uint8_t retry_count;
    TimeSyncedCallback on_time_synced;

    bool keep_alive_locked = false;
    bool pending_shutdown = false;
};
