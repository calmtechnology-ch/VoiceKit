/**
 * @file main.cpp
 * @brief Exemple OpenAIConversation v0.6.3.2 — Modern Voice UI Edition
 *        Dialogue vocal bidirectionnel temps réel avec OpenAI Realtime API
 *        (gpt-realtime-2.1-mini) sur M5Stack CoreS3 (320x240 M5GFX).
 *
 * Évolutions v0.6.3.2 :
 * - Remplacement intégral de l'ancienne interface par la Modern Voice UI :
 *   fond bleu nuit premium, contrastes doux, en-tête compact avec Wi-Fi et badge d'état court,
 *   orbe vocal lumineux animé à 5 états (respiration, réaction RMS réelle, rotation orbitale,
 *   ondes sonores, alerte fixe), message d'état centré en français, deux cartes conversationnelles
 *   arrondies avec retour à la ligne en pixels réels (FrenchUI), grand bouton d'action tactile.
 * - Architecture modulaire stricte : VoiceUI (composition/cartes/bouton), VoiceOrb (orbe autonome),
 *   FrenchUI (moteur UTF-8 et diacritiques sans police CJK).
 * - Orbe vocal 18 fps max non-bloquant avec double-buffering M5Canvas, zero delay(),
 *   auto-dégradation si voice.update() est sous charge.
 * - Réaction visuelle authentique au niveau sonore RMS (VoxCoreVoice::getAudioLevel()) sans aucune simulation.
 * - Intégrité totale de VoiceKit v0.6.3.1 et de la chaîne audio/réseau préservée.
 * - Diagnostics série complets conservés (durées voice.update(), Heap/PSRAM libres, latences).
 * - Commandes série de test (P: Parler, R: Relâcher, S: Stop, D: Diagnostics).
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

// Moteur vocal VoiceKit (propriétaire exclusif audio/réseau)
static VoxCoreVoice voice;

// Interface graphique moderne
static VoiceUI s_ui;

// Mémoire des transcriptions complètes
static String s_userFullText = "";
static String s_assistantFullText = "";
static String s_errorMessage = "";

// Gestion tactile robuste (inspirée de CoreS3-TouchDiagnostics v0.1.0.1)
static bool s_prevPressed = false;
static uint32_t s_contactId = 0;
static uint32_t s_lastPressTimeMs = 0;
static int32_t s_lastTouchX = 0;
static int32_t s_lastTouchY = 0;
static bool s_touchInListening = false;
static constexpr uint32_t TOUCH_DEBOUNCE_MS = 40;

// Instrumentation et diagnostics
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

// Indicateur de dégradation / charge élevée
static bool s_throttleUi = false;

// =============================================================================
// Diagnostics Périodiques Série
// =============================================================================

static void printPeriodicDiagnostics() {
    uint32_t avgUpUs = (s_updateCount > 0) ? (uint32_t)(s_sumUpdateDurUs / s_updateCount) : 0;
    uint32_t avgUiUs = (s_uiRenderCount > 0) ? (uint32_t)(s_sumUiRenderDurUs / s_uiRenderCount) : 0;

    Serial.println("--------------------------------------------------------");
    Serial.println("  [DIAGNOSTICS] OpenAIConversation v0.6.3.2.1 (Corrective)");
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
    Serial.printf("[DIAG][STAT] Tours termines : %u | Stops : %u | Etat moteur : %s | Wi-Fi : %s (%d dBm)\n",
                  (unsigned)s_turnsCompleted,
                  (unsigned)s_stopInterruptCount,
                  voiceStateToString(voice.getState()),
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
    Serial.println("  VoiceKit v0.6.3.2.1 — Modern Voice UI Edition         ");
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
    config.conversationInstruction =
        "Tu es un assistant vocal bienveillant, intelligent et concis. "
        "Reponds toujours en francais direct et naturel en une ou deux phrases maximum.";

    // 1. Callbacks d'etat et de cycle de vie VoiceKit
    voice.onReady([]() {
        Serial.println("[OpenAIConv] Session OpenAI Realtime prete et confirmee !");
        s_errorMessage = "";
        s_ui.setErrorMessage("");
        s_ui.setState(ConvUIState::READY);
    });

    voice.onStateChanged([](VoiceState state) {
        Serial.printf("[OpenAIConv] Transition etat moteur : %s\n", voiceStateToString(state));
        switch (state) {
            case VoiceState::READY:
            case VoiceState::IDLE:
                s_ui.setState(ConvUIState::READY);
                break;
            case VoiceState::LISTENING:
                s_ui.setState(ConvUIState::LISTENING);
                break;
            case VoiceState::PROCESSING:
            case VoiceState::TRANSCRIBING:
                s_ui.setState(ConvUIState::PROCESSING);
                break;
            case VoiceState::SPEAKING:
                s_ui.setState(ConvUIState::SPEAKING);
                break;
            case VoiceState::STOPPED:
                s_ui.setState(ConvUIState::STOPPED);
                break;
            case VoiceState::CONNECTING_WIFI:
            case VoiceState::CONNECTING_SERVICE:
            case VoiceState::UNINITIALIZED:
            case VoiceState::DISCONNECTED:
                s_ui.setState(ConvUIState::INITIALIZING);
                break;
            case VoiceState::ERROR_STATE:
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
}

// =============================================================================
// Boucle Principale (loop)
// =============================================================================

void loop() {
    uint32_t now = millis();

    // 1. Polling materiel M5Unified
    M5.update();

    // 2. Traitement cyclique VoiceKit avec mesure temporelle haute précision
    uint32_t tUpStart = micros();
    voice.update();
    uint32_t upDurUs = micros() - tUpStart;

    s_updateCount++;
    s_sumUpdateDurUs += upDurUs;
    if (upDurUs > s_maxUpdateDurUs) {
        s_maxUpdateDurUs = upDurUs;
    }

    // Auto-dégradation : si voice.update() prend plus de 5 ms, alléger la cadence graphique
    s_throttleUi = (upDurUs > 5000);

    // 3. Commandes série (P: Parler, R: Relâcher, S: Stop, D: Diagnostics)
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'P' || c == 'p') {
            Serial.println("[SERIE] Commande 'P' (PARLER) recue");
            if (voice.isReady() && !voice.isBusy()) {
                s_userFullText = "";
                s_assistantFullText = "";
                s_ui.setUserText("");
                s_ui.setAssistantText("");
                s_turnCounter++;
                Serial.printf("[CONV][TURN] Debut tour #%u (via Serie)\n", s_turnCounter);
                if (!voice.startListening()) {
                    Serial.println("[SERIE][WARN] voice.startListening() a echoue");
                }
            } else {
                Serial.printf("[SERIE][WARN] Commande PARLER rejetee : moteur non pret (etat=%s)\n",
                              voiceStateToString(voice.getState()));
            }
        } else if (c == 'R' || c == 'r') {
            Serial.println("[SERIE] Commande 'R' (RELACHER) recue");
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
            voice.stop();
        } else if (c == 'D' || c == 'd') {
            printPeriodicDiagnostics();
        }
    }

    // 4. Gestion tactile robuste (CoreS3-TouchDiagnostics v0.1.0.1)
    int pointCount = M5.Touch.getCount();
    bool isPressed = false;
    int32_t curX = s_lastTouchX;
    int32_t curY = s_lastTouchY;

    if (pointCount > 0) {
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
        if (now - s_lastPressTimeMs >= TOUCH_DEBOUNCE_MS) {
            s_lastPressTimeMs = now;
            s_contactId++;
            Serial.printf("[TOUCH][PRESS] Contact #%u a (%d, %d) [inButton=%s, etat=%s]\n",
                          (unsigned)s_contactId, (int)curX, (int)curY, inButton ? "OUI" : "NON",
                          uiStateToString(s_ui.getState()));

            if (inButton) {
                ConvUIState curSt = s_ui.getState();
                if (curSt == ConvUIState::PROCESSING || curSt == ConvUIState::SPEAKING) {
                    // Interruption prioritaire immédiate STOP sur front montant
                    s_stopInterruptCount++;
                    Serial.printf("[TOUCH][STOP] Interruption prioritaire STOP (contact #%u)\n", (unsigned)s_contactId);
                    s_touchInListening = false;
                    voice.stop();
                } else if (curSt == ConvUIState::ERROR_STATE) {
                    // Récupération contrôlée après erreur
                    Serial.printf("[TOUCH][RECOVER] Reinitialisation demandee apres erreur (contact #%u)\n", (unsigned)s_contactId);
                    s_errorMessage = "";
                    s_ui.setErrorMessage("");
                    s_touchInListening = false;
                    voice.stop();
                    if (voice.isReady()) {
                        s_ui.setState(ConvUIState::READY);
                    }
                } else if (voice.isReady() && !voice.isBusy() && !s_touchInListening) {
                    // Démarrage de la prise de parole Push-to-Talk
                    s_userFullText = "";
                    s_assistantFullText = "";
                    s_ui.setUserText("");
                    s_ui.setAssistantText("");
                    s_turnCounter++;
                    Serial.printf("[CONV][TURN] Debut tour #%u (contact #%u)\n", s_turnCounter, (unsigned)s_contactId);
                    if (voice.startListening()) {
                        s_touchInListening = true;
                    } else {
                        Serial.println("[TOUCH][WARN] voice.startListening() a echoue -> recuperation immediate");
                        s_touchInListening = false;
                        s_ui.setState(ConvUIState::READY);
                    }
                } else {
                    Serial.printf("[TOUCH][WARN] Demande PARLER ignoree : moteur non pret (etat=%s, busy=%d)\n",
                                  voiceStateToString(voice.getState()), voice.isBusy() ? 1 : 0);
                }
            }
        }
    }
    // Transition 2 : MAINTIEN / GLISSEMENT HORS ZONE
    else if (s_prevPressed && isPressed) {
        if (s_touchInListening) {
            // Marge de tolérance de 15 px autour du bouton
            bool stillNearby = s_ui.isPointNearbyButton(curX, curY, 15);
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
        uint32_t pressDuration = (now >= s_lastPressTimeMs) ? (now - s_lastPressTimeMs) : 0;
        Serial.printf("[TOUCH][RELEASE] Contact #%u relache a (%d, %d) apres %u ms\n",
                      (unsigned)s_contactId, (int)s_lastTouchX, (int)s_lastTouchY, (unsigned)pressDuration);

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

    s_prevPressed = isPressed;

    // 5. Statut Wi-Fi transmis à l'IHM
    s_ui.setWifiConnected(WiFi.isConnected());

    // 6. Rafraîchissement ciblé Modern Voice UI (mesure temporelle)
    uint32_t audioLevel = (s_ui.getState() == ConvUIState::LISTENING) ? voice.getAudioLevel() : 0;
    uint32_t tUiStart = micros();
    s_ui.update(now, audioLevel, s_throttleUi);
    uint32_t uiDurUs = micros() - tUiStart;

    s_uiRenderCount++;
    s_sumUiRenderDurUs += uiDurUs;
    if (uiDurUs > s_maxUiRenderDurUs) {
        s_maxUiRenderDurUs = uiDurUs;
    }

    // 7. Diagnostics périodiques automatiques
    if (now - s_lastDiagPrintMs >= DIAG_INTERVAL_MS) {
        s_lastDiagPrintMs = now;
        printPeriodicDiagnostics();
    }

    // Boucle coopérative non-bloquante : zero delay() artificiel
}
