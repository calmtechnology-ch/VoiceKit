/**
 * @file main.cpp
 * @brief Application VoiceUxLiveDemo — Démonstration du pont vocal VoiceKit v0.6.4.2
 *        sur M5Stack CoreS3 (ESP32-S3).
 * 
 * Fonctionnalités :
 * 1. Capture microphone Push-To-Talk unitaire via l'écran tactile ou la liaison série.
 * 2. Transcription vocale en temps réel (STT unitaire VoiceKit).
 * 3. Filtrage TextGate et routage déterministe d'intentions locales (VoiceUxController).
 * 4. Mémoire locale ReplyMemory (5 réponses en RAM, pré-remplie avec 2 réponses déterministes).
 * 5. Restitution vocale haute fidélité (TTS VoiceKit).
 * 6. Machine à états sécurisée évitant tout conflit microphone/haut-parleur.
 * 7. Commandes tactiles & console de diagnostic série conforme aux spécifications.
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <VoxCoreVoice.h>
#include <VoiceUxLiveBridge.h>
#include "AppConfig.h"

// Instance du moteur vocal VoiceKit et du pont applicatif Voice UX
static VoxCoreVoice s_voice;
static VoiceUxLiveBridge s_bridge;

// États et variables pour l'interface graphique M5GFX
static bool s_needsRedraw = true;
static bool s_touchListening = false;
static String s_serialCmdBuffer = "";
static String s_lastActionLog = "Initialisation...";
static uint32_t s_lastActionTimeMs = 0;

// Coordonnées dynamiques des zones tactiles écran
static int s_btnTalkX = 10;
static int s_btnTalkY = 175;
static int s_btnTalkW = 190;
static int s_btnTalkH = 55;

static int s_btnStopX = 210;
static int s_btnStopY = 175;
static int s_btnStopW = 100;
static int s_btnStopH = 55;

static void updateLayout() {
    int dw = M5.Display.width();
    int dh = M5.Display.height();

    if (dw >= 300) {
        // Mode Paysage (320 × 240)
        s_btnTalkX = 10;
        s_btnTalkY = dh - 65;
        s_btnTalkW = dw - 130;
        s_btnTalkH = 55;

        s_btnStopX = dw - 110;
        s_btnStopY = dh - 65;
        s_btnStopW = 100;
        s_btnStopH = 55;
    } else {
        // Mode Portrait (240 × 320)
        s_btnTalkX = 10;
        s_btnTalkY = dh - 110;
        s_btnTalkW = dw - 20;
        s_btnTalkH = 48;

        s_btnStopX = 10;
        s_btnStopY = dh - 54;
        s_btnStopW = dw - 20;
        s_btnStopH = 44;
    }
}

// =============================================================================
// Interface Graphique LCD M5GFX
// =============================================================================

static void drawUI() {
    updateLayout();
    int dw = M5.Display.width();
    int dh = M5.Display.height();

    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_BLACK);

    // 1. Bandeau supérieur d'en-tête
    M5.Display.fillRect(0, 0, dw, 24, M5.Display.color565(15, 30, 60));
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(15, 30, 60));
    M5.Display.drawString(dw >= 300 ? " VoiceKit v0.6.4.2 - Voice UX Live" : " VoiceKit UX Live", 4, 6);

    // Badge Wi-Fi
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    uint16_t wifiColor = wifiOk ? TFT_GREEN : TFT_ORANGE;
    M5.Display.setTextColor(wifiColor, M5.Display.color565(15, 30, 60));
    M5.Display.drawRightString(wifiOk ? "WiFi OK" : "WiFi ...", dw - 6, 6);

    // 2. Badge d'état principal du pont & du moteur vocal
    VoiceUxBridgeState st = s_bridge.getState();
    const char* stateLabel = voiceUxBridgeStateToString(st);
    uint16_t stateColor = TFT_LIGHTGRAY;
    bool voiceReady = (s_voice.isReady() || s_voice.getState() == VoiceState::READY);

    if (st == VoiceUxBridgeState::READY && !voiceReady) {
        VoiceState vs = s_voice.getState();
        switch (vs) {
            case VoiceState::CONNECTING_WIFI:
                stateLabel = "CONNEXION WIFI...";
                stateColor = TFT_ORANGE;
                break;
            case VoiceState::CONNECTING_SERVICE:
                stateLabel = (s_voice.getConfig().provider == VoiceProviderType::OPENAI)
                                 ? "CONNEXION OPENAI..."
                                 : "CONNEXION GEMINI...";
                stateColor = TFT_YELLOW;
                break;
            case VoiceState::ERROR_STATE:
                stateLabel = "ERREUR SERVICE VOCAL";
                stateColor = TFT_RED;
                break;
            default:
                stateLabel = "INITIALISATION VOCALE...";
                stateColor = TFT_LIGHTGRAY;
                break;
        }
    } else {
        switch (st) {
            case VoiceUxBridgeState::READY:
                stateLabel = "PRET";
                stateColor = TFT_GREEN;
                break;
            case VoiceUxBridgeState::LISTENING:
                stateLabel = "ECOUTE";
                stateColor = TFT_RED;
                break;
            case VoiceUxBridgeState::TRANSCRIBING:
                stateLabel = "TRANSCRIPTION";
                stateColor = TFT_CYAN;
                break;
            case VoiceUxBridgeState::VOICE_UX_PROCESSING:
                stateLabel = "ANALYSE UX";
                stateColor = TFT_MAGENTA;
                break;
            case VoiceUxBridgeState::ACTION_PENDING:
                stateLabel = "ACTION";
                stateColor = TFT_YELLOW;
                break;
            case VoiceUxBridgeState::SPEAKING:
                stateLabel = "REPONSE VOCALE";
                stateColor = TFT_SKYBLUE;
                break;
            case VoiceUxBridgeState::PAUSED:
                stateLabel = "PAUSE";
                stateColor = TFT_ORANGE;
                break;
            case VoiceUxBridgeState::ERROR:
                stateLabel = "ERREUR";
                stateColor = TFT_RED;
                break;
        }
    }

    // Affichage du badge d'état sous l'en-tête
    M5.Display.fillRoundRect(8, 28, dw - 16, 22, 4, stateColor);
    M5.Display.setTextColor(TFT_BLACK, stateColor);
    M5.Display.drawCenterString(stateLabel, dw / 2, 32);

    // 3. Cadre central : Transcription utilisateur & intention
    int boxY = 54;
    int boxH = (dw >= 300) ? 62 : 72;
    M5.Display.drawRoundRect(8, boxY, dw - 16, boxH, 4, M5.Display.color565(50, 60, 80));
    M5.Display.setTextSize(1);

    int maxChars = (dw >= 300) ? 40 : 28;

    if (st == VoiceUxBridgeState::LISTENING) {
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.drawString("Microphone actif : dictez...", 14, boxY + 6);
        const String& interim = s_bridge.getInterimText();
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        String disp = interim.isEmpty() ? "(En attente de voix...)" : interim;
        if ((int)disp.length() > maxChars) disp = disp.substring(0, maxChars - 3) + "...";
        M5.Display.drawString(disp, 14, boxY + 24);
    } else {
        const String& finalTxt = s_bridge.getLastFinalText();
        M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        M5.Display.drawString("Derniere transcription :", 14, boxY + 6);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        String disp = finalTxt.isEmpty() ? "(Aucune prise de parole)" : finalTxt;
        if ((int)disp.length() > maxChars) disp = disp.substring(0, maxChars - 3) + "...";
        M5.Display.drawString(disp, 14, boxY + 22);

        // Décision & intention
        const VoiceUxResult& res = s_bridge.getController().getLastResult();
        M5.Display.setTextColor(TFT_GOLD, TFT_BLACK);
        String sub;
        if (dw >= 300) {
            sub = "Intent: " + String(voiceUxIntentToString(res.intent)) +
                  " | Gate: " + String(textGateDecisionToString(res.gateDecision));
            if (res.processingTimeUs > 0) {
                sub += " (" + String(res.processingTimeUs) + " us)";
            }
        } else {
            sub = "Int: " + String(voiceUxIntentToString(res.intent)) +
                  " | " + String(textGateDecisionToString(res.gateDecision));
        }
        M5.Display.drawString(sub, 14, boxY + 40);
    }

    // 4. Cadre Mémoire conversationnelle (ReplyMemory)
    int memY = (dw >= 300) ? 120 : (boxY + boxH + 6);
    int memH = (dw >= 300) ? 48 : 56;
    M5.Display.drawRoundRect(8, memY, dw - 16, memH, 4, M5.Display.color565(50, 60, 80));
    const ReplyMemory& mem = s_bridge.getReplyMemory();
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.drawString("Memoire (" + String(mem.getCount()) + "/" + String(mem.getCapacity()) + ") :", 14, memY + 6);

    String lastReply = mem.getLastReply();
    if (lastReply.isEmpty()) lastReply = "(Memoire vide)";
    int memChars = (dw >= 300) ? 42 : 30;
    if ((int)lastReply.length() > memChars) lastReply = lastReply.substring(0, memChars - 3) + "...";
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString(lastReply, 14, memY + 24);

    // 5. Bouton tactile PARLER (Push-To-Talk)
    uint16_t talkBg;
    const char* talkLabel;

    if (st == VoiceUxBridgeState::LISTENING) {
        talkBg = M5.Display.color565(200, 30, 30);
        talkLabel = "RELACHER POUR FINIR";
    } else if (voiceReady) {
        talkBg = M5.Display.color565(30, 90, 200);
        talkLabel = "PARLER (Maintenir)";
    } else {
        talkBg = M5.Display.color565(60, 65, 80);
        talkLabel = "EN ATTENTE SERVICE...";
    }

    M5.Display.fillRoundRect(s_btnTalkX, s_btnTalkY, s_btnTalkW, s_btnTalkH, 6, talkBg);
    M5.Display.drawRoundRect(s_btnTalkX, s_btnTalkY, s_btnTalkW, s_btnTalkH, 6, TFT_WHITE);
    M5.Display.setTextColor(TFT_WHITE, talkBg);
    M5.Display.drawCenterString(talkLabel, s_btnTalkX + (s_btnTalkW / 2), s_btnTalkY + (s_btnTalkH / 2) - 4);

    // 6. Bouton tactile STOP prioritaire
    uint16_t stopBg = M5.Display.color565(160, 20, 20);
    M5.Display.fillRoundRect(s_btnStopX, s_btnStopY, s_btnStopW, s_btnStopH, 6, stopBg);
    M5.Display.drawRoundRect(s_btnStopX, s_btnStopY, s_btnStopW, s_btnStopH, 6, TFT_WHITE);
    M5.Display.setTextColor(TFT_WHITE, stopBg);
    M5.Display.drawCenterString("STOP", s_btnStopX + (s_btnStopW / 2), s_btnStopY + (s_btnStopH / 2) - 4);

    M5.Display.endWrite();
    s_needsRedraw = false;
}

// =============================================================================
// Traitement des Commandes Série de Diagnostic
// =============================================================================

static void printHelp() {
    Serial.println("\n--- Commandes de diagnostic VoiceKit v0.6.4.2 ---");
    Serial.println("  P          : Demarrer la capture microphone");
    Serial.println("  R          : Terminer la capture microphone");
    Serial.println("  S          : STOP prioritaire");
    Serial.println("  T <texte>  : Injecter une transcription simulee");
    Serial.println("  M <texte>  : Memoriser manuellement une reponse");
    Serial.println("  H          : Afficher l'historique de reponses");
    Serial.println("  C          : Effacer l'historique");
    Serial.println("  D          : Activer/desactiver le module Voice UX");
    Serial.println("  O <0..3>   : Changer l'orientation ecran (0=Portrait, 1=Paysage, 2=Port.inv, 3=Pays.inv)");
    Serial.println("  ?          : Afficher cette aide");
    Serial.println("-------------------------------------------------");
}

static void handleSerialCommand(const String& line) {
    String cmd = line;
    cmd.trim();
    if (cmd.isEmpty()) return;

    char first = cmd.charAt(0);
    if (first == 'P' || first == 'p') {
        Serial.println("[CMD] P -> Demarrage ecoute STT");
        if (s_bridge.canStartListening() && (s_voice.isReady() || s_voice.getState() == VoiceState::READY)) {
            bool ok = s_voice.startListening();
            if (ok) {
                s_bridge.onListeningStarted();
                s_touchListening = true;
                s_needsRedraw = true;
            } else {
                Serial.println("[CMD][ERREUR] Echec startListening()");
            }
        } else {
            Serial.printf("[CMD][REFUS] Capture interdite dans l'etat courant (pont=%s, vocal=%s, wifi=%s)\n",
                          voiceUxBridgeStateToString(s_bridge.getState()),
                          voiceStateToString(s_voice.getState()),
                          (WiFi.status() == WL_CONNECTED) ? "CONNECTE" : "DECONNECTE");
        }
    } else if (first == 'R' || first == 'r') {
        Serial.println("[CMD] R -> Arret ecoute STT");
        if (s_voice.isListening()) {
            s_voice.stopListening();
            s_bridge.onListeningFinished();
            s_touchListening = false;
            s_needsRedraw = true;
        }
    } else if (first == 'S' || first == 's') {
        Serial.println("[CMD] S -> STOP prioritaire");
        s_bridge.triggerPriorityStop();
        s_voice.stop();
        s_touchListening = false;
        s_needsRedraw = true;
        Serial.println("[STATE] READY (apres STOP prioritaire)");
    } else if ((first == 'T' || first == 't') && cmd.length() > 2) {
        String simText = cmd.substring(2);
        simText.trim();
        Serial.printf("[STT][SIM] Transcription simulee injectee : \"%s\"\n", simText.c_str());
        s_bridge.feedSimulatedText(simText);
        s_needsRedraw = true;
    } else if ((first == 'M' || first == 'm') && cmd.length() > 2) {
        String memText = cmd.substring(2);
        memText.trim();
        bool ok = s_bridge.rememberReply(memText);
        Serial.printf("[MEM] Memorisation manuelle : \"%s\" -> %s\n", memText.c_str(), ok ? "OK" : "REFUS");
        s_needsRedraw = true;
    } else if (first == 'H' || first == 'h') {
        const ReplyMemory& mem = s_bridge.getReplyMemory();
        Serial.printf("\n--- Historique ReplyMemory (%u/%u) ---\n",
                      (unsigned)mem.getCount(), (unsigned)mem.getCapacity());
        for (size_t i = 0; i < mem.getCount(); ++i) {
            Serial.printf("  [%u] %s\n", (unsigned)i, mem.getReplyAt(i).c_str());
        }
        Serial.println("--------------------------------------");
    } else if (first == 'C' || first == 'c') {
        s_bridge.clearMemory();
        Serial.println("[MEM] Historique efface.");
        s_needsRedraw = true;
    } else if (first == 'D' || first == 'd') {
        bool current = s_bridge.isEnabled();
        s_bridge.setEnabled(!current);
        Serial.printf("[UX] Voice UX %s\n", !current ? "ACTIVE" : "DESACTIVE");
        s_needsRedraw = true;
    } else if ((first == 'O' || first == 'o') && cmd.length() > 2) {
        int r = cmd.substring(2).toInt();
        if (r >= 0 && r <= 3) {
            M5.Display.setRotation(r);
            Serial.printf("[DISPLAY] Rotation ecran reglee a %d (%dx%d)\n", r, M5.Display.width(), M5.Display.height());
            s_needsRedraw = true;
        } else {
            Serial.println("[DISPLAY][ERREUR] Rotation invalide (0=Portrait, 1=Paysage, 2=Portrait inv, 3=Paysage inv)");
        }
    } else if (first == '?') {
        printHelp();
    } else {
        Serial.printf("[CMD] Commande inconnue : '%c'. Tapez '?' pour l'aide.\n", first);
    }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  VoiceKit v0.6.4.2 — Voice UX Live Demo                ");
    Serial.println("  Pont STT -> VoiceUxController -> ReplyMemory -> TTS   ");
    Serial.println("========================================================");

    // 1. Initialisation matérielle M5Stack CoreS3
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = true;
    M5.begin(cfg);
    M5.Display.setRotation(0);

    // 2. Initialisation du pont applicatif VoiceUxLiveBridge
    if (!s_bridge.begin()) {
        Serial.println("[FATAL] Echec initialisation VoiceUxLiveBridge !");
    }

    // Remplissage avec les deux réponses déterministes prescrites par le cahier des charges
    s_bridge.seedTestReplies();
    Serial.println("[MEM] 2 reponses locales deterministes pre-chargees dans ReplyMemory");

    // Callbacks d'événements du pont
    s_bridge.onStateChanged([](VoiceUxBridgeState oldSt, VoiceUxBridgeState newSt) {
        Serial.printf("[STATE] %s -> %s\n", voiceUxBridgeStateToString(oldSt), voiceUxBridgeStateToString(newSt));
        s_needsRedraw = true;
    });

    s_bridge.onDiagnostic([](const String& diag) {
        Serial.println(diag);
    });

    // 3. Configuration de VoiceKit STT / TTS
    VoiceKitConfig vConfig;
    if (AppConfig::VOICE_PROVIDER == 1) {
        vConfig.provider = VoiceProviderType::OPENAI;
        vConfig.apiKey = OPENAI_KEY;
    } else {
        vConfig.provider = VoiceProviderType::GEMINI;
        vConfig.apiKey = GEMINI_KEY;
        if (vConfig.apiKey.isEmpty() && strlen(OPENAI_KEY) > 0) {
            vConfig.provider = VoiceProviderType::OPENAI;
            vConfig.apiKey = OPENAI_KEY;
        }
    }
    vConfig.wifiMode = WiFiMode::MANAGED_WIFI;
    vConfig.wifiSSID = WIFI_SSID;
    vConfig.wifiPassword = WIFI_PASSWORD;
    vConfig.volume = AppConfig::DEFAULT_VOLUME;
    vConfig.sttFinalTimeoutMs = AppConfig::STT_FINAL_TIMEOUT_MS;
    vConfig.muteSpeakerDuringSTT = true;

    // 4. Enregistrement des callbacks du moteur vocal VoiceKit
    s_voice.onStateChanged([](VoiceState vs) {
        Serial.printf("[VOICE] Changement d'etat : %s\n", voiceStateToString(vs));
        s_needsRedraw = true;
    });

    s_voice.onReady([]() {
        Serial.println("[VOICE] Moteur vocal pret.");
        s_needsRedraw = true;
    });

    s_voice.onListeningStarted([]() {
        Serial.println("[VOICE] Capture microphone active !");
        s_bridge.onListeningStarted();
        s_needsRedraw = true;
    });

    s_voice.onListeningFinished([]() {
        Serial.println("[VOICE] Fin de capture microphone. Traitement STT...");
        s_bridge.onListeningFinished();
        s_needsRedraw = true;
    });

    // Réception des transcriptions STT (partielles et finales)
    s_voice.onTranscription([](const String& text, bool isFinal) {
        if (isFinal) {
            Serial.printf("[STT][REAL] Transcription finale : %s\n", text.c_str());
            s_bridge.feedTranscription(text, true);
        } else {
            Serial.printf("... [interim] %s\n", text.c_str());
            s_bridge.feedTranscription(text, false);
        }
        s_needsRedraw = true;
    });

    s_voice.onSpeechStarted([]() {
        Serial.println("[VOICE] Restitution TTS demarree.");
        s_needsRedraw = true;
    });

    s_voice.onSpeechFinished([]() {
        Serial.println("[VOICE] Restitution TTS terminee.");
        s_bridge.notifySpeechFinished(true);
        s_needsRedraw = true;
    });

    s_voice.onError([](const String& err) {
        Serial.printf("[VOICE][ERREUR] %s\n", err.c_str());
        s_bridge.notifyError(err);
        s_needsRedraw = true;
    });

    // 5. Démarrage de VoiceKit
    if (!s_voice.begin(vConfig)) {
        Serial.printf("[FATAL] Echec demarrage VoiceKit : %s\n", s_voice.getLastError().c_str());
        s_bridge.notifyError(s_voice.getLastError());
    }

    drawUI();
    printHelp();
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
    // 0. Détection de changement d'état Wi-Fi pour mise à jour immédiate du badge écran
    static bool s_lastWifiOk = false;
    bool curWifi = (WiFi.status() == WL_CONNECTED);
    if (curWifi != s_lastWifiOk) {
        s_lastWifiOk = curWifi;
        s_needsRedraw = true;
    }

    // 1. Maintenance du moteur vocal VoiceKit
    s_voice.update();

    // 2. Traitement sécurisé des actions en attente de VoiceUxLiveBridge
    if (s_bridge.hasPendingAction()) {
        VoiceUxBridgeAction act = s_bridge.consumePendingAction();
        const VoiceUxResult& res = s_bridge.getController().getLastResult();

        Serial.printf("[UX] TextGate : %s\n", textGateDecisionToString(res.gateDecision));
        Serial.printf("[UX] Intent : %s\n", voiceUxIntentToString(act.intent));
        if (res.processingTimeUs > 0) {
            Serial.printf("[UX] Temps traitement : %u us\n", (unsigned)res.processingTimeUs);
        }

        switch (act.type) {
            case VoiceUxBridgeActionType::TTS_SPEAK: {
                if (act.isRecall) {
                    Serial.println("[MEM] Reponse rappelee disponible pour restitution");
                }
                Serial.printf("[ACTION] TTS en attente : \"%s\"\n", act.text.c_str());
                if (s_bridge.canSpeak() && (s_voice.isReady() || s_voice.getState() == VoiceState::READY)) {
                    s_bridge.notifySpeechStarted(act.text);
                    Serial.println("[VOICE] TTS demarre");
                    s_voice.say(act.text);
                } else {
                    Serial.println("[ACTION][REPORTE] Moteur vocal non disponible pour TTS");
                }
                break;
            }
            case VoiceUxBridgeActionType::STOP: {
                Serial.println("[ACTION] Commande STOP executee");
                s_bridge.triggerPriorityStop();
                s_voice.stop();
                break;
            }
            case VoiceUxBridgeActionType::PAUSE: {
                Serial.println("[ACTION] Mise en PAUSE de la session");
                s_bridge.setPaused(true);
                break;
            }
            case VoiceUxBridgeActionType::RESUME: {
                Serial.println("[ACTION] Reprise de session");
                s_bridge.setPaused(false);
                break;
            }
            case VoiceUxBridgeActionType::FORWARD_AI: {
                Serial.printf("[ACTION] Phrase ordinaire a transmettre : \"%s\"\n", act.text.c_str());
                // Dans cette version 0.6.4.2 sans conversation multi-tours IA,
                // la phrase est simplement notifiée.
                break;
            }
            default:
                break;
        }
        s_needsRedraw = true;
    }

    // 3. Gestion tactile M5Unified (CoreS3)
    M5.update();
    auto touch = M5.Touch.getDetail();

    bool inTalkArea = (touch.x >= s_btnTalkX && touch.x <= (s_btnTalkX + s_btnTalkW) &&
                       touch.y >= s_btnTalkY && touch.y <= (s_btnTalkY + s_btnTalkH));

    bool inStopArea = (touch.x >= s_btnStopX && touch.x <= (s_btnStopX + s_btnStopW) &&
                       touch.y >= s_btnStopY && touch.y <= (s_btnStopY + s_btnStopH));

    // Appui sur le bouton PARLER (Push-To-Talk)
    if (touch.wasPressed() && inTalkArea) {
        if (s_bridge.canStartListening() && (s_voice.isReady() || s_voice.getState() == VoiceState::READY)) {
            Serial.println("[TOUCH] Appui PARLER -> Demarrage ecoute");
            s_touchListening = true;
            bool ok = s_voice.startListening();
            if (ok) {
                s_bridge.onListeningStarted();
                s_needsRedraw = true;
            }
        } else {
            Serial.printf("[TOUCH][ATTENTE] Capture interdite : pont=%s, vocal=%s, wifi=%s\n",
                          voiceUxBridgeStateToString(s_bridge.getState()),
                          voiceStateToString(s_voice.getState()),
                          (WiFi.status() == WL_CONNECTED) ? "CONNECTE" : "DECONNECTE");
        }
    }

    // Relâchement du bouton PARLER
    if (touch.wasReleased() && s_touchListening) {
        Serial.println("[TOUCH] Relachement PARLER -> Fin de prise de parole");
        s_touchListening = false;
        if (s_voice.isListening()) {
            s_voice.stopListening();
            s_bridge.onListeningFinished();
            s_needsRedraw = true;
        }
    }

    // Appui sur le bouton STOP (prioritaire)
    if (touch.wasPressed() && inStopArea) {
        Serial.println("[TOUCH] Appui STOP prioritaire !");
        s_touchListening = false;
        s_bridge.triggerPriorityStop();
        s_voice.stop();
        s_needsRedraw = true;
    }

    // 4. Lecture des commandes de diagnostic série
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            handleSerialCommand(s_serialCmdBuffer);
            s_serialCmdBuffer = "";
        } else {
            s_serialCmdBuffer += c;
        }
    }

    // 5. Rafraîchissement graphique de l'écran si nécessaire
    if (s_needsRedraw) {
        drawUI();
    }

    delay(10);
}

