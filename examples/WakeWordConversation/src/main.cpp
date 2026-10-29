#include <M5Unified.h>
#include <VoiceKit.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// =============================================================================
// VoiceKit v0.6.3.2 — Exemple WakeWordConversation (M5Stack CoreS3, M5GFX pur)
//
// Cycle complet mains libres (exclusivement WakeNet acoustique « Bonjour ESP ») :
//   1. WakeNet écoute localement « Bonjour ESP » (WAKE_ARMED)
//   2. Détection acoustique confirmée (WAKE_DETECTED : BONJOUR ESP DÉTECTÉ)
//   3. Suspension de WakeNet et libération contrôlée du microphone (PREPARING_SESSION)
//   4. Connexion au fournisseur vocal configuré : Gemini Live ou OpenAI (CONNECTING : CONNEXION)
//   5. Activation automatique de la conversation (CONVERSATION_LISTENING : PRÊT — PARLEZ)
//   6. Échanges vocaux bidirectionnels multi-tours (ÉCOUTE <-> RÉPONSE)
//   7. Détection de fin de session (silence 30 s, commande vocale finale, ou STOP manuel)
//   8. Arrêt complet non bloquant et libération des ressources réseau/audio (ENDING_SESSION : FIN DE CONVERSATION)
//   9. Réactivation de WakeNet (RETURNING_TO_WAKE : RETOUR EN ÉCOUTE)
//  10. Retour à l'état WAKE_ARMED, répétable sans redémarrage.
// =============================================================================

// Choix du fournisseur par défaut (GEMINI par défaut, ou OPENAI en alternatif)
#ifndef ACTIVE_VOICE_PROVIDER
#define ACTIVE_VOICE_PROVIDER VoiceProviderType::GEMINI
#endif

static VoiceKit voice;

static String gLastUserText = "";
static String gLastAssistantText = "";
static String gBannerSubText = "Dites « Bonjour ESP » pour demarrer une conversation";
static uint32_t gLastRmsRedrawMs = 0;
static int gLastRmsBarW = -1;
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
static constexpr uint16_t COL_ERROR       = 0xF800; // Rouge (erreur explicite)

static bool isWakeNetUnavailable() {
    return !voice.isWakeWordEngineAvailable() ||
           !voice.isWakeWordEngineInitialized() ||
           voice.isWakeWordError();
}

static uint16_t getBannerColor(WakeWordConversationState state, bool userSpokeInTurn) {
    if (isWakeNetUnavailable()) {
        return COL_ERROR;
    }
    switch (state) {
        case WakeWordConversationState::WAKE_ARMED:
            return COL_ARMED;
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

static void drawRmsBar(bool forceRedraw) {
    const WakeWordConversationController& ctrl = voice.getWakeWordConversationController();
    uint32_t rms = voice.getEngine().getAudioLevel();
    int barW = (int)((rms > 3000 ? 3000 : rms) * 296 / 3000);
    if (!forceRedraw && barW == gLastRmsBarW) {
        return;
    }
    gLastRmsBarW = barW;

    M5.Display.startWrite();
    if (barW > 0) {
        M5.Display.fillRect(12, 188, barW, 6, ctrl.hasUserSpokenInTurn() ? TFT_YELLOW : TFT_GREEN);
    }
    if (barW < 296) {
        M5.Display.fillRect(12 + barW, 188, 296 - barW, 6, 0x0000);
    }
    M5.Display.endWrite();
}

static void drawInterface() {
    const WakeWordConversationState state = voice.getWakeWordConversationState();
    const WakeWordConversationMetrics& m = voice.getWakeWordConversationMetrics();
    const WakeWordConversationController& ctrl = voice.getWakeWordConversationController();
    const bool wakeNetUnavailable = isWakeNetUnavailable();
    const char* statusLabel = wakeNetUnavailable
        ? "ERREUR WAKENET INDISPONIBLE"
        : voice.getWakeWordConversationBannerText();
    const uint16_t bannerCol = getBannerColor(state, ctrl.hasUserSpokenInTurn());

    M5.Display.startWrite();
    M5.Display.fillScreen(COL_BG);

    // 1. En-tête : Titre + statut du moteur acoustique WakeNet
    M5.Display.fillRect(0, 0, 320, 24, 0x0000);
    M5.Display.setTextColor(TFT_WHITE, 0x0000);
    M5.Display.setTextDatum(middle_left);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.drawString("VoiceKit v0.6.3.2 - WakeConv", 6, 12);

    M5.Display.setTextDatum(middle_right);
    if (wakeNetUnavailable) {
        M5.Display.setTextColor(COL_ERROR, 0x0000);
        M5.Display.drawString("[ERREUR WAKENET]", 314, 12);
    } else {
        M5.Display.setTextColor(0x07E0, 0x0000);
        M5.Display.drawString("[WAKENET ACOUSTIQUE]", 314, 12);
    }

    // 2. Bandeau principal d'état (Section 8)
    M5.Display.fillRoundRect(6, 28, 308, 42, 6, bannerCol);
    uint16_t bannerTextCol = (bannerCol == COL_LISTENING || bannerCol == COL_DETECTED)
        ? TFT_BLACK : TFT_WHITE;
    M5.Display.setTextColor(bannerTextCol, bannerCol);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(statusLabel, 160, 42);
    String subText = gBannerSubText;
    if (wakeNetUnavailable) {
        String wakeErr = voice.getWakeWordLastError();
        if (!wakeErr.isEmpty()) {
            subText = wakeErr;
        } else if (subText.isEmpty()) {
            subText = "Modele WakeNet wn10_fr_bonjouresp indisponible";
        }
    }
    if (subText.length() > 46) {
        subText = subText.substring(0, 46) + "...";
    }
    M5.Display.drawString(subText.c_str(), 160, 59);

    // 3. Compteurs de sessions, détections acoustiques, reconnexions et erreurs
    M5.Display.fillRoundRect(6, 74, 308, 44, 5, COL_CARD);
    M5.Display.setTextColor(TFT_CYAN, COL_CARD);
    M5.Display.setTextDatum(top_left);

    char line1[64];
    snprintf(line1, sizeof(line1), "Sessions: %u | Detections acoustiques: %u",
             (unsigned)m.sessionCount,
             (unsigned)m.realDetectionCount);
    M5.Display.drawString(line1, 12, 78);

    char line2[64];
    snprintf(line2, sizeof(line2), "Tours: %u | Reconnexions: %u | Erreurs: %u",
             (unsigned)m.turnCount,
             (unsigned)m.reconnectCount,
             (unsigned)m.errorCount);
    M5.Display.setTextColor(TFT_LIGHTGREY, COL_CARD);
    M5.Display.drawString(line2, 12, 97);

    // 4. Zone de dialogue (Utilisateur / Assistant)
    M5.Display.fillRoundRect(6, 122, 308, 78, 5, COL_CARD);
    M5.Display.setTextColor(TFT_YELLOW, COL_CARD);
    String userDisplay = gLastUserText.isEmpty() ? String("Vous : (en attente de parole)") : ("Vous : " + gLastUserText);
    if (userDisplay.length() > 44) userDisplay = userDisplay.substring(0, 44) + "...";
    M5.Display.drawString(userDisplay.c_str(), 12, 127);

    M5.Display.setTextColor(TFT_WHITE, COL_CARD);
    String aiDisplay = gLastAssistantText.isEmpty() ? String("IA : ---") : ("IA : " + gLastAssistantText);
    if (aiDisplay.length() > 44) {
        M5.Display.drawString(aiDisplay.substring(0, 44).c_str(), 12, 149);
        String line2Ai = aiDisplay.substring(44);
        if (line2Ai.length() > 44) line2Ai = line2Ai.substring(0, 44) + "...";
        M5.Display.drawString(line2Ai.c_str(), 12, 167);
    } else {
        M5.Display.drawString(aiDisplay.c_str(), 12, 149);
    }

    // 5. Deux boutons tactiles répartis sur la largeur de l'écran 320 x 240
    M5.Display.fillRoundRect(6, 204, 150, 32, 5, ctrl.isSessionInProgress() ? 0xC800 : 0x3186);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("STOP SESSION", 81, 220);

    M5.Display.fillRoundRect(164, 204, 150, 32, 5, 0x2965);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.drawString("DIAGNOSTICS", 239, 220);

    M5.Display.endWrite();

    // Barre de niveau micro RMS en temps réel
    drawRmsBar(true);
}

void setup() {
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = true;
    M5.begin(cfg);

    Serial.begin(115200);
    delay(200);

    M5.Display.setRotation(1);

    VoiceKitConfig vcfg;
    vcfg.provider = ACTIVE_VOICE_PROVIDER;
    vcfg.wifiMode = WiFiMode::MANAGED_WIFI;
    vcfg.wifiSSID = WIFI_SSID;
    vcfg.wifiPassword = WIFI_PASSWORD;
    vcfg.apiKey = (vcfg.provider == VoiceProviderType::OPENAI) ? OPENAI_API_KEY : GEMINI_KEY;
    vcfg.volume = 75;
    vcfg.language = "fr-FR";

    // Activation exclusive du moteur acoustique WakeNet « Bonjour ESP » (wn10_fr_bonjouresp)
    vcfg.wakeWordEngine = WakeWordEngineType::WAKENET;
    vcfg.wakeWordPhrase = "Bonjour ESP";
    vcfg.wakeWordModelName = "wn10_fr_bonjouresp";
    vcfg.enableWakeWordConversation = true;
    vcfg.wakeConversationSilenceTimeoutMs = 30000;      // 30 s d'inactivité par défaut
    vcfg.wakeConversationMaxSessionDurationMs = 180000; // 3 min max par session
    vcfg.wakeConversationVoiceStopEnabled = true;       // Fermeture vocale (« Au revoir »)
    vcfg.wakeConversationDisconnectAfterSession = true; // Libération réseau entre sessions

    // Callbacks applicatifs (exécutés depuis les événements réels du contrôleur)
    voice.onWakeConvDetected([](const String& word, float, bool) {
        gLastUserText = "";
        gLastAssistantText = "";
        gBannerSubText = "Detection ACOUSTIQUE : « " + word + " »";
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
        gBannerSubText = voice.getWakeWordConversationController().hasUserSpokenInTurn()
            ? "Parole en cours d'acquisition..."
            : "Ecoute active (dites « Au revoir » pour terminer)";
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
        gBannerSubText = "Dites « Bonjour ESP » pour parler";
        gUiDirty = true;
    });

    voice.onWakeConvRecoverableError([](const String& err) {
        gBannerSubText = err;
        gUiDirty = true;
    });

    voice.onWakeWordError([](const String& err) {
        gBannerSubText = err.isEmpty()
            ? String("Erreur : WakeNet (wn10_fr_bonjouresp) indisponible")
            : err;
        Serial.printf("[WakeWordConversation] ERREUR WakeNet : %s\n", gBannerSubText.c_str());
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

    if (isWakeNetUnavailable()) {
        String wakeErr = voice.getWakeWordLastError();
        if (wakeErr.isEmpty()) {
            wakeErr = "Erreur : WakeNet (wn10_fr_bonjouresp) indisponible";
        }
        gBannerSubText = wakeErr;
        Serial.printf("[WakeWordConversation] ERREUR EXPLICITE : %s\n", wakeErr.c_str());
    }

    drawInterface();
    gUiDirty = false;
    gLastRmsRedrawMs = millis();
}

void loop() {
    M5.update();
    voice.update();

    // Gestion tactile M5GFX : deux boutons répartis sur la largeur de l'écran (320 x 240)
    if (M5.Touch.getCount() > 0) {
        auto t = M5.Touch.getDetail(0);
        if (t.wasPressed() && t.y >= 200) {
            if (t.x < 160) {
                // Bouton 1 : STOP SESSION (arrêt manuel explicite de la conversation)
                if (voice.isWakeWordConversationInProgress()) {
                    voice.stopWakeWordConversationSession(true);
                    gBannerSubText = "Arret manuel demande";
                    gUiDirty = true;
                }
            } else {
                // Bouton 2 : DIAGNOSTICS (affichage des informations techniques)
                voice.printWakeWordConversationDiagnostics();
            }
        }
    }

    // Commandes série ("diag", "stop") — aucune commande de simulation ("wake") n'est acceptée
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.equalsIgnoreCase("diag")) {
            voice.printWakeWordConversationDiagnostics();
        } else if (cmd.equalsIgnoreCase("stop")) {
            voice.stopWakeWordConversationSession(true);
        }
    }

    const uint32_t now = millis();
    if (gUiDirty) {
        gUiDirty = false;
        gLastRmsRedrawMs = now;
        drawInterface();
    } else if (now - gLastRmsRedrawMs >= 120) {
        gLastRmsRedrawMs = now;
        drawRmsBar(false);
    }

    delay(5);
}
