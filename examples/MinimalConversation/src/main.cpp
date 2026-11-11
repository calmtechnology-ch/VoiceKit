/**
 * @file main.cpp
 * @brief Exemple MinimalConversation — Conversation vocale bidirectionnelle native (Mode B)
 *        sur M5Stack CoreS3 avec VoiceKit v0.5.3.1 et Google Gemini Live.
 * 
 * Fonctionnement :
 * - Appui maintenu sur « PARLER » : capture vocale PCM 16 kHz et streaming continu.
 * - Relâchement : fin d'activité vocale (activityEnd), passage en réflexion (PROCESSING).
 * - Restitution sonore : Gemini répond oralement en PCM 24 kHz via le haut-parleur CoreS3.
 * - Bouton STOP actif pendant la réflexion ou la restitution pour interrompre instantanément.
 * - Affichage en temps réel : transcription utilisateur + réponse textuelle + métriques de tour.
 * - Commandes série : 'P' (Parler appui), 'R' (Relâcher), 'S' (STOP).
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

// États de l'interface conversationnelle
enum class ConvUIState {
    INITIALIZING,
    READY,
    LISTENING,
    PROCESSING,
    SPEAKING,
    STOPPED,
    ERROR_STATE
};

static VoxCoreVoice voice;
static ConvUIState s_uiState = ConvUIState::INITIALIZING;

static String s_userText = "";
static String s_assistantText = "";
static String s_statusDetail = "";
static String s_errorMessage = "";
static bool s_needsRedraw = true;
static bool s_touchActive = false;

// Coordonnées du bouton tactile principal (bas d'écran)
static constexpr int BTN_X = 20;
static constexpr int BTN_Y = 165;
static constexpr int BTN_W = 280;
static constexpr int BTN_H = 65;

static void setConvState(ConvUIState st) {
    if (s_uiState != st) {
        s_uiState = st;
        s_needsRedraw = true;
    }
}

static void drawUI() {
    // 1. Fond d'écran
    M5.Display.fillScreen(TFT_BLACK);

    // 2. En-tête : Titre & version
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("VoiceKit v0.4.0 — Conversation", 10, 8);

    // Badge d'état visuel en haut à droite
    const char* statusText = "INITIALISATION";
    uint16_t statusColor = TFT_LIGHTGRAY;

    switch (s_uiState) {
        case ConvUIState::INITIALIZING:
            statusText = "CONNEXION...";
            statusColor = TFT_ORANGE;
            break;
        case ConvUIState::READY:
            statusText = "PRET";
            statusColor = TFT_GREEN;
            break;
        case ConvUIState::LISTENING:
            statusText = "ECOUTE EN COURS";
            statusColor = TFT_RED;
            break;
        case ConvUIState::PROCESSING:
            statusText = "REFLEXION...";
            statusColor = TFT_MAGENTA;
            break;
        case ConvUIState::SPEAKING:
            statusText = "REPONSE GEMINI";
            statusColor = TFT_CYAN;
            break;
        case ConvUIState::STOPPED:
            statusText = "INTERROMPU";
            statusColor = TFT_YELLOW;
            break;
        case ConvUIState::ERROR_STATE:
            statusText = "ERREUR";
            statusColor = TFT_RED;
            break;
    }

    // Dessin du badge de statut
    int badgeW = 120;
    int badgeH = 18;
    int badgeX = 320 - badgeW - 10;
    int badgeY = 6;
    M5.Display.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, statusColor);
    M5.Display.setTextColor(TFT_BLACK, statusColor);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.drawString(statusText, badgeX + (badgeW / 2), badgeY + (badgeH / 2));
    M5.Display.setTextDatum(TL_DATUM);

    // Ligne de séparation
    M5.Display.drawFastHLine(10, 28, 300, TFT_DARKGRAY);

    // 3. Cadre de transcription utilisateur (Question)
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("Vous :", 10, 34);

    M5.Display.fillRoundRect(10, 48, 300, 48, 4, 0x18E3); // Fond bleu nuit très foncé
    M5.Display.drawRoundRect(10, 48, 300, 48, 4, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, 0x18E3);

    String dispUser = s_userText.isEmpty() ? "(Maintenez PARLER et posez votre question...)" : s_userText;
    if (dispUser.length() > 90) {
        dispUser = dispUser.substring(0, 87) + "...";
    }
    M5.Display.setFont(&fonts::efontJA_12);
    M5.Display.drawString(dispUser, 16, 54);
    M5.Display.setFont(&fonts::Font0);

    // 4. Cadre de réponse de l'assistant (Gemini)
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("Gemini Live :", 10, 100);

    M5.Display.fillRoundRect(10, 114, 300, 44, 4, 0x0821);
    M5.Display.drawRoundRect(10, 114, 300, 44, 4, TFT_DARKCYAN);
    M5.Display.setTextColor(0x7FFF, 0x0821);

    String dispGemini = s_assistantText.isEmpty() ? "(En attente de dialogue...)" : s_assistantText;
    if (dispGemini.length() > 95) {
        dispGemini = dispGemini.substring(0, 92) + "...";
    }
    M5.Display.setFont(&fonts::efontJA_12);
    M5.Display.drawString(dispGemini, 16, 120);
    M5.Display.setFont(&fonts::Font0);

    // 5. Bouton tactile principal
    uint16_t btnColor = TFT_BLUE;
    const char* btnLabel = "PARLER";
    const char* btnSub = "Appuyer et maintenir pour parler";

    if (s_uiState == ConvUIState::LISTENING) {
        btnColor = TFT_RED;
        btnLabel = "ENREGISTREMENT...";
        btnSub = "Relacher pour envoyer a Gemini";
    } else if (s_uiState == ConvUIState::PROCESSING || s_uiState == ConvUIState::SPEAKING) {
        btnColor = 0xC800; // Rouge orangé vif pour STOP
        btnLabel = "STOP";
        btnSub = "Toucher pour interrompre";
    } else if (s_uiState == ConvUIState::INITIALIZING) {
        btnColor = TFT_DARKGRAY;
        btnLabel = "PATIENTEZ...";
        btnSub = "Connexion Wi-Fi et session en cours";
    }

    M5.Display.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 10, btnColor);
    M5.Display.drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 10, TFT_WHITE);

    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, btnColor);
    M5.Display.drawString(btnLabel, BTN_X + (BTN_W / 2), BTN_Y + 22);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGRAY, btnColor);
    M5.Display.drawString(btnSub, BTN_X + (BTN_W / 2), BTN_Y + 48);
    M5.Display.setTextDatum(TL_DATUM);

    s_needsRedraw = false;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(400);

    Serial.println("\n========================================================");
    Serial.println("  VoiceKit v0.4.0 — MinimalConversation (Mode B Native) ");
    Serial.println("  M5Stack CoreS3 (ES7210 Mic + AW88298 Speaker)         ");
    Serial.println("========================================================");

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    drawUI();

    // Configuration de VoiceKit avec le mode conversationnel activé
    VoiceKitConfig config;
    config.apiKey = GEMINI_KEY;
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    config.volume = 70;
    config.enableConversation = true;
    config.conversationInstruction = 
        "Tu es un assistant vocal bienveillant, intelligent et concis. "
        "Reponds toujours en francais direct et naturel en une ou deux phrases maximum.";

    // 1. Écouteurs de cycle de vie et d'état
    voice.onReady([]() {
        Serial.println("[MinimalConversation] VoiceKit connecte et PRET !");
        setConvState(ConvUIState::READY);
    });

    voice.onStateChanged([](VoiceState state) {
        Serial.printf("[MinimalConversation] Etat moteur : %s\n", voiceStateToString(state));
        switch (state) {
            case VoiceState::READY:
                setConvState(ConvUIState::READY);
                break;
            case VoiceState::LISTENING:
                setConvState(ConvUIState::LISTENING);
                break;
            case VoiceState::PROCESSING:
                setConvState(ConvUIState::PROCESSING);
                break;
            case VoiceState::SPEAKING:
                setConvState(ConvUIState::SPEAKING);
                break;
            case VoiceState::STOPPED:
                setConvState(ConvUIState::STOPPED);
                break;
            case VoiceState::ERROR_STATE:
                setConvState(ConvUIState::ERROR_STATE);
                break;
            default:
                break;
        }
    });

    voice.onError([](const String& err) {
        Serial.printf("[MinimalConversation][ERREUR] %s\n", err.c_str());
        s_errorMessage = err;
        setConvState(ConvUIState::ERROR_STATE);
    });

    // 2. Écouteur de transcription utilisateur (Question)
    voice.onUserText([](const String& text, bool isFinal) {
        s_userText = text;
        s_needsRedraw = true;
        if (isFinal) {
            Serial.printf("[MinimalConversation][USER FINAL] %s\n", text.c_str());
        }
    });

    // 3. Écouteur de transcription assistant (Gemini)
    voice.onAssistantText([](const String& text) {
        s_assistantText = text;
        s_needsRedraw = true;
        Serial.printf("[MinimalConversation][GEMINI] %s\n", text.c_str());
    });

    // 4. Écouteur de métriques complètes du tour de conversation
    voice.onConversationTurnMetrics([](const ConversationMetrics& m) {
        Serial.println("--------------------------------------------------------");
        Serial.printf("[MinimalConversation] Bilan Tour #%llu :\n", (unsigned long long)m.turnId);
        Serial.printf("  - Duree micro   : %u ms (%u blocs emis)\n", (unsigned)m.micCaptureDurationMs, (unsigned)m.audioBlocksSent);
        Serial.printf("  - Latence audio : %u ms avant premier son\n", (unsigned)m.firstAudioLatencyMs);
        Serial.printf("  - Blocs audio RX: %u blocs (pertes: %u)\n", (unsigned)m.pcmBlocksReceived, (unsigned)m.droppedSamples);
        Serial.printf("  - Duree totale  : %u ms\n", (unsigned)m.totalTurnDurationMs);
        Serial.println("--------------------------------------------------------");
        setConvState(ConvUIState::READY);
    });

    // Initialisation VoiceKit
    if (!voice.begin(config)) {
        Serial.printf("[MinimalConversation] Echec voice.begin() : %s\n", voice.getLastError().c_str());
        s_errorMessage = voice.getLastError();
        setConvState(ConvUIState::ERROR_STATE);
        return;
    }

    Serial.println("[MinimalConversation] Demarrage termine. En attente de synchronisation...");
}

void loop() {
    M5.update();
    voice.update();

    // 1. Gestion des commandes série de diagnostic
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'P' || c == 'p') {
            Serial.println("[Serie] Commande PARLER detectee");
            if (voice.isReady()) {
                s_userText = "";
                s_assistantText = "";
                voice.startListening();
            }
        } else if (c == 'R' || c == 'r') {
            Serial.println("[Serie] Commande RELACHER detectee");
            if (voice.isListening()) {
                voice.stopListening();
            }
        } else if (c == 'S' || c == 's') {
            Serial.println("[Serie] Commande STOP detectee");
            voice.stop();
        }
    }

    // 2. Gestion de l'écran tactile CoreS3
    auto touchCount = M5.Touch.getCount();
    if (touchCount > 0) {
        auto detail = M5.Touch.getDetail(0);
        bool inButton = (detail.x >= BTN_X && detail.x <= (BTN_X + BTN_W) &&
                         detail.y >= BTN_Y && detail.y <= (BTN_Y + BTN_H));

        if (detail.wasPressed() && inButton) {
            if (s_uiState == ConvUIState::PROCESSING || s_uiState == ConvUIState::SPEAKING) {
                // En cours de réflexion ou de restitution -> STOP prioritaire
                Serial.println("[Touch] Bouton STOP presse");
                voice.stop();
            } else if (voice.isReady() && !s_touchActive) {
                // En attente -> Début d'écoute (PARLER)
                s_touchActive = true;
                s_userText = "";
                s_assistantText = "";
                Serial.println("[Touch] Bouton PARLER presse -> startListening()");
                voice.startListening();
            }
        } else if (detail.wasReleased() && s_touchActive) {
            // Relâchement du bouton PARLER
            s_touchActive = false;
            Serial.println("[Touch] Bouton PARLER relache -> stopListening()");
            voice.stopListening();
        }
    } else if (s_touchActive) {
        // Sécurité si le doigt glisse hors de l'écran
        s_touchActive = false;
        if (voice.isListening()) {
            Serial.println("[Touch] Perte de contact tactile -> stopListening()");
            voice.stopListening();
        }
    }

    // 3. Rafraîchissement graphique si besoin
    if (s_needsRedraw) {
        drawUI();
    }

    delay(10);
}
