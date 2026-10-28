/**
 * @file main.cpp
 * @brief Leçon 01 — Mon premier programme VoiceKit
 * 
 * Objectif : Initialiser la façade VoiceKit et établir une session sécurisée
 *            avec le service Google Gemini Live sur M5Stack CoreS3.
 * 
 * Cette leçon initialise le haut-parleur au volume par défaut de VoiceKit, sans activer le microphone. Elle se concentre
 * sur la connexion réseau Wi-Fi, la synchronisation horaire et l'initialisation
 * de la machine à états de VoiceKit.
 */

#include <Arduino.h>
#include <M5Unified.h>

// Chargement sécurisé des identifiants (Wi-Fi et clé API) avant VoiceKit
#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #include "secrets.example.h"
#endif

#include <VoiceKit.h>
#include <WiFi.h>

#ifndef GEMINI_KEY
    #define GEMINI_KEY ""
#endif
#ifndef WIFI_PASSWORD
    #define WIFI_PASSWORD ""
#endif

// Déclaration de l'instance unique de VoiceKit
static VoiceKit voice;

// Mémorisation de l'état précédent pour n'afficher que les changements
static VoiceState s_lastState = VoiceState::UNINITIALIZED;

void setup() {
    // 1. Initialisation de la console série à 115200 bauds
    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  Tutoriel VoiceKit — Leçon 01 : Hello VoiceKit        ");
    Serial.println("  Initialisation et connexion à Google Gemini Live      ");
    Serial.println("========================================================");

    // 2. Initialisation matérielle du M5Stack CoreS3
    // VoiceKit exige une sortie audio initialisée dès voice.begin().
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = false;
    M5.begin(cfg);

    // Affichage d'accueil sur l'écran du CoreS3
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString("VoiceKit - Lecon 01", 10, 20);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("Connexion en cours...", 10, 60);

    // Vérification de sécurité des identifiants
    if (String(GEMINI_KEY) == "VOTRE_CLE_GEMINI_LIVE" || String(GEMINI_KEY).isEmpty()) {
        Serial.println("\n[ATTENTION] Vous devez renseigner votre cle d'API Gemini Live !");
        Serial.println("Copiez include/secrets.example.h vers include/secrets.h et editez-le.");
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString("Cle API manquante !", 10, 90);
    }

    // 3. Configuration des paramètres de VoiceKit
    VoiceKitConfig config;
    config.apiKey = GEMINI_KEY;

    // En mode MANAGED_WIFI, VoiceKit prend en charge lui-même la connexion Wi-Fi
    // et la synchronisation de l'horloge système (SNTP) requise par TLS.
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;

    // Fournisseur par défaut : Google Gemini Live
    config.provider = VoiceProviderType::GEMINI;

    // 4. Enregistrement des fonctions de rappel (callbacks)

    // Déclenché lorsque la session Gemini Live est établie et prête à l'emploi
    voice.onReady([]() {
        Serial.println("[VoiceKit] >>> Callback onReady : VoiceKit est PRET ! <<<");
        M5.Display.fillRect(0, 60, 320, 180, TFT_BLACK);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.drawString("PRET !", 10, 70);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
        M5.Display.drawString("Session Gemini Live active", 10, 110);
        M5.Display.drawString("IP : " + WiFi.localIP().toString(), 10, 130);
    });

    // Déclenché en cas d'erreur de réseau, d'authentification ou de protocole
    voice.onError([](const String& erreur) {
        Serial.printf("[VoiceKit] >>> Callback onError : %s <<<\n", erreur.c_str());
        M5.Display.fillRect(0, 60, 320, 180, TFT_BLACK);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.setTextSize(1);
        M5.Display.drawString("Erreur : " + erreur, 10, 70);
    });

    // Déclenché lors de chaque transition d'état interne
    voice.onStateChanged([](VoiceState state) {
        Serial.printf("[VoiceKit] Changement d'etat : %s\n", voiceStateToString(state));
    });

    // 5. Lancement de VoiceKit
    Serial.println("[VoiceKit] Demarrage du moteur vocal avec voice.begin()...");
    if (!voice.begin(config)) {
        Serial.printf("[VoiceKit][FATAL] Echec voice.begin() : %s\n", voice.getLastError().c_str());
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString("Echec initialisation", 10, 90);
    }
}

void loop() {
    // 1. Mise à jour des composants M5Stack
    M5.update();

    // 2. Traitement périodique obligatoire de VoiceKit
    voice.update();

    // 3. Suivi visuel des transitions d'état
    VoiceState currentState = voice.getState();
    if (currentState != s_lastState) {
        s_lastState = currentState;
        if (!voice.isReady() && currentState != VoiceState::ERROR_STATE) {
            M5.Display.fillRect(0, 60, 320, 30, TFT_BLACK);
            M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
            M5.Display.setTextSize(1);
            M5.Display.drawString("Etat : " + String(voiceStateToString(currentState)), 10, 60);
        }
    }

    delay(10);
}
