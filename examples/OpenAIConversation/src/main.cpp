/**
 * @file main.cpp
 * @brief Exemple OpenAIConversation — Dialogue vocal bidirectionnel temps reel
 *        avec OpenAI Realtime API (gpt-realtime-2.1-mini) sur M5Stack CoreS3.
 * 
 * VoiceKit v0.6.3.1 :
 * - Fournisseur vocal configure en VoiceProviderType::OPENAI (OpenAI Realtime API).
 * - Connexion WebSocket TLS directe vers wss://api.openai.com/v1/realtime.
 * - Proprietaire unique du microphone ES7210 via VoiceKit HAL (M5AudioInput, 16 kHz).
 * - Reechantillonnage materiel transparent 16 kHz vers 24 kHz sans latence.
 * - Restitution audio native 24 kHz via haut-parleur AW88298 sans coupure finale.
 * - Gestion tactile robuste inspiree de CoreS3-TouchDiagnostics v0.1.0.1 (PRESS / RELEASE fiables,
 *   anti-rebond, gestion des glissements hors zone et contacts perdus, aucun double declenchement).
 * - Priorite absolue au bouton STOP pendant le traitement et la restitution audio.
 * - Machine a etats centralisee calquee sur l'etat reel du moteur VoiceKit (source de verite).
 * - Affichage M5GFX 320x240 avec rafraichissement cible des zones modifiees (zero scintillement).
 * - Support complet et authentique des accents francais UTF-8 et multi-lignes sans coupure de codepoint.
 * - Diagnostics chronometres complets (durees de voice.update(), Heap/PSRAM libres, latences).
 * - Commandes serie de controle et de test (P: Parler, R: Relacher, S: Stop, D: Diagnostics).
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>
#include <WiFi.h>
#include "FrenchUI.h"

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
// Etats de l'interface graphique CoreS3 (M5GFX sans LVGL)
// =============================================================================
enum class ConvUIState : uint8_t {
    INITIALIZING, ///< En attente Wi-Fi / connexion WebSocket OpenAI
    READY,        ///< Pret a converser (Push-to-Talk)
    LISTENING,    ///> Capture microphone active (bouton maintenu)
    PROCESSING,   ///< Attente reponse / generation OpenAI Realtime
    SPEAKING,     ///< Restitution audio en cours sur AW88298
    STOPPED,      ///< Session ou reponse interrompue par STOP
    ERROR_STATE   ///< Erreur de transport, audio ou reseau
};

static inline const char* uiStateToString(ConvUIState st) {
    switch (st) {
        case ConvUIState::INITIALIZING: return "INITIALIZING";
        case ConvUIState::READY:        return "READY";
        case ConvUIState::LISTENING:    return "LISTENING";
        case ConvUIState::PROCESSING:   return "PROCESSING";
        case ConvUIState::SPEAKING:     return "SPEAKING";
        case ConvUIState::STOPPED:      return "STOPPED";
        case ConvUIState::ERROR_STATE:  return "ERROR_STATE";
        default:                        return "UNKNOWN";
    }
}

// Instance principale VoiceKit (VoxCoreVoice)
static VoxCoreVoice voice;
static ConvUIState s_uiState = ConvUIState::INITIALIZING;

// Contenus textuels
static String s_userText = "";
static String s_assistantText = "";
static String s_errorMessage = "";

// Drapeaux de rafraichissement graphique partiel
static bool s_dirtyFull = true;
static bool s_dirtyBadge = true;
static bool s_dirtyUserBox = true;
static bool s_dirtyAssistantBox = true;
static bool s_dirtyBtn = true;

// Coordonnees geometriques (Ecran paysage 320x240)
static constexpr int BADGE_X = 190;
static constexpr int BADGE_Y = 6;
static constexpr int BADGE_W = 120;
static constexpr int BADGE_H = 18;

static constexpr int USER_BOX_X = 10;
static constexpr int USER_BOX_Y = 48;
static constexpr int USER_BOX_W = 300;
static constexpr int USER_BOX_H = 48;

static constexpr int ASSISTANT_BOX_X = 10;
static constexpr int ASSISTANT_BOX_Y = 114;
static constexpr int ASSISTANT_BOX_W = 300;
static constexpr int ASSISTANT_BOX_H = 44;

static constexpr int BTN_X = 20;
static constexpr int BTN_Y = 165;
static constexpr int BTN_W = 280;
static constexpr int BTN_H = 65;

// Variables de gestion tactile (inspirees de CoreS3-TouchDiagnostics v0.1.0.1)
static bool s_prevPressed = false;
static uint32_t s_contactId = 0;
static uint32_t s_lastPressTimeMs = 0;
static int32_t s_lastTouchX = 0;
static int32_t s_lastTouchY = 0;
static bool s_touchInListening = false;
static constexpr uint32_t TOUCH_DEBOUNCE_MS = 40;

// Variables d'instrumentation et de diagnostics
static uint32_t s_turnCounter = 0;
static uint32_t s_turnsCompleted = 0;
static uint64_t s_updateCount = 0;
static uint64_t s_sumUpdateDurUs = 0;
static uint32_t s_maxUpdateDurUs = 0;
static uint32_t s_lastDiagPrintMs = 0;
static constexpr uint32_t DIAG_INTERVAL_MS = 10000;

// =============================================================================
// Machine a etats UI & Transitions centralisees
// =============================================================================

static void setConvState(ConvUIState st) {
    if (s_uiState != st) {
        Serial.printf("[STATE] Transition UI : %s -> %s\n",
                      uiStateToString(s_uiState), uiStateToString(st));
        s_uiState = st;
        s_dirtyBadge = true;
        s_dirtyBtn = true;
    }
}

// =============================================================================
// Rendu Graphique Partiel M5GFX (Zero Scintillement)
// =============================================================================

static void drawBadge() {
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

    M5.Display.fillRoundRect(BADGE_X, BADGE_Y, BADGE_W, BADGE_H, 4, statusColor);
    M5.Display.setTextColor(TFT_BLACK, statusColor);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(1);
    M5.Display.drawString(statusText, BADGE_X + (BADGE_W / 2), BADGE_Y + (BADGE_H / 2));
    M5.Display.setTextDatum(TL_DATUM);
}

static void drawUserBox() {
    FrenchUI::drawCardContent(
        M5.Display,
        USER_BOX_X, USER_BOX_Y, USER_BOX_W, USER_BOX_H,
        s_userText,
        "(Maintenez PARLER et posez votre question...)",
        TFT_WHITE, TFT_LIGHTGRAY,
        0x18E3, TFT_NAVY,
        46, 3
    );
}

static void drawAssistantBox() {
    String content = s_assistantText;
    if (s_uiState == ConvUIState::ERROR_STATE && !s_errorMessage.isEmpty()) {
        content = "Erreur : " + s_errorMessage;
    }

    FrenchUI::drawCardContent(
        M5.Display,
        ASSISTANT_BOX_X, ASSISTANT_BOX_Y, ASSISTANT_BOX_W, ASSISTANT_BOX_H,
        content,
        "(En attente de dialogue...)",
        0x7FFF, TFT_DARKCYAN,
        0x0821, TFT_DARKCYAN,
        46, 3
    );
}

static void drawMainBtn() {
    uint16_t btnColor = TFT_BLUE;
    const char* btnTitle = "PARLER";
    const char* btnSub = "Appuyer et maintenir pour parler";

    if (s_uiState == ConvUIState::LISTENING) {
        btnColor = TFT_RED;
        btnTitle = "ENREGISTREMENT...";
        btnSub = "Relacher pour envoyer a OpenAI";
    } else if (s_uiState == ConvUIState::PROCESSING || s_uiState == ConvUIState::SPEAKING) {
        btnColor = 0xC800; // Rouge orangé vif pour STOP
        btnTitle = "STOP";
        btnSub = "Toucher pour interrompre";
    } else if (s_uiState == ConvUIState::INITIALIZING) {
        btnColor = TFT_DARKGRAY;
        btnTitle = "PATIENTEZ...";
        btnSub = "Connexion Wi-Fi et session en cours";
    } else if (s_uiState == ConvUIState::ERROR_STATE) {
        btnColor = 0x8800;
        btnTitle = "ERREUR - REESSAYER";
        btnSub = "Toucher pour reinitialiser";
    }

    M5.Display.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 10, btnColor);
    M5.Display.drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 10, TFT_WHITE);

    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, btnColor);
    M5.Display.drawString(btnTitle, BTN_X + (BTN_W / 2), BTN_Y + 22);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGRAY, btnColor);
    M5.Display.drawString(btnSub, BTN_X + (BTN_W / 2), BTN_Y + 48);
    M5.Display.setTextDatum(TL_DATUM);
}

static void updateUI() {
    if (s_dirtyFull) {
        s_dirtyFull = false;
        M5.Display.fillScreen(TFT_BLACK);

        // En-tête fixe
        M5.Display.setTextSize(1);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.drawString("VoiceKit v0.6.3.1 — OpenAI Realtime", 10, 8);
        M5.Display.drawFastHLine(10, 28, 300, TFT_DARKGRAY);

        // Libellés fixes
        M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
        M5.Display.drawString("Vous (Micro 16 kHz) :", 10, 34);

        M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        M5.Display.drawString("OpenAI Realtime (PCM 24 kHz) :", 10, 100);

        s_dirtyBadge = true;
        s_dirtyUserBox = true;
        s_dirtyAssistantBox = true;
        s_dirtyBtn = true;
    }

    if (s_dirtyBadge) {
        s_dirtyBadge = false;
        drawBadge();
    }

    if (s_dirtyUserBox) {
        s_dirtyUserBox = false;
        drawUserBox();
    }

    if (s_dirtyAssistantBox) {
        s_dirtyAssistantBox = false;
        drawAssistantBox();
    }

    if (s_dirtyBtn) {
        s_dirtyBtn = false;
        drawMainBtn();
    }
}

// =============================================================================
// Journalisation Structuree et Diagnostics
// =============================================================================

static void printPeriodicDiagnostics() {
    uint32_t avgUs = (s_updateCount > 0) ? (uint32_t)(s_sumUpdateDurUs / s_updateCount) : 0;
    Serial.println("--------------------------------------------------------");
    Serial.printf("[DIAG][LOOP] voice.update() : Moyenne = %u us, Max = %u us (sur %llu cycles)\n",
                  (unsigned)avgUs, (unsigned)s_maxUpdateDurUs, (unsigned long long)s_updateCount);
    Serial.printf("[DIAG][MEM]  Heap libre : %u octets (min: %u) | PSRAM libre : %u octets (min: %u)\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getFreePsram(), (unsigned)ESP.getMinFreePsram());
    Serial.printf("[DIAG][STAT] Tours termines : %u | Etat moteur : %s | Wi-Fi : %s (RSSI: %d dBm)\n",
                  (unsigned)s_turnsCompleted,
                  voiceStateToString(voice.getState()),
                  WiFi.isConnected() ? "CONNECTE" : "DECONNECTE",
                  WiFi.isConnected() ? WiFi.RSSI() : 0);
    Serial.println("--------------------------------------------------------");

    s_updateCount = 0;
    s_sumUpdateDurUs = 0;
    s_maxUpdateDurUs = 0;
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
    Serial.println("  VoiceKit v0.6.3.1 — OpenAI Realtime Conversation      ");
    Serial.println("  M5Stack CoreS3 (ES7210 Mic + AW88298 Speaker)         ");
    Serial.println("========================================================");

    M5.Display.setRotation(1);
    updateUI();

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
        setConvState(ConvUIState::READY);
    });

    voice.onStateChanged([](VoiceState state) {
        Serial.printf("[OpenAIConv] Etat moteur : %s\n", voiceStateToString(state));
        switch (state) {
            case VoiceState::READY:
            case VoiceState::IDLE:
                setConvState(ConvUIState::READY);
                break;
            case VoiceState::LISTENING:
                setConvState(ConvUIState::LISTENING);
                break;
            case VoiceState::PROCESSING:
            case VoiceState::TRANSCRIBING:
                setConvState(ConvUIState::PROCESSING);
                break;
            case VoiceState::SPEAKING:
                setConvState(ConvUIState::SPEAKING);
                break;
            case VoiceState::STOPPED:
                setConvState(ConvUIState::STOPPED);
                break;
            case VoiceState::CONNECTING_WIFI:
            case VoiceState::CONNECTING_SERVICE:
            case VoiceState::UNINITIALIZED:
            case VoiceState::DISCONNECTED:
                setConvState(ConvUIState::INITIALIZING);
                break;
            case VoiceState::ERROR_STATE:
                setConvState(ConvUIState::ERROR_STATE);
                break;
            default:
                break;
        }
    });

    voice.onError([](const String& err) {
        Serial.printf("[DIAG][ERR] Erreur rencontree : %s\n", err.c_str());
        s_errorMessage = err;
        setConvState(ConvUIState::ERROR_STATE);
        s_dirtyAssistantBox = true;
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
        s_userText = text;
        s_dirtyUserBox = true;
        if (isFinal) {
            Serial.printf("[OpenAIConv][USER WHISPER] %s\n", text.c_str());
        }
    });

    // 3. Transcription assistant (OpenAI)
    voice.onAssistantText([](const String& text) {
        s_assistantText = text;
        s_dirtyAssistantBox = true;
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
        // Note de stabilisation v0.6.3.1 : Ne plus forcer artificiellement READY ici.
        // L'etat reel est pilote exclusivement par le moteur audio et onStateChanged.
    });

    // Demarrage de VoiceKit
    if (!voice.begin(config)) {
        Serial.printf("[OpenAIConv][ERREUR] Echec voice.begin() : %s\n", voice.getLastError().c_str());
        s_errorMessage = voice.getLastError();
        setConvState(ConvUIState::ERROR_STATE);
        return;
    }

    Serial.println("[OpenAIConv] Demarrage initialise. Connexion Wi-Fi et session OpenAI en cours...");
}

// =============================================================================
// Boucle Principale (loop)
// =============================================================================

void loop() {
    uint32_t now = millis();

    // 1. Polling materiel M5Unified
    M5.update();

    // 2. Traitement cyclique VoiceKit avec mesure temporelle
    uint32_t tUpStart = micros();
    voice.update();
    uint32_t upDurUs = micros() - tUpStart;

    s_updateCount++;
    s_sumUpdateDurUs += upDurUs;
    if (upDurUs > s_maxUpdateDurUs) {
        s_maxUpdateDurUs = upDurUs;
    }

    // 3. Commandes serie (P: Parler, R: Relacher, S: Stop, D: Diagnostics)
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'P' || c == 'p') {
            Serial.println("[SERIE] Commande 'P' (PARLER) recue");
            if (voice.isReady() && !voice.isBusy()) {
                s_userText = "";
                s_assistantText = "";
                s_dirtyUserBox = true;
                s_dirtyAssistantBox = true;
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
            s_touchInListening = false;
            voice.stop();
        } else if (c == 'D' || c == 'd') {
            printPeriodicDiagnostics();
        }
    }

    // 4. Gestion tactile inspiree de CoreS3-TouchDiagnostics v0.1.0.1
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

    bool inButton = (curX >= BTN_X && curX <= (BTN_X + BTN_W) &&
                     curY >= BTN_Y && curY <= (BTN_Y + BTN_H));

    // Transition 1 : PRESS (Front montant)
    if (!s_prevPressed && isPressed) {
        if (now - s_lastPressTimeMs >= TOUCH_DEBOUNCE_MS) {
            s_lastPressTimeMs = now;
            s_contactId++;
            Serial.printf("[TOUCH][PRESS] Contact #%u a (%d, %d) [inButton=%s, etat=%s]\n",
                          (unsigned)s_contactId, (int)curX, (int)curY, inButton ? "OUI" : "NON",
                          uiStateToString(s_uiState));

            if (inButton) {
                if (s_uiState == ConvUIState::PROCESSING || s_uiState == ConvUIState::SPEAKING) {
                    // Interruption prioritaire immediate STOP
                    Serial.printf("[TOUCH][STOP] Interruption prioritaire STOP (contact #%u)\n", (unsigned)s_contactId);
                    s_touchInListening = false;
                    voice.stop();
                } else if (s_uiState == ConvUIState::ERROR_STATE) {
                    // Recuperation controlee apres erreur
                    Serial.printf("[TOUCH][RECOVER] Reinitialisation demandee apres erreur (contact #%u)\n", (unsigned)s_contactId);
                    s_errorMessage = "";
                    s_touchInListening = false;
                    voice.stop();
                    if (voice.isReady()) {
                        setConvState(ConvUIState::READY);
                    }
                } else if (voice.isReady() && !voice.isBusy() && !s_touchInListening) {
                    // Demarrage de la prise de parole (Push-to-Talk)
                    s_userText = "";
                    s_assistantText = "";
                    s_dirtyUserBox = true;
                    s_dirtyAssistantBox = true;
                    s_turnCounter++;
                    Serial.printf("[CONV][TURN] Debut tour #%u (contact #%u)\n", s_turnCounter, (unsigned)s_contactId);
                    if (voice.startListening()) {
                        s_touchInListening = true;
                    } else {
                        Serial.println("[TOUCH][WARN] voice.startListening() a echoue -> recuperation immediate");
                        s_touchInListening = false;
                        setConvState(ConvUIState::READY);
                    }
                } else {
                    Serial.printf("[TOUCH][WARN] Demande PARLER ignoree : tour precedent non cloture (moteur=%s, busy=%d)\n",
                                  voiceStateToString(voice.getState()), voice.isBusy() ? 1 : 0);
                }
            }
        }
    }
    // Transition 2 : MAINTIEN / GLISSEMENT
    else if (s_prevPressed && isPressed) {
        if (s_touchInListening) {
            // Marge de tolerance de 15px autour du bouton
            bool stillNearby = (curX >= BTN_X - 15 && curX <= (BTN_X + BTN_W + 15) &&
                                curY >= BTN_Y - 15 && curY <= (BTN_Y + BTN_H + 15));
            if (!stillNearby) {
                Serial.printf("[TOUCH][SLIDE_OUT] Glissement hors zone (%d, %d) -> fin de parole securisee\n", (int)curX, (int)curY);
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
    // Transition 3 : RELACHEMENT (Front descendant)
    else if (s_prevPressed && !isPressed) {
        uint32_t pressDuration = (now >= s_lastPressTimeMs) ? (now - s_lastPressTimeMs) : 0;
        Serial.printf("[TOUCH][RELEASE] Contact #%u relache a (%d, %d) apres %u ms\n",
                      (unsigned)s_contactId, (int)s_lastTouchX, (int)s_lastTouchY, (unsigned)pressDuration);

        if (s_touchInListening) {
            s_touchInListening = false;
            if (voice.isListening()) {
                Serial.printf("[AUDIO][MIC] Relachement PARLER -> transmission a OpenAI (duree parole: %u ms)\n", (unsigned)pressDuration);
                if (!voice.stopListening()) {
                    Serial.println("[TOUCH][WARN] voice.stopListening() a echoue -> cancelListening()");
                    voice.cancelListening();
                    voice.stop();
                }
            }
        }
    }

    s_prevPressed = isPressed;

    // 5. Diagnostics periodiques automatiques
    if (now - s_lastDiagPrintMs >= DIAG_INTERVAL_MS) {
        s_lastDiagPrintMs = now;
        printPeriodicDiagnostics();
    }

    // 6. Rafraichissement graphique cible
    updateUI();

    delay(10);
}
