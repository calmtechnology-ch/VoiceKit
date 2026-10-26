/**
 * @file main.cpp
 * @brief Exemple OpenAIConversation — Dialogue vocal bidirectionnel temps réel
 *        avec OpenAI Realtime API (gpt-realtime-2.1-mini) sur M5Stack CoreS3.
 * 
 * VoiceKit v0.6.0 :
 * - Fournisseur vocal configuré en VoiceProviderType::OPENAI
 * - WebSocket TLS direct vers wss://api.openai.com/v1/realtime
 * - Microphone ES7210 16 kHz rééchantillonné en continu vers 24 kHz
 * - Restitution audio native 24 kHz sur haut-parleur AW88298 sans coupure
 * - Transcriptions STT Whisper (input_audio_transcription) et TTS assistant
 * - Bouton tactile STOP d'interruption prioritaire immédiate
 * - Reconnexion maîtrisée et affichage instrumenté des métriques de latence
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

#ifndef OPENAI_API_KEY
    #define OPENAI_API_KEY ""
#endif

#ifndef WIFI_SSID
    #define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
    #define WIFI_PASSWORD ""
#endif

// États de l'interface graphique CoreS3 (sans LVGL)
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

    // 2. En-tête : Titre & Provider
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("VoiceKit v0.6.0 — OpenAI Realtime", 10, 8);

    // Badge d'état visuel en haut à droite
    const char* statusText = "INITIALISATION";
    uint16_t statusColor = TFT_LIGHTGRAY;

    switch (s_uiState) {
        case ConvUIState::INITIALIZING:
            statusText = "CONNEXION...";
            statusColor = TFT_ORANGE;
            break;
        case ConvUIState::READY:
            statusText = "PRET (OPENAI)";
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
            statusText = "REPONSE VOCALE";
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

    // 3. Cadre de transcription utilisateur (Whisper STT)
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("Vous (Micro 16 kHz) :", 10, 34);

    M5.Display.fillRoundRect(10, 48, 300, 48, 4, 0x18E3);
    M5.Display.drawRoundRect(10, 48, 300, 48, 4, TFT_NAVY);
    M5.Display.setTextColor(TFT_WHITE, 0x18E3);

    String dispUser = s_userText.isEmpty() ? "(Maintenez PARLER et posez votre question...)" : s_userText;
    if (dispUser.length() > 90) {
        dispUser = dispUser.substring(0, 87) + "...";
    }
    M5.Display.drawString(dispUser, 16, 54);

    // 4. Cadre de réponse de l'assistant OpenAI
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("OpenAI Realtime (PCM 24 kHz) :", 10, 100);

    M5.Display.fillRoundRect(10, 114, 300, 44, 4, 0x0821);
    M5.Display.drawRoundRect(10, 114, 300, 44, 4, TFT_DARKCYAN);
    M5.Display.setTextColor(0x7FFF, 0x0821);

    String dispAssistant = s_assistantText.isEmpty() ? "(En attente de dialogue...)" : s_assistantText;
    if (dispAssistant.length() > 95) {
        dispAssistant = dispAssistant.substring(0, 92) + "...";
    }
    M5.Display.drawString(dispAssistant, 16, 120);

    // 5. Bouton tactile principal
    uint16_t btnColor = TFT_BLUE;
    const char* btnLabel = "PARLER";
    const char* btnSub = "Appuyer et maintenir pour parler";

    if (s_uiState == ConvUIState::LISTENING) {
        btnColor = TFT_RED;
        btnLabel = "ENREGISTREMENT...";
        btnSub = "Relâcher pour envoyer à OpenAI";
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
    Serial.println("  VoiceKit v0.6.0 — OpenAI Realtime Conversation        ");
    Serial.println("  M5Stack CoreS3 (ES7210 Mic + AW88298 Speaker)         ");
    Serial.println("========================================================");

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    drawUI();

    // Configuration de VoiceKit pour OpenAI Realtime
    VoiceKitConfig config;
    config.provider = VoiceProviderType::OPENAI;
    config.apiKey = OPENAI_API_KEY;
    config.model = "gpt-realtime-2.1-mini";
    config.voice = "alloy";
    config.language = "fr-FR";
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    config.enableFastConnect = true;
    config.volume = 70;
    config.enableConversation = true;
    config.conversationInstruction = 
        "Tu es un assistant vocal bienveillant, intelligent et concis. "
        "Reponds toujours en francais direct et naturel en une ou deux phrases maximum.";

    // 1. Callbacks de cycle de vie et d'état
    voice.onReady([]() {
        Serial.println("[OpenAIConv] Session OpenAI Realtime prete et connectee !");
        setConvState(ConvUIState::READY);
    });

    voice.onStateChanged([](VoiceState state) {
        Serial.printf("[OpenAIConv] Etat moteur : %s\n", voiceStateToString(state));
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
        Serial.printf("[OpenAIConv][ERREUR] %s\n", err.c_str());
        s_errorMessage = err;
        setConvState(ConvUIState::ERROR_STATE);
    });

    // 2. Transcription utilisateur (Whisper)
    voice.onUserText([](const String& text, bool isFinal) {
        s_userText = text;
        s_needsRedraw = true;
        if (isFinal) {
            Serial.printf("[OpenAIConv][USER WHISPER] %s\n", text.c_str());
        }
    });

    // 3. Transcription assistant (OpenAI)
    voice.onAssistantText([](const String& text) {
        s_assistantText = text;
        s_needsRedraw = true;
        Serial.printf("[OpenAIConv][OPENAI TEXT] %s\n", text.c_str());
    });

    // 4. Métriques de latence du tour conversationnel
    voice.onConversationTurnMetrics([](const ConversationMetrics& m) {
        Serial.println("========================================================");
        Serial.printf("[OpenAIConv] Bilan Tour #%llu termine :\n", (unsigned long long)m.turnId);
        Serial.printf("  - Duree micro      : %u ms (%u blocs emis)\n", (unsigned)m.micCaptureDurationMs, (unsigned)m.audioBlocksSent);
        Serial.printf("  - Latence 1er son  : %u ms\n", (unsigned)m.firstAudioLatencyMs);
        Serial.printf("  - Blocs audio RX   : %u blocs (pertes: %u)\n", (unsigned)m.pcmBlocksReceived, (unsigned)m.droppedSamples);
        Serial.printf("  - Duree totale     : %u ms\n", (unsigned)m.totalTurnDurationMs);
        Serial.println("  [LATENCE INSTRUMENTEE]");
        if (m.latency.hasSpeechEndDetected()) {
            Serial.printf("  - VAD speechEnd -> micStopped : %d ms\n", m.latency.getSpeechEndToMicStoppedMs());
        }
        Serial.printf("  - micStopped -> activityEnd   : %d ms\n", m.latency.getMicStoppedToActivityEndMs());
        Serial.printf("  - activityEnd -> 1er audio RX : %d ms\n", m.latency.getActivityEndToFirstAudioMs());
        Serial.printf("  - 1er audio RX -> debut DMA   : %d ms\n", m.latency.getFirstAudioToPlaybackMs());
        Serial.printf("  - Latence vocale percue       : %d ms\n", m.latency.getResponseLatencyMs());
        Serial.println("========================================================");
        setConvState(ConvUIState::READY);
    });

    // Initialisation du moteur VoiceKit
    if (!voice.begin(config)) {
        Serial.printf("[OpenAIConv] Echec voice.begin() : %s\n", voice.getLastError().c_str());
        s_errorMessage = voice.getLastError();
        setConvState(ConvUIState::ERROR_STATE);
        return;
    }

    Serial.println("[OpenAIConv] Demarrage termine. Connexion Wi-Fi et OpenAI Realtime en cours...");
}

void loop() {
    M5.update();
    voice.update();

    // 1. Commandes série pour automatisation et tests
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

    // 2. Gestion tactile CoreS3
    auto touchCount = M5.Touch.getCount();
    if (touchCount > 0) {
        auto detail = M5.Touch.getDetail(0);
        bool inButton = (detail.x >= BTN_X && detail.x <= (BTN_X + BTN_W) &&
                         detail.y >= BTN_Y && detail.y <= (BTN_Y + BTN_H));

        if (detail.wasPressed() && inButton) {
            if (s_uiState == ConvUIState::PROCESSING || s_uiState == ConvUIState::SPEAKING) {
                // Interruption prioritaire immédiate (STOP)
                Serial.println("[Touch] Bouton STOP presse -> voice.stop()");
                voice.stop();
            } else if (voice.isReady() && !s_touchActive) {
                // Début de prise de parole
                s_touchActive = true;
                s_userText = "";
                s_assistantText = "";
                Serial.println("[Touch] Bouton PARLER presse -> voice.startListening()");
                voice.startListening();
            }
        } else if (detail.wasReleased() && s_touchActive) {
            // Fin de prise de parole
            s_touchActive = false;
            Serial.println("[Touch] Bouton PARLER relache -> voice.stopListening()");
            voice.stopListening();
        }
    } else if (s_touchActive) {
        // Sécurité si glissement hors écran
        s_touchActive = false;
        if (voice.isListening()) {
            Serial.println("[Touch] Glissement hors ecran -> stopListening()");
            voice.stopListening();
        }
    }

    // 3. Rafraîchissement graphique
    if (s_needsRedraw) {
        drawUI();
    }

    delay(10);
}
