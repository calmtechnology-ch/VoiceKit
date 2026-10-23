/**
 * @file main.cpp
 * @brief Exemple MinimalTTS — Démonstration de base de VoxCore VoiceKit v0.1.0
 * Matériel : M5Stack CoreS3 (ESP32-S3)
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>
#include <WiFi.h>
#include <time.h>

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #include "secrets.example.h"
#endif

static VoxCoreVoice voice;
static bool announcedWelcome = false;
static bool voiceInitialized = false;
static bool ntpStarted = false;
static bool pendingTest = false;
static uint32_t pendingTestStartMs = 0;
static constexpr uint32_t REQUEST_TIMEOUT_MS = 60000;

static bool canAnnounce() {
    return voiceInitialized && WiFi.status() == WL_CONNECTED &&
           VoxCoreVoice::isSystemTimeValid() && voice.isReady() && !voice.isBusy();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n==================================================");
    Serial.println("  VoxCore VoiceKit v0.1.0.1 — Exemple MinimalTTS");
    Serial.println("==================================================");

    // 1. Initialisation matérielle de l'application hôte
    auto cfg = M5.config();
    cfg.internal_spk = true;
    M5.begin(cfg);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("VoxCore VoiceKit", 10, 20);
    M5.Display.setTextSize(1);
    M5.Display.drawString("Appuyez sur l'ecran pour parler", 10, 50);

    // 2. Configuration de VoiceKit
    VoiceKitConfig vConfig;
    vConfig.apiKey = GEMINI_KEY;
#ifdef GEMINI_MODEL
    vConfig.model = GEMINI_MODEL;
#endif
    vConfig.wifiMode = WiFiMode::MANAGED_WIFI; // VoiceKit gère la connexion Wi-Fi
    vConfig.wifiSSID = WIFI_SSID;
    vConfig.wifiPassword = WIFI_PASSWORD;
    vConfig.volume = 70;
    vConfig.queuePolicy = QueuePolicy::REJECT_IF_BUSY;

    // Callbacks
    voice.onReady([]() {
        Serial.println("[MinimalTTS] Moteur vocal pret !");
        M5.Display.drawString("Statut: PRET                 ", 10, 80);
        M5.Display.fillRect(0, 110, M5.Display.width(), 20, TFT_BLACK);
    });

    voice.onSpeechStarted([]() {
        Serial.println("[MinimalTTS] Parole en cours...");
        M5.Display.drawString("Statut: PAROLE EN COURS...   ", 10, 80);
    });

    voice.onSpeechFinished([]() {
        Serial.println("[MinimalTTS] Fin de parole effective !");
        M5.Display.drawString("Statut: ATTENTE TOUCHE       ", 10, 80);
    });

    voice.onError([](const String& err) {
        Serial.printf("[MinimalTTS] ERREUR : %s\n", err.c_str());
        M5.Display.drawString("Erreur: " + err, 10, 110);
    });

    // 3. Démarrage du composant vocal
    voiceInitialized = voice.begin(vConfig);
    if (!voiceInitialized) {
        Serial.println("[MinimalTTS] Echec demarrage VoiceKit");
    }
}

void loop() {
    M5.update();

    // L'heure UTC doit être valide avant que VoiceKit ouvre la session TLS.
    if (voiceInitialized && !ntpStarted && WiFi.status() == WL_CONNECTED) {
        configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
        ntpStarted = true;
        M5.Display.drawString("Statut: SYNCHRONISATION...   ", 10, 80);
    }

    // Traitement périodique obligatoire
    if (voiceInitialized) voice.update();

    // Détection d'un appui tactile sur l'écran pour prononcer un test
    auto detail = M5.Touch.getDetail();
    if (detail.wasClicked() || detail.wasPressed()) {
        if (voiceInitialized && !voice.isBusy() && !pendingTest) {
            pendingTest = true;
            pendingTestStartMs = millis();
            Serial.println("[MinimalTTS] Touche detectee -> Annonce en attente du reseau et du service vocal");
            M5.Display.drawString("Statut: ANNONCE EN ATTENTE   ", 10, 80);
        } else {
            Serial.println("[MinimalTTS] Touche ignoree : moteur vocal deja occupe");
        }
    }

    if (pendingTest && millis() - pendingTestStartMs >= REQUEST_TIMEOUT_MS) {
        pendingTest = false;
        Serial.println("[MinimalTTS] Delai depasse : reseau ou service vocal indisponible");
        M5.Display.drawString("Erreur: connexion trop longue", 10, 110);
    }

    // Une seule annonce est envoyée, après vérification de tous les prérequis.
    if (canAnnounce()) {
        if (pendingTest) {
            pendingTest = false;
            announcedWelcome = true;
            voice.say("Ceci est un test de synthese vocale reussi.");
        } else if (!announcedWelcome) {
            announcedWelcome = voice.say("Bonjour, VoxCore VoiceKit est pret sur CoreS3.");
        }
    }

    delay(5);
}
