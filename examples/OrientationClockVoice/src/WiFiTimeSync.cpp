#include "WiFiTimeSync.h"

#if defined(ESP32) || defined(ARDUINO)
#include <esp_sntp.h>
#else
static inline bool sntp_enabled() { return false; }
static inline void sntp_stop() {}
#endif

static volatile bool s_sntp_event_received = false;

static void sntpNotificationCallback(struct timeval* tv) {
    (void)tv;
    s_sntp_event_received = true;
}

WiFiTimeSync::WiFiTimeSync()
    : state(SyncState::IDLE),
      state_start_ms(0),
      idle_start_ms(0),
      idle_duration_ms(0),
      retry_count(0),
      on_time_synced(nullptr),
      keep_alive_locked(false),
      pending_shutdown(false) {
}

void WiFiTimeSync::init() {
#if defined(ESP32) || defined(ARDUINO)
    setenv("TZ", TIMEZONE_POSIX, 1);
    tzset();
#endif
    shutDownWiFi();
}

void WiFiTimeSync::shutDownWiFi() {
    if (keep_alive_locked) {
        Serial.println("[WiFiTimeSync] Maintien du Wi-Fi actif : verrou VoiceKit en cours");
        pending_shutdown = true;
        return;
    }
    pending_shutdown = false;
    if (sntp_enabled()) {
        sntp_stop();
    }
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WiFiTimeSync] Radio Wi-Fi eteinte (economie d'energie)");
}

void WiFiTimeSync::setKeepAliveLock(bool lock) {
    keep_alive_locked = lock;
    if (!keep_alive_locked && pending_shutdown) {
        Serial.println("[WiFiTimeSync] Liberation du verrou VoiceKit -> Extinction differee du Wi-Fi");
        shutDownWiFi();
    }
}

void WiFiTimeSync::requestWiFiForVoice() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }
    if (state == SyncState::CONNECTING) {
        return;
    }
    Serial.println("[WiFiTimeSync] Activation Wi-Fi a la demande pour VoiceKit...");
    startSync();
}

void WiFiTimeSync::startSync() {
    if (strlen(WIFI_SSID) == 0 || strcmp(WIFI_SSID, "VOTRE_SSID_WIFI") == 0) {
        Serial.println("[WiFiTimeSync] Aucun identifiant Wi-Fi valide configure.");
        transitionToIdle(LONG_RETRY_DELAY_MS);
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        state = SyncState::SYNCING;
        state_start_ms = millis();
        s_sntp_event_received = false;
#if defined(ESP32) || defined(ARDUINO)
        configTzTime(TIMEZONE_POSIX, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
#endif
        return;
    }

    Serial.printf("[WiFiTimeSync] Connexion Wi-Fi vers '%s'...\n", WIFI_SSID);

    if (sntp_enabled()) {
        sntp_stop();
    }
    s_sntp_event_received = false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    state = SyncState::CONNECTING;
    state_start_ms = millis();
}

void WiFiTimeSync::transitionToIdle(uint32_t duration_ms) {
    shutDownWiFi();
    state = SyncState::IDLE;
    idle_start_ms = millis();
    idle_duration_ms = duration_ms;
}

void WiFiTimeSync::transitionToFailed() {
    retry_count++;
    uint32_t backoff = (retry_count >= MAX_CONSECUTIVE_RETRIES) ? LONG_RETRY_DELAY_MS : SHORT_RETRY_DELAY_MS;
    Serial.printf("[WiFiTimeSync] Echec synchronisation (%u/%u). Prochain essai dans %u min.\n",
                  retry_count, MAX_CONSECUTIVE_RETRIES, backoff / 60000U);
    transitionToIdle(backoff);
}

void WiFiTimeSync::update() {
    uint32_t now = millis();

    switch (state) {
        case SyncState::IDLE:
            if (now - idle_start_ms >= idle_duration_ms) {
                startSync();
            }
            break;

        case SyncState::CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WiFiTimeSync] Connecte ! IP: %s (en %u ms)\n",
                              WiFi.localIP().toString().c_str(), now - state_start_ms);

#if defined(ESP32) || defined(ARDUINO)
                if (sntp_enabled()) {
                    sntp_stop();
                }
                s_sntp_event_received = false;
                sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
                sntp_set_time_sync_notification_cb(sntpNotificationCallback);

                configTzTime(TIMEZONE_POSIX, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
#endif
                state = SyncState::SYNCING;
                state_start_ms = now;
            } else if (now - state_start_ms >= WIFI_CONNECT_TIMEOUT_MS) {
                Serial.println("[WiFiTimeSync] Timeout de connexion Wi-Fi.");
                transitionToFailed();
            }
            break;

        case SyncState::SYNCING: {
            if (s_sntp_event_received) {
                s_sntp_event_received = false;

                time_t now_sec = time(nullptr);
                struct tm timeinfo;
                localtime_r(&now_sec, &timeinfo);

                if (timeinfo.tm_year >= 124) {
                    Serial.printf("[WiFiTimeSync] Synchro NTP REUSSIE: %04d-%02d-%02d %02d:%02d:%02d\n",
                                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

                    retry_count = 0;
                    struct tm utc_info;
                    gmtime_r(&now_sec, &utc_info);

                    if (on_time_synced) {
                        on_time_synced(utc_info);
                    }

                    transitionToIdle(RESYNC_INTERVAL_MS);
                }
            } else if (now - state_start_ms >= NTP_TIMEOUT_MS) {
                Serial.println("[WiFiTimeSync] Timeout reponse NTP.");
                transitionToFailed();
            }
            break;
        }

        default:
            break;
    }
}
