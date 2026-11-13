/**
 * @file main.cpp
 * @brief Exemple ContinuousSTT — Reconnaissance vocale continue mains libres
 *        pour M5Stack CoreS3 avec VoiceKit v0.4.1.1 et Google Gemini Live.
 * 
 * Fonctionnalités :
 * - Reconnaissance vocale continue sans maintien de bouton (mains libres / dictée vocale moderne).
 * - Détecteur d'activité vocale (VAD) léger et non bloquant sur blocs PCM 16 kHz.
 * - Détection automatique de début de parole (200 ms min) et de fin d'intervention (1 200 ms silence).
 * - Hystérésis adaptative et suivi du plancher de bruit ambiant.
 * - Durée maximale de segment bornée à 30 secondes.
 * - Affichage dynamique M5Unified (320x240) :
 *     * 7 états : ATTENTE, ÉCOUTE, PAROLE, SILENCE, TRANSCRIPTION, TERMINÉ, ERREUR.
 *     * Transcriptions partielles (intérim en direct) et définitives différenciées.
 *     * Conservation intégrale de l'UTF-8 français (accents é, è, à, ç, ê, ô, etc.).
 *     * VU-mètre RMS micro temps réel et jauge de progression du silence avant clôture.
 *     * Compteur de segments transcrits.
 *     * Bouton tactile PAUSE / REPRENDRE.
 * - Session Gemini Live unique avec reprise automatique après finalisation.
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>
#include <VoiceActivityDetector.h>
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

// États fonctionnels de l'application ContinuousSTT
enum class AppState {
    ATTENTE,        ///< Initialisation, négociation réseau ou pause manuelle
    ECOUTE,         ///< Microphone ouvert, streaming actif, attente de voix dans le bruit ambiant
    PAROLE,         ///< Parole active détectée par le VAD (> 200 ms validés)
    SILENCE,        ///< Pause post-parole, décompte du silence (0 -> 1 200 ms) avant clôture
    TRANSCRIPTION,  ///< stopListening() envoyé, attente de la transcription finale authentique Gemini
    TERMINE,        ///< Transcription finale authentique reçue, validée et conservée à l'écran
    ERREUR          ///< Erreur réseau, session ou micro
};

// Instances
static VoxCoreVoice voice;
static VoiceActivityDetector vad;

// Variables d'état
static AppState s_state = AppState::ATTENTE;
static AppState s_lastDrawnState = AppState::ATTENTE;
static bool s_paused = false;
static uint32_t s_segmentCount = 0;
static String s_interimText = "";
static String s_finalText = "";
static bool s_lastFinalConfirmed = true;
static String s_errorMessage = "";

// Drapeaux de synchronisation thread-safe
static volatile bool s_speechEndRequested = false;
static volatile uint32_t s_lastLevelRms = 0;

static uint32_t s_lastCompletedTimeMs = 0;
static uint32_t s_lastLevelDrawMs = 0;
static bool s_needsFullRedraw = true;
static bool s_needsTextRedraw = true;

// Coordonnées du bouton tactile PAUSE / REPRENDRE
static constexpr int BTN_X = 10;
static constexpr int BTN_Y = 194;
static constexpr int BTN_W = 300;
static constexpr int BTN_H = 40;

// Zone de texte transcription
static constexpr int BOX_X = 10;
static constexpr int BOX_Y = 74;
static constexpr int BOX_W = 300;
static constexpr int BOX_H = 114;

// VU-mètre et jauge silence
static constexpr int VU_X = 10;
static constexpr int VU_Y = 50;
static constexpr int VU_W = 145;
static constexpr int VU_H = 16;

static constexpr int SIL_X = 165;
static constexpr int SIL_Y = 50;
static constexpr int SIL_W = 145;
static constexpr int SIL_H = 16;

static void setAppState(AppState newState) {
    if (s_state != newState) {
        s_state = newState;
        s_needsFullRedraw = true;
    }
}

// Couleurs et libellés des 7 états
static void getStateVisuals(AppState st, const char*& label, uint16_t& bgCol, uint16_t& txtCol) {
    switch (st) {
        case AppState::ATTENTE:
            label = s_paused ? "PAUSE" : "ATTENTE";
            bgCol = M5.Display.color565(90, 95, 105);
            txtCol = TFT_WHITE;
            break;
        case AppState::ECOUTE:
            label = "ECOUTE";
            bgCol = M5.Display.color565(20, 120, 220);
            txtCol = TFT_WHITE;
            break;
        case AppState::PAROLE:
            label = "PAROLE";
            bgCol = M5.Display.color565(35, 180, 50);
            txtCol = TFT_WHITE;
            break;
        case AppState::SILENCE:
            label = "SILENCE";
            bgCol = M5.Display.color565(230, 160, 20);
            txtCol = TFT_BLACK;
            break;
        case AppState::TRANSCRIPTION:
            label = "TRANSCRIPTION";
            bgCol = M5.Display.color565(160, 40, 200);
            txtCol = TFT_WHITE;
            break;
        case AppState::TERMINE:
            label = "TERMINE";
            bgCol = M5.Display.color565(20, 170, 90);
            txtCol = TFT_WHITE;
            break;
        case AppState::ERREUR:
            label = "ERREUR";
            bgCol = M5.Display.color565(220, 30, 30);
            txtCol = TFT_WHITE;
            break;
    }
}

static void drawHeaderAndBorders() {
    M5.Display.fillScreen(TFT_BLACK);

    // Titre de l'application
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("VoiceKit ContinuousSTT", 10, 8);

    // Bandeau d'état (badge pilule)
    const char* stLabel = "";
    uint16_t stBg = TFT_DARKGRAY;
    uint16_t stTxt = TFT_WHITE;
    getStateVisuals(s_state, stLabel, stBg, stTxt);

    int badgeW = 125;
    int badgeX = 320 - badgeW - 10;
    int badgeY = 5;
    int badgeH = 20;
    M5.Display.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 5, stBg);
    M5.Display.setTextColor(stTxt, stBg);
    M5.Display.drawCenterString(stLabel, badgeX + (badgeW / 2), badgeY + 6);

    // Bandeau d'informations (Segments et bruit)
    char infoBuf[64];
    snprintf(infoBuf, sizeof(infoBuf), "Segments: %u  |  Bruit: %u RMS",
             (unsigned)s_segmentCount, (unsigned)vad.getNoiseFloor());
    M5.Display.setTextColor(M5.Display.color565(180, 190, 205), TFT_BLACK);
    M5.Display.drawString(infoBuf, 10, 32);

    // Cadre VU-Mètre
    M5.Display.drawRoundRect(VU_X, VU_Y, VU_W, VU_H, 3, M5.Display.color565(60, 70, 85));

    // Cadre Jauge Silence
    M5.Display.drawRoundRect(SIL_X, SIL_Y, SIL_W, SIL_H, 3, M5.Display.color565(60, 70, 85));

    // Cadre central de texte
    uint16_t boxBg = M5.Display.color565(15, 20, 28);
    M5.Display.fillRoundRect(BOX_X, BOX_Y, BOX_W, BOX_H, 6, boxBg);
    M5.Display.drawRoundRect(BOX_X, BOX_Y, BOX_W, BOX_H, 6, M5.Display.color565(50, 60, 80));

    // Bouton tactile PAUSE / REPRENDRE
    uint16_t btnCol = s_paused ? M5.Display.color565(20, 120, 220) : M5.Display.color565(160, 35, 35);
    const char* btnTxt = s_paused ? "REPRENDRE (Toucher pour dicter)" : "PAUSE (Toucher pour suspendre)";

    M5.Display.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, btnCol);
    M5.Display.drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, TFT_WHITE);
    M5.Display.setTextColor(TFT_WHITE, btnCol);
    M5.Display.setTextSize(1);
    M5.Display.drawCenterString(btnTxt, BTN_X + (BTN_W / 2), BTN_Y + 14);

    s_lastDrawnState = s_state;
    s_needsFullRedraw = false;
    s_needsTextRedraw = true;
}

static void drawGauges() {
    uint32_t rms = s_lastLevelRms;
    uint32_t speechThresh = vad.getSpeechThreshold();

    // 1. Barregraphe VU-mètre micro (0 à 4000 RMS)
    int innerVuW = VU_W - 4;
    int innerVuH = VU_H - 4;
    int filledW = (int)((rms * innerVuW) / 4000);
    if (filledW > innerVuW) filledW = innerVuW;

    uint16_t vuCol = (rms >= speechThresh) ? TFT_GREEN : M5.Display.color565(30, 100, 180);

    M5.Display.fillRect(VU_X + 2, VU_Y + 2, filledW, innerVuH, vuCol);
    M5.Display.fillRect(VU_X + 2 + filledW, VU_Y + 2, innerVuW - filledW, innerVuH, M5.Display.color565(25, 30, 40));

    // Marqueur de seuil parole
    int markerX = VU_X + 2 + (int)((speechThresh * innerVuW) / 4000);
    if (markerX < VU_X + VU_W - 2) {
        M5.Display.drawFastVLine(markerX, VU_Y + 1, VU_H - 2, TFT_YELLOW);
    }

    // Libellé VU
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(25, 30, 40));
    M5.Display.drawString("MIC", VU_X + 6, VU_Y + 5);

    // 2. Barregraphe progression silence (0 à 1 200 ms)
    int innerSilW = SIL_W - 4;
    int innerSilH = SIL_H - 4;
    float silRatio = vad.getSilenceProgressRatio();
    int filledSilW = (int)(silRatio * innerSilW);
    if (filledSilW > innerSilW) filledSilW = innerSilW;

    uint16_t silCol = (silRatio >= 0.8f) ? TFT_RED : TFT_YELLOW;

    M5.Display.fillRect(SIL_X + 2, SIL_Y + 2, filledSilW, innerSilH, silCol);
    M5.Display.fillRect(SIL_X + 2 + filledSilW, SIL_Y + 2, innerSilW - filledSilW, innerSilH, M5.Display.color565(25, 30, 40));

    char silTxt[32];
    if (s_state == AppState::SILENCE) {
        snprintf(silTxt, sizeof(silTxt), "SIL: %u ms", (unsigned)vad.getCurrentSilenceMs());
    } else {
        snprintf(silTxt, sizeof(silTxt), "SIL: 0 ms");
    }
    M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(25, 30, 40));
    M5.Display.drawString(silTxt, SIL_X + 6, SIL_Y + 5);
}

static void drawTextContent() {
    uint16_t boxBg = M5.Display.color565(15, 20, 28);

    // Effacer l'intérieur de la zone de texte
    M5.Display.fillRect(BOX_X + 2, BOX_Y + 2, BOX_W - 4, BOX_H - 4, boxBg);

    // Police avec prise en charge des accents UTF-8 français
    M5.Display.setFont(&fonts::efontJA_12);
    M5.Display.setTextSize(1);
    M5.Display.setTextWrap(true);

    int textY = BOX_Y + 6;

    // 1. Section Transcription Finale Confirmée
    M5.Display.setTextColor(M5.Display.color565(80, 220, 120), boxBg);
    M5.Display.drawString("[Texte d\xc3\xa9""finitif valid\xc3\xa9""] :", BOX_X + 8, textY);
    textY += 16;

    if (s_finalText.length() > 0) {
        if (s_lastFinalConfirmed) {
            M5.Display.setTextColor(TFT_WHITE, boxBg);
        } else {
            M5.Display.setTextColor(TFT_ORANGE, boxBg);
        }
        M5.Display.drawString(s_finalText.c_str(), BOX_X + 8, textY);
    } else {
        M5.Display.setTextColor(TFT_DARKGRAY, boxBg);
        M5.Display.drawString("(Aucune transcription pour le moment)", BOX_X + 8, textY);
    }

    textY += 36;

    // Ligne séparatrice subtile
    M5.Display.drawFastHLine(BOX_X + 6, textY, BOX_W - 12, M5.Display.color565(40, 50, 70));
    textY += 6;

    // 2. Section Transcription Partielle en cours (intérim Gemini)
    if (s_state == AppState::ERREUR) {
        M5.Display.setTextColor(TFT_RED, boxBg);
        M5.Display.drawString("[Erreur] :", BOX_X + 8, textY);
        textY += 16;
        M5.Display.setTextColor(TFT_WHITE, boxBg);
        M5.Display.drawString(s_errorMessage.c_str(), BOX_X + 8, textY);
    } else if (s_state == AppState::TRANSCRIPTION) {
        M5.Display.setTextColor(TFT_MAGENTA, boxBg);
        M5.Display.drawString("[Finalisation Gemini Live...] :", BOX_X + 8, textY);
        textY += 16;
        if (s_interimText.length() > 0) {
            M5.Display.setTextColor(TFT_LIGHTGRAY, boxBg);
            M5.Display.drawString(s_interimText.c_str(), BOX_X + 8, textY);
        }
    } else if (s_interimText.length() > 0) {
        M5.Display.setTextColor(TFT_YELLOW, boxBg);
        M5.Display.drawString("[En direct] :", BOX_X + 8, textY);
        textY += 16;
        M5.Display.setTextColor(TFT_WHITE, boxBg);
        M5.Display.drawString(s_interimText.c_str(), BOX_X + 8, textY);
    } else {
        M5.Display.setTextColor(M5.Display.color565(120, 150, 180), boxBg);
        if (s_state == AppState::PAROLE) {
            M5.Display.drawString("[Voix d\xc3\xa9t\xc3\xa9""ct\xc3\xa9""e, transmission en cours...]", BOX_X + 8, textY);
        } else if (s_state == AppState::SILENCE) {
            M5.Display.drawString("[Silence d\xc3\xa9t\xc3\xa9""ct\xc3\xa9"", validation imminente...]", BOX_X + 8, textY);
        } else if (s_state == AppState::ECOUTE) {
            M5.Display.drawString("(Parlez librement, d\xc3\xa9t\xc3\xa9""ction automatique)", BOX_X + 8, textY);
        } else if (s_paused) {
            M5.Display.drawString("(Dict\xc3\xa9""e en pause, touchez REPRENDRE)", BOX_X + 8, textY);
        } else {
            M5.Display.drawString("(Connexion \xc3\xa0"" Gemini Live...)", BOX_X + 8, textY);
        }
    }

    s_needsTextRedraw = false;
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  VoxCore VoiceKit v0.4.1.1 — Exemple ContinuousSTT     ");
    Serial.println("  Reconnaissance vocale continue mains libres CoreS3   ");
    Serial.println("  Detection VAD 16 kHz & session Gemini Live continue   ");
    Serial.println("========================================================");

    // 1. Initialisation matérielle CoreS3
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = true;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    drawHeaderAndBorders();

    // 2. Configuration du VAD léger
    VadConfig vadCfg;
    vadCfg.minSpeechDurationMs = 200;      // 200 ms de voix consécutive requise
    vadCfg.silenceDurationMs = 1200;       // 1 200 ms de silence pour terminer le segment
    vadCfg.maxSegmentDurationMs = 30000;   // 30 secondes max par segment
    vadCfg.initialNoiseFloor = 200;        // Plancher initial indicatif
    vad.setConfig(vadCfg);

    // 3. Configuration VoiceKit STT
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

    // 4. Branchement du VAD sur chaque bloc audio PCM 16 kHz (32 ms)
    voice.onAudioChunk([](const int16_t* samples, size_t count, uint32_t rms) {
        (void)samples;
        (void)count;
        s_lastLevelRms = rms;

        if (voice.isListening()) {
            bool ended = vad.processRms(rms, 32);
            if (ended) {
                s_speechEndRequested = true;
            }
        }
    });

    // 5. Enregistrement des callbacks VoiceKit
    voice.onReady([]() {
        Serial.println("[ContinuousSTT] Callback onReady : Moteur et Gemini Live prets !");
        if (!s_paused && !voice.isListening() && !voice.isTranscribing()) {
            vad.reset();
            bool ok = voice.startListening();
            if (ok) {
                setAppState(AppState::ECOUTE);
            }
        } else if (s_paused) {
            setAppState(AppState::ATTENTE);
        }
    });

    voice.onListeningStarted([]() {
        Serial.println("[ContinuousSTT] Callback onListeningStarted : Microphone ouvert.");
        vad.reset();
        s_speechEndRequested = false;
        setAppState(AppState::ECOUTE);
    });

    voice.onListeningFinished([]() {
        Serial.println("[ContinuousSTT] Callback onListeningFinished : Fin acquisition audio.");
        setAppState(AppState::TRANSCRIPTION);
    });

    // Réception des transcriptions partielles et finales
    voice.onTranscription([](const String& text, bool isFinal) {
        if (isFinal) {
            Serial.printf("\n>>> [TRANSCRIPTION FINALE VALIDEE] %s\n\n", text.c_str());
            s_finalText = text;
            s_interimText = "";
            s_lastFinalConfirmed = true;
            s_segmentCount++;
            s_lastCompletedTimeMs = millis();
            setAppState(AppState::TERMINE);
        } else {
            Serial.printf("... [partiel] %s\n", text.c_str());
            s_interimText = text;
            s_needsTextRedraw = true;
        }
    });

    voice.onError([](const String& err) {
        Serial.printf("[ContinuousSTT][ERREUR] %s\n", err.c_str());
        s_errorMessage = err;
        setAppState(AppState::ERREUR);
    });

    // 6. Démarrage de VoiceKit
    if (!voice.begin(vConfig)) {
        s_errorMessage = voice.getLastError();
        setAppState(AppState::ERREUR);
        Serial.println("[ContinuousSTT][FATAL] Echec initialisation VoiceKit !");
    }
}

void loop() {
    uint32_t now = millis();

    // 1. Mise à jour de la bibliothèque VoiceKit
    voice.update();

    // 2. Gestion tactile M5Unified
    M5.update();
    auto touch = M5.Touch.getDetail();

    if (touch.wasPressed()) {
        bool inButtonArea = (touch.x >= BTN_X && touch.x <= (BTN_X + BTN_W) &&
                             touch.y >= BTN_Y && touch.y <= (BTN_Y + BTN_H));
        if (inButtonArea) {
            s_paused = !s_paused;
            Serial.printf("[ContinuousSTT] Bouton tactile : %s\n", s_paused ? "PAUSE" : "REPRENDRE");

            if (s_paused) {
                if (voice.isListening()) {
                    voice.cancelListening();
                }
                setAppState(AppState::ATTENTE);
            } else {
                if (voice.isReady() && !voice.isListening() && !voice.isTranscribing()) {
                    vad.reset();
                    voice.startListening();
                    setAppState(AppState::ECOUTE);
                }
            }
            s_needsFullRedraw = true;
        }
    }

    // 3. Prise en compte de la demande de fin de parole du VAD
    if (s_speechEndRequested) {
        s_speechEndRequested = false;
        if (voice.isListening()) {
            Serial.printf("[ContinuousSTT] VAD fin de parole detectee (%s, duree segment: %u ms) -> stopListening()\n",
                          vad.getEndReasonStr(), (unsigned)vad.getSegmentDurationMs());
            voice.stopListening();
            setAppState(AppState::TRANSCRIPTION);
        }
    }

    // 4. Synchronisation de la machine à états de l'interface en mode écoute
    if (voice.isListening()) {
        if (vad.getState() == VadState::SPEECH_ACTIVE) {
            setAppState(AppState::PAROLE);
        } else if (vad.getState() == VadState::SILENCE_STARTING) {
            setAppState(AppState::SILENCE);
        } else {
            setAppState(AppState::ECOUTE);
        }
    }

    // 5. Détection du timeout de transcription finale (STT)
    if (s_state == AppState::TRANSCRIPTION) {
        if (voice.isReady() && !voice.isTranscribing()) {
            // VoiceKit est revenu à READY sans confirmation finale : timeout dépassé !
            Serial.println("[ContinuousSTT] Timeout attente finale : transcription non confirmee");
            s_finalText = "[Transcription non confirm\xc3\xa9""e (d\xc3\xa9""lai d\xc3\xa9""pass\xc3\xa9"")]";
            s_lastFinalConfirmed = false;
            s_interimText = "";
            s_lastCompletedTimeMs = now;
            setAppState(AppState::TERMINE);
        }
    }

    // 6. Reprise automatique mains libres après finalisation du segment
    if (s_state == AppState::TERMINE) {
        if (now - s_lastCompletedTimeMs >= 400) {
            if (!s_paused && voice.isReady() && !voice.isListening() && !voice.isTranscribing()) {
                vad.reset();
                s_interimText = "";
                Serial.println("[ContinuousSTT] Reprise automatique de l'ecoute (mains libres)...");
                if (voice.startListening()) {
                    setAppState(AppState::ECOUTE);
                }
            }
        }
    }

    // 7. Rafraîchissement graphique optimisé
    if (s_needsFullRedraw) {
        drawHeaderAndBorders();
        drawGauges();
        drawTextContent();
    } else {
        if (s_needsTextRedraw) {
            drawTextContent();
        }

        // Rafraîchissement périodique fluide du VU-mètre et du silence (30 ms)
        if (now - s_lastLevelDrawMs >= 30) {
            s_lastLevelDrawMs = now;
            drawGauges();
        }
    }

    delay(5);
}
