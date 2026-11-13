/**
 * @file main.cpp
 * @brief Exemple MinimalSTT — Reconnaissance vocale STT (Speech-to-Text) sur M5Stack CoreS3
 *        avec VoiceKit v0.5.3.1 et Google Gemini Live.
 * 
 * Fonctionnement :
 * - Appui maintenu sur le bouton « DICTER » : capture et transmission progressive PCM 16 kHz.
 * - Relâchement du bouton : fin de capture (activityEnd) et réception de la transcription.
 * - Affichage en direct des transcriptions partielles (intérim) et du résultat final.
 * - Restitution également sur le moniteur série (115200 baud).
 * - Sans LVGL obligatoire, basé directement sur M5Unified.
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>
#include <WiFi.h>

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #include "secrets.example.h"
#endif

#ifndef GEMINI_KEY
    #ifdef GEMINI_API_KEY
        #define GEMINI_KEY GEMINI_API_KEY
    #else
        #define GEMINI_KEY ""
    #endif
#endif

#ifndef WIFI_PASSWORD
    #ifdef WIFI_PASS
        #define WIFI_PASSWORD WIFI_PASS
    #else
        #define WIFI_PASSWORD ""
    #endif
#endif

// États normalisés de l'application STT
enum class DemoState {
    INITIALIZING,
    READY,
    LISTENING,
    TRANSCRIBING,
    ERROR
};

static VoxCoreVoice voice;
static DemoState s_uiState = DemoState::INITIALIZING;

static String s_interimText = "";
static String s_finalText = "";
static String s_errorMessage = "";
static bool s_needsRedraw = true;
static bool s_touchActive = false;
static bool s_captureHappened = false;

// Coordonnées du bouton tactile DICTER
static constexpr int BTN_X = 20;
static constexpr int BTN_Y = 165;
static constexpr int BTN_W = 280;
static constexpr int BTN_H = 65;

static void setDemoState(DemoState st) {
    if (s_uiState != st) {
        s_uiState = st;
        s_needsRedraw = true;
    }
}

static void drawUI() {
    // 1. Fond principal
    M5.Display.fillScreen(TFT_BLACK);

    // 2. Barre d'en-tête (Titre et badge de statut)
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("VoiceKit v0.6.5.2 STT", 10, 10);

    const char* statusText = "INITIALISATION";
    uint16_t statusColor = TFT_LIGHTGRAY;

    switch (s_uiState) {
        case DemoState::INITIALIZING:
            statusText = "CONNEXION...";
            statusColor = TFT_ORANGE;
            break;
        case DemoState::READY:
            statusText = "PRET";
            statusColor = TFT_GREEN;
            break;
        case DemoState::LISTENING:
            statusText = "ECOUTE";
            statusColor = TFT_RED;
            break;
        case DemoState::TRANSCRIBING:
            statusText = "TRANSCRIPTION";
            statusColor = TFT_CYAN;
            break;
        case DemoState::ERROR:
            statusText = "ERREUR";
            statusColor = TFT_MAGENTA;
            break;
    }

    // Badge statut
    int badgeW = 120;
    int badgeX = M5.Display.width() - badgeW - 10;
    M5.Display.fillRoundRect(badgeX, 6, badgeW, 22, 4, statusColor);
    M5.Display.setTextColor(TFT_BLACK, statusColor);
    M5.Display.drawCenterString(statusText, badgeX + (badgeW / 2), 10);

    // 3. Cadre central de transcription (Zone de texte)
    int boxX = 10;
    int boxY = 36;
    int boxW = 300;
    int boxH = 120;
    uint16_t boxBg = M5.Display.color565(25, 30, 40);
    M5.Display.fillRoundRect(boxX, boxY, boxW, boxH, 6, boxBg);
    M5.Display.drawRoundRect(boxX, boxY, boxW, boxH, 6, M5.Display.color565(60, 70, 90));

    M5.Display.setTextSize(1);
    if (s_uiState == DemoState::ERROR) {
        M5.Display.setTextColor(TFT_RED, boxBg);
        M5.Display.drawString("Erreur :", boxX + 10, boxY + 10);
        M5.Display.setTextColor(TFT_WHITE, boxBg);
        M5.Display.drawString(s_errorMessage.c_str(), boxX + 10, boxY + 30);
        M5.Display.setTextColor(TFT_YELLOW, boxBg);
        M5.Display.drawString("Touchez l'ecran pour reinitialiser", boxX + 10, boxY + 70);
    } else if (s_uiState == DemoState::LISTENING) {
        M5.Display.setTextColor(TFT_YELLOW, boxBg);
        M5.Display.drawString("Parlez maintenant... [Dictee en direct]", boxX + 10, boxY + 10);
        M5.Display.setTextColor(TFT_WHITE, boxBg);
        if (s_interimText.length() > 0) {
            M5.Display.drawString(s_interimText.c_str(), boxX + 10, boxY + 35);
        } else {
            M5.Display.setTextColor(TFT_DARKGRAY, boxBg);
            M5.Display.drawString("(En attente de voix...)", boxX + 10, boxY + 35);
        }
    } else if (s_uiState == DemoState::TRANSCRIBING) {
        M5.Display.setTextColor(TFT_CYAN, boxBg);
        M5.Display.drawString("Finalisation de la transcription...", boxX + 10, boxY + 10);
        if (s_interimText.length() > 0) {
            M5.Display.setTextColor(TFT_LIGHTGRAY, boxBg);
            M5.Display.drawString(s_interimText.c_str(), boxX + 10, boxY + 35);
        }
    } else if (s_finalText.length() > 0) {
        M5.Display.setTextColor(TFT_GREEN, boxBg);
        M5.Display.drawString("Derniere transcription recue :", boxX + 10, boxY + 10);
        M5.Display.setTextColor(TFT_WHITE, boxBg);
        M5.Display.drawString(s_finalText.c_str(), boxX + 10, boxY + 35);
    } else {
        M5.Display.setTextColor(TFT_LIGHTGRAY, boxBg);
        M5.Display.drawString("Appuyez sur le bouton ci-dessous", boxX + 10, boxY + 25);
        M5.Display.drawString("et parlez en maintenant l'appui.", boxX + 10, boxY + 45);
        M5.Display.setTextColor(TFT_DARKGRAY, boxBg);
        M5.Display.drawString("Relachez des que vous avez termine.", boxX + 10, boxY + 70);
    }

    // 4. Bouton tactile « DICTER »
    uint16_t btnColor = M5.Display.color565(30, 80, 200);
    const char* btnLabel = "DICTER (Maintenir)";

    if (s_uiState == DemoState::LISTENING) {
        btnColor = M5.Display.color565(220, 30, 30);
        btnLabel = "ENREGISTREMENT...";
    } else if (s_uiState == DemoState::TRANSCRIBING) {
        btnColor = M5.Display.color565(180, 120, 20);
        btnLabel = "TRAITEMENT...";
    } else if (s_uiState == DemoState::ERROR) {
        btnColor = M5.Display.color565(120, 30, 30);
        btnLabel = "REINITIALISER";
    }

    M5.Display.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 10, btnColor);
    M5.Display.drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 10, TFT_WHITE);
    M5.Display.setTextColor(TFT_WHITE, btnColor);
    M5.Display.setTextSize(2);
    M5.Display.drawCenterString(btnLabel, BTN_X + (BTN_W / 2), BTN_Y + 22);

    s_needsRedraw = false;
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  VoxCore VoiceKit v0.6.5.2 — Exemple MinimalSTT       ");
    Serial.println("  Reconnaissance vocale Push-to-Talk fiabilisee         ");
    Serial.println("========================================================");

    // 1. Initialisation matérielle CoreS3
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = true;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    drawUI();

    // 2. Configuration VoiceKit STT
    VoiceKitConfig vConfig;
    vConfig.apiKey = GEMINI_KEY;
#ifdef GEMINI_MODEL
    vConfig.model = GEMINI_MODEL;
#endif
    vConfig.systemInstruction = VOICEKIT_DEFAULT_STT_INSTRUCTION;
    vConfig.wifiMode = WiFiMode::MANAGED_WIFI;
    vConfig.wifiSSID = WIFI_SSID;
    vConfig.wifiPassword = WIFI_PASSWORD;
    vConfig.muteSpeakerDuringSTT = true;
    vConfig.sttFinalTimeoutMs = 6000;

    // 3. Enregistrement des callbacks
    voice.onReady([]() {
        Serial.println("[MinimalSTT] Callback onReady : Prêt pour la dictée !");
        setDemoState(DemoState::READY);
    });

    voice.onListeningStarted([]() {
        Serial.println("[MinimalSTT] Callback onListeningStarted : Microphone actif !");
        s_captureHappened = true;
        setDemoState(DemoState::LISTENING);
    });

    voice.onListeningFinished([]() {
        Serial.println("[MinimalSTT] Callback onListeningFinished : Acquisition terminée.");
        setDemoState(DemoState::TRANSCRIBING);
    });

    // Callback de transcription (partielle et finale)
    voice.onTranscription([](const String& text, bool isFinal) {
        if (!s_captureHappened) {
            Serial.printf("[MinimalSTT][WARN] Transcription reçue sans capture correspondante : %s\n", text.c_str());
            return;
        }
        if (isFinal) {
            Serial.printf("\n>>> [TRANSCRIPTION FINALE] %s\n\n", text.c_str());
            s_finalText = text;
            s_interimText = "";
            s_captureHappened = false;
            setDemoState(DemoState::READY);
        } else {
            Serial.printf("... [interim] %s\n", text.c_str());
            s_interimText = text;
            s_needsRedraw = true;
        }
    });

    voice.onError([](const String& err) {
        Serial.printf("[MinimalSTT][ERREUR] %s\n", err.c_str());
        s_errorMessage = err;
        s_touchActive = false;
        s_captureHappened = false;
        setDemoState(DemoState::ERROR);
    });

    // 4. Initialisation de VoiceKit
    if (!voice.begin(vConfig)) {
        s_errorMessage = voice.getLastError();
        setDemoState(DemoState::ERROR);
        Serial.println("[MinimalSTT][FATAL] Echec initialisation VoiceKit !");
    }
}

void loop() {
    // 1. Mise à jour de la bibliothèque VoiceKit
    voice.update();

    // 2. Gestion tactile M5Unified
    M5.update();
    auto touch = M5.Touch.getDetail();

    bool inButtonArea = (touch.x >= BTN_X && touch.x <= (BTN_X + BTN_W) &&
                         touch.y >= BTN_Y && touch.y <= (BTN_Y + BTN_H));

    // Récupération après état ERROR : un toucher réinitialise si le moteur est prêt
    if (s_uiState == DemoState::ERROR && touch.wasPressed()) {
        Serial.println("[MinimalSTT] Réinitialisation manuelle suite erreur");
        s_touchActive = false;
        s_captureHappened = false;
        s_errorMessage = "";
        setDemoState(voice.isReady() ? DemoState::READY : DemoState::INITIALIZING);
        return;
    }

    // Début de l'appui sur le bouton DICTER (anti-double commande stricte)
    if (touch.wasPressed() && inButtonArea && !s_touchActive) {
        if (s_uiState == DemoState::READY && voice.isReady() && !voice.isListening()) {
            Serial.println("[MinimalSTT] Appui detecte sur DICTER -> Demarrage ecoute");
            s_interimText = "";
            s_captureHappened = false;
            bool ok = voice.startListening();
            if (ok) {
                s_touchActive = true;
                // Ne considérer actif qu'après confirmation explicite
                setDemoState(DemoState::LISTENING);
            } else {
                Serial.println("[MinimalSTT] Refus de startListening() par le moteur vocal");
                s_touchActive = false;
                s_errorMessage = "Demarrage ecoute refuse";
                setDemoState(DemoState::ERROR);
            }
        } else {
            Serial.printf("[MinimalSTT] Appui ignore : non pret (uiState=%d, voiceReady=%d, voiceListening=%d)\n",
                          (int)s_uiState, voice.isReady(), voice.isListening());
        }
    }

    // Relâchement (ou sortie de zone tactile alors qu'un appui était actif)
    if (s_touchActive) {
        bool released = touch.wasReleased() || !touch.isPressed();
        if (released) {
            Serial.println("[MinimalSTT] Relachement / sortie tactile detectee -> Fin de la capture");
            s_touchActive = false;
            if (voice.isListening()) {
                voice.stopListening();
                setDemoState(DemoState::TRANSCRIBING);
            } else {
                // Si l'écoute n'était pas encore active lors du relâchement rapide
                setDemoState(DemoState::READY);
            }
        }
    }

    // 3. Rafraîchissement graphique si besoin
    if (s_needsRedraw) {
        drawUI();
    }

    delay(10);
}
