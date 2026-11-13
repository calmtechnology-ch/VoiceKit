/**
 * @file main.cpp
 * @brief Exemple VoiceUxDemo — Fondation Voice UX locale pour VoiceKit v0.6.4.1
 * 
 * Fonctionne à 100% hors-ligne, sans Wi-Fi, sans clé API et sans capture audio.
 * Démontre la reconnaissance locale et le filtrage des commandes conversationnelles :
 * 1. Filtrage déterministe TextGate (ACCEPT / IGNORE / INCOMPLETE).
 * 2. Reconnaissance stricte d'intentions VoiceIntentRouter (REPEAT, STOP, PAUSE, RESUME, SLOWER, REPHRASE).
 * 3. Mémoire conversationnelle légère bornée ReplyMemory (5 réponses en RAM).
 * 4. Façade VoiceUxController avec instrumentation temps réel (microsecondes).
 * 5. Interface double écran LCD M5Unified et commandes série (T, M, H, C, D).
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoiceUxController.h>

static VoiceUxController s_ux;
static String s_serialBuffer = "";
static String s_lastActionMsg = "Pret - En attente de commande";

// =============================================================================
// Interface Graphique LCD (M5GFX)
// =============================================================================

static void refreshScreen() {
    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_BLACK);

    // En-tête
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLUE);
    M5.Display.fillRect(0, 0, 320, 24, TFT_BLUE);
    M5.Display.drawString(" VoiceKit v0.6.4.1 - Voice UX Demo", 6, 6);

    // Badge d'état ACTIVE / DISABLED
    bool active = s_ux.isEnabled();
    uint16_t badgeColor = active ? TFT_GREEN : TFT_RED;
    M5.Display.fillRect(230, 3, 84, 18, badgeColor);
    M5.Display.setTextColor(TFT_BLACK, badgeColor);
    M5.Display.drawString(active ? " ACTIF " : " DESACTIVE ", 234, 6);

    // Panneau 1 : Analyse de la dernière transcription
    M5.Display.drawRoundRect(6, 28, 308, 82, 4, TFT_DARKGREY);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString("ANALYSE DE TRANSCRIPTION :", 12, 34);

    const VoiceUxResult& res = s_ux.getLastResult();
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    String rawDisplay = res.rawText.isEmpty() ? "(Aucun texte analyse)" : res.rawText;
    if (rawDisplay.length() > 36) rawDisplay = rawDisplay.substring(0, 33) + "...";
    M5.Display.drawString("Texte : " + rawDisplay, 14, 50);

    // Décision TextGate
    uint16_t decColor = TFT_WHITE;
    if (res.gateDecision == TextGateDecision::ACCEPT) decColor = TFT_GREEN;
    else if (res.gateDecision == TextGateDecision::IGNORE) decColor = TFT_RED;
    else if (res.gateDecision == TextGateDecision::INCOMPLETE) decColor = TFT_YELLOW;

    M5.Display.setTextColor(decColor, TFT_BLACK);
    M5.Display.drawString("TextGate : " + String(textGateDecisionToString(res.gateDecision)), 14, 66);

    // Intention reconnue
    uint16_t intColor = (res.intent == VoiceUxIntent::NONE) ? TFT_LIGHTGREY : TFT_MAGENTA;
    M5.Display.setTextColor(intColor, TFT_BLACK);
    M5.Display.drawString("Intention : " + String(voiceUxIntentToString(res.intent)), 150, 66);

    // Raison & Temps
    M5.Display.setTextColor(TFT_GOLD, TFT_BLACK);
    String reasonDisplay = res.reason;
    if (reasonDisplay.length() > 30) reasonDisplay = reasonDisplay.substring(0, 27) + "...";
    M5.Display.drawString(reasonDisplay + " (" + String(res.processingTimeUs) + " us)", 14, 84);

    // Panneau 2 : Mémoire des réponses (ReplyMemory)
    M5.Display.drawRoundRect(6, 114, 308, 64, 4, TFT_DARKGREY);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    const ReplyMemory& mem = s_ux.getReplyMemory();
    M5.Display.drawString("MEMOIRE CONVERSATIONNELLE (" + String(mem.getCount()) + "/" + String(mem.getCapacity()) + ") :", 12, 120);

    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    String lastReply = mem.getLastReply();
    if (lastReply.isEmpty()) lastReply = "(Memoire vide)";
    if (lastReply.length() > 42) lastReply = lastReply.substring(0, 39) + "...";
    M5.Display.drawString("Derniere : " + lastReply, 14, 136);

    if (!res.recallText.isEmpty()) {
        M5.Display.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
        String recallDisp = res.recallText;
        if (recallDisp.length() > 40) recallDisp = recallDisp.substring(0, 37) + "...";
        M5.Display.drawString("Rappel   : " + recallDisp, 14, 154);
    } else {
        M5.Display.setTextColor(TFT_DARKGRAY, TFT_BLACK);
        M5.Display.drawString("Rappel   : (aucun)", 14, 154);
    }

    // Panneau 3 : Statistiques d'exploitation
    M5.Display.drawRoundRect(6, 182, 308, 36, 4, TFT_DARKGREY);
    const VoiceUxStats& stats = s_ux.getStats();
    M5.Display.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    M5.Display.drawString(
        "Recus:" + String(stats.processedCount) +
        " | Ok:" + String(stats.acceptedCount) +
        " | Ign:" + String(stats.ignoredCount) +
        " | Intentions:" + String(stats.recognizedIntentCount),
        14, 194
    );

    // Pied de page : Aide interactive
    M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5.Display.drawString("Cmd: T <txt> | M <rep> | H | C | D | Touch: Toggle", 8, 224);

    M5.Display.endWrite();
}

// =============================================================================
// Traitement des Commandes Série Interactives
// =============================================================================

static void printHelp() {
    Serial.println("\n=======================================================");
    Serial.println("  VoiceKit v0.6.4.1 — Voice UX Foundation Demo");
    Serial.println("  M5Stack CoreS3 (100% Hors-Ligne / Sans Wi-Fi / Sans IA)");
    Serial.println("=======================================================");
    Serial.println("Commandes disponibles :");
    Serial.println("  T <texte>   : Analyser une transcription");
    Serial.println("                Exemples :");
    Serial.println("                  T Répète");
    Serial.println("                  T Mets en pause");
    Serial.println("                  T Continue");
    Serial.println("                  T Quelle heure est-il ?");
    Serial.println("                  T Euh...");
    Serial.println("                  T Pourquoi la voiture s'arrête-t-elle ?");
    Serial.println("  M <reponse> : Mémoriser une réponse de l'assistant");
    Serial.println("                Exemple : M La température est de 22 degrés.");
    Serial.println("  H           : Afficher l'historique complet en mémoire");
    Serial.println("  C           : Effacer la mémoire des réponses");
    Serial.println("  D           : Activer / Désactiver le module Voice UX");
    Serial.println("  ?           : Afficher cette aide");
    Serial.println("=======================================================\n");
}

static void handleSerialLine(const String& line) {
    String trimmed = line;
    trimmed.trim();
    if (trimmed.isEmpty()) return;

    char cmd = trimmed[0];
    String arg = (trimmed.length() > 1) ? trimmed.substring(1) : "";
    arg.trim();

    if (cmd == 'T' || cmd == 't') {
        if (arg.isEmpty()) {
            Serial.println("[ERREUR] Syntaxe : T <texte a analyser>");
            return;
        }

        Serial.println("\n-------------------------------------------------------");
        Serial.printf("[VOICE UX] Analyse du texte : \"%s\"\n", arg.c_str());

        VoiceUxResult res = s_ux.processFinalText(arg);

        Serial.printf("[VOICE UX] Decision TextGate : %s\n", textGateDecisionToString(res.gateDecision));
        Serial.printf("[VOICE UX] Intention         : %s\n", voiceUxIntentToString(res.intent));
        Serial.printf("[VOICE UX] Traite localement : %s\n", res.handledLocally ? "OUI" : "NON (Pass-through)");
        Serial.printf("[VOICE UX] Raison            : %s\n", res.reason.c_str());
        Serial.printf("[VOICE UX] Temps de calcul   : %u us\n", res.processingTimeUs);

        if (!res.recallText.isEmpty()) {
            Serial.printf("[VOICE UX] Texte rappele     : \"%s\"\n", res.recallText.c_str());
        }
        Serial.println("-------------------------------------------------------");
        refreshScreen();
    }
    else if (cmd == 'M' || cmd == 'm') {
        if (arg.isEmpty()) {
            Serial.println("[ERREUR] Syntaxe : M <texte de reponse a memoriser>");
            return;
        }
        bool ok = s_ux.rememberReply(arg);
        if (ok) {
            Serial.printf("[VOICE UX] Reponse memorisee : \"%s\" (total : %u/%u)\n",
                          arg.c_str(),
                          (unsigned int)s_ux.getReplyMemory().getCount(),
                          (unsigned int)s_ux.getReplyMemory().getCapacity());
        } else {
            Serial.println("[VOICE UX] Echec memorisation (module desactive ou texte vide)");
        }
        refreshScreen();
    }
    else if (cmd == 'H' || cmd == 'h') {
        Serial.println("\n=======================================================");
        Serial.println("  HISTORIQUE DES REPONSES EN MEMOIRE (ReplyMemory)");
        Serial.println("=======================================================");
        const ReplyMemory& mem = s_ux.getReplyMemory();
        size_t count = mem.getCount();
        if (count == 0) {
            Serial.println("  (Memoire vide)");
        } else {
            for (size_t i = 0; i < count; ++i) {
                String rep = mem.getReplyAt(i);
                const char* tag = (i == 0) ? " [DERNIERE]" : (i == 1) ? " [PRECEDENTE]" : "";
                Serial.printf("  [%u]%s : %s\n", (unsigned int)i, tag, rep.c_str());
            }
        }
        Serial.println("=======================================================\n");
    }
    else if (cmd == 'C' || cmd == 'c') {
        s_ux.getReplyMemory().clear();
        Serial.println("[VOICE UX] Memoire conversationnelle integralement effacee.");
        refreshScreen();
    }
    else if (cmd == 'D' || cmd == 'd') {
        bool newState = !s_ux.isEnabled();
        s_ux.setEnabled(newState);
        Serial.printf("[VOICE UX] Statut du module : %s\n", newState ? "ACTIVE" : "DESACTIVE");
        refreshScreen();
    }
    else if (cmd == '?') {
        printHelp();
    }
    else {
        Serial.printf("[INCONNU] Commande non reconnue : '%c'. Tapez '?' pour l'aide.\n", cmd);
    }
}

// =============================================================================
// Initialisation & Boucle Principale
// =============================================================================

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(400);

    // Initialisation du contrôleur Voice UX
    s_ux.begin();
    s_ux.setEnabled(true); // Actif pour la démonstration

    // Écoute des événements émis
    s_ux.onResult([](const VoiceUxResult& res) {
        // Enregistreur d'événements pour l'UI
    });

    // Pré-remplir quelques réponses types pour tester immédiatement 'Répète' et 'Réponse précédente'
    s_ux.rememberReply("La température actuelle est de 22 degrés.");
    s_ux.rememberReply("L'humidité relative de l'air est de 48 pour cent.");

    refreshScreen();
    printHelp();
}

void loop() {
    M5.update();

    // Interaction tactile CoreS3 : basculer l'activation du module
    if (M5.BtnA.wasPressed() || (M5.Touch.getCount() > 0 && M5.Touch.getDetail(0).wasClicked())) {
        s_ux.setEnabled(!s_ux.isEnabled());
        Serial.printf("[TACTILE] Module Voice UX bascule : %s\n", s_ux.isEnabled() ? "ACTIVE" : "DESACTIVE");
        refreshScreen();
        delay(200);
    }

    // Lecture des commandes série
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (!s_serialBuffer.isEmpty()) {
                handleSerialLine(s_serialBuffer);
                s_serialBuffer = "";
            }
        } else {
            s_serialBuffer += c;
        }
    }

    // Maintenance périodique non bloquante
    s_ux.update();

    delay(10);
}

