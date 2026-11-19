/**
 * @file main.cpp
 * @brief Démonstration OrientationClockVoice — CoreS3 Orientation Clock intégrant VoxCore VoiceKit
 * 
 * L'application conserve :
 * - Son interface minimaliste LVGL 9
 * - Son orientation automatique avec BMI270
 * - Sa gestion matérielle RTC et NTP
 * - Sa rotation matérielle M5GFX
 * - Son fonctionnement continu en cas d'indisponibilité du Wi-Fi ou de Gemini
 * 
 * Ajout VoiceKit :
 * - Appui tactile sur l'écran -> annonce de l'heure courante en français ("Il est vingt heures neuf.")
 * - Coordination Wi-Fi pour empêcher la coupure radio pendant la parole
 * - Indicateur visuel discret dans le coin sans dénaturer le cadran
 */

#include <Arduino.h>
#include <M5Unified.h>

#include "DeviceOrientation.h"
#include "OrientationManager.h"
#include "ClockManager.h"
#include "WiFiTimeSync.h"
#include "ClockUI.h"
#include "FrenchTimeFormatter.h"
#include "AlarmManager.h"
#include "CachedAudioOutput.h"
#include <VoxCoreVoice.h>

#if __has_include("wifi_config.h")
    #include "wifi_config.h"
#else
    #include "wifi_config.h.example"
#endif

// Composants de l'horloge
static OrientationManager orientation_mgr;
static ClockManager clock_mgr;
static WiFiTimeSync wifi_sync;
static ClockUI clock_ui;
static AlarmManager alarm_mgr;
static CachedAudioOutput audio_cache;

// Composant vocal VoiceKit invité
static VoxCoreVoice voice;

static bool ui_ready = false;
static bool imu_ready = false;

enum class VoiceRequestState {
    IDLE,
    WAITING_WIFI,
    ANNOUNCING
};

static VoiceRequestState voice_req_state = VoiceRequestState::IDLE;
static String pending_speech_text = "";
static uint32_t voice_req_start_ms = 0;
static uint32_t wifi_keepalive_until_ms = 0;
static String cached_speech_text = "";
static uint8_t voice_volume = 75;
static bool alarm_ringing = false;
static uint32_t next_alarm_tone_ms = 0;
static constexpr uint32_t WIFI_VOICE_TIMEOUT_MS = 15000;
static constexpr uint32_t WIFI_KEEPALIVE_POST_SPEECH_MS = 15000; // Maintien radio chaud 15 s après parole

// Prononciation vocale de l'heure
static void showOfflineFallback(const char* reason) {
    Serial.printf("[ClockVoice] Secours local: %s\n", reason);
    if (cached_speech_text.length() > 0 && audio_cache.playCached(cached_speech_text)) {
        if (ui_ready) clock_ui.setStatus("Lecture du cache hors ligne", 0x38BDF8);
        return;
    }
    M5.Speaker.tone(880, 90);
    if (ui_ready) clock_ui.setStatus("Hors ligne - heure a l'ecran", 0xEA4335);
}

static void requestSpeech(const String& speechText) {
    if (voice.isBusy() || audio_cache.isReplaying() || voice_req_state != VoiceRequestState::IDLE) {
        Serial.println("[ClockVoice] Demande vocale ignoree : requete deja en cours ou parole active.");
        return;
    }

    cached_speech_text = speechText;
    Serial.printf("[ClockVoice] Annonce vocale demandee: \"%s\"\n", speechText.c_str());

    // Une phrase deja en cache est immediate lorsque le reseau est absent.
    if (WiFi.status() != WL_CONNECTED && audio_cache.playCached(speechText)) {
        if (ui_ready) clock_ui.setStatus("Voix hors ligne (cache)", 0x38BDF8);
        return;
    }

    // Verrouiller formellement le maintien radio Wi-Fi pour VoiceKit
    wifi_keepalive_until_ms = 0;
    wifi_sync.setKeepAliveLock(true);

    if (WiFi.status() == WL_CONNECTED && VoxCoreVoice::isSystemTimeValid()) {
        audio_cache.startCapture(speechText);
        bool accepted = voice.say(speechText);
        if (accepted) {
            voice_req_state = VoiceRequestState::ANNOUNCING;
            pending_speech_text = "";
        } else {
            audio_cache.cancelCapture();
            Serial.printf("[ClockVoice] voice.say() refuse immediatement : %s\n", voice.getLastError().c_str());
            wifi_sync.setKeepAliveLock(false);
            voice_req_state = VoiceRequestState::IDLE;
            pending_speech_text = "";
            showOfflineFallback("requete refusee");
        }
    } else {
        pending_speech_text = speechText;
        voice_req_state = VoiceRequestState::WAITING_WIFI;
        voice_req_start_ms = millis();
        if (ui_ready) clock_ui.setVoiceIndicator(VoiceState::CONNECTING_WIFI);
        wifi_sync.requestWiFiForVoice();
    }
}

static void announceCurrentTime() {
    int current_hour = 0;
    int current_min = 0;
    String speechText;
    if (!clock_mgr.hasValidTime() || !clock_mgr.getLocalTime(current_hour, current_min)) {
        Serial.println("[ClockVoice] Heure non disponible dans ClockManager.");
        speechText = "L'heure n'est pas encore synchronisée.";
    } else {
        speechText = formatTimeInFrench(current_hour, current_min, TimeSpeechStyle::NATURAL);
    }
    requestSpeech(speechText);
}

static void refreshAlarmUI() {
    if (!ui_ready) return;
    String alarm_text = alarm_mgr.displayText();
    clock_ui.setAlarm(alarm_text.c_str(), alarm_mgr.isEnabled());
}

static void adjustVolume(int delta) {
    int next = constrain((int)voice_volume + delta, 10, 100);
    voice_volume = (uint8_t)next;
    voice.setVolume(voice_volume);
    char status[24];
    snprintf(status, sizeof(status), "Volume %u %%", voice_volume);
    if (ui_ready) clock_ui.setStatus(status, 0x38BDF8);
    M5.Speaker.tone(1200, 35);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=======================================================");
    Serial.println("  OrientationClockVoice V2.1 + VoxCore VoiceKit");
    Serial.println("=======================================================\n");

    // 1. Initialisation matérielle CoreS3 par l'application hôte
    auto cfg = M5.config();
    cfg.internal_imu = true;
    cfg.internal_rtc = true;
    cfg.internal_spk = true;
    M5.begin(cfg);

    // 2. Initialisation de l'orientation BMI270
    imu_ready = orientation_mgr.init();
    DeviceOrientation initial_orient = orientation_mgr.getOrientation();
    if (!imu_ready) {
        Serial.println("[Clock] WARN: Echec init IMU, position par defaut PAYSAGE_NORMAL");
    }

    // 3. Initialisation de l'interface LVGL 9
    ui_ready = clock_ui.init(initial_orient);
    if (!ui_ready) {
        Serial.println("[Clock] ERROR: Echec init ClockUI !");
    }

    // 4. Initialisation de l'horloge et du RTC
    clock_mgr.init();
    alarm_mgr.init();
    if (ui_ready) {
        char initial_time[8];
        clock_mgr.getTimeString(initial_time, sizeof(initial_time));
        clock_ui.setTime(initial_time);
        refreshAlarmUI();
    }

    // 5. Initialisation Wi-Fi et synchronisation NTP de l'horloge
    wifi_sync.init();
    wifi_sync.setOnTimeSynced([](const tm& utc_timeinfo) {
        clock_mgr.saveToRtc(utc_timeinfo);
        if (ui_ready) {
            char synced_time[8];
            if (clock_mgr.getTimeString(synced_time, sizeof(synced_time))) {
                clock_ui.setTime(synced_time);
            }
        }
    });

    // 6. Initialisation de VoxCore VoiceKit en composant invité (EXTERNAL_WIFI)
    VoiceKitConfig vConfig;
    vConfig.apiKey = GEMINI_KEY;
#ifdef GEMINI_MODEL
    vConfig.model = GEMINI_MODEL;
#else
    vConfig.model = VOICEKIT_DEFAULT_MODEL;
#endif
    vConfig.wifiMode = WiFiMode::EXTERNAL_WIFI; // VoiceKit s'adapte au Wi-Fi de l'horloge
    vConfig.hardwareAlreadyInitialized = true;  // M5.begin() déjà effectué
    vConfig.connectOnDemand = true;             // Connexion à la demande pour batterie
    vConfig.autoDisconnectIdleMs = 15000;       // Déconnexion après 15 s d'inactivité
    vConfig.volume = voice_volume;
    vConfig.audioOutput = &audio_cache;

    // Synchronisation de l'indicateur d'état LVGL
    voice.onStateChanged([](VoiceState state) {
        if (ui_ready) {
            clock_ui.setVoiceIndicator(state);
        }
    });

    voice.onSpeechStarted([]() {
        Serial.println("[ClockVoice] Restitution vocale de l'heure en cours...");
        if (ui_ready) {
            clock_ui.setVoiceIndicator(VoiceState::SPEAKING);
        }
    });

    voice.onSpeechFinished([]() {
        Serial.println("[ClockVoice] Annonce terminee. Maintien Wi-Fi chaud pendant 15 s...");
        if (ui_ready) {
            if (alarm_ringing) {
                clock_ui.setStatus("Tap: +5 min | long: stop", 0xFBBF24);
            } else {
                clock_ui.setVoiceIndicator(VoiceState::READY);
            }
        }
        voice_req_state = VoiceRequestState::IDLE;
        audio_cache.finishCapture();
        // Maintien radio chaud 15 s pour que tout appui consécutif soit immédiat (< 800 ms)
        wifi_keepalive_until_ms = millis() + WIFI_KEEPALIVE_POST_SPEECH_MS;
    });

    voice.onError([](const String& err) {
        Serial.printf("[ClockVoice] Erreur vocale : %s\n", err.c_str());
        if (ui_ready) {
            clock_ui.setVoiceIndicator(VoiceState::ERROR_STATE);
        }
        voice_req_state = VoiceRequestState::IDLE;
        audio_cache.cancelCapture();
        pending_speech_text = "";
        wifi_keepalive_until_ms = 0;
        wifi_sync.setKeepAliveLock(false);
        showOfflineFallback("erreur VoiceKit");
    });

    voice.begin(vConfig);
    cached_speech_text = audio_cache.cachedText();
    if (cached_speech_text.length() > 0) {
        Serial.printf("[AudioCache] Phrase restauree: \"%s\"\n", cached_speech_text.c_str());
    }

    // 7. Lancement de la première synchronisation horaire Wi-Fi
    wifi_sync.startSync();

    Serial.println("[Clock] Horloge prete. Touchez l'ecran pour entendre l'heure !");
}

void loop() {
    M5.update();

    // 1. Mise à jour de l'affichage LVGL 9
    if (ui_ready) {
        clock_ui.update();
    }

    // 2. Gestion de l'orientation BMI270
    if (imu_ready) {
        DeviceOrientation new_orient;
        if (orientation_mgr.update(&new_orient)) {
            if (ui_ready) {
                clock_ui.setOrientation(new_orient);
            }
        }
    }

    // 3. Rafraîchissement des chiffres de l'horloge
    char current_time[8];
    if (clock_mgr.update(current_time, sizeof(current_time))) {
        if (ui_ready) {
            clock_ui.setTime(current_time);
            time_t now = time(nullptr);
            struct tm local_time;
            localtime_r(&now, &local_time);
            String date_text = formatDateInFrench(local_time);
            clock_ui.setDate(date_text.c_str());
        }
    }

    // 3b. Reveil local persistant jusqu'au snooze ou a l'arret.
    if (alarm_mgr.update()) {
        alarm_ringing = true;
        next_alarm_tone_ms = 0;
        if (ui_ready) {
            clock_ui.hideAlarmEditor();
            clock_ui.setStatus("Tap: +5 min | long: stop", 0xFBBF24);
        }
        if (!voice.isBusy() && voice_req_state == VoiceRequestState::IDLE) {
            requestSpeech("C'est l'heure. Bonjour !");
        }
    }

    // 4. Mise à jour de la synchronisation Wi-Fi
    wifi_sync.update();

    // 5. Mise à jour du moteur vocal VoiceKit
    voice.update();
    audio_cache.update();

    if (alarm_ringing && !voice.isSpeaking() && !audio_cache.isReplaying() &&
        (int32_t)(millis() - next_alarm_tone_ms) >= 0) {
        M5.Speaker.tone(1200, 500);
        next_alarm_tone_ms = millis() + 1400;
    }

    // 5b. Traitement asynchrone d'une annonce en attente de la reconnexion Wi-Fi
    if (voice_req_state == VoiceRequestState::WAITING_WIFI) {
        if (WiFi.status() == WL_CONNECTED && VoxCoreVoice::isSystemTimeValid()) {
            Serial.printf("[ClockVoice] Wi-Fi connecte (%u ms) -> Lancement de voice.say(\"%s\")...\n",
                          millis() - voice_req_start_ms, pending_speech_text.c_str());
            if (ui_ready) {
                clock_ui.setVoiceIndicator(VoiceState::GENERATING);
            }
            audio_cache.startCapture(pending_speech_text);
            bool accepted = voice.say(pending_speech_text);
            if (accepted) {
                voice_req_state = VoiceRequestState::ANNOUNCING;
                pending_speech_text = "";
            } else {
                audio_cache.cancelCapture();
                Serial.printf("[ClockVoice] voice.say() refuse apres reconnexion Wi-Fi : %s\n",
                              voice.getLastError().c_str());
                if (ui_ready) {
                    clock_ui.setVoiceIndicator(VoiceState::ERROR_STATE);
                }
                voice_req_state = VoiceRequestState::IDLE;
                pending_speech_text = "";
                wifi_sync.setKeepAliveLock(false);
                showOfflineFallback("requete refusee apres connexion");
            }
        } else if (millis() - voice_req_start_ms > WIFI_VOICE_TIMEOUT_MS || wifi_sync.getState() == SyncState::FAILED) {
            Serial.println("[ClockVoice] Echec ou timeout de reconnexion Wi-Fi pour annonce vocale.");
            if (ui_ready) {
                clock_ui.setVoiceIndicator(VoiceState::ERROR_STATE);
            }
            voice_req_state = VoiceRequestState::IDLE;
            pending_speech_text = "";
            wifi_keepalive_until_ms = 0;
            wifi_sync.setKeepAliveLock(false);
            showOfflineFallback("connexion impossible");
        }
    }

    // 5c. Securite verrou Wi-Fi : si l'annonce s'est achevee ou a ete interrompue
    if (voice_req_state == VoiceRequestState::ANNOUNCING && !voice.isBusy() && wifi_keepalive_until_ms == 0) {
        voice_req_state = VoiceRequestState::IDLE;
        wifi_keepalive_until_ms = millis() + WIFI_KEEPALIVE_POST_SPEECH_MS;
    }

    // 5d. Liberation differee du verrou Wi-Fi apres 15 s d'inactivite
    if (wifi_keepalive_until_ms > 0 && (int32_t)(millis() - wifi_keepalive_until_ms) >= 0) {
        wifi_keepalive_until_ms = 0;
        if (!voice.isBusy() && voice_req_state == VoiceRequestState::IDLE) {
            Serial.println("[ClockVoice] Fin du maintien Wi-Fi chaud (15 s) -> Liberation du verrou Wi-Fi");
            wifi_sync.setKeepAliveLock(false);
        }
    }

    // 6. Gestes : tap=heure, appui long=toggle reveil, swipe vertical=volume.
    static bool touch_active = false;
    static int32_t touch_start_x = 0, touch_start_y = 0;
    static int32_t touch_last_x = 0, touch_last_y = 0;
    static uint32_t touch_start_ms = 0;
    bool modal_touch_handled = false;
    auto touch = M5.Touch.getDetail();
    if (touch.wasPressed() && !touch_active) {
        touch_active = true;
        touch_start_x = touch_last_x = touch.x;
        touch_start_y = touch_last_y = touch.y;
        touch_start_ms = millis();
    } else if (touch_active && touch.isPressed()) {
        touch_last_x = touch.x;
        touch_last_y = touch.y;
    } else if (touch_active && (touch.wasReleased() || !touch.isPressed())) {
        touch_active = false;
        int32_t dx = touch_last_x - touch_start_x;
        int32_t dy = touch_last_y - touch_start_y;
        uint32_t duration = millis() - touch_start_ms;
        bool in_virtual_buttons = touch_start_y >= clock_ui.getDisplayHeight() - 38;

        if (alarm_ringing) {
            modal_touch_handled = true;
            alarm_ringing = false;
            if (voice.isBusy()) voice.stop();
            audio_cache.stop(0, AudioStopReason::USER_STOP);
            audio_cache.cancelCapture();
            if (duration >= 800) {
                alarm_mgr.cancelSnooze();
                if (ui_ready) clock_ui.setStatus("Reveil arrete", 0x94A3B8);
            } else {
                alarm_mgr.snooze(5);
                if (ui_ready) clock_ui.setStatus("Snooze: rappel dans 5 min", 0xFBBF24);
            }
        } else if (clock_ui.isAlarmEditorVisible()) {
            modal_touch_handled = true;
            int32_t width = clock_ui.getDisplayWidth();
            int32_t height = clock_ui.getDisplayHeight();
            if (touch_last_y >= height - 60 && width > 0) {
                int column = constrain((touch_last_x * 6) / width, 0, 5);
                switch (column) {
                    case 0: alarm_mgr.shiftMinutes(-60); break;
                    case 1: alarm_mgr.shiftMinutes(60); break;
                    case 2: alarm_mgr.shiftMinutes(-5); break;
                    case 3: alarm_mgr.shiftMinutes(5); break;
                    case 4: alarm_mgr.toggle(); break;
                    case 5: clock_ui.hideAlarmEditor(); break;
                }
                refreshAlarmUI();
                if (clock_ui.isAlarmEditorVisible()) {
                    clock_ui.updateAlarmEditor(alarm_mgr.hour(), alarm_mgr.minute(), alarm_mgr.isEnabled());
                }
            }
        } else if (!in_virtual_buttons && abs(dy) >= 45 && abs(dy) > abs(dx)) {
            adjustVolume(dy < 0 ? 10 : -10);
        } else if (!in_virtual_buttons && duration >= 800) {
            clock_ui.showAlarmEditor(alarm_mgr.hour(), alarm_mgr.minute(), alarm_mgr.isEnabled());
        } else if (!in_virtual_buttons) {
            announceCurrentTime();
        }
    }

    // Boutons virtuels : A/C reglent le reveil, B l'active, PWR repete le message cache.
    if (!modal_touch_handled && !alarm_ringing && !clock_ui.isAlarmEditorVisible() && M5.BtnA.wasClicked()) {
        alarm_mgr.shiftMinutes(-15);
        refreshAlarmUI();
    }
    if (!modal_touch_handled && !alarm_ringing && !clock_ui.isAlarmEditorVisible() && M5.BtnB.wasClicked()) {
        alarm_mgr.toggle();
        refreshAlarmUI();
    }
    if (!modal_touch_handled && !alarm_ringing && !clock_ui.isAlarmEditorVisible() && M5.BtnC.wasClicked()) {
        alarm_mgr.shiftMinutes(15);
        refreshAlarmUI();
    }
    if (M5.BtnPWR.wasClicked()) {
        if (cached_speech_text.length() > 0 && audio_cache.playCached(cached_speech_text)) {
            if (ui_ready) clock_ui.setStatus("Repetition hors ligne", 0x38BDF8);
        } else if (cached_speech_text.length() > 0) requestSpeech(cached_speech_text);
        else announceCurrentTime();
    }

    delay(5);
}
