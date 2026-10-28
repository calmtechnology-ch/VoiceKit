/**
 * @file main.cpp
 * @brief Exemple autonome — VoiceKit v0.6.2.1 (Wake Word Foundation — M5Stack CoreS3)
 *
 * Fonctionnement :
 * - S'exécute sur M5Stack CoreS3 SANS écran LVGL et SANS connexion Wi-Fi ni Gemini/OpenAI.
 * - Valide la capture audio locale partagée (ES7210, PCM 16000 Hz / 16-bit mono) via VoiceKit.
 * - Valide et affiche clairement sur le moniteur série les transitions de la machine à états :
 *     DISABLED -> INITIALIZING -> ARMED (ARMED — En écoute) -> DETECTED (DETECTED — Mot reconnu)
 *     -> SUSPENDED (SUSPENDED — Détection suspendue) -> ARMED (ARMED — Écoute réactivée)
 * - Lorsqu'un événement DETECTED est émis, affiche un encadré très visible avec
 *   "MOT DE RÉVEIL DÉTECTÉ", le texte "Bonjour ESP", le moteur actif ("RECONNAISSANCE RÉELLE"
 *   ou "SIMULATION (TEST)") et les compteurs de détections, de réarmements et d'erreurs.
 * - Toutes les impressions série sont produites exclusivement dans la boucle principale
 *   (jamais dans le callback audio critique FreeRTOS).
 * - Aucun journal répétitif n'est émis à chaque passage de loop() : seuls les changements
 *   d'état, les détections, les erreurs et les commandes explicites sont affichés.
 *
 * Commandes Moniteur Série (115200 bauds) :
 * - "wake" ou "bonjour" ou "bonjour esp" : déclenche une détection de test simulée (si ARMED et moteur TEST)
 * - "r" ou "resume"                      : reprend l'écoute après fin de conversation (SUSPENDED -> ARMED)
 * - "s" ou "suspend"                     : suspend manuellement le détecteur (ARMED -> SUSPENDED)
 * - "d" ou "disable"                     : désactive le détecteur ( -> DISABLED)
 * - "e" ou "enable"                      : réactive le détecteur (DISABLED -> INITIALIZING -> ARMED)
 * - "c" ou "cycle"                       : exécute 5 cycles d'activation/suspension/reprise
 * - "w" ou "wakenet"                     : sélectionne et teste le moteur WAKENET
 * - "t" ou "test"                        : sélectionne et arme le moteur TEST (SIMULATION (TEST))
 * - "m" ou "metrics"                     : affiche les métriques mémoire (RAM/PSRAM), CPU, pont audio et compteurs
 *
 * Interactions M5Stack CoreS3 (sans moniteur série) :
 * - Appui tactile moitié gauche de l'écran : déclenche "Bonjour ESP" (en mode TEST et état ARMED)
 * - Appui tactile moitié droite de l'écran : simule la fin de conversation (SUSPENDED -> ARMED)
 */

#include <M5CoreS3.h>
#include <VoiceKit.h>

VoiceKit voice;
static uint32_t s_lastTouchMs = 0;
static uint32_t s_lastRmsDrawMs = 0;
static uint32_t s_detectedSuspendStartMs = 0;
static bool s_pendingAutoResumeAfterDetection = false;
static uint32_t s_lastDisplayedDetectionIndex = 0;
static constexpr uint32_t AUTO_RESUME_AFTER_DETECT_MS = 2500;

static const char* getEngineRecognitionNature() {
    return voice.isWakeWordSimulated() ? "SIMULATION (TEST)" : "RECONNAISSANCE RÉELLE";
}

static const char* getStateFrenchMessage(WakeWordState state) {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    switch (state) {
        case WakeWordState::INITIALIZING:
            return "INITIALIZING — Initialisation en cours";
        case WakeWordState::ARMED:
            return (m.rearmCount == 0) ? "ARMED — En écoute" : "ARMED — Écoute réactivée";
        case WakeWordState::DETECTED:
            return "DETECTED — Mot reconnu";
        case WakeWordState::SUSPENDED:
            return "SUSPENDED — Détection suspendue";
        case WakeWordState::ERROR:
            return "ERROR — Erreur du détecteur";
        case WakeWordState::DISABLED:
        default:
            return "DISABLED — Détecteur désactivé";
    }
}

static void drawLiveRmsMeter() {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    CoreS3.Display.fillRect(10, 175, 300, 18, TFT_BLACK);
    CoreS3.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setCursor(10, 178);
    CoreS3.Display.printf("RMS:%4u(Pk:%4u) Det:%u Rearm:%u Err:%u",
                          m.lastRms, m.peakRms, m.detectionCount, m.rearmCount, m.errorCount);

    int barWidth = (int)(m.lastRms / 15);
    if (barWidth > 76) barWidth = 76;
    CoreS3.Display.drawRect(234, 177, 78, 10, TFT_DARKGREY);
    if (barWidth > 0) {
        uint16_t barColor = (m.lastRms > 400) ? TFT_GREEN : TFT_CYAN;
        CoreS3.Display.fillRect(235, 178, barWidth, 8, barColor);
    }
}

static void updateSimpleStatusScreen() {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    CoreS3.Display.fillScreen(TFT_BLACK);
    CoreS3.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setCursor(10, 10);
    CoreS3.Display.printf("VoiceKit %s\n", VOICEKIT_VERSION);

    CoreS3.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setCursor(10, 36);
    CoreS3.Display.println("Wake Word Foundation (Offline / No LVGL)");
    CoreS3.Display.println("Audio: CoreS3 ES7210 PCM 16000 Hz / 16-bit");

    CoreS3.Display.setCursor(10, 68);
    if (voice.isWakeWordSimulated()) {
        CoreS3.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
        CoreS3.Display.printf("Moteur: %s — SIMULATION (TEST)\n", voice.getWakeWordEngineName());
    } else {
        CoreS3.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        CoreS3.Display.printf("Moteur: %s — RECONNAISSANCE REELLE\n", voice.getWakeWordEngineName());
    }
    CoreS3.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    CoreS3.Display.printf("Modele: %s (\"Bonjour ESP\")\n", voice.getWakeWordModelName());

    WakeWordState st = voice.getWakeWordState();
    uint16_t stateColor = TFT_LIGHTGREY;
    if (st == WakeWordState::ARMED) stateColor = TFT_GREEN;
    else if (st == WakeWordState::DETECTED) stateColor = TFT_YELLOW;
    else if (st == WakeWordState::SUSPENDED) stateColor = TFT_ORANGE;
    else if (st == WakeWordState::ERROR) stateColor = TFT_RED;

    CoreS3.Display.setTextColor(stateColor, TFT_BLACK);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setCursor(10, 105);
    CoreS3.Display.printf("Etat: %s\n", wakeWordStateToString(st));

    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setCursor(10, 138);
    if (!voice.getLastDetectedWakeWord().isEmpty() && (st == WakeWordState::DETECTED || st == WakeWordState::SUSPENDED)) {
        CoreS3.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        CoreS3.Display.printf(">>> MOT DE REVEIL DETECTE : %s (#%u) <<<\n",
                              voice.getLastDetectedWakeWord().c_str(),
                              m.detectionCount);
        CoreS3.Display.printf("Mode : %s\n",
                              voice.isWakeWordSimulated() ? "SIMULATION (TEST) - Non acoustique" : "RECONNAISSANCE REELLE");
    } else if (st == WakeWordState::ARMED && !voice.isWakeWordSimulated()) {
        CoreS3.Display.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
        CoreS3.Display.println("Dites : \"Bonjour ESP\" pres du microphone...");
    } else if (st == WakeWordState::ARMED && voice.isWakeWordSimulated()) {
        CoreS3.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
        CoreS3.Display.println("Mode SIMULATION (TEST) : tapez 'bonjour esp' ou touchez GAUCHE");
    }
    if (voice.isWakeWordError()) {
        CoreS3.Display.setTextColor(TFT_RED, TFT_BLACK);
        CoreS3.Display.printf("Erreur: %s\n", voice.getWakeWordLastError().c_str());
    }

    drawLiveRmsMeter();

    CoreS3.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    CoreS3.Display.setCursor(10, 205);
    CoreS3.Display.println("Touch LEFT : Trigger TEST 'Bonjour ESP'");
    CoreS3.Display.println("Touch RIGHT: Resume ARMED immediately");
}

static void printActiveEngineStatus() {
    Serial.printf("[WAKE] Moteur actif : %s (modèle : %s) — %s\n",
                  voice.getWakeWordEngineName(),
                  voice.getWakeWordModelName(),
                  getEngineRecognitionNature());
    if (voice.isWakeWordSimulated()) {
        Serial.println("[WAKE] Information : Le moteur TEST est une SIMULATION (TEST) déterministe et ne réalise aucune détection acoustique.");
    }
}

static void printStateTransitionMessage(WakeWordState state) {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    const char* stateMsg = getStateFrenchMessage(state);
    const char* engineName = voice.getWakeWordEngineName();
    const char* engineNature = getEngineRecognitionNature();

    Serial.printf("[WAKE] État : %s | Moteur : %s [%s] | Détections=%u | Réarmements=%u | Erreurs=%u\n",
                  stateMsg,
                  engineName,
                  engineNature,
                  m.detectionCount,
                  m.rearmCount,
                  m.errorCount);
}

static void printWakeWordDetectionBanner(const String& word) {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    const String detectedPhrase = word.isEmpty() ? String("Bonjour ESP") : word;
    const uint32_t realDetections = (m.detectionCount >= m.simulatedDetectionCount)
        ? (m.detectionCount - m.simulatedDetectionCount)
        : 0u;

    Serial.println();
    Serial.println("============================================================");
    Serial.println("||                 MOT DE RÉVEIL DÉTECTÉ                  ||");
    Serial.println("============================================================");
    Serial.printf("  Texte reconnu          : %s\n", detectedPhrase.c_str());
    Serial.printf("  Moteur actif           : %s (%s)\n",
                  voice.getWakeWordEngineName(),
                  voice.getWakeWordModelName());
    Serial.printf("  Mode de reconnaissance : %s\n", getEngineRecognitionNature());
    if (voice.isWakeWordSimulated()) {
        Serial.println("  Origine                : Déclenchement simulé du moteur TEST (non acoustique)");
    } else {
        Serial.println("  Origine                : Détection acoustique réelle (microphone CoreS3 ES7210)");
    }
    Serial.printf("  Compteur de détections : #%u (réelles=%u | simulées=%u)\n",
                  m.detectionCount,
                  realDetections,
                  m.simulatedDetectionCount);
    Serial.printf("  Compteurs cumulés      : Détections=%u | Réarmements=%u | Erreurs=%u\n",
                  m.detectionCount,
                  m.rearmCount,
                  m.errorCount);
    Serial.println("============================================================");
}

static void printDiagnostics() {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    const uint32_t realDetections = (m.detectionCount >= m.simulatedDetectionCount)
        ? (m.detectionCount - m.simulatedDetectionCount)
        : 0u;

    Serial.println("------------------------------------------------------------");
    Serial.printf("[WAKE][DIAG] Enabled=%s | CompiledSupport=%s | ModelAvail=%s | EngineAvail=%s | EngineInit=%s\n",
                  voice.isWakeWordEnabled() ? "YES" : "NO",
                  voice.isWakeWordEngineCompiledSupportAvailable() ? "YES" : "NO",
                  voice.isWakeWordEngineModelAvailable() ? "YES" : "NO",
                  voice.isWakeWordEngineAvailable() ? "YES" : "NO",
                  voice.isWakeWordEngineInitialized() ? "YES" : "NO");
    Serial.printf("[WAKE][DIAG] Listening=%s | RealRecognition=%s | Simulated=%s | Mode=%s\n",
                  voice.isWakeWordListening() ? "YES" : "NO",
                  voice.isWakeWordRealRecognitionActive() ? "YES" : "NO",
                  voice.isWakeWordSimulated() ? "YES" : "NO",
                  getEngineRecognitionNature());
    Serial.printf("[WAKE][DIAG] State=%s (%s) | Engine=%s | Model=%s\n",
                  wakeWordStateToString(voice.getWakeWordState()),
                  getStateFrenchMessage(voice.getWakeWordState()),
                  voice.getWakeWordEngineName(),
                  voice.getWakeWordModelName());
    Serial.printf("[WAKE][DIAG] Internal RAM free=%u B (min=%u B) | PSRAM free=%u B (min=%u B)\n",
                  m.freeInternalHeap, m.minFreeInternalHeap, m.freePsram, m.minFreePsram);
    Serial.printf("[WAKE][DIAG] Audio Push max=%u us (last=%u us) | Frame Process max=%u us (last=%u us)\n",
                  m.maxAudioPushDurationUs, m.lastAudioPushDurationUs,
                  m.maxWakeProcessDurationUs, m.lastWakeProcessDurationUs);
    Serial.printf("[WAKE][DIAG] Update Budget=%u us | Update Process max=%u us (last=%u us) | BudgetExceeded=%u | MaxFrames/Update=%u (last=%u)\n",
                  m.processingBudgetUs, m.maxUpdateProcessDurationUs, m.lastUpdateProcessDurationUs,
                  m.processingBudgetExceeded, (unsigned)m.maxFramesPerUpdate, (unsigned)m.lastFramesProcessedInUpdate);
    Serial.printf("[WAKE][DIAG] Frames rx=%u | processed=%u | dropped=%u | saturations=%u | queue=%u/%u (max=%u)\n",
                  m.framesReceived, m.framesProcessed, m.framesDropped, m.queueSaturations,
                  (unsigned)m.currentQueueOccupancy, (unsigned)m.queueCapacity, (unsigned)m.maxQueueOccupancy);
    Serial.printf("[WAKE][DIAG] RMS last=%u | peak=%u | Detections=%u (real=%u, simulated=%u) | Rearms=%u | Errors=%u | Transitions=%u | CaptureStopFailures=%u\n",
                  m.lastRms, m.peakRms,
                  m.detectionCount, realDetections, m.simulatedDetectionCount,
                  m.rearmCount, m.errorCount, m.stateTransitionCount,
                  m.captureStopFailures);
    Serial.println("------------------------------------------------------------");
}

void setup() {
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.printf("[WAKE] VoiceKit %s\n", VOICEKIT_VERSION);

    // Configuration purement hors ligne (aucun Wi-Fi, aucune clé API Gemini/OpenAI)
    VoiceKitConfig vConfig;
    vConfig.apiKey = "";
    vConfig.wifiMode = WiFiMode::EXTERNAL_WIFI;
    vConfig.wakeWordPhrase = "Bonjour ESP";
    vConfig.wakeWordAutoResume = false; // Reprise contrôlée explicite pour observer SUSPENDED -> ARMED
    vConfig.enableWakeWord = false;     // Initialisé ci-dessous après enregistrement des callbacks

    if (!voice.begin(vConfig)) {
        Serial.printf("[WAKE] ERREUR : Échec initialisation VoiceKit : %s\n", voice.getLastError().c_str());
        return;
    }

    Serial.println("[WAKE] CoreS3 ES7210 prêt (PCM 16000 Hz / 16-bit mono)");

    // Callbacks de la machine à états et de détection :
    // Exécutés exclusivement dans la boucle principale via voice.update() (jamais dans le callback audio critique)
    voice.onWakeWordStateChanged([](WakeWordState state) {
        printStateTransitionMessage(state);
        updateSimpleStatusScreen();
    });

    voice.onWakeWordDetected([](const String& word) {
        const WakeWordMetrics& m = voice.getWakeWordMetrics();
        // Garantie stricte : ne comptabiliser et n'afficher une détection qu'une seule fois par événement réel
        if (m.detectionCount == 0 || m.detectionCount == s_lastDisplayedDetectionIndex) {
            return;
        }
        s_lastDisplayedDetectionIndex = m.detectionCount;
        printWakeWordDetectionBanner(word);
        s_detectedSuspendStartMs = millis();
        s_pendingAutoResumeAfterDetection = true;
    });

    voice.onWakeWordError([](const String& err) {
        const WakeWordMetrics& m = voice.getWakeWordMetrics();
        Serial.printf("[WAKE] Erreur Wake Word (#%u) : %s\n", m.errorCount, err.c_str());
        updateSimpleStatusScreen();
    });

    // 1. Vérification explicite de la disponibilité du moteur officiel Espressif WakeNet
    if (voice.getWakeWordManager().isEngineAvailable(WakeWordEngineType::WAKENET)) {
        voice.setWakeWordProcessingBudgetUs(25000);
        voice.setWakeWordEngine(WakeWordEngineType::WAKENET);
        voice.enableWakeWord(true);
        printActiveEngineStatus();
    } else {
        Serial.println("[WAKE] Moteur WAKENET indisponible dans cet environnement -> Repli explicite sur le moteur TEST — SIMULATION (TEST)");
        voice.setWakeWordEngine(WakeWordEngineType::TEST);
        voice.enableWakeWord(true);
        printActiveEngineStatus();
    }

    Serial.println("[WAKE] Commandes série : 'bonjour esp' (déclencher test), 'r' (reprendre ARMED), 's' (suspendre), 'd' (désactiver), 'e' (activer), 'c' (cycles), 'w' (moteur WAKENET), 't' (moteur TEST), 'm' (métriques mémoire/CPU)");
    updateSimpleStatusScreen();
}

void loop() {
    CoreS3.update();
    voice.update();

    // Consommation non bloquante de l'événement (sans journalisation dupliquée ni double comptage)
    WakeWordEvent ev;
    if (voice.pollWakeWordEvent(ev)) {
        updateSimpleStatusScreen();
    }

    // Lecture des commandes sur le moniteur série
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        String lower = cmd;
        lower.toLowerCase();

        if (lower == "r" || lower == "resume") {
            s_pendingAutoResumeAfterDetection = false;
            voice.resumeWakeWord();
        } else if (lower == "s" || lower == "suspend") {
            s_pendingAutoResumeAfterDetection = false;
            voice.suspendWakeWord();
        } else if (lower == "d" || lower == "disable") {
            s_pendingAutoResumeAfterDetection = false;
            voice.disableWakeWord();
        } else if (lower == "e" || lower == "enable") {
            voice.enableWakeWord(true);
        } else if (lower == "w" || lower == "wakenet") {
            s_pendingAutoResumeAfterDetection = false;
            Serial.println("[WAKE] Basculement vers le moteur WAKENET (vérification de disponibilité réelle)...");
            voice.setWakeWordEngine(WakeWordEngineType::WAKENET);
            voice.enableWakeWord(true);
            printActiveEngineStatus();
        } else if (lower == "t" || lower == "test") {
            s_pendingAutoResumeAfterDetection = false;
            Serial.println("[WAKE] Basculement vers le moteur TEST — SIMULATION (TEST)...");
            voice.setWakeWordEngine(WakeWordEngineType::TEST);
            voice.enableWakeWord(true);
            printActiveEngineStatus();
        } else if (lower == "c" || lower == "cycle") {
            s_pendingAutoResumeAfterDetection = false;
            Serial.println("[WAKE] Exécution de 5 cycles d'activation / suspension / reprise / désactivation...");
            for (int i = 0; i < 5; ++i) {
                voice.disableWakeWord();
                voice.enableWakeWord(true);
                voice.suspendWakeWord();
                voice.resumeWakeWord();
            }
            printDiagnostics();
        } else if (lower == "m" || lower == "metrics") {
            printDiagnostics();
        } else if (!cmd.isEmpty()) {
            if (!voice.handleWakeWordSerialCommand(cmd)) {
                Serial.printf("[WAKE] Commande ignorée ('%s') : vérifiez que le moteur actif est TEST et que l'état est ARMED.\n", cmd.c_str());
            }
        }
    }

    // Interaction tactile simple sur M5Stack CoreS3 (sans LVGL)
    if (CoreS3.Touch.getCount() > 0 && (millis() - s_lastTouchMs > 400)) {
        auto pt = CoreS3.Touch.getDetail(0);
        if (pt.wasPressed()) {
            s_lastTouchMs = millis();
            if (pt.x < 160) {
                // Moitié gauche : déclencher la détection de test si ARMED et moteur TEST
                if (voice.getWakeWordState() == WakeWordState::ARMED && voice.isWakeWordSimulated()) {
                    voice.triggerTestWakeWord("Bonjour ESP");
                }
            } else {
                // Moitié droite : reprendre l'écoute (SUSPENDED -> ARMED)
                if (voice.getWakeWordState() == WakeWordState::SUSPENDED) {
                    s_pendingAutoResumeAfterDetection = false;
                    voice.resumeWakeWord();
                }
            }
        }
    }

    // Reprise automatique après affichage de la détection (2.5 s) pour permettre plusieurs essais successifs
    if (s_pendingAutoResumeAfterDetection) {
        if (voice.getWakeWordState() != WakeWordState::SUSPENDED) {
            s_pendingAutoResumeAfterDetection = false;
        } else if (millis() - s_detectedSuspendStartMs >= AUTO_RESUME_AFTER_DETECT_MS) {
            s_pendingAutoResumeAfterDetection = false;
            voice.resumeWakeWord();
        }
    }

    // Rafraîchissement fluide du VU-mètre RMS sur l'écran toutes les 120 ms (aucune sortie série dans loop)
    if (voice.getWakeWordState() == WakeWordState::ARMED && (millis() - s_lastRmsDrawMs >= 120)) {
        s_lastRmsDrawMs = millis();
        drawLiveRmsMeter();
    }

    delay(5);
}
