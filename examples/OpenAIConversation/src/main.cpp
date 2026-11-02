/**
 * @file main.cpp
 * @brief Exemple OpenAIConversation v0.6.3.2.2 — Modern Voice UI Edition (Corrective)
 *        Dialogue vocal bidirectionnel temps reel avec OpenAI Realtime API
 *        (gpt-realtime-2.1-mini) sur M5Stack CoreS3 (320x240 M5GFX).
 *
 * Evolutions v0.6.3.2.2 (Corrective Bouton PARLER & Reponses Concises) :
 * - Priorite absolue bouton PARLER : resolution definitive des blocages.
 * - Gestion tactile robuste inspiree de CoreS3-TouchDiagnostics v0.1.0.1 :
 *   detection stricte des transitions PRESS / RELEASE sur details tactiles fiables,
 *   suppression du debounce artificiel destructeur d'appuis rapides, suivi monotone
 *   des contacts (contactId), securite anti-incoherence (purge s_touchInListening si non presse).
 * - Diagnostic exhaustif des refus : ajout de [PTT][REJECT] avec cause exacte,
 *   detail de l'etat moteur, session et busy flags.
 * - Mecanisme de recuperation non-bloquant pour etats anormalement persistants :
 *   timeout d'ecoute (15 s), timeout de traitement serveur (15 s), reprise STOPPED -> READY (1 s)
 *   et recuperation apres erreur sans aucun ESP.restart().
 * - Tracabilite fine des etats : [TOUCH][PRESS], [TOUCH][RELEASE], [VOICE][STATE] avec
 *   duree passee dans chaque etat en millisecondes, [PTT][RECOVERY].
 * - Instruction conversationnelle optimisee pour des reponses vocales concises
 *   (15 a 25 mots maximum, directes et sans formules inutiles).
 * - Modele gpt-realtime-2.1-mini et Modern Voice UI scrupuleusement conserves.
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>
#include <WiFi.h>
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
// Instances et Variables Globales
// =============================================================================

// Moteur vocal VoiceKit (proprietaire exclusif audio/reseau)
static VoxCoreVoice voice;

// Interface graphique moderne
static VoiceUI s_ui;

// Memoire des transcriptions completes
static String s_userFullText = "";
static String s_assistantFullText = "";
static String s_errorMessage = "";

// Gestion tactile robuste (inspiree de CoreS3-TouchDiagnostics v0.1.0.1)
static bool s_prevPressed = false;
static uint32_t s_contactId = 0;
static uint32_t s_pressStartTimeMs = 0;
static uint32_t s_lastPressTimeMs = 0;
static int32_t s_lastTouchX = 0;
static int32_t s_lastTouchY = 0;
static bool s_touchInListening = false;
static bool s_serialListening = false;
static bool s_simTouchPressed = false;
static int32_t s_simTouchX = 160;
static int32_t s_simTouchY = 210;

// Diagnostics des etats et transitions temporelles
static VoiceState s_lastVoiceState = VoiceState::UNINITIALIZED;
static uint32_t s_lastVoiceStateChangeMs = 0;
static uint32_t s_listeningStartTimeMs = 0;
static uint32_t s_processingStartTimeMs = 0;
static uint32_t s_stoppedStartTimeMs = 0;
static uint32_t s_errorStartTimeMs = 0;

// Compteurs de diagnostics PTT
static uint32_t s_pttRejectCount = 0;
static uint32_t s_pttRecoveryCount = 0;

// Instrumentation et diagnostics de boucle
static uint32_t s_turnCounter = 0;
static uint32_t s_turnsCompleted = 0;
static uint32_t s_stopInterruptCount = 0;
static uint64_t s_updateCount = 0;
static uint64_t s_sumUpdateDurUs = 0;
static uint32_t s_maxUpdateDurUs = 0;
static uint64_t s_uiRenderCount = 0;
static uint64_t s_sumUiRenderDurUs = 0;
static uint32_t s_maxUiRenderDurUs = 0;
static uint32_t s_lastDiagPrintMs = 0;
static constexpr uint32_t DIAG_INTERVAL_MS = 10000;

// Indicateur de degradation / charge elevee
static bool s_throttleUi = false;

// =============================================================================
// Fonctions Auxiliaires de Diagnostic PTT
// =============================================================================

static const char* getPttRejectCause(const VoxCoreVoice& v, bool inListening) {
    if (inListening || s_serialListening) return "ALREADY_IN_LISTENING";
    if (!WiFi.isConnected()) return "WIFI_NOT_CONNECTED";
    if (v.isConnecting()) return "CONNECTING_SERVICE";
    if (v.getSession().getState() != LiveSessionState::CONNECTED) {
        switch (v.getSession().getState()) {
            case LiveSessionState::CONNECTING:       return "SESSION_CONNECTING";
            case LiveSessionState::RECONNECTING:     return "SESSION_RECONNECTING";
            case LiveSessionState::WAITING_RESPONSE: return "SESSION_WAITING_RESPONSE";
            case LiveSessionState::PLAYING:          return "SESSION_PLAYING";
            case LiveSessionState::ERROR:            return "SESSION_ERROR";
            case LiveSessionState::DISCONNECTED:     return "SESSION_DISCONNECTED";
            default:                                 return "SESSION_NOT_CONNECTED";
        }
    }
    if (v.getConversationController().isTurnActive()) return "TURN_ALREADY_ACTIVE";
    if (v.isSpeaking()) return "SPEAKER_BUSY";
    if (v.isListening()) return "MIC_ALREADY_LISTENING";
    if (v.isTranscribing()) return "TRANSCRIBING";
    if (v.getState() == VoiceState::ERROR_STATE) return "VOICE_ERROR_STATE";
    if (v.getState() == VoiceState::STOPPED) return "VOICE_STOPPED";
    if (v.isBusy()) return "ENGINE_BUSY";
    if (!v.isReady()) return "NOT_READY";
    return "UNKNOWN";
}

static void logPttReject(const char* context, const char* cause) {
    s_pttRejectCount++;
    uint32_t nowMs = millis();
    uint32_t voiceStateDurMs = (nowMs >= s_lastVoiceStateChangeMs) ? (nowMs - s_lastVoiceStateChangeMs) : 0;
    Serial.printf("[PTT][REJECT] Refus PARLER (%s) #%u : cause=\"%s\" | voice_state=%s (%u ms) | session_state=%s | busy=%d (turnActive=%d, spk=%d, mic=%d, waitResp=%d) | ready=%d | inListening=%d\n",
                  context, (unsigned)s_pttRejectCount, cause, voiceStateToString(voice.getState()), (unsigned)voiceStateDurMs,
                  liveSessionStateToString(voice.getSession().getState()), voice.isBusy() ? 1 : 0,
                  voice.getConversationController().isTurnActive() ? 1 : 0, voice.isSpeaking() ? 1 : 0,
                  voice.isListening() ? 1 : 0, (voice.getSession().getState() == LiveSessionState::WAITING_RESPONSE) ? 1 : 0,
                  voice.isReady() ? 1 : 0, s_touchInListening ? 1 : 0);
}

static void logPttRecovery(const char* motif, uint32_t durationMs) {
    s_pttRecoveryCount++;
    Serial.printf("[PTT][RECOVERY] #%u Declenchement recuperation non bloquante : motif=\"%s\" apres %u ms -> nettoyage controle\n",
                  (unsigned)s_pttRecoveryCount, motif, (unsigned)durationMs);
}

// =============================================================================
// Diagnostics Periodiques Serie
// =============================================================================

static void printPeriodicDiagnostics() {
    uint32_t avgUpUs = (s_updateCount > 0) ? (uint32_t)(s_sumUpdateDurUs / s_updateCount) : 0;
    uint32_t avgUiUs = (s_uiRenderCount > 0) ? (uint32_t)(s_sumUiRenderDurUs / s_uiRenderCount) : 0;
    uint32_t nowMs = millis();
    uint32_t curVoiceStateDur = (nowMs >= s_lastVoiceStateChangeMs) ? (nowMs - s_lastVoiceStateChangeMs) : 0;

    Serial.println("--------------------------------------------------------");
    Serial.println("  [DIAGNOSTICS] OpenAIConversation v0.6.3.2.2 (Corrective)");
    Serial.printf("[DIAG][LOOP]   voice.update() : Moyenne = %u us, Max = %u us (sur %llu cycles)\n",
                  (unsigned)avgUpUs, (unsigned)s_maxUpdateDurUs, (unsigned long long)s_updateCount);
    Serial.printf("[DIAG][UI]     Rendu graphique : Moyenne = %u us, Max = %u us (sur %llu cycles)\n",
                  (unsigned)avgUiUs, (unsigned)s_maxUiRenderDurUs, (unsigned long long)s_uiRenderCount);
    Serial.printf("[DIAG][UI_SUB] Max elements : Orbe=%u us | Header=%u us | Etat=%u us | Vous=%u us | Asst=%u us | Btn=%u us\n",
                  (unsigned)s_ui.getOrbMaxUs(), (unsigned)s_ui.getHeaderMaxUs(),
                  (unsigned)s_ui.getStateTextMaxUs(), (unsigned)s_ui.getUserCardMaxUs(),
                  (unsigned)s_ui.getAsstCardMaxUs(), (unsigned)s_ui.getMainBtnMaxUs());
    Serial.printf("[DIAG][MEM]    Heap libre : %u octets (min: %u) | PSRAM libre : %u octets (min: %u)\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getFreePsram(), (unsigned)ESP.getMinFreePsram());
    Serial.printf("[DIAG][PTT]    Rejets PTT : %u | Recuperations PTT : %u | Contact ID : #%u | InListening : %d\n",
                  (unsigned)s_pttRejectCount, (unsigned)s_pttRecoveryCount,
                  (unsigned)s_contactId, s_touchInListening ? 1 : 0);
    Serial.printf("[DIAG][STAT]   Tours termines : %u | Stops : %u | Etat moteur : %s (%u ms) | Wi-Fi : %s (%d dBm)\n",
                  (unsigned)s_turnsCompleted,
                  (unsigned)s_stopInterruptCount,
                  voiceStateToString(voice.getState()),
                  (unsigned)curVoiceStateDur,
                  WiFi.isConnected() ? "CONNECTE" : "DECONNECTE",
                  WiFi.isConnected() ? WiFi.RSSI() : 0);
    Serial.println("--------------------------------------------------------");

    s_updateCount = 0;
    s_sumUpdateDurUs = 0;
    s_maxUpdateDurUs = 0;
    s_uiRenderCount = 0;
    s_sumUiRenderDurUs = 0;
    s_maxUiRenderDurUs = 0;
}

// =============================================================================
// Initialisation (setup)
// =============================================================================

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(300);

    Serial.println("\n========================================================");
    Serial.println("  VoiceKit v0.6.3.2.2 — Modern Voice UI Edition         ");
    Serial.println("  OpenAI Realtime Conversation (gpt-realtime-2.1-mini)  ");
    Serial.println("  M5Stack CoreS3 (320x240 M5GFX, ES7210 + AW88298)     ");
    Serial.println("========================================================");

    M5.Display.setRotation(1);

    // Initialisation de la Modern Voice UI
    if (!s_ui.begin(&M5.Display)) {
        Serial.println("[UI][WARN] Echec initialisation VoiceUI");
    }

    // Configuration du moteur vocal VoiceKit
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

    // Consigne conversationnelle concise specifique v0.6.3.2.2
    config.conversationInstruction =
        "Tu es un assistant vocal français. "
        "Réponds directement à la question en une seule phrase courte, généralement 15 à 25 mots maximum. "
        "Évite les introductions, les répétitions et les formules de politesse inutiles. "
        "Si l'utilisateur demande explicitement une explication détaillée, adapte la longueur de ta réponse.";

    // 1. Callbacks d'etat et de cycle de vie VoiceKit
    voice.onReady([]() {
        Serial.println("[OpenAIConv] Session OpenAI Realtime prete et confirmee !");
        s_errorMessage = "";
        s_ui.setErrorMessage("");
        s_ui.setState(ConvUIState::READY);
        s_listeningStartTimeMs = 0;
        s_processingStartTimeMs = 0;
        s_stoppedStartTimeMs = 0;
        s_errorStartTimeMs = 0;
    });

    voice.onStateChanged([](VoiceState state) {
        uint32_t nowMs = millis();
        uint32_t stateDurationMs = (s_lastVoiceStateChangeMs > 0 && nowMs >= s_lastVoiceStateChangeMs) ? (nowMs - s_lastVoiceStateChangeMs) : 0;
        Serial.printf("[VOICE][STATE] %s -> %s (duree etat precedent : %u ms)\n",
                      voiceStateToString(s_lastVoiceState), voiceStateToString(state), (unsigned)stateDurationMs);
        s_lastVoiceState = state;
        s_lastVoiceStateChangeMs = nowMs;

        switch (state) {
            case VoiceState::READY:
                s_listeningStartTimeMs = 0;
                s_processingStartTimeMs = 0;
                s_stoppedStartTimeMs = 0;
                s_errorStartTimeMs = 0;
                s_errorMessage = "";
                s_ui.setErrorMessage("");
                s_ui.setState(ConvUIState::READY);
                break;
            case VoiceState::IDLE:
                // IDLE signifie repos/attente : si le moteur est reellement pret, afficher READY, sinon INITIALIZING
                if (voice.isReady()) {
                    s_ui.setState(ConvUIState::READY);
                } else {
                    s_ui.setState(ConvUIState::INITIALIZING);
                }
                break;
            case VoiceState::LISTENING:
                s_listeningStartTimeMs = nowMs;
                s_ui.setState(ConvUIState::LISTENING);
                break;
            case VoiceState::PROCESSING:
            case VoiceState::TRANSCRIBING:
                s_processingStartTimeMs = nowMs;
                s_ui.setState(ConvUIState::PROCESSING);
                break;
            case VoiceState::SPEAKING:
                s_ui.setState(ConvUIState::SPEAKING);
                break;
            case VoiceState::STOPPED:
                s_stoppedStartTimeMs = nowMs;
                s_ui.setState(ConvUIState::STOPPED);
                break;
            case VoiceState::CONNECTING_WIFI:
            case VoiceState::CONNECTING_SERVICE:
            case VoiceState::UNINITIALIZED:
            case VoiceState::DISCONNECTED:
                s_ui.setState(ConvUIState::INITIALIZING);
                break;
            case VoiceState::ERROR_STATE:
                s_errorStartTimeMs = nowMs;
                s_ui.setState(ConvUIState::ERROR_STATE);
                break;
            default:
                break;
        }
    });

    voice.onError([](const String& err) {
        Serial.printf("[DIAG][ERR] Erreur rencontree : %s\n", err.c_str());
        s_errorMessage = err;
        s_ui.setErrorMessage(err);
        s_ui.setState(ConvUIState::ERROR_STATE);
    });

    voice.onListeningStarted([]() {
        Serial.println("[AUDIO][MIC] Debut de capture microphone ES7210 (PCM 16 kHz)");
    });

    voice.onListeningFinished([]() {
        Serial.println("[AUDIO][MIC] Fin de capture microphone ES7210");
    });

    voice.onSpeechStarted([]() {
        Serial.println("[AUDIO][RX] Premier paquet audio OpenAI Realtime recu -> debut restitution DAC");
    });

    voice.onSpeechFinished([]() {
        Serial.println("[AUDIO][SPK] Fin effective de restitution haut-parleur AW88298 (tampon vide)");
    });

    // 2. Transcription utilisateur (Whisper)
    voice.onUserText([](const String& text, bool isFinal) {
        s_userFullText = text;
        s_ui.setUserText(text);
        if (isFinal) {
            Serial.printf("[OpenAIConv][USER WHISPER] %s\n", text.c_str());
        }
    });

    // 3. Transcription assistant (OpenAI)
    voice.onAssistantText([](const String& text) {
        s_assistantFullText = text;
        s_ui.setAssistantText(text);
        Serial.printf("[OpenAIConv][OPENAI TEXT] %s\n", text.c_str());
    });

    // 4. Metriques instrumentees de fin de tour
    voice.onConversationTurnMetrics([](const ConversationMetrics& m) {
        s_turnsCompleted++;
        Serial.println("========================================================");
        Serial.printf("[CONV][TURN] Bilan Tour #%llu termine :\n", (unsigned long long)m.turnId);
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
    });

    // Demarrage de VoiceKit
    if (!voice.begin(config)) {
        Serial.printf("[OpenAIConv][ERREUR] Echec voice.begin() : %s\n", voice.getLastError().c_str());
        s_errorMessage = voice.getLastError();
        s_ui.setErrorMessage(s_errorMessage);
        s_ui.setState(ConvUIState::ERROR_STATE);
        return;
    }

    Serial.println("[OpenAIConv] Demarrage initialise. Connexion Wi-Fi et session OpenAI en cours...");

    // Reinitialisation des compteurs pour isoler les mesures du regime permanent
    s_ui.resetPerformanceMetrics();
    s_uiRenderCount = 0;
    s_sumUiRenderDurUs = 0;
    s_maxUiRenderDurUs = 0;
    s_updateCount = 0;
    s_sumUpdateDurUs = 0;
    s_maxUpdateDurUs = 0;
    s_lastVoiceStateChangeMs = millis();
}

// =============================================================================
// Boucle Principale (loop)
// =============================================================================

void loop() {
    uint32_t now = millis();

    // 1. Polling materiel M5Unified
    M5.update();

    // 2. Traitement cyclique VoiceKit avec mesure temporelle haute precision
    uint32_t tUpStart = micros();
    voice.update();
    uint32_t upDurUs = micros() - tUpStart;

    s_updateCount++;
    s_sumUpdateDurUs += upDurUs;
    if (upDurUs > s_maxUpdateDurUs) {
        s_maxUpdateDurUs = upDurUs;
    }

    // Auto-degradation : si voice.update() prend plus de 5 ms, alleger la cadence graphique
    s_throttleUi = (upDurUs > 5000);

    // 3. Commandes serie de test (P: Parler, R: Relâcher, S: Stop, D: Diagnostics)
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'P' || c == 'p') {
            Serial.println("[SERIE] Commande 'P' (PARLER) recue");
            if (voice.isReady() && !voice.isBusy() && !s_touchInListening && !s_serialListening) {
                s_userFullText = "";
                s_assistantFullText = "";
                s_ui.setUserText("");
                s_ui.setAssistantText("");
                s_turnCounter++;
                Serial.printf("[CONV][TURN] Debut tour #%u (via Serie)\n", s_turnCounter);
                if (voice.startListening()) {
                    s_serialListening = true;
                    s_listeningStartTimeMs = millis();
                } else {
                    logPttReject("SERIE_P", "START_LISTENING_FAILED");
                }
            } else {
                const char* cause = getPttRejectCause(voice, s_touchInListening);
                logPttReject("SERIE_P", cause);
            }
        } else if (c == 'R' || c == 'r') {
            Serial.println("[SERIE] Commande 'R' (RELACHER) recue");
            s_serialListening = false;
            if (voice.isListening()) {
                if (!voice.stopListening()) {
                    Serial.println("[SERIE][WARN] voice.stopListening() a echoue -> cancelListening()");
                    voice.cancelListening();
                    voice.stop();
                }
            }
        } else if (c == 'S' || c == 's') {
            Serial.println("[SERIE] Commande 'S' (STOP prioritaire) recue");
            s_stopInterruptCount++;
            s_touchInListening = false;
            s_serialListening = false;
            voice.stop();
        } else if (c == 'D' || c == 'd') {
            printPeriodicDiagnostics();
        } else if (c == 'T' || c == 't') {
            Serial.println("[SERIE] Simulation appui tactile PARLER");
            s_simTouchPressed = true;
            s_simTouchX = 160;
            s_simTouchY = 210;
        } else if (c == 'U' || c == 'u') {
            Serial.println("[SERIE] Simulation relachement tactile PARLER");
            s_simTouchPressed = false;
        } else if (c == 'E' || c == 'e') {
            Serial.println("[SERIE] Simulation injection d'erreur pour test recuperation");
            s_ui.setState(ConvUIState::ERROR_STATE);
            s_errorStartTimeMs = millis();
            s_errorMessage = "Erreur de test simulee";
            s_ui.setErrorMessage(s_errorMessage.c_str());
        }
    }

    // 4. Gestion tactile robuste (CoreS3-TouchDiagnostics v0.1.0.1)
    int pointCount = M5.Touch.getCount();
    bool isPressed = false;
    int32_t curX = s_lastTouchX;
    int32_t curY = s_lastTouchY;

    if (s_simTouchPressed) {
        isPressed = true;
        curX = s_simTouchX;
        curY = s_simTouchY;
        s_lastTouchX = curX;
        s_lastTouchY = curY;
    } else if (pointCount > 0) {
        auto detail = M5.Touch.getDetail(0);
        isPressed = detail.isPressed();
        curX = detail.x;
        curY = detail.y;
        s_lastTouchX = curX;
        s_lastTouchY = curY;
    }

    bool inButton = s_ui.isPointInButton(curX, curY);

    // Transition 1 : PRESS (Front montant)
    if (!s_prevPressed && isPressed) {
        s_contactId++;
        s_pressStartTimeMs = now;
        s_lastPressTimeMs = now;

        Serial.printf("[TOUCH][PRESS] Contact #%u a (%d, %d) [inButton=%s, etat_ui=%s, etat_moteur=%s]\n",
                      (unsigned)s_contactId, (int)curX, (int)curY, inButton ? "OUI" : "NON",
                      uiStateToString(s_ui.getState()), voiceStateToString(voice.getState()));

        if (inButton) {
            ConvUIState curSt = s_ui.getState();
            if (curSt == ConvUIState::PROCESSING || curSt == ConvUIState::SPEAKING) {
                // Interruption prioritaire immediate STOP sur front montant
                s_stopInterruptCount++;
                Serial.printf("[TOUCH][STOP] Interruption prioritaire STOP (contact #%u)\n", (unsigned)s_contactId);
                s_touchInListening = false;
                voice.stop();
            } else if (curSt == ConvUIState::ERROR_STATE) {
                // Recuperation controlee apres erreur
                uint32_t errDur = (s_errorStartTimeMs > 0 && now >= s_errorStartTimeMs) ? (now - s_errorStartTimeMs) : 0;
                logPttRecovery("Reinitialisation utilisateur suite a erreur", errDur);
                s_errorMessage = "";
                s_ui.setErrorMessage("");
                s_touchInListening = false;
                voice.stop();
                if (voice.isReady()) {
                    s_ui.setState(ConvUIState::READY);
                } else if (!WiFi.isConnected()) {
                    s_ui.setState(ConvUIState::INITIALIZING);
                }
            } else {
                // Demande de prise de parole Push-to-Talk (PARLER)
                // Verifier si le moteur est reellement pret sans jamais forcer artificiellement
                if (voice.isReady() && !voice.isBusy() && !s_touchInListening && !s_serialListening) {
                    s_userFullText = "";
                    s_assistantFullText = "";
                    s_ui.setUserText("");
                    s_ui.setAssistantText("");
                    s_turnCounter++;
                    Serial.printf("[CONV][TURN] Debut tour #%u (contact #%u)\n", s_turnCounter, (unsigned)s_contactId);
                    if (voice.startListening()) {
                        s_touchInListening = true;
                        s_listeningStartTimeMs = now;
                    } else {
                        logPttReject("TOUCH_PRESS", "START_LISTENING_FAILED");
                        s_touchInListening = false;
                    }
                } else {
                    const char* cause = getPttRejectCause(voice, s_touchInListening);
                    logPttReject("TOUCH_PRESS", cause);

                    // Mecanisme de recuperation non bloquant pour les etats anormalement persistants
                    bool canRecover = false;
                    if (s_touchInListening && !voice.isListening()) {
                        canRecover = true;
                    } else if (voice.getConversationController().isTurnActive() &&
                               !voice.isSpeaking() && !voice.isListening() &&
                               voice.getSession().getState() == LiveSessionState::CONNECTED) {
                        canRecover = true;
                    } else if (voice.getState() == VoiceState::STOPPED || curSt == ConvUIState::STOPPED) {
                        canRecover = true;
                    }

                    if (canRecover) {
                        uint32_t stDur = (s_lastVoiceStateChangeMs > 0 && now >= s_lastVoiceStateChangeMs) ? (now - s_lastVoiceStateChangeMs) : 0;
                        logPttRecovery(cause, stDur);
                        s_touchInListening = false;
                        voice.stop();
                        if (voice.isReady() && !voice.isBusy()) {
                            s_userFullText = "";
                            s_assistantFullText = "";
                            s_ui.setUserText("");
                            s_ui.setAssistantText("");
                            s_turnCounter++;
                            Serial.printf("[CONV][TURN] Debut tour #%u apres recuperation (contact #%u)\n",
                                          s_turnCounter, (unsigned)s_contactId);
                            if (voice.startListening()) {
                                s_touchInListening = true;
                                s_listeningStartTimeMs = now;
                            }
                        }
                    }
                }
            }
        }
    }
    // Transition 2 : MAINTIEN / GLISSEMENT HORS ZONE
    else if (s_prevPressed && isPressed) {
        if (s_touchInListening) {
            // Marge de tolerance de 20 px autour du bouton
            bool stillNearby = s_ui.isPointNearbyButton(curX, curY, 20);
            if (!stillNearby) {
                Serial.printf("[TOUCH][SLIDE_OUT] Glissement hors zone (%d, %d) -> fin de parole securisee\n",
                              (int)curX, (int)curY);
                s_touchInListening = false;
                if (voice.isListening()) {
                    if (!voice.stopListening()) {
                        Serial.println("[TOUCH][WARN] stopListening() a echoue suite a glissement -> cancelListening()");
                        voice.cancelListening();
                        voice.stop();
                    }
                }
            }
        }
    }
    // Transition 3 : RELÂCHEMENT (Front descendant)
    else if (s_prevPressed && !isPressed) {
        uint32_t pressDuration = (now >= s_pressStartTimeMs) ? (now - s_pressStartTimeMs) : 0;
        Serial.printf("[TOUCH][RELEASE] Contact #%u relache a (%d, %d) apres %u ms [inListening=%d]\n",
                      (unsigned)s_contactId, (int)s_lastTouchX, (int)s_lastTouchY, (unsigned)pressDuration,
                      s_touchInListening ? 1 : 0);

        if (s_touchInListening) {
            s_touchInListening = false;
            if (voice.isListening()) {
                Serial.printf("[AUDIO][MIC] Relachement PARLER -> transmission a OpenAI (duree parole: %u ms)\n",
                              (unsigned)pressDuration);
                if (!voice.stopListening()) {
                    Serial.println("[TOUCH][WARN] voice.stopListening() a echoue -> cancelListening()");
                    voice.cancelListening();
                    voice.stop();
                }
            }
        }
    }

    // Securite de coherence PTT tactile : si aucun appui physique n'est actif, s_touchInListening doit etre faux
    if (!isPressed && s_touchInListening) {
        Serial.println("[PTT][RECOVERY] Incoherence detectee : s_touchInListening=true sans appui physique -> liberation");
        s_touchInListening = false;
        if (voice.isListening() && !s_serialListening) {
            voice.stopListening();
        }
    }

    s_prevPressed = isPressed;

    // 5. Watchdog de recuperation non bloquant pour etats anormalement persistants
    // 5.1 Timeout d'ecoute micro (15 s max pour une prise de parole PTT)
    if (voice.isListening() && s_listeningStartTimeMs > 0 && now >= s_listeningStartTimeMs && (now - s_listeningStartTimeMs >= 15000)) {
        logPttRecovery("Timeout ecoute microphone (15 s)", now - s_listeningStartTimeMs);
        s_touchInListening = false;
        s_serialListening = false;
        s_listeningStartTimeMs = 0;
        voice.stopListening();
    }
    // 5.2 Timeout d'attente de reponse serveur (15 s max en PROCESSING)
    if (s_ui.getState() == ConvUIState::PROCESSING && s_processingStartTimeMs > 0 && now >= s_processingStartTimeMs && (now - s_processingStartTimeMs >= 15000)) {
        logPttRecovery("Timeout attente reponse serveur (15 s)", now - s_processingStartTimeMs);
        s_touchInListening = false;
        s_serialListening = false;
        s_processingStartTimeMs = 0;
        voice.stop();
        if (voice.isReady()) {
            s_ui.setState(ConvUIState::READY);
        }
    }
    // 5.3 Reprise automatique apres transition STOPPED (> 1 s)
    if (s_ui.getState() == ConvUIState::STOPPED && s_stoppedStartTimeMs > 0 && now >= s_stoppedStartTimeMs && (now - s_stoppedStartTimeMs >= 1000)) {
        s_stoppedStartTimeMs = 0;
        if (voice.isReady()) {
            logPttRecovery("Transition automatique STOPPED -> READY (apres 1 s)", 1000);
            s_ui.setState(ConvUIState::READY);
        }
    }
    // 5.4 Reprise automatique apres ERROR_STATE persistant (> 15 s) si Wi-Fi connecte
    if (s_ui.getState() == ConvUIState::ERROR_STATE && s_errorStartTimeMs > 0 && now >= s_errorStartTimeMs && (now - s_errorStartTimeMs >= 15000)) {
        s_errorStartTimeMs = 0;
        if (WiFi.isConnected()) {
            logPttRecovery("Tentative automatique de reprise apres erreur persistante (15 s)", 15000);
            s_errorMessage = "";
            s_ui.setErrorMessage("");
            voice.stop();
            if (voice.isReady()) {
                s_ui.setState(ConvUIState::READY);
            }
        }
    }

    // 6. Statut Wi-Fi transmis a l'IHM
    s_ui.setWifiConnected(WiFi.isConnected());

    // 7. Rafraichissement cible Modern Voice UI (mesure temporelle)
    uint32_t audioLevel = (s_ui.getState() == ConvUIState::LISTENING) ? voice.getAudioLevel() : 0;
    uint32_t tUiStart = micros();
    s_ui.update(now, audioLevel, s_throttleUi);
    uint32_t uiDurUs = micros() - tUiStart;

    s_uiRenderCount++;
    s_sumUiRenderDurUs += uiDurUs;
    if (uiDurUs > s_maxUiRenderDurUs) {
        s_maxUiRenderDurUs = uiDurUs;
    }

    // 8. Diagnostics periodiques automatiques
    if (now - s_lastDiagPrintMs >= DIAG_INTERVAL_MS) {
        s_lastDiagPrintMs = now;
        printPeriodicDiagnostics();
    }

    // Boucle cooperative non-bloquante : zero delay() artificiel
}
