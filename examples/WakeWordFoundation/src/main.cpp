/**
 * @file main.cpp
 * @brief Exemple autonome — VoiceKit v0.6.5.2 (Wake Word Foundation — M5Stack CoreS3)
 *
 * Fonctionnement :
 * - S'exécute sur M5Stack CoreS3 SANS écran LVGL et SANS connexion Wi-Fi ni Gemini/OpenAI.
 * - Valide la capture audio locale partagée (ES7210, PCM 16000 Hz / 16-bit mono) via VoiceKit.
 * - Affiche sur le moniteur série ET sur l'écran M5Stack CoreS3 (M5GFX sans LVGL) la carte
 *   visuelle v0.6.5.2 mettant immédiatement en évidence chaque détection de « Bonjour ESP » :
 *     VOICEKIT — WAKE WORD                                    v0.6.5.2
 *     ────────────────────────────────────────────────────────────────
 *     🎙 ARMED — En écoute...
 *     ╭──────────────────────────────────────────────────────────────╮
 *     │  ✓ MOT DE RÉVEIL DÉTECTÉ !          [DETECTED — Mot reconnu] │
 *     │  « Bonjour ESP »                                             │
 *     │  Détection réussie — événement #1                            │
 *     ╰──────────────────────────────────────────────────────────────╯
 *     ⏸ SUSPENDED — Suspension temporaire (Détection suspendue)
 *     ✓ ARMED — Prêt pour un nouveau réveil (Écoute réactivée)
 *     ────────────────────────────────────────────────────────────────
 *     Détections : 1 | Réarmements : 1 | Erreurs : 0
 * - Toutes les impressions série sont produites exclusivement dans la boucle principale
 *   (jamais dans le callback audio critique FreeRTOS).
 * - Aucun journal répétitif n'est émis à chaque passage de loop().
 */

#include <M5CoreS3.h>
#include <VoiceKit.h>

VoiceKit voice;
static uint32_t s_lastTouchMs = 0;
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
            return (m.rearmCount == 0)
                ? "ARMED — En écoute..."
                : "ARMED — Prêt pour un nouveau réveil (Écoute réactivée)";
        case WakeWordState::DETECTED:
            return "DETECTED — Mot reconnu";
        case WakeWordState::SUSPENDED:
            return "SUSPENDED — Suspension temporaire (Détection suspendue)";
        case WakeWordState::ERROR:
            return "ERROR — Erreur du détecteur";
        case WakeWordState::DISABLED:
        default:
            return "DISABLED — Détecteur désactivé";
    }
}

/**
 * @brief Rendu léger de texte français UTF-8 avec DejaVu (sans embarquer 1.3 Mo de police CJK).
 * Dessine les accents français (é, è, ê, É, Ê), les chevrons « » et le tiret cadratin —.
 */
static void drawFrenchText(int x, int y, const char* utf8, const lgfx::GFXfont* font, uint16_t fg, uint16_t bg) {
    CoreS3.Display.setFont(font);
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setTextColor(fg, bg);

    const bool isLarge = (font == &fonts::DejaVu18);
    const int fontH = isLarge ? 18 : 12;
    int cx = x;
    char chBuf[2] = {0, 0};

    const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8);
    while (*p) {
        if (*p < 0x80) {
            chBuf[0] = static_cast<char>(*p++);
            CoreS3.Display.drawString(chBuf, cx, y);
            cx += CoreS3.Display.textWidth(chBuf);
        } else if (p[0] == 0xC3 && p[1] != 0) {
            uint8_t c2 = p[1];
            p += 2;
            bool isUpper = (c2 == 0x89 || c2 == 0x88 || c2 == 0x8A);
            chBuf[0] = isUpper ? 'E' : 'e';
            CoreS3.Display.drawString(chBuf, cx, y);
            int w = CoreS3.Display.textWidth(chBuf);
            int ax = cx + (w / 2);
            int ay = y + (isUpper ? 0 : (isLarge ? 3 : 2));
            if (c2 == 0xA9 || c2 == 0x89) {
                // Accent aigu (é / É)
                CoreS3.Display.drawLine(ax - 1, ay, ax + 2, ay - 2, fg);
            } else if (c2 == 0xA8 || c2 == 0x88) {
                // Accent grave (è / È)
                CoreS3.Display.drawLine(ax + 1, ay, ax - 2, ay - 2, fg);
            } else if (c2 == 0xAA || c2 == 0x8A) {
                // Accent circonflexe (ê / Ê)
                CoreS3.Display.drawLine(ax - 2, ay, ax, ay - 2, fg);
                CoreS3.Display.drawLine(ax, ay - 2, ax + 2, ay, fg);
            }
            cx += w;
        } else if (p[0] == 0xC2 && (p[1] == 0xAB || p[1] == 0xBB)) {
            bool isLeft = (p[1] == 0xAB);
            p += 2;
            int cy = y + (fontH / 2) + 1;
            int d = isLarge ? 4 : 3;
            for (int k = 0; k < 2; ++k) {
                int gx = cx + k * (d + 1);
                if (isLeft) {
                    CoreS3.Display.drawLine(gx + d, cy - d, gx, cy, fg);
                    CoreS3.Display.drawLine(gx, cy, gx + d, cy + d, fg);
                } else {
                    CoreS3.Display.drawLine(gx, cy - d, gx + d, cy, fg);
                    CoreS3.Display.drawLine(gx + d, cy, gx, cy + d, fg);
                }
            }
            cx += isLarge ? 14 : 10;
        } else if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0x94) {
            // Tiret cadratin (—)
            p += 3;
            int cy = y + (fontH / 2) + 1;
            int dashW = isLarge ? 12 : 9;
            CoreS3.Display.drawFastHLine(cx, cy, dashW, fg);
            cx += dashW + 2;
        } else {
            ++p;
        }
    }
}

/**
 * @brief Dessine la carte visuelle v0.6.2.1 sur l'écran 320x240 du M5Stack CoreS3 (M5GFX pur, sans LVGL).
 */
static void updateWakeWordCardScreen() {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    const WakeWordState st = voice.getWakeWordState();
    const bool hasDetection = (m.detectionCount > 0) || (st == WakeWordState::DETECTED);
    const bool isSimulated = voice.isWakeWordSimulated();

    const uint16_t colOuterBg     = CoreS3.Display.color565(7, 10, 16);
    const uint16_t colCardBg      = CoreS3.Display.color565(13, 18, 30);
    const uint16_t colDivider     = CoreS3.Display.color565(35, 45, 63);
    const uint16_t colHeader      = CoreS3.Display.color565(148, 163, 184);
    const uint16_t colBadgeBg     = CoreS3.Display.color565(8, 60, 38);
    const uint16_t colBadgeBorder = CoreS3.Display.color565(16, 112, 70);
    const uint16_t colBadgeText   = CoreS3.Display.color565(52, 211, 153);
    const uint16_t colBlue        = CoreS3.Display.color565(96, 165, 250);
    const uint16_t colBoxBg       = hasDetection
                                      ? CoreS3.Display.color565(10, 77, 44)
                                      : CoreS3.Display.color565(18, 28, 45);
    const uint16_t colBoxBorder   = hasDetection
                                      ? CoreS3.Display.color565(22, 130, 65)
                                      : CoreS3.Display.color565(45, 60, 85);
    const uint16_t colBoxTitle    = CoreS3.Display.color565(226, 232, 240);
    const uint16_t colBoxCheck    = CoreS3.Display.color565(74, 222, 128);
    const uint16_t colBoxSub      = hasDetection
                                      ? CoreS3.Display.color565(110, 231, 183)
                                      : CoreS3.Display.color565(148, 163, 184);
    const uint16_t colAmber       = (st == WakeWordState::SUSPENDED || hasDetection)
                                      ? CoreS3.Display.color565(251, 191, 36)
                                      : CoreS3.Display.color565(90, 75, 35);
    const uint16_t colRearmIcon   = (m.rearmCount > 0 && st == WakeWordState::ARMED)
                                      ? CoreS3.Display.color565(52, 211, 153)
                                      : CoreS3.Display.color565(60, 90, 80);
    const uint16_t colRearmText   = (m.rearmCount > 0 && st == WakeWordState::ARMED)
                                      ? CoreS3.Display.color565(203, 213, 225)
                                      : CoreS3.Display.color565(90, 102, 120);
    const uint16_t colFooter      = CoreS3.Display.color565(120, 134, 155);

    CoreS3.Display.startWrite();
    CoreS3.Display.fillScreen(colOuterBg);

    // Carte principale arrondie
    CoreS3.Display.fillRoundRect(4, 4, 312, 232, 10, colCardBg);
    CoreS3.Display.drawRoundRect(4, 4, 312, 232, 10, colDivider);

    // 1. En-tête : VOICEKIT — WAKE WORD + badge v0.6.2.1
    drawFrenchText(16, 13, "VOICEKIT — WAKE WORD", &fonts::DejaVu12, colHeader, colCardBg);

    CoreS3.Display.fillRoundRect(246, 9, 58, 18, 9, colBadgeBg);
    CoreS3.Display.drawRoundRect(246, 9, 58, 18, 9, colBadgeBorder);
    drawFrenchText(253, 14, VOICEKIT_VERSION, &fonts::DejaVu9, colBadgeText, colBadgeBg);

    CoreS3.Display.drawFastHLine(16, 33, 288, colDivider);

    // 2. Ligne d'écoute initiale : icône micro bleue + ARMED — En écoute...
    CoreS3.Display.fillRoundRect(20, 41, 5, 8, 2, colBlue);
    CoreS3.Display.drawFastVLine(18, 45, 4, colBlue);
    CoreS3.Display.drawFastVLine(26, 45, 4, colBlue);
    CoreS3.Display.drawFastHLine(19, 50, 7, colBlue);
    CoreS3.Display.drawFastVLine(22, 51, 3, colBlue);
    CoreS3.Display.drawFastHLine(19, 54, 7, colBlue);

    if (st == WakeWordState::ERROR) {
        drawFrenchText(34, 42, "ERROR — Erreur du détecteur", &fonts::DejaVu12, TFT_RED, colCardBg);
    } else if (st == WakeWordState::DISABLED) {
        drawFrenchText(34, 42, "DISABLED — Détecteur désactivé", &fonts::DejaVu12, colHeader, colCardBg);
    } else {
        drawFrenchText(34, 42, "ARMED — En écoute...", &fonts::DejaVu12, colBlue, colCardBg);
    }

    // 3. Encadré vert de détection : MOT DE RÉVEIL DÉTECTÉ ! / « Bonjour ESP » / Détection réussie — événement #N
    CoreS3.Display.fillRoundRect(16, 61, 288, 88, 8, colBoxBg);
    CoreS3.Display.drawRoundRect(16, 61, 288, 88, 8, colBoxBorder);

    // Icône coche dans un cercle
    CoreS3.Display.drawCircle(30, 76, 6, colBoxCheck);
    CoreS3.Display.drawLine(27, 76, 29, 78, colBoxCheck);
    CoreS3.Display.drawLine(29, 78, 33, 73, colBoxCheck);

    if (hasDetection) {
        drawFrenchText(42, 70, "MOT DE RÉVEIL DÉTECTÉ !", &fonts::DejaVu12, colBoxTitle, colBoxBg);
    } else {
        drawFrenchText(42, 70, "EN ÉCOUTE DU MOT DE RÉVEIL", &fonts::DejaVu12, colBoxTitle, colBoxBg);
    }

    const String phrase = voice.getLastDetectedWakeWord().isEmpty()
        ? String("Bonjour ESP")
        : voice.getLastDetectedWakeWord();
    char phraseLine[64];
    snprintf(phraseLine, sizeof(phraseLine), "« %s »", phrase.c_str());
    drawFrenchText(26, 95, phraseLine, &fonts::DejaVu18, TFT_WHITE, colBoxBg);

    char subLine[80];
    if (hasDetection) {
        if (isSimulated) {
            snprintf(subLine, sizeof(subLine), "Simulation TEST (non acoustique) — #%u", m.detectionCount);
        } else {
            snprintf(subLine, sizeof(subLine), "Détection réussie — événement #%u", m.detectionCount);
        }
    } else {
        snprintf(subLine, sizeof(subLine), "Moteur %s — %s",
                 voice.getWakeWordEngineName(),
                 getEngineRecognitionNature());
    }
    drawFrenchText(26, 126, subLine, &fonts::DejaVu12, colBoxSub, colBoxBg);

    // 4. Ligne SUSPENDED — Suspension temporaire
    CoreS3.Display.fillCircle(22, 166, 6, colAmber);
    CoreS3.Display.fillRect(20, 163, 1, 6, colCardBg);
    CoreS3.Display.fillRect(23, 163, 1, 6, colCardBg);
    drawFrenchText(34, 160, "SUSPENDED — Suspension temporaire", &fonts::DejaVu12, colAmber, colCardBg);

    // 5. Ligne ARMED — Prêt pour un nouveau réveil
    CoreS3.Display.drawCircle(22, 188, 6, colRearmIcon);
    CoreS3.Display.drawLine(19, 188, 21, 190, colRearmIcon);
    CoreS3.Display.drawLine(21, 190, 25, 185, colRearmIcon);
    drawFrenchText(34, 182, "ARMED — Prêt pour un nouveau réveil", &fonts::DejaVu12, colRearmText, colCardBg);

    // 6. Pied de page : Détections : X | Réarmements : Y | Erreurs : Z
    CoreS3.Display.drawFastHLine(16, 206, 288, colDivider);
    char footerLine[80];
    snprintf(footerLine, sizeof(footerLine), "Détections : %u | Réarmements : %u | Erreurs : %u",
             m.detectionCount, m.rearmCount, m.errorCount);
    drawFrenchText(16, 215, footerLine, &fonts::DejaVu12, colFooter, colCardBg);

    CoreS3.Display.endWrite();
}

static void printSerialHeader() {
    Serial.println();
    Serial.printf("VOICEKIT — WAKE WORD                                            %s\n", VOICEKIT_VERSION);
    Serial.printf("Moteur actif : %s (%s) — %s\n",
                  voice.getWakeWordEngineName(),
                  voice.getWakeWordModelName(),
                  getEngineRecognitionNature());
    if (voice.isWakeWordSimulated()) {
        Serial.println("Note : Le moteur TEST est une SIMULATION (TEST) et ne réalise aucune détection acoustique.");
    }
    Serial.println("────────────────────────────────────────────────────────────────────────");
}

static void printCountersFooter() {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    Serial.println("────────────────────────────────────────────────────────────────────────");
    Serial.printf("Détections : %u | Réarmements : %u | Erreurs : %u\n\n",
                  m.detectionCount, m.rearmCount, m.errorCount);
}

static void printStateTransitionMessage(WakeWordState state) {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    switch (state) {
        case WakeWordState::INITIALIZING:
            Serial.println("⏳ INITIALIZING — Initialisation en cours");
            break;
        case WakeWordState::ARMED:
            if (m.rearmCount == 0) {
                Serial.println();
                Serial.println("🎙 ARMED — En écoute...");
            } else {
                Serial.println("✓ ARMED — Prêt pour un nouveau réveil (Écoute réactivée)");
                printCountersFooter();
            }
            break;
        case WakeWordState::DETECTED:
            Serial.println("✓ DETECTED — Mot reconnu");
            break;
        case WakeWordState::SUSPENDED:
            Serial.println("⏸ SUSPENDED — Suspension temporaire (Détection suspendue)");
            break;
        case WakeWordState::ERROR:
            Serial.println("✖ ERROR — Erreur du détecteur");
            printCountersFooter();
            break;
        case WakeWordState::DISABLED:
        default:
            Serial.println("○ DISABLED — Détecteur désactivé");
            break;
    }
}

static void printWakeWordDetectionBanner(const String& word) {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    const String detectedPhrase = word.isEmpty() ? String("Bonjour ESP") : word;

    Serial.println();
    Serial.println("╭──────────────────────────────────────────────────────────────────────╮");
    Serial.println("│  ✓ MOT DE RÉVEIL DÉTECTÉ !                                           │");
    Serial.println("│                                                                      │");
    Serial.printf("│  « %s »                                                     │\n", detectedPhrase.c_str());
    Serial.println("│                                                                      │");
    if (voice.isWakeWordSimulated()) {
        Serial.printf("│  Déclenchement simulé (non acoustique) — événement #%-16u│\n", m.detectionCount);
    } else {
        Serial.printf("│  Détection réussie — événement #%-36u│\n", m.detectionCount);
    }
    Serial.printf("│  Moteur : %s — %s\n",
                  voice.getWakeWordEngineName(),
                  getEngineRecognitionNature());
    Serial.println("╰──────────────────────────────────────────────────────────────────────╯");
    Serial.println();
}

static void printDiagnostics() {
    const WakeWordMetrics& m = voice.getWakeWordMetrics();
    const uint32_t realDetections = (m.detectionCount >= m.simulatedDetectionCount)
        ? (m.detectionCount - m.simulatedDetectionCount)
        : 0u;

    Serial.println("────────────────────────────────────────────────────────────────────────");
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
    Serial.printf("[WAKE][DIAG] RMS last=%u | peak=%u | Detections=%u (real=%u, simulated=%u) | Rearms=%u | Errors=%u | Transitions=%u\n",
                  m.lastRms, m.peakRms,
                  m.detectionCount, realDetections, m.simulatedDetectionCount,
                  m.rearmCount, m.errorCount, m.stateTransitionCount);
    Serial.printf("Détections : %u | Réarmements : %u | Erreurs : %u\n",
                  m.detectionCount, m.rearmCount, m.errorCount);
    Serial.println("────────────────────────────────────────────────────────────────────────");
}

void setup() {
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    Serial.begin(115200);
    delay(300);

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

    // Callbacks de la machine à états et de détection :
    // Exécutés exclusivement dans la boucle principale via voice.update() (jamais dans le callback audio critique)
    voice.onWakeWordStateChanged([](WakeWordState state) {
        printStateTransitionMessage(state);
        updateWakeWordCardScreen();
    });

    voice.onWakeWordDetected([](const String& word) {
        const WakeWordMetrics& m = voice.getWakeWordMetrics();
        // Garantie stricte : ne comptabiliser et n'afficher une détection qu'une seule fois par événement réel
        if (m.detectionCount == 0 || m.detectionCount == s_lastDisplayedDetectionIndex) {
            return;
        }
        s_lastDisplayedDetectionIndex = m.detectionCount;
        printWakeWordDetectionBanner(word);
        updateWakeWordCardScreen();
        s_detectedSuspendStartMs = millis();
        s_pendingAutoResumeAfterDetection = true;
    });

    voice.onWakeWordError([](const String& err) {
        const WakeWordMetrics& m = voice.getWakeWordMetrics();
        Serial.printf("✖ Erreur Wake Word (#%u) : %s\n", m.errorCount, err.c_str());
        updateWakeWordCardScreen();
    });

    // 1. Vérification explicite de la disponibilité du moteur officiel Espressif WakeNet
    if (voice.getWakeWordManager().isEngineAvailable(WakeWordEngineType::WAKENET)) {
        voice.setWakeWordProcessingBudgetUs(25000);
        voice.setWakeWordEngine(WakeWordEngineType::WAKENET);
    } else {
        voice.setWakeWordEngine(WakeWordEngineType::TEST);
    }

    printSerialHeader();
    voice.enableWakeWord(true);
    updateWakeWordCardScreen();
}

void loop() {
    CoreS3.update();
    voice.update();

    // Consommation non bloquante de l'événement (sans journalisation dupliquée ni double comptage)
    WakeWordEvent ev;
    if (voice.pollWakeWordEvent(ev)) {
        updateWakeWordCardScreen();
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
            voice.setWakeWordEngine(WakeWordEngineType::WAKENET);
            printSerialHeader();
            voice.enableWakeWord(true);
        } else if (lower == "t" || lower == "test") {
            s_pendingAutoResumeAfterDetection = false;
            voice.setWakeWordEngine(WakeWordEngineType::TEST);
            printSerialHeader();
            voice.enableWakeWord(true);
        } else if (lower == "c" || lower == "cycle") {
            s_pendingAutoResumeAfterDetection = false;
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
                Serial.printf("Commande ignorée ('%s') : vérifiez que le moteur actif est TEST et que l'état est ARMED.\n", cmd.c_str());
            }
        }
    }

    // Interaction tactile simple sur M5Stack CoreS3 (sans LVGL)
    if (CoreS3.Touch.getCount() > 0 && (millis() - s_lastTouchMs > 400)) {
        auto pt = CoreS3.Touch.getDetail(0);
        if (pt.wasPressed()) {
            s_lastTouchMs = millis();
            if (pt.x < 160) {
                if (voice.getWakeWordState() == WakeWordState::ARMED && voice.isWakeWordSimulated()) {
                    voice.triggerTestWakeWord("Bonjour ESP");
                }
            } else {
                if (voice.getWakeWordState() == WakeWordState::SUSPENDED) {
                    s_pendingAutoResumeAfterDetection = false;
                    voice.resumeWakeWord();
                }
            }
        }
    }

    // Reprise automatique après affichage de la détection (2.5 s) pour enchainer les réveils
    if (s_pendingAutoResumeAfterDetection) {
        if (voice.getWakeWordState() != WakeWordState::SUSPENDED) {
            s_pendingAutoResumeAfterDetection = false;
        } else if (millis() - s_detectedSuspendStartMs >= AUTO_RESUME_AFTER_DETECT_MS) {
            s_pendingAutoResumeAfterDetection = false;
            voice.resumeWakeWord();
        }
    }

    delay(5);
}
