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

// Prononciation vocale de l'heure
static void announceCurrentTime() {
    if (voice.isBusy()) {
        Serial.println("[ClockVoice] Demande vocale ignoree : parole deja en cours.");
        return;
    }

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Si l'heure système n'est pas encore synchronisée, utiliser l'heure brute
    int hour = timeinfo.tm_hour;
    int min = timeinfo.tm_min;

    String speechText = formatTimeInFrench(hour, min);
    Serial.printf("[ClockVoice] Annonce vocale demandee: \"%s\"\n", speechText.c_str());

    // Coordination Wi-Fi : verrouiller le maintien radio et réveiller le Wi-Fi si nécessaire
    wifi_sync.setKeepAliveLock(true);

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[ClockVoice] Wi-Fi inactif : demande de reconnexion a WiFiTimeSync...");
        wifi_sync.requestWiFiForVoice();
    }

    bool accepted = voice.say(speechText);
    if (!accepted) {
        Serial.printf("[ClockVoice] voice.say() refuse : %s\n", voice.getLastError().c_str());
        // Si la demande est immédiatement rejetée (ex: pas de Wi-Fi), libérer le verrou
        if (WiFi.status() != WL_CONNECTED) {
            wifi_sync.setKeepAliveLock(false);
        }
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
        Serial.println("[ClockVoice] Annonce terminee. Liberation du verrou Wi-Fi.");
        if (ui_ready) {
            clock_ui.setVoiceIndicator(VoiceState::READY);
        }
        // Coordination Wi-Fi : libération du verrou une fois la phrase intégralement émise
        wifi_sync.setKeepAliveLock(false);
    });

    voice.onError([](const String& err) {
        Serial.printf("[ClockVoice] Erreur vocale : %s\n", err.c_str());
        if (ui_ready) {
            clock_ui.setVoiceIndicator(VoiceState::ERROR_STATE);
        }
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

    // 6. Détection de l'appui tactile pour annoncer l'heure
    auto detail = M5.Touch.getDetail();
    if (detail.wasClicked() || detail.wasPressed()) {
        announceCurrentTime();
    }

    delay(5);
}
