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
static constexpr uint32_t WIFI_VOICE_TIMEOUT_MS = 15000;
static constexpr uint32_t WIFI_KEEPALIVE_POST_SPEECH_MS = 15000; // Maintien radio chaud 15 s après parole

// Prononciation vocale de l'heure
static void announceCurrentTime() {
    if (voice.isBusy() || voice_req_state != VoiceRequestState::IDLE) {
        Serial.println("[ClockVoice] Demande vocale ignoree : requete deja en cours ou parole active.");
        return;
    }

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    String speechText;
    if (timeinfo.tm_year < 124) {
        Serial.println("[ClockVoice] Heure non synchronisee (< 2024).");
        speechText = "L'heure n'est pas encore synchronisée.";
    } else {
        speechText = formatTimeInFrench(timeinfo.tm_hour, timeinfo.tm_min);
    }

    Serial.printf("[ClockVoice] Annonce vocale demandee: \"%s\"\n", speechText.c_str());

    // Verrouiller formellement le maintien radio Wi-Fi pour VoiceKit
    wifi_keepalive_until_ms = 0;
    wifi_sync.setKeepAliveLock(true);

    if (WiFi.status() == WL_CONNECTED && VoxCoreVoice::isSystemTimeValid()) {
        // Wi-Fi déjà prêt et heure système valide pour TLS : envoi direct
        bool accepted = voice.say(speechText);
        if (accepted) {
            voice_req_state = VoiceRequestState::ANNOUNCING;
            pending_speech_text = "";
        } else {
            Serial.printf("[ClockVoice] voice.say() refuse immediatement : %s\n", voice.getLastError().c_str());
            wifi_sync.setKeepAliveLock(false);
            voice_req_state = VoiceRequestState::IDLE;
            pending_speech_text = "";
        }
    } else {
        // Wi-Fi éteint ou en cours de réveil : mise en attente asynchrone sécurisée
        Serial.println("[ClockVoice] Wi-Fi inactif : demande de reconnexion et mise en attente de l'annonce...");
        pending_speech_text = speechText;
        voice_req_state = VoiceRequestState::WAITING_WIFI;
        voice_req_start_ms = millis();
        if (ui_ready) {
            clock_ui.setVoiceIndicator(VoiceState::CONNECTING_WIFI);
        }
        wifi_sync.requestWiFiForVoice();
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=======================================================");
    Serial.println("  CoreS3 Orientation Clock + VoxCore VoiceKit v0.1.0");
    Serial.println("=======================================================\n");

    // 1. Initialisation matérielle CoreS3 par l'application hôte
    auto cfg = M5.config();
    cfg.internal_imu = true;
    cfg.internal_rtc = true;
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
    if (ui_ready) {
        char initial_time[8];
        clock_mgr.getTimeString(initial_time, sizeof(initial_time));
        clock_ui.setTime(initial_time);
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
    vConfig.model = "models/gemini-2.0-flash-exp";
    vConfig.wifiMode = WiFiMode::EXTERNAL_WIFI; // VoiceKit s'adapte au Wi-Fi de l'horloge
    vConfig.hardwareAlreadyInitialized = true;  // M5.begin() déjà effectué
    vConfig.connectOnDemand = true;             // Connexion à la demande pour batterie
    vConfig.autoDisconnectIdleMs = 15000;       // Déconnexion après 15 s d'inactivité
    vConfig.volume = 75;

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
            clock_ui.setVoiceIndicator(VoiceState::READY);
        }
        voice_req_state = VoiceRequestState::IDLE;
        // Maintien radio chaud 15 s pour que tout appui consécutif soit immédiat (< 800 ms)
        wifi_keepalive_until_ms = millis() + WIFI_KEEPALIVE_POST_SPEECH_MS;
    });

    voice.onError([](const String& err) {
        Serial.printf("[ClockVoice] Erreur vocale : %s\n", err.c_str());
        if (ui_ready) {
            clock_ui.setVoiceIndicator(VoiceState::ERROR_STATE);
        }
        voice_req_state = VoiceRequestState::IDLE;
        pending_speech_text = "";
        wifi_keepalive_until_ms = 0;
        wifi_sync.setKeepAliveLock(false);
    });

    voice.begin(vConfig);

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
        }
    }

    // 4. Mise à jour de la synchronisation Wi-Fi
    wifi_sync.update();

    // 5. Mise à jour du moteur vocal VoiceKit
    voice.update();

    // 5b. Traitement asynchrone d'une annonce en attente de la reconnexion Wi-Fi
    if (voice_req_state == VoiceRequestState::WAITING_WIFI) {
        if (WiFi.status() == WL_CONNECTED && VoxCoreVoice::isSystemTimeValid()) {
            Serial.printf("[ClockVoice] Wi-Fi connecte (%u ms) -> Lancement de voice.say(\"%s\")...\n",
                          millis() - voice_req_start_ms, pending_speech_text.c_str());
            if (ui_ready) {
                clock_ui.setVoiceIndicator(VoiceState::GENERATING);
            }
            bool accepted = voice.say(pending_speech_text);
            if (accepted) {
                voice_req_state = VoiceRequestState::ANNOUNCING;
                pending_speech_text = "";
            } else {
                Serial.printf("[ClockVoice] voice.say() refuse apres reconnexion Wi-Fi : %s\n",
                              voice.getLastError().c_str());
                if (ui_ready) {
                    clock_ui.setVoiceIndicator(VoiceState::ERROR_STATE);
                }
                voice_req_state = VoiceRequestState::IDLE;
                pending_speech_text = "";
                wifi_sync.setKeepAliveLock(false);
            }
        } else if (millis() - voice_req_start_ms > WIFI_VOICE_TIMEOUT_MS) {
            Serial.println("[ClockVoice] Timeout de reconnexion Wi-Fi pour annonce vocale.");
            if (ui_ready) {
                clock_ui.setVoiceIndicator(VoiceState::ERROR_STATE);
            }
            voice_req_state = VoiceRequestState::IDLE;
            pending_speech_text = "";
            wifi_sync.setKeepAliveLock(false);
        }
    }

    // 5c. Libération différée du verrou Wi-Fi après 15 s d'inactivité
    if (wifi_keepalive_until_ms > 0 && millis() >= wifi_keepalive_until_ms) {
        wifi_keepalive_until_ms = 0;
        if (!voice.isBusy() && voice_req_state == VoiceRequestState::IDLE) {
            Serial.println("[ClockVoice] Fin du maintien Wi-Fi chaud (15 s) -> Liberation du verrou Wi-Fi");
            wifi_sync.setKeepAliveLock(false);
        }
    }

    // 6. Détection de l'appui tactile pour annoncer l'heure
    bool screenTouched = false;
    if (M5.Touch.getCount() > 0) {
        auto detail = M5.Touch.getDetail(0);
        if (detail.wasClicked() || detail.wasPressed()) {
            screenTouched = true;
        }
    } else {
        auto detail = M5.Touch.getDetail();
        if (detail.wasClicked() || detail.wasPressed()) {
            screenTouched = true;
        }
    }

    // Support des zones tactiles virtuelles du bandeau / boutons CoreS3
    if (M5.BtnA.wasClicked() || M5.BtnA.wasPressed() ||
        M5.BtnB.wasClicked() || M5.BtnB.wasPressed() ||
        M5.BtnC.wasClicked() || M5.BtnC.wasPressed() ||
        M5.BtnPWR.wasClicked()) {
        screenTouched = true;
    }

    if (screenTouched) {
        announceCurrentTime();
    }

    delay(5);
}
