/**
 * @file main.cpp
 * @brief Exemple autonome — VoiceKit v0.6.2 (Wake Word Foundation — M5Stack CoreS3)
 *
 * Fonctionnement :
 * - S'exécute sur M5Stack CoreS3 SANS écran LVGL et SANS connexion Wi-Fi ni Gemini/OpenAI.
 * - Valide la capture audio locale partagée (ES7210, PCM 16000 Hz / 16-bit mono) via VoiceKit.
 * - Valide la machine à états Wake Word :
 *     DISABLED -> INITIALIZING -> ARMED -> DETECTED -> SUSPENDED -> ARMED
 * - Vérifie la disponibilité du moteur officiel Espressif ESP-SR WakeNet10 (`wn10_fr_bonjouresp`).
 * - En l'absence du binaire ESP-SR sous Arduino-ESP32 2.0.17 (ESP-IDF 4.4.7), affiche
 *   explicitement l'indisponibilité de WakeNet10, puis permet de valider toute la chaîne
 *   audio et logicielle avec le moteur de test déterministe (`TEST`), en indiquant
 *   clairement que le déclenchement est simulé (et non une reconnaissance acoustique).
 *
 * Commandes Moniteur Série (115200 bauds) :
 * - "wake" ou "bonjour" ou "bonjour esp" : déclenche une détection de test simulée (si ARMED et moteur TEST)
 * - "r" ou "resume"                      : reprend l'écoute après fin de conversation (SUSPENDED -> ARMED)
 * - "s" ou "suspend"                     : suspend manuellement le détecteur (ARMED -> SUSPENDED)
 * - "d" ou "disable"                     : désactive le détecteur ( -> DISABLED)
 * - "e" ou "enable"                      : réactive le détecteur (DISABLED -> INITIALIZING -> ARMED)
 * - "w" ou "wakenet"                     : teste l'activation du moteur WAKENET (affiche l'état explicite ERROR sous IDF 4.4)
 * - "t" ou "test"                        : sélectionne et arme le moteur TEST (simulation déterministe)
 * - "m" ou "metrics"                     : affiche les diagnostics mémoire (RAM/PSRAM), CPU et pont audio
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
static constexpr uint32_t AUTO_RESUME_AFTER_DETECT_MS = 2500;

static void drawLiveRmsMeter() {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    CoreS3.Display.fillRect(10, 175, 300, 18, TFT_BLACK);
    CoreS3.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setCursor(10, 178);
    CoreS3.Display.printf("Mic RMS: %4u (Peak: %4u) | Det: %u", m.lastRms, m.peakRms, m.detectionCount);

    int barWidth = (int)(m.lastRms / 15);
    if (barWidth > 100) barWidth = 100;
    CoreS3.Display.drawRect(210, 177, 102, 10, TFT_DARKGREY);
    if (barWidth > 0) {
        uint16_t barColor = (m.lastRms > 400) ? TFT_GREEN : TFT_CYAN;
        CoreS3.Display.fillRect(211, 178, barWidth, 8, barColor);
    }
}

static void updateSimpleStatusScreen() {
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
        CoreS3.Display.println("Engine: TEST - SIMULATED (NON ACOUSTIC)");
    } else {
        CoreS3.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        CoreS3.Display.printf("Engine: %s (OFFICIEL ESPRESSIF)\n", voice.getWakeWordEngineName());
    }
    CoreS3.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    CoreS3.Display.printf("Model : %s (\"Bonjour ESP\")\n", voice.getWakeWordModelName());

    WakeWordState st = voice.getWakeWordState();
    uint16_t stateColor = TFT_LIGHTGREY;
    if (st == WakeWordState::ARMED) stateColor = TFT_GREEN;
    else if (st == WakeWordState::DETECTED) stateColor = TFT_YELLOW;
    else if (st == WakeWordState::SUSPENDED) stateColor = TFT_ORANGE;
    else if (st == WakeWordState::ERROR) stateColor = TFT_RED;

    CoreS3.Display.setTextColor(stateColor, TFT_BLACK);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setCursor(10, 105);
    CoreS3.Display.printf("State: %s\n", wakeWordStateToString(st));

    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setCursor(10, 138);
    if (!voice.getLastDetectedWakeWord().isEmpty()) {
        CoreS3.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        CoreS3.Display.printf(">>> DETECTE : %s %s <<<\n",
                              voice.getLastDetectedWakeWord().c_str(),
                              voice.isWakeWordSimulated() ? "[SIMULATED]" : "[ACOUSTIC]");
    } else if (st == WakeWordState::ARMED && !voice.isWakeWordSimulated()) {
        CoreS3.Display.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
        CoreS3.Display.println("Dites : \"Bonjour ESP\" pres du microphone...");
    }
    if (voice.isWakeWordError()) {
        CoreS3.Display.setTextColor(TFT_RED, TFT_BLACK);
        CoreS3.Display.printf("Error: %s\n", voice.getWakeWordLastError().c_str());
    }

    drawLiveRmsMeter();

    CoreS3.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    CoreS3.Display.setCursor(10, 205);
    CoreS3.Display.println("Touch LEFT : Trigger TEST 'Bonjour ESP'");
    CoreS3.Display.println("Touch RIGHT: Resume ARMED immediately");
}

static void printDiagnostics() {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    Serial.println("------------------------------------------------------------");
    Serial.printf("[WAKE][DIAG] Enabled=%s | CompiledSupport=%s | ModelAvail=%s | EngineAvail=%s | EngineInit=%s\n",
                  voice.isWakeWordEnabled() ? "YES" : "NO",
                  voice.isWakeWordEngineCompiledSupportAvailable() ? "YES" : "NO",
                  voice.isWakeWordEngineModelAvailable() ? "YES" : "NO",
                  voice.isWakeWordEngineAvailable() ? "YES" : "NO",
                  voice.isWakeWordEngineInitialized() ? "YES" : "NO");
    Serial.printf("[WAKE][DIAG] Listening=%s | RealRecognition=%s | Simulated=%s\n",
                  voice.isWakeWordListening() ? "YES" : "NO",
                  voice.isWakeWordRealRecognitionActive() ? "YES" : "NO",
                  voice.isWakeWordSimulated() ? "YES" : "NO");
    Serial.printf("[WAKE][DIAG] State=%s | Engine=%s | Model=%s\n",
                  wakeWordStateToString(voice.getWakeWordState()),
                  voice.isWakeWordSimulated() ? "TEST — SIMULATED" : voice.getWakeWordEngineName(),
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
    Serial.printf("[WAKE][DIAG] RMS last=%u | peak=%u | Detections=%u (simulated=%u) | Transitions=%u | CaptureStopFailures=%u | Errors=%u\n",
                  m.lastRms, m.peakRms,
                  m.detectionCount, m.simulatedDetectionCount, m.stateTransitionCount,
                  m.captureStopFailures, m.errorCount);
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
        Serial.printf("[WAKE] ERROR: VoiceKit begin failed: %s\n", voice.getLastError().c_str());
        return;
    }

    Serial.println("[WAKE] CoreS3 ES7210 ready");
    Serial.println("[WAKE] PCM 16000 Hz / 16-bit");

    // Callbacks de la machine à états et de détection (exécutés dans voice.update())
    voice.onWakeWordStateChanged([](WakeWordState state) {
        Serial.printf("[WAKE] State: %s\n", wakeWordStateToString(state));
        updateSimpleStatusScreen();
    });

    voice.onWakeWordDetected([](const String& word) {
        Serial.printf("[WAKE] Detection: %s\n", word.c_str());
        if (voice.isWakeWordSimulated()) {
            Serial.println("[WAKE] Notice: Detection SIMULEE (moteur TEST - aucune reconnaissance vocale acoustique)");
        } else {
            Serial.println("[WAKE] Detection acoustique officielle Espressif WakeNet reussie ! Reprise automatique dans 2.5 s...");
        }
        s_detectedSuspendStartMs = millis();
        s_pendingAutoResumeAfterDetection = true;
    });

    voice.onWakeWordError([](const String& err) {
        Serial.printf("[WAKE] Error: %s\n", err.c_str());
        updateSimpleStatusScreen();
    });

    // 1. Vérification explicite de la disponibilité du moteur officiel Espressif WakeNet
    if (voice.getWakeWordManager().isEngineAvailable(WakeWordEngineType::WAKENET)) {
        voice.setWakeWordProcessingBudgetUs(25000);
        voice.setWakeWordEngine(WakeWordEngineType::WAKENET);
        voice.enableWakeWord(true);
        Serial.printf("[WAKE] Engine: WAKENET (%s) — Reconnaissance acoustique officielle active !\n",
                      voice.getWakeWordModelName());
    } else {
        Serial.println("[WAKE] WAKENET status: UNAVAILABLE -> Fallback to TEST — SIMULATED");
        voice.setWakeWordEngine(WakeWordEngineType::TEST);
        voice.enableWakeWord(true);
    }

    Serial.println("[WAKE] Commandes serie : 'bonjour esp' (declencher test), 'r' (reprendre ARMED), 's' (suspendre), 'd' (desactiver), 'e' (activer), 'c' (cycles activation/desactivation), 'w' (tester WAKENET), 't' (moteur TEST), 'm' (diagnostics)");
    updateSimpleStatusScreen();
}

void loop() {
    CoreS3.update();
    voice.update();

    // Exemple de consommation non bloquante compatible boucle LVGL (CoreS3-VoiceUI-Demo)
    WakeWordEvent ev;
    if (voice.pollWakeWordEvent(ev)) {
        // Dans CoreS3-VoiceUI-Demo, la boucle LVGL utilisera ici :
        // setVoiceState(VoiceState::LISTENING);
        // setTranscript("Bonjour ESP détecté");
        updateSimpleStatusScreen();
    }

    // Lecture des commandes sur le moniteur série
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        String lower = cmd;
        lower.toLowerCase();

        if (lower == "r" || lower == "resume") {
            voice.resumeWakeWord();
        } else if (lower == "s" || lower == "suspend") {
            voice.suspendWakeWord();
        } else if (lower == "d" || lower == "disable") {
            voice.disableWakeWord();
        } else if (lower == "e" || lower == "enable") {
            voice.enableWakeWord(true);
        } else if (lower == "w" || lower == "wakenet") {
            Serial.println("[WAKE] Switching engine to WAKENET (explicit availability check)...");
            voice.setWakeWordEngine(WakeWordEngineType::WAKENET);
            voice.enableWakeWord(true);
        } else if (lower == "t" || lower == "test") {
            Serial.println("[WAKE] Engine: TEST — SIMULATED");
            voice.setWakeWordEngine(WakeWordEngineType::TEST);
            voice.enableWakeWord(true);
        } else if (lower == "c" || lower == "cycle") {
            Serial.println("[WAKE] Execution de 5 cycles d'activation / suspension / reprise / desactivation...");
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
                Serial.printf("[WAKE] Commande ignoree ('%s') : verifiez que le moteur est TEST et l'etat ARMED.\n", cmd.c_str());
            }
        }
    }

    // Interaction tactile simple sur M5Stack CoreS3 (sans LVGL)
    if (CoreS3.Touch.getCount() > 0 && (millis() - s_lastTouchMs > 400)) {
        auto pt = CoreS3.Touch.getDetail(0);
        if (pt.wasPressed()) {
            s_lastTouchMs = millis();
            if (pt.x < 160) {
                // Moitié gauche : déclencher la détection de test si ARMED
                if (voice.getWakeWordState() == WakeWordState::ARMED && voice.isWakeWordSimulated()) {
                    voice.triggerTestWakeWord("Bonjour ESP");
                }
            } else {
                // Moitié droite : reprendre l'écoute (SUSPENDED -> ARMED)
                if (voice.getWakeWordState() == WakeWordState::SUSPENDED) {
                    voice.resumeWakeWord();
                }
            }
        }
    }

    // Reprise automatique après affichage de la détection (2.5 s) pour permettre plusieurs essais vocaux successifs
    if (s_pendingAutoResumeAfterDetection) {
        if (voice.getWakeWordState() != WakeWordState::SUSPENDED) {
            s_pendingAutoResumeAfterDetection = false;
        } else if (millis() - s_detectedSuspendStartMs >= AUTO_RESUME_AFTER_DETECT_MS) {
            s_pendingAutoResumeAfterDetection = false;
            voice.resumeWakeWord();
        }
    }

    // Rafraîchissement fluide du VU-mètre RMS microphone toutes les 120 ms
    if (voice.getWakeWordState() == WakeWordState::ARMED && (millis() - s_lastRmsDrawMs >= 120)) {
        s_lastRmsDrawMs = millis();
        drawLiveRmsMeter();
    }

    delay(5);
}

