/**
 * @file main.cpp
 * @brief Leçon 02 — Faire parler le CoreS3 (Text-to-Speech / TTS)
 * 
 * Objectif : Découvrir la synthèse vocale avec VoiceKit sur M5Stack CoreS3.
 *            Activer le haut-parleur interne, prononcer une phrase d'accueil
 *            dès que le moteur est prêt, et suivre les événements de parole.
 */

#include <Arduino.h>
#include <M5Unified.h>

// Chargement sécurisé des identifiants (Wi-Fi et clé API)
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

// Instance unique de VoiceKit
static VoiceKit voice;

// Indicateur pour ne prononcer la phrase d'accueil qu'une seule fois
static bool s_welcomeSpoken = false;

// Phrase de test initiale
static const char* FIRST_PHRASE = "Bonjour, je suis VoiceKit !";

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  Tutoriel VoiceKit — Leçon 02 : Text-to-Speech (TTS)  ");
    Serial.println("  Faire parler le CoreS3 avec Google Gemini Live       ");
    Serial.println("========================================================");

    // 1. Initialisation matérielle avec activation du haut-parleur interne
    auto cfg = M5.config();
    cfg.internal_spk = true;   // Active l'amplificateur AW88298 et le haut-parleur
    cfg.internal_mic = false;  // Le microphone n'est pas encore utilisé dans cette leçon
    M5.begin(cfg);

    // Affichage d'accueil sur l'écran du CoreS3
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString("VoiceKit - Lecon 02", 10, 15);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("Synthese Vocale (TTS)", 10, 45);
    M5.Display.drawString("Connexion en cours...", 10, 75);

    // 2. Configuration de VoiceKit
    VoiceKitConfig config;
    config.apiKey = GEMINI_KEY;
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    config.provider = VoiceProviderType::GEMINI;

    // Réglage du volume sonore initial (entre 0 et 100 %)
    config.volume = 70;

    // Voix et langue française
    config.voice = "Aoede";
    config.language = "fr-FR";

    // 3. Enregistrement des callbacks

    // Callback onReady : déclenché dès que la session est prête
    voice.onReady([]() {
        Serial.println("[VoiceKit] Moteur connecte et pret !");
        M5.Display.fillRect(0, 75, 320, 165, TFT_BLACK);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.drawString("PRET !", 10, 80);
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
        M5.Display.drawString("Generation de la parole en cours...", 10, 120);
    });

    // Callback onSpeechStarted : le son commence à sortir du haut-parleur
    voice.onSpeechStarted([]() {
        Serial.println("[VoiceKit] >>> Callback onSpeechStarted : Restitution vocale active !");
        M5.Display.fillRect(0, 115, 320, 60, TFT_BLACK);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.setTextSize(1);
        M5.Display.drawString("Parole en cours (haut-parleur)...", 10, 120);
    });

    // Callback onSpeechFinished : la phrase est totalement terminée
    voice.onSpeechFinished([]() {
        Serial.println("[VoiceKit] >>> Callback onSpeechFinished : Restitution vocale terminee !");
        M5.Display.fillRect(0, 115, 320, 60, TFT_BLACK);
        M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        M5.Display.setTextSize(1);
        M5.Display.drawString("Parole terminee. Touchez l'ecran pour rejouer.", 10, 120);
    });

    // Callback onError : en cas d'incident
    voice.onError([](const String& err) {
        Serial.printf("[VoiceKit] Erreur : %s\n", err.c_str());
        M5.Display.fillRect(0, 75, 320, 165, TFT_BLACK);
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString("Erreur : " + err, 10, 80);
    });

    // 4. Démarrage de VoiceKit
    Serial.println("[VoiceKit] Initialisation du moteur vocal...");
    if (!voice.begin(config)) {
        Serial.printf("[VoiceKit][FATAL] Echec voice.begin() : %s\n", voice.getLastError().c_str());
    }
}

void loop() {
    M5.update();
    voice.update();

    // 1. Premier test : prononcer la phrase d'accueil dès que VoiceKit est prêt
    // Nous vérifions à la fois que le moteur est prêt (isReady()) et qu'il n'est
    // pas déjà en train de parler (!isSpeaking()) avant d'envoyer say().
    if (!s_welcomeSpoken && voice.isReady() && !voice.isSpeaking()) {
        s_welcomeSpoken = true;
        Serial.printf("[VoiceKit] Envoi de la premiere phrase : \"%s\"\n", FIRST_PHRASE);
        
        bool ok = voice.say(FIRST_PHRASE);
        if (!ok) {
            Serial.println("[VoiceKit] La requete say() a ete refusee (moteur occupe).");
        }
    }

    // 2. Déclenchement manuel : touchez l'écran pour rejouer la phrase
    auto touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
        if (voice.isReady() && !voice.isSpeaking()) {
            Serial.println("[Touch] Ecran touche -> Rejeu de la phrase TTS");
            voice.say(FIRST_PHRASE);
        } else if (voice.isSpeaking()) {
            Serial.println("[Touch] Ecran touche pendant la parole -> Le moteur est occupe !");
        } else {
            Serial.println("[Touch] Ecran touche mais le moteur n'est pas encore pret.");
        }
    }

    delay(10);
}
