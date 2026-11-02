/**
 * @file main.cpp
 * @brief Exemple OpenAIConversationLVGL v0.1.0 — Modern Voice UI avec LVGL 9
 *        Dialogue vocal bidirectionnel temps reel avec OpenAI Realtime API
 *        (gpt-realtime-2.1-mini) sur M5Stack CoreS3 (320x240 LVGL 9).
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>
#include <WiFi.h>

#include "AppConfig.h"
#include "Diagnostics.h"
#include "PTTController.h"
#include "VoiceUI.h"

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

// =============================================================================
// Instances Globales
// =============================================================================

static VoxCoreVoice s_voice;
static Diagnostics  s_diag;
static PTTController s_ptt(s_voice, s_diag);
static VoiceUI      s_ui;

// Variables de suivi d'etat
static VoiceState s_lastVoiceState = VoiceState::UNINITIALIZED;
static uint32_t   s_lastVoiceStateChangeMs = 0;
static bool       s_throttleUi = false;

// Buffers de decouplage thread-safe pour les textes asynchrones VoiceKit -> LVGL
static portMUX_TYPE s_uiMux = portMUX_INITIALIZER_UNLOCKED;
static String s_pendingUserText = "";
static bool s_hasPendingUserText = false;
static String s_pendingAssistantText = "";
static bool s_hasPendingAssistantText = false;
static String s_pendingErrorText = "";
static bool s_hasPendingErrorText = false;

// =============================================================================
// Initialisation (setup)
// =============================================================================

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(300);

    Serial.println("\n========================================================");
    Serial.println("  VoiceKit v0.1.0 — OpenAIConversationLVGL Edition       ");
    Serial.println("  OpenAI Realtime (gpt-realtime-2.1-mini) + LVGL 9      ");
    Serial.println("  M5Stack CoreS3 (320x240 LCD + Capacitive Touch)       ");
    Serial.println("========================================================");

    // 1. Initialisation PTT et Diagnostics
    s_ptt.begin();

    // 2. Initialisation Interface Graphique LVGL 9
    if (!s_ui.init(&s_ptt, &s_diag)) {
        Serial.println("[MAIN][ERR] Echec initialisation VoiceUI LVGL 9 !");
    }

    // 3. Configuration de VoiceKit
    VoiceKitConfig vConfig;
    vConfig.provider = VoiceProviderType::OPENAI;
    vConfig.apiKey = OPENAI_API_KEY;
    vConfig.model = AppConfig::OPENAI_MODEL;
    vConfig.voice = AppConfig::OPENAI_VOICE;
    vConfig.language = AppConfig::OPENAI_LANGUAGE;
    vConfig.wifiMode = WiFiMode::MANAGED_WIFI;
    vConfig.wifiSSID = WIFI_SSID;
    vConfig.wifiPassword = WIFI_PASSWORD;
    vConfig.enableFastConnect = true;
    vConfig.volume = AppConfig::DEFAULT_VOLUME;
    vConfig.enableConversation = true;
    vConfig.conversationInstruction = AppConfig::CONVERSATION_INSTRUCTION;

    // 4. Callbacks d'evenements VoiceKit
    s_voice.onReady([]() {
        Serial.println("[VOICE] Session OpenAI Realtime prete et confirmee !");
        s_ptt.notifyReady();
        s_ui.setErrorMessage("");
        s_ui.syncWithPTT(PTTState::READY);
    });

    s_voice.onStateChanged([](VoiceState state) {
        uint32_t nowMs = millis();
        uint32_t prevDurMs = (s_lastVoiceStateChangeMs > 0 && nowMs >= s_lastVoiceStateChangeMs) ? (nowMs - s_lastVoiceStateChangeMs) : 0;
        s_diag.logVoiceStateChange(voiceStateToString(s_lastVoiceState), voiceStateToString(state), prevDurMs);

        s_lastVoiceState = state;
        s_lastVoiceStateChangeMs = nowMs;

        s_ptt.notifyVoiceStateChanged(state);
    });

    s_voice.onError([](const String& err) {
        Serial.printf("[VOICE][ERR] Erreur : %s\n", err.c_str());
        s_ptt.notifyError(err);
        portENTER_CRITICAL(&s_uiMux);
        s_pendingErrorText = err;
        s_hasPendingErrorText = true;
        portEXIT_CRITICAL(&s_uiMux);
    });

    s_voice.onListeningStarted([]() {
        Serial.println("[AUDIO][MIC] Debut de capture ES7210");
        s_ptt.notifyListeningStarted();
    });

    s_voice.onListeningFinished([]() {
        Serial.println("[AUDIO][MIC] Fin de capture ES7210");
        s_ptt.notifyListeningFinished();
    });

    s_voice.onSpeechStarted([]() {
        Serial.println("[AUDIO][RX] Restitution audio OpenAI Realtime vers AW88298");
        s_ptt.notifySpeechStarted();
    });

    s_voice.onSpeechFinished([]() {
        Serial.println("[AUDIO][SPK] Fin de restitution haut-parleur");
        s_ptt.notifySpeechFinished();
    });

    // 5. Transcription Utilisateur (Whisper)
    s_voice.onUserText([](const String& text, bool isFinal) {
        if (isFinal) {
            Serial.printf("[OpenAIConvLVGL][USER WHISPER] %s\n", text.c_str());
        }
        portENTER_CRITICAL(&s_uiMux);
        s_pendingUserText = text;
        s_hasPendingUserText = true;
        portEXIT_CRITICAL(&s_uiMux);
    });

    // 6. Transcription Assistant (OpenAI Realtime)
    s_voice.onAssistantText([](const String& text) {
        Serial.printf("[OpenAIConvLVGL][OPENAI TEXT] %s\n", text.c_str());
        portENTER_CRITICAL(&s_uiMux);
        s_pendingAssistantText = text;
        s_hasPendingAssistantText = true;
        portEXIT_CRITICAL(&s_uiMux);
    });

    // 7. Metriques du tour de conversation
    s_voice.onConversationTurnMetrics([](const ConversationMetrics& m) {
        s_diag.recordTurnCompleted();
        s_diag.recordFirstAudioLatency(m.firstAudioLatencyMs);

        Serial.println("========================================================");
        Serial.printf("[CONV][TURN] Tour #%llu termine avec succes :\n", (unsigned long long)m.turnId);
        Serial.printf("  - Duree micro     : %u ms (%u blocs PCM)\n", (unsigned)m.micCaptureDurationMs, (unsigned)m.audioBlocksSent);
        Serial.printf("  - Latence 1er son : %u ms\n", (unsigned)m.firstAudioLatencyMs);
        Serial.printf("  - Paquets RX      : %u (perdus: %u)\n", (unsigned)m.pcmBlocksReceived, (unsigned)m.droppedSamples);
        Serial.printf("  - Duree tour      : %u ms\n", (unsigned)m.totalTurnDurationMs);
        Serial.println("========================================================");
    });

    // 8. Demarrage du moteur vocal VoiceKit
    if (!s_voice.begin(vConfig)) {
        Serial.printf("[MAIN][ERR] Echec voice.begin() : %s\n", s_voice.getLastError().c_str());
        s_ui.setErrorMessage(s_voice.getLastError());
        s_ptt.notifyError(s_voice.getLastError());
        s_ui.syncWithPTT(PTTState::ERROR_STATE);
        return;
    }

    Serial.println("[MAIN] Initialisation terminee avec succes. Connexion en cours...");
    s_lastVoiceStateChangeMs = millis();
}

// =============================================================================
// Boucle Principale non-bloquante (loop)
// =============================================================================

void loop() {
    uint32_t now = millis();

    // 1. Polling materiel M5Unified (gestion tactile, touches et capteurs)
    M5.update();

    // 2. Traitement cyclique VoiceKit avec mesure temporelle de haute precision
    uint32_t tUpStart = micros();
    s_voice.update();
    uint32_t upDurUs = micros() - tUpStart;
    s_diag.recordVoiceUpdate(upDurUs);

    // Auto-degradation de la cadence graphique si voice.update() depasse 5 ms
    s_throttleUi = (upDurUs > 5000);

    // 3. Commandes Serie de test et d'automatisation
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'P' || c == 'p' || c == 'T' || c == 't') {
            s_ui.setUserText("");
            s_ui.setAssistantText("");
            s_ptt.triggerSerialPress();
        } else if (c == 'R' || c == 'r' || c == 'U' || c == 'u') {
            s_ptt.triggerSerialRelease();
        } else if (c == 'S' || c == 's') {
            Serial.println("[SERIE] Commande 'S' (STOP prioritaire)");
            s_ptt.triggerStop();
        } else if (c == 'D' || c == 'd') {
            uint32_t stDur = (now >= s_lastVoiceStateChangeMs) ? (now - s_lastVoiceStateChangeMs) : 0;
            s_diag.printReport(voiceStateToString(s_voice.getState()), stDur,
                               WiFi.isConnected(), WiFi.isConnected() ? WiFi.RSSI() : 0,
                               pttStateToString(s_ptt.getState()));
        } else if (c == 'E' || c == 'e') {
            Serial.println("[SERIE] Injection d'erreur de test");
            s_ptt.notifyError("Erreur test simulee");
            portENTER_CRITICAL(&s_uiMux);
            s_pendingErrorText = "Erreur test simulee";
            s_hasPendingErrorText = true;
            portEXIT_CRITICAL(&s_uiMux);
        }
    }

    // 4. Machine a etats Push-to-Talk independante (avec detection de contact physique)
    s_ptt.update(now, s_ui.isPhysicalTouchActive());

    // 5. Transfert thread-safe des textes asynchrones VoiceKit vers l'interface LVGL
    String userTextCopy, asstTextCopy, errCopy;
    bool doUser = false, doAsst = false, doErr = false;

    portENTER_CRITICAL(&s_uiMux);
    if (s_hasPendingUserText) {
        userTextCopy = s_pendingUserText;
        s_hasPendingUserText = false;
        doUser = true;
    }
    if (s_hasPendingAssistantText) {
        asstTextCopy = s_pendingAssistantText;
        s_hasPendingAssistantText = false;
        doAsst = true;
    }
    if (s_hasPendingErrorText) {
        errCopy = s_pendingErrorText;
        s_hasPendingErrorText = false;
        doErr = true;
    }
    portEXIT_CRITICAL(&s_uiMux);

    if (doUser) s_ui.setUserText(userTextCopy);
    if (doAsst) s_ui.setAssistantText(asstTextCopy);
    if (doErr) s_ui.setErrorMessage(errCopy);

    // 6. Synchronisation de l'interface avec l'etat PTT
    s_ui.syncWithPTT(s_ptt.getState());
    s_ui.setWifiStatus(WiFi.isConnected(), WiFi.isConnected() ? WiFi.RSSI() : 0);

    // 7. Rafraichissement graphique et tactile LVGL 9
    uint32_t audioLevel = s_ptt.isListening() ? s_voice.getAudioLevel() : 0;
    s_ui.update(now, audioLevel, s_throttleUi);

    // 8. Surveillance et diagnostics periodiques
    uint32_t curVoiceStateDur = (now >= s_lastVoiceStateChangeMs) ? (now - s_lastVoiceStateChangeMs) : 0;
    s_diag.update(now, voiceStateToString(s_voice.getState()), curVoiceStateDur,
                  WiFi.isConnected(), WiFi.isConnected() ? WiFi.RSSI() : 0,
                  pttStateToString(s_ptt.getState()));
}

