/**
 * @file main.cpp
 * @brief Exemple MinimalTTS — Démonstration de base de VoxCore VoiceKit v0.1.0
 * Matériel : M5Stack CoreS3 (ESP32-S3)
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #include "secrets.example.h"
#endif

static VoxCoreVoice voice;
static bool announcedWelcome = false;

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n==================================================");
    Serial.println("  VoxCore VoiceKit v0.1.0 — Exemple MinimalTTS");
    Serial.println("==================================================");

    // 1. Initialisation matérielle de l'application hôte
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("VoxCore VoiceKit", 10, 20);
    M5.Display.setTextSize(1);
    M5.Display.drawString("Appuyez sur l'ecran pour parler", 10, 50);

    // 2. Configuration de VoiceKit
    VoiceKitConfig vConfig;
    vConfig.apiKey = GEMINI_KEY;
    vConfig.wifiMode = WiFiMode::MANAGED_WIFI; // VoiceKit gère la connexion Wi-Fi
    vConfig.wifiSSID = WIFI_SSID;
    vConfig.wifiPassword = WIFI_PASSWORD;
    vConfig.volume = 70;
    vConfig.queuePolicy = QueuePolicy::REJECT_IF_BUSY;

    // Callbacks
    voice.onReady([]() {
        Serial.println("[MinimalTTS] Moteur vocal pret !");
        M5.Display.drawString("Statut: PRET                 ", 10, 80);
        if (!announcedWelcome) {
            announcedWelcome = true;
            voice.say("Bonjour, VoxCore VoiceKit est pret sur CoreS3.");
        }
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
    if (!voice.begin(vConfig)) {
        Serial.println("[MinimalTTS] Echec demarrage VoiceKit");
    }
}

void loop() {
    M5.update();

    // Traitement périodique obligatoire
    voice.update();

    // Détection d'un appui tactile sur l'écran pour prononcer un test
    auto detail = M5.Touch.getDetail();
    if (detail.wasClicked() || detail.wasPressed()) {
        if (!voice.isBusy()) {
            Serial.println("[MinimalTTS] Touche detectee -> Lancement de say()");
            voice.say("Ceci est un test de synthese vocale reussi.");
        } else {
            Serial.println("[MinimalTTS] Touche ignoree : moteur vocal deja occupe");
        }
    }

    delay(5);
}
