#include <M5Unified.h>
#include <VoiceKit.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// =============================================================================
// VoiceKit v0.6.3 — Exemple WakeWordConversation (M5Stack CoreS3, M5GFX pur)
//
// Cycle complet mains libres :
//   1. WakeNet écoute localement « Bonjour ESP » (WAKE_ARMED)
//   2. Détection acoustique confirmée (WAKE_DETECTED)
//   3. Suspension de WakeNet et libération contrôlée du microphone (PREPARING_SESSION)
//   4. Connexion au fournisseur vocal configuré : Gemini Live ou OpenAI (CONNECTING)
//   5. Activation automatique de la conversation (CONVERSATION_LISTENING : PRÊT — PARLEZ)
//   6. Échanges vocaux bidirectionnels multi-tours (ÉCOUTE <-> RÉPONSE VOCALE)
//   7. Détection de fin de session (silence 30 s, commande vocale finale, ou STOP manuel)
//   8. Arrêt complet et libération des ressources réseau/audio (ENDING_SESSION)
//   9. Réactivation de WakeNet (RETURNING_TO_WAKE)
//  10. Retour à l'état WAKE_ARMED, répétable sans redémarrage.
// =============================================================================

// Choix du fournisseur par défaut (GEMINI par défaut, ou OPENAI en alternatif)
#ifndef ACTIVE_VOICE_PROVIDER
#define ACTIVE_VOICE_PROVIDER VoiceProviderType::GEMINI
#endif

static VoiceKit voice;
static M5Canvas canvas(&M5.Display);

static String gLastUserText = "";
static String gLastAssistantText = "";
static String gBannerSubText = "Dites « Bonjour ESP » pour demarrer une conversation";
static uint32_t gLastUiRedrawMs = 0;
static bool gUiDirty = true;

// Couleurs M5GFX
static constexpr uint16_t COL_BG          = 0x0842; // Bleu nuit profond
static constexpr uint16_t COL_CARD        = 0x10A4;
static constexpr uint16_t COL_ARMED       = 0x05E8; // Vert émeraude (écoute WakeNet)
static constexpr uint16_t COL_DETECTED    = 0xFD20; // Orange vif (mot de réveil détecté)
static constexpr uint16_t COL_CONNECTING  = 0x04FF; // Cyan (connexion WebSocket TLS)
static constexpr uint16_t COL_READY_SPEAK = 0x2689; // Vert clair (prêt — parlez)
static constexpr uint16_t COL_LISTENING   = 0xFFE0; // Jaune (écoute parole utilisateur)
static constexpr uint16_t COL_SPEAKING    = 0x3BFF; // Bleu ciel (réponse vocale IA)
static constexpr uint16_t COL_ENDING      = 0xB596; // Gris bleuté (fin / retour écoute)
static constexpr uint16_t COL_ERROR       = 0xF800; // Rouge (erreur récupérable)
static constexpr uint16_t COL_SIMULATED   = 0xFBE0; // Ambre (avertissement mode TEST simulé)

static uint16_t getBannerColor(WakeWordConversationState state, bool userSpokeInTurn) {
    switch (state) {
        case WakeWordConversationState::WAKE_ARMED:
            return voice.isWakeWordSimulated() ? COL_SIMULATED : COL_ARMED;
        case WakeWordConversationState::WAKE_DETECTED:
        case WakeWordConversationState::PREPARING_SESSION:
            return COL_DETECTED;
        case WakeWordConversationState::CONNECTING:
            return COL_CONNECTING;
        case WakeWordConversationState::CONVERSATION_LISTENING:
            return userSpokeInTurn ? COL_LISTENING : COL_READY_SPEAK;
        case WakeWordConversationState::CONVERSATION_SPEAKING:
            return COL_SPEAKING;
        case WakeWordConversationState::ENDING_SESSION:
        case WakeWordConversationState::RETURNING_TO_WAKE:
            return COL_ENDING;
        case WakeWordConversationState::RECOVERABLE_ERROR:
            return COL_ERROR;
        default:
            return COL_ENDING;
    }
}

static void drawInterface() {
    canvas.fillScreen(COL_BG);

    const WakeWordConversationState state = voice.getWakeWordConversationState();
    const WakeWordConversationMetrics& m = voice.getWakeWordConversationMetrics();
    const WakeWordConversationController& ctrl = voice.getWakeWordConversationController();
    const bool simulatedEngine = voice.isWakeWordSimulated();
    const char* statusLabel = voice.getWakeWordConversationStatusText();
    const uint16_t bannerCol = getBannerColor(state, ctrl.hasUserSpokenInTurn());

    // 1. En-tête : Titre + badge d'authenticité acoustique (ne jamais confondre TEST et WAKENET)
    canvas.fillRect(0, 0, 320, 24, 0x0000);
    canvas.setTextColor(TFT_WHITE, 0x0000);
    canvas.setTextDatum(middle_left);
    canvas.setFont(&fonts::Font2);
    canvas.drawString("VoiceKit v0.6.3 - WakeConv", 6, 12);

    canvas.setTextDatum(middle_right);
    if (simulatedEngine) {
        canvas.setTextColor(COL_SIMULATED, 0x0000);
        canvas.drawString("[MODE TEST SIMULE]", 314, 12);
    } else {
        canvas.setTextColor(0x07E0, 0x0000);
        canvas.drawString("[WAKENET ACOUSTIQUE]", 314, 12);
    }

    // 2. Bandeau principal d'état (Section 8)
    canvas.fillRoundRect(6, 28, 308, 42, 6, bannerCol);
    uint16_t bannerTextCol = (bannerCol == COL_LISTENING || bannerCol == COL_DETECTED || bannerCol == COL_SIMULATED)
        ? TFT_BLACK : TFT_WHITE;
    canvas.setTextColor(bannerTextCol, bannerCol);
    canvas.setTextDatum(middle_center);
    canvas.drawString(statusLabel, 160, 42);
    canvas.drawString(gBannerSubText.c_str(), 160, 59);

    // 3. Compteurs de sessions, détections (réelles vs simulées), reconnexions et erreurs
    canvas.fillRoundRect(6, 74, 308, 44, 5, COL_CARD);
    canvas.setTextColor(TFT_CYAN, COL_CARD);
    canvas.setTextDatum(top_left);

    char line1[64];
    snprintf(line1, sizeof(line1), "Sessions: %u | Detections: %u (Acoust:%u / Sim:%u)",
             (unsigned)m.sessionCount,
             (unsigned)m.detectionCount,
             (unsigned)m.realDetectionCount,
             (unsigned)m.simulatedDetectionCount);
    canvas.drawString(line1, 12, 78);

    char line2[64];
    snprintf(line2, sizeof(line2), "Tours: %u | Reconnexions: %u | Erreurs: %u",
             (unsigned)m.turnCount,
             (unsigned)m.reconnectCount,
             (unsigned)m.errorCount);
    canvas.setTextColor(TFT_LIGHTGREY, COL_CARD);
    canvas.drawString(line2, 12, 97);

    // 4. Zone de dialogue (Utilisateur / Assistant)
    canvas.fillRoundRect(6, 122, 308, 78, 5, COL_CARD);
    canvas.setTextColor(TFT_YELLOW, COL_CARD);
    String userDisplay = gLastUserText.isEmpty() ? String("Vous : (en attente de parole)") : ("Vous : " + gLastUserText);
    if (userDisplay.length() > 44) userDisplay = userDisplay.substring(0, 44) + "...";
    canvas.drawString(userDisplay.c_str(), 12, 127);

    canvas.setTextColor(TFT_WHITE, COL_CARD);
    String aiDisplay = gLastAssistantText.isEmpty() ? String("IA : ---") : ("IA : " + gLastAssistantText);
    if (aiDisplay.length() > 44) {
        canvas.drawString(aiDisplay.substring(0, 44).c_str(), 12, 149);
        String line2Ai = aiDisplay.substring(44);
        if (line2Ai.length() > 44) line2Ai = line2Ai.substring(0, 44) + "...";
        canvas.drawString(line2Ai.c_str(), 12, 167);
    } else {
        canvas.drawString(aiDisplay.c_str(), 12, 149);
    }

    // Barre de niveau micro RMS en temps réel
    uint32_t rms = voice.getEngine().getAudioLevel();
    int barW = (int)((rms > 3000 ? 3000 : rms) * 296 / 3000);
    canvas.fillRect(12, 188, 296, 6, 0x0000);
    if (barW > 0) {
        canvas.fillRect(12, 188, barW, 6, ctrl.hasUserSpokenInTurn() ? TFT_YELLOW : TFT_GREEN);
    }

    // 5. Boutons tactiles bas d'écran (STOP manuel / Diagnostics)
    canvas.fillRoundRect(6, 204, 148, 32, 5, ctrl.isSessionInProgress() ? 0xC800 : 0x3186);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(middle_center);
    canvas.drawString("STOP SESSION", 80, 220);

    canvas.fillRoundRect(166, 204, 148, 32, 5, 0x2965);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString("DIAG SERIE", 240, 220);

    canvas.pushSprite(0, 0);
}

void setup() {
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = true;
    M5.begin(cfg);

    Serial.begin(115200);
    delay(200);

    M5.Display.setRotation(1);
    canvas.setColorDepth(16);
    canvas.createSprite(320, 240);

    VoiceKitConfig vcfg;
    vcfg.provider = ACTIVE_VOICE_PROVIDER;
    vcfg.wifiMode = WiFiMode::MANAGED_WIFI;
    vcfg.wifiSSID = WIFI_SSID;
    vcfg.wifiPassword = WIFI_PASSWORD;
    vcfg.apiKey = (vcfg.provider == VoiceProviderType::OPENAI) ? OPENAI_API_KEY : GEMINI_KEY;
    vcfg.volume = 75;
    vcfg.language = "fr-FR";

    // Activation du mode Wake Word Conversation (v0.6.3) avec WakeNet « Bonjour ESP »
    vcfg.wakeWordEngine = WakeWordEngineType::WAKENET;
    vcfg.wakeWordPhrase = "Bonjour ESP";
    vcfg.enableWakeWordConversation = true;
    vcfg.wakeConversationSilenceTimeoutMs = 30000;      // 30 s d'inactivité par défaut
    vcfg.wakeConversationMaxSessionDurationMs = 180000; // 3 min max par session
    vcfg.wakeConversationVoiceStopEnabled = true;       // Fermeture vocale (« Au revoir »)
    vcfg.wakeConversationDisconnectAfterSession = true; // Libération réseau entre sessions

    // Callbacks applicatifs (Section 7 — tous exécutés dans la tâche principale)
    voice.onWakeConvDetected([](const String& word, float, bool isSim) {
        gLastUserText = "";
        gLastAssistantText = "";
        gBannerSubText = isSim
            ? ("Declenchement SIMULE : " + word)
            : ("Detection ACOUSTIQUE : « " + word + " »");
        gUiDirty = true;
    });

    voice.onWakeConvConnecting([]() {
        gBannerSubText = (voice.getConfig().provider == VoiceProviderType::OPENAI)
            ? "Etablissement WebSocket OpenAI Realtime..."
            : "Etablissement WebSocket Gemini Live...";
        gUiDirty = true;
    });

    voice.onWakeConvReady([]() {
        gBannerSubText = "Session prete — Parlez librement";
        gUiDirty = true;
    });

    voice.onWakeConvListening([]() {
        gBannerSubText = "Ecoute active (dites « Au revoir » pour terminer)";
        gUiDirty = true;
    });

    voice.onWakeConvSpeaking([]() {
        gBannerSubText = "Restitution de la reponse vocale...";
        gUiDirty = true;
    });

    voice.onWakeConvEnded([](WakeWordSessionEndReason reason) {
        gBannerSubText = String("Session terminee (") + wakeWordSessionEndReasonToString(reason) + ")";
        gUiDirty = true;
    });

    voice.onWakeConvReturnedToWake([]() {
        gBannerSubText = voice.isWakeWordSimulated()
            ? "Mode TEST actif (non acoustique)"
            : "Dites « Bonjour ESP » pour parler";
        gUiDirty = true;
    });

    voice.onWakeConvRecoverableError([](const String& err) {
        gBannerSubText = err;
        gUiDirty = true;
    });

    voice.onWakeConvStateChanged([](WakeWordConversationState) {
        gUiDirty = true;
    });

    voice.onUserText([](const String& text, bool isFinal) {
        gLastUserText = text;
        if (isFinal) {
            gBannerSubText = "Transcription finale recue";
        }
        gUiDirty = true;
    });

    voice.onAssistantText([](const String& text) {
        gLastAssistantText = text;
        gUiDirty = true;
    });

    voice.begin(vcfg);
    drawInterface();
}

void loop() {
    M5.update();
    voice.update();

    // Gestion tactile simple M5GFX (sans LVGL)
    if (M5.Touch.getCount() > 0) {
        auto t = M5.Touch.getDetail(0);
        if (t.wasPressed() && t.y >= 200) {
            if (t.x < 160) {
                // Bouton 1 (gauche) : Arrêt manuel explicite de la session
                if (voice.isWakeWordConversationInProgress()) {
                    voice.stopWakeWordConversationSession(true);
                    gBannerSubText = "Arret manuel demande";
                    gUiDirty = true;
                }
            } else {
                // Bouton 2 (droite) : Impression des diagnostics complets sur le port série
                voice.printWakeWordConversationDiagnostics();
            }
        }
    }

    // Commandes série optionnelles ("diag", "stop", "wake")
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.equalsIgnoreCase("diag")) {
            voice.printWakeWordConversationDiagnostics();
        } else if (cmd.equalsIgnoreCase("stop")) {
            voice.stopWakeWordConversationSession(true);
        } else {
            voice.handleWakeWordSerialCommand(cmd);
        }
    }

    const uint32_t now = millis();
    if (gUiDirty || (now - gLastUiRedrawMs >= 150)) {
        gUiDirty = false;
        gLastUiRedrawMs = now;
        drawInterface();
    }
}

