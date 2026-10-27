/**
 * @file main.cpp
 * @brief Leçon 03 — Contrôler la synthèse vocale (Speech Control)
 * 
 * Objectif : Piloter VoiceKit en direct via le moniteur série :
 *            - Déclencher la parole à la demande (T)
 *            - Interrompre la parole immédiatement (S)
 *            - Régler le volume sonore de 0 à 100 % (+ / -)
 *            - Consulter l'état détaillé du système (?)
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

// Phrase de test
static const char* SPEECH_TEXT = "VoiceKit repond parfaitement aux commandes serie. Vous pouvez regler le volume ou interrompre la parole a tout moment.";

// Fonction d'aide pour rafraîchir l'affichage sur l'écran du CoreS3
static void updateScreen(const String& actionMsg = "") {
    M5.Display.fillRect(0, 45, 320, 195, TFT_BLACK);
    
    // État du moteur
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("Etat : ", 10, 50);
    
    uint16_t stateColor = voice.isReady() ? TFT_GREEN : TFT_ORANGE;
    if (voice.isSpeaking()) stateColor = TFT_CYAN;
    M5.Display.setTextColor(stateColor, TFT_BLACK);
    M5.Display.drawString(voiceStateToString(voice.getState()), 60, 50);

    // Volume sonore
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("Volume : ", 10, 75);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString(String(voice.getVolume()) + " %", 80, 75);

    // Aide mémoire des commandes
    M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
    M5.Display.drawString("Commandes serie (115200 bauds) :", 10, 110);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("[T] Parler   [S] Stop", 10, 130);
    M5.Display.drawString("[+] Vol +    [-] Vol -", 10, 150);
    M5.Display.drawString("[?] Statut systeme", 10, 170);

    // Dernier message d'action
    if (!actionMsg.isEmpty()) {
        M5.Display.setTextColor(TFT_MAGENTA, TFT_BLACK);
        M5.Display.drawString(actionMsg, 10, 205);
    }
}

// Affichage du menu d'aide dans la console série
static void printHelp() {
    Serial.println("\n--------------------------------------------------------");
    Serial.println("  Commandes disponibles dans le moniteur serie :        ");
    Serial.println("  T : Declencher la phrase de test                      ");
    Serial.println("  S : Arreter immediatement la parole (STOP)            ");
    Serial.println("  + : Augmenter le volume (+10 %)                       ");
    Serial.println("  - : Diminuer le volume (-10 %)                        ");
    Serial.println("  ? : Afficher l'etat complet du systeme                ");
    Serial.println("--------------------------------------------------------");
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  Tutoriel VoiceKit — Leçon 03 : Speech Control        ");
    Serial.println("  Controle interactif de la parole via le port serie    ");
    Serial.println("========================================================");

    // 1. Initialisation matérielle avec haut-parleur interne
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = false;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString("VoiceKit - Lecon 03", 10, 15);
    updateScreen("Connexion en cours...");

    // 2. Configuration VoiceKit
    VoiceKitConfig config;
    config.apiKey = GEMINI_KEY;
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    config.provider = VoiceProviderType::GEMINI;
    config.volume = 70; // Volume initial 70%
    config.voice = "Aoede";
    config.language = "fr-FR";

    // 3. Callbacks
    voice.onReady([]() {
        Serial.println("\n[VoiceKit] >>> Moteur vocal PRET ! <<<");
        printHelp();
        updateScreen("Moteur pret !");
    });

    voice.onSpeechStarted([]() {
        Serial.println("[VoiceKit] >>> Parole en cours...");
        updateScreen("Parole en cours");
    });

    voice.onSpeechFinished([]() {
        Serial.println("[VoiceKit] >>> Fin de parole.");
        updateScreen("Fin de parole");
    });

    voice.onError([](const String& err) {
        Serial.printf("[VoiceKit] Erreur signalee : %s\n", err.c_str());
        updateScreen("Erreur : " + err);
    });

    // 4. Initialisation
    Serial.println("[VoiceKit] Demarrage...");
    if (!voice.begin(config)) {
        Serial.printf("[VoiceKit][FATAL] Echec begin : %s\n", voice.getLastError().c_str());
        updateScreen("Echec initialisation");
    }
}

void loop() {
    M5.update();
    voice.update();

    // Gestion non bloquante des commandes saisies sur la console série
    if (Serial.available() > 0) {
        char cmd = Serial.read();

        // Ignorer les retours chariot et sauts de ligne
        if (cmd == '\r' || cmd == '\n' || cmd == ' ') {
            return;
        }

        switch (cmd) {
            case 'T':
            case 't': {
                // Déclencher la parole
                if (!voice.isReady()) {
                    Serial.println("[Commande T] Impossible : VoiceKit n'est pas encore pret.");
                    updateScreen("Refus : Non pret");
                } else if (voice.isSpeaking() || voice.isBusy()) {
                    Serial.println("[Commande T] Impossible : VoiceKit est deja occupe.");
                    updateScreen("Refus : Moteur occupe");
                } else {
                    Serial.printf("[Commande T] Lancement : \"%s\"\n", SPEECH_TEXT);
                    bool ok = voice.say(SPEECH_TEXT);
                    if (ok) {
                        updateScreen("Requete TTS envoyee");
                    } else {
                        Serial.println("[Commande T] Requete refusee par le moteur.");
                    }
                }
                break;
            }

            case 'S':
            case 's': {
                // Arrêt immédiat de la parole
                Serial.println("[Commande S] Interruption immediate demandee (STOP) !");
                voice.stop();
                updateScreen("Interruption STOP");
                break;
            }

            case '+': {
                // Augmenter le volume (+10 %, max 100 %)
                uint8_t vol = voice.getVolume();
                if (vol <= 90) {
                    vol += 10;
                } else {
                    vol = 100;
                }
                voice.setVolume(vol);
                Serial.printf("[Commande +] Volume augmente a : %u %%\n", vol);
                updateScreen("Volume : " + String(vol) + " %");
                break;
            }

            case '-': {
                // Diminuer le volume (-10 %, min 0 %)
                uint8_t vol = voice.getVolume();
                if (vol >= 10) {
                    vol -= 10;
                } else {
                    vol = 0;
                }
                voice.setVolume(vol);
                Serial.printf("[Commande -] Volume diminue a : %u %%\n", vol);
                updateScreen("Volume : " + String(vol) + " %");
                break;
            }

            case '?': {
                // Diagnostic complet
                Serial.println("\n===== DIAGNOSTIC DU SYSTEME =====");
                Serial.printf("  Etat actuel       : %s\n", voiceStateToString(voice.getState()));
                Serial.printf("  Pret (isReady)    : %s\n", voice.isReady() ? "OUI" : "NON");
                Serial.printf("  Occupe (isBusy)   : %s\n", voice.isBusy() ? "OUI" : "NON");
                Serial.printf("  Parle (isSpeaking): %s\n", voice.isSpeaking() ? "OUI" : "NON");
                Serial.printf("  Volume actuel     : %u %%\n", voice.getVolume());
                Serial.printf("  Derniere erreur   : %s\n", voice.getLastError().isEmpty() ? "Aucune" : voice.getLastError().c_str());
                Serial.printf("  Adresse IP        : %s\n", WiFi.localIP().toString().c_str());
                Serial.println("=================================");
                updateScreen("Diagnostic affiche");
                break;
            }

            default:
                Serial.printf("[Commande] Caractere '%c' non reconnu. Tapez ? pour l'aide.\n", cmd);
                break;
        }
    }

    delay(10);
}
