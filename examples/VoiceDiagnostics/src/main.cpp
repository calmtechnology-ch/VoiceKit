/**
 * @file main.cpp
 * @brief Exemple officiel VoiceDiagnostics — Outil de diagnostic et visualisation
 *        pour le moteur vocal VoiceKit v0.5.1 sur M5Stack CoreS3.
 * 
 * Cet exemple offre une interface tactile 320x240 M5Unified (sans LVGL)
 * structurée en 4 écrans de diagnostic :
 * - SYSTEM  : État VoiceKit, mode vocal, RAM libre, PSRAM libre, Uptime, infos ESP32
 * - AUDIO   : Niveau micro RMS, capture STT, restitution TTS, compteurs audio, test TTS
 * - NETWORK : Connexion Wi-Fi, IP, RSSI, session Gemini Live, reconnexions, erreurs
 * - EVENTS  : Historique horodaté des événements, transitions, synthèse, écoute, erreurs
 * 
 * Conforme aux contraintes VoiceKit :
 * - Utilisation exclusive de la façade `VoiceKit voice;` (aucun second moteur)
 * - Aucune tâche FreeRTOS supplémentaire ni traitement lourd dans les callbacks
 * - Rafraîchissement écran maîtrisé à environ 5 Hz (200 ms) sans clignotement
 * - Sortie série structurée [DIAG] et CSV facultatif avec limitation en streaming audio
 * - Données authentiques sans simulation, affichage "N/D" si indisponible
 * - Protection stricte des identifiants (aucun mot de passe ni clé API affichés)
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoiceKit.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

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

// =============================================================================
// Configuration de la journalisation de diagnostic
// =============================================================================
// Passer ENABLE_CSV_OUTPUT à 1 pour obtenir un flux CSV au lieu du format [DIAG]
#ifndef ENABLE_CSV_OUTPUT
#define ENABLE_CSV_OUTPUT 0
#endif

#define DIAG_LOG_INTERVAL_MS   1000
#define UI_REFRESH_INTERVAL_MS 200

// Instance unique de la façade VoiceKit
static VoiceKit voice;

// Navigation par onglets (4 écrans)
enum class Page : uint8_t {
    SYSTEM = 0,
    AUDIO,
    NETWORK,
    EVENTS
};
static Page s_currentPage = Page::SYSTEM;
static bool s_forceFullRedraw = true;

// Horodatages
static uint32_t s_lastDrawMs = 0;
static uint32_t s_lastLogMs  = 0;

// Compteurs et états opérationnels authentiques
static uint32_t s_errors          = 0;
static uint32_t s_reconnects      = 0;
static uint32_t s_speechStarts    = 0;
static uint32_t s_speechEnds      = 0;
static uint32_t s_listeningStarts = 0;
static uint32_t s_listeningEnds   = 0;
static uint32_t s_sttFinals       = 0;

static bool s_seenReady         = false;
static bool s_everConnected     = false;
static bool s_hasConvMetrics    = false;
static uint32_t s_lastLatencyMs = 0;
static uint32_t s_lastDroppedSamples = 0;

static String s_lastErrorStr        = "";
static String s_latestTranscriptStr = "";

// Palette de couleurs 16-bit RGB565 (M5Unified)
static constexpr uint16_t COLOR_BG       = 0x0841; // Fond général bleu-nuit sombre
static constexpr uint16_t COLOR_PANEL    = 0x18E3; // Fond des panneaux et cartes
static constexpr uint16_t COLOR_FG       = 0xFFFF; // Texte blanc
static constexpr uint16_t COLOR_ACCENT   = 0x2E9F; // Bleu cyan d'accentuation
static constexpr uint16_t COLOR_MUTED    = 0xAD55; // Texte secondaire atténué
static constexpr uint16_t COLOR_WARN     = 0xFD20; // Orange / Rouge d'alerte
static constexpr uint16_t COLOR_GREEN    = 0x07E0; // Vert validation
static constexpr uint16_t COLOR_BTN_TTS  = 0x1AB2; // Cyan foncé bouton TEST TTS
static constexpr uint16_t COLOR_BTN_STOP = 0xA2C3; // Rouge bouton STOP

// =============================================================================
// Historique circulaire des événements (zéro allocation dynamique)
// =============================================================================
struct EventRecord {
    uint32_t timestampMs;
    char text[46];
    uint16_t color;
};

static constexpr size_t MAX_EVENTS = 6;
static EventRecord s_eventLog[MAX_EVENTS];
static size_t s_eventHead  = 0;
static size_t s_eventTotal = 0;

static void addEvent(const char* msg, uint16_t color = COLOR_FG) {
    s_eventTotal++;
    EventRecord& rec = s_eventLog[s_eventHead];
    rec.timestampMs = millis();
    strncpy(rec.text, msg, sizeof(rec.text) - 1);
    rec.text[sizeof(rec.text) - 1] = '\0';
    rec.color = color;
    s_eventHead = (s_eventHead + 1) % MAX_EVENTS;

    Serial.printf("[EVENT] t=%lu %s\n", (unsigned long)rec.timestampMs, rec.text);
}

// =============================================================================
// Fonctions utilitaires d'affichage
// =============================================================================
static const char* stateName(VoiceState s) {
    switch (s) {
        case VoiceState::UNINITIALIZED:      return "UNINIT";
        case VoiceState::IDLE:               return "IDLE";
        case VoiceState::CONNECTING_WIFI:    return "WIFI";
        case VoiceState::CONNECTING_SERVICE: return "SERVICE";
        case VoiceState::READY:              return "READY";
        case VoiceState::SPEAKING:           return "SPEAKING";
        case VoiceState::LISTENING:          return "LISTENING";
        case VoiceState::TRANSCRIBING:       return "TRANSCRIBING";
        case VoiceState::DISCONNECTED:       return "DISCONNECTED";
        case VoiceState::ERROR_STATE:        return "ERROR";
        default:                             return "UNKNOWN";
    }
}

static const char* modeName(VoiceKitMode m) {
    switch (m) {
        case VoiceKitMode::IDLE:                 return "IDLE";
        case VoiceKitMode::CONTINUOUS_LISTENING: return "CONTINUOUS";
        case VoiceKitMode::CONVERSATION:         return "CONVERSATION";
        default:                                 return "UNKNOWN";
    }
}

static String kib(size_t bytes) {
    return String((unsigned long)(bytes / 1024)) + " KB";
}

static void textAt(int x, int y, const String& txt, uint16_t color = COLOR_FG) {
    M5.Display.setTextColor(color, COLOR_PANEL);
    M5.Display.setCursor(x, y);
    M5.Display.print(txt);
}

static void line(int y, const String& key, const String& value, uint16_t valueColor = COLOR_FG) {
    textAt(14, y, key, COLOR_MUTED);
    textAt(152, y, value, valueColor);
}

static void drawUI() {
    // 1. Redessin complet lors de l'initialisation ou d'un changement d'onglet
    if (s_forceFullRedraw) {
        M5.Display.fillScreen(COLOR_BG);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextSize(1);

        // En-tête supérieur
        M5.Display.setTextColor(COLOR_FG, COLOR_BG);
        M5.Display.setCursor(10, 6);
        M5.Display.print("VoiceDiagnostics  v0.5.1");

        // Barre d'onglets (SYS, AUDIO, NET, EVENTS)
        const char* tabs[4] = {"SYS", "AUDIO", "NET", "EVENTS"};
        for (int i = 0; i < 4; ++i) {
            int x = 6 + i * 78;
            bool active = (i == (int)s_currentPage);
            uint16_t tabColor = active ? COLOR_ACCENT : COLOR_PANEL;
            M5.Display.fillRoundRect(x, 23, 74, 25, 4, tabColor);
            M5.Display.setTextColor(COLOR_FG, tabColor);
            M5.Display.setCursor(x + 12, 32);
            M5.Display.print(tabs[i]);
        }

        // Boutons d'action inférieurs
        M5.Display.fillRoundRect(6, 194, 150, 40, 6, COLOR_BTN_TTS);
        M5.Display.fillRoundRect(164, 194, 150, 40, 6, COLOR_BTN_STOP);
        M5.Display.setTextColor(COLOR_FG, COLOR_BTN_TTS);
        M5.Display.setCursor(44, 209);
        M5.Display.print("TEST TTS");
        M5.Display.setTextColor(COLOR_FG, COLOR_BTN_STOP);
        M5.Display.setCursor(194, 209);
        M5.Display.print("STOP AUDIO");

        s_forceFullRedraw = false;
    }

    // 2. Indicateur de statut en haut à droite
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    const char* statusTxt = "WAIT  ";
    uint16_t statusCol = COLOR_WARN;

    if (voice.isReady()) {
        statusTxt = "ONLINE";
        statusCol = COLOR_GREEN;
    } else if (voice.isBusy()) {
        statusTxt = "BUSY  ";
        statusCol = COLOR_ACCENT;
    } else if (voice.getState() == VoiceState::CONNECTING_WIFI) {
        statusTxt = "WIFI  ";
        statusCol = COLOR_ACCENT;
    } else if (voice.getState() == VoiceState::CONNECTING_SERVICE) {
        statusTxt = "GEMINI";
        statusCol = COLOR_ACCENT;
    } else if (voice.getState() == VoiceState::ERROR_STATE) {
        statusTxt = "ERROR ";
        statusCol = COLOR_WARN;
    }
    M5.Display.setTextColor(statusCol, COLOR_BG);
    M5.Display.setCursor(240, 6);
    M5.Display.print(statusTxt);

    // 3. Rafraîchissement ciblé du panneau central (308x138 px) sans clignotement
    M5.Display.fillRoundRect(6, 52, 308, 138, 6, COLOR_PANEL);

    if (s_currentPage == Page::SYSTEM) {
        // --- ÉCRAN 1 : SYSTEM ---
        uint16_t stateCol = voice.isReady() ? COLOR_GREEN : (voice.getState() == VoiceState::ERROR_STATE ? COLOR_WARN : COLOR_FG);
        line(58,  "VoiceKit Etat",   String(stateName(voice.getState())), stateCol);
        line(76,  "Mode vocal",      String(modeName(voice.getMode())));
        line(94,  "RAM libre / min", kib(ESP.getFreeHeap()) + " / " + kib(ESP.getMinFreeHeap()));
        line(112, "PSRAM libre",     kib(ESP.getFreePsram()));
        line(130, "Uptime",          String((unsigned long)(millis() / 1000)) + " s");
        line(148, "ESP32 CPU",       String(ESP.getCpuFreqMHz()) + " MHz");
        line(166, "Puce / Flash",    String(ESP.getChipModel()) + " / " + kib(ESP.getFlashChipSize()));
    }
    else if (s_currentPage == Page::AUDIO) {
        // --- ÉCRAN 2 : AUDIO ---
        uint32_t micRms = voice.getEngine().getAudioLevel();
        line(58,  "Microphone RMS",    String(micRms));
        line(76,  "Capture STT",       voice.isListening() ? "EN COURS" : (voice.isTranscribing() ? "TRANSCRIPTION" : "INACTIF"), voice.isListening() ? COLOR_ACCENT : COLOR_FG);
        line(94,  "Restitution TTS",   voice.isSpeaking() ? "EN COURS" : (voice.isBusy() ? "OCCUPE" : "INACTIF"), voice.isSpeaking() ? COLOR_GREEN : COLOR_FG);
        line(112, "TTS debuts / fins", String(s_speechStarts) + " / " + String(s_speechEnds));
        line(130, "Micro debuts/fins", String(s_listeningStarts) + " / " + String(s_listeningEnds));
        line(148, "STT finales",       String(s_sttFinals));
        line(166, "Conv. lat. / perte", s_hasConvMetrics ? (String(s_lastLatencyMs) + " ms / " + String(s_lastDroppedSamples)) : "N/D");
    }
    else if (s_currentPage == Page::NETWORK) {
        // --- ÉCRAN 3 : NETWORK ---
        bool wifiOk = (WiFi.status() == WL_CONNECTED);
        line(58,  "Connexion Wi-Fi",   wifiOk ? "CONNECTE" : "DECONNECTE", wifiOk ? COLOR_GREEN : COLOR_WARN);
        line(76,  "Adresse IP",        wifiOk ? WiFi.localIP().toString() : "N/D");
        line(94,  "Signal RSSI",       wifiOk ? (String(WiFi.RSSI()) + " dBm") : "N/D");
        line(112, "Session Gemini",    voice.isReady() ? "PRETE (READY)" : (voice.getEngine().isConnecting() ? "CONNEXION..." : "NON PRETE"), voice.isReady() ? COLOR_GREEN : COLOR_WARN);
        line(130, "Reconnexions",      String(s_reconnects));
        line(148, "Erreurs observees", String(s_errors), s_errors > 0 ? COLOR_WARN : COLOR_FG);
        line(166, "Derniere erreur",   s_lastErrorStr.length() > 0 ? s_lastErrorStr.substring(0, 24) : "Aucune", s_lastErrorStr.length() > 0 ? COLOR_WARN : COLOR_FG);
    }
    else {
        // --- ÉCRAN 4 : EVENTS ---
        size_t count = (s_eventTotal < MAX_EVENTS) ? s_eventTotal : MAX_EVENTS;
        size_t start = (s_eventTotal < MAX_EVENTS) ? 0 : s_eventHead;
        for (size_t i = 0; i < count; ++i) {
            size_t idx = (start + i) % MAX_EVENTS;
            const auto& rec = s_eventLog[idx];
            char lineBuf[64];
            snprintf(lineBuf, sizeof(lineBuf), "[%lus] %s", (unsigned long)(rec.timestampMs / 1000), rec.text);
            textAt(14, 58 + i * 18, lineBuf, rec.color);
        }
        if (count == 0) {
            textAt(14, 58, "En attente d'evenements...", COLOR_MUTED);
        }
    }
}

// =============================================================================
// Journal de diagnostic série
// =============================================================================
static void logDiagnostic() {
    uint32_t now = millis();

    // Éviter d'émettre des écritures série excessives pendant le streaming audio
    if (voice.isSpeaking() || voice.isListening()) {
        if (now - s_lastLogMs < 3000) {
            return;
        }
    }
    s_lastLogMs = now;

#if ENABLE_CSV_OUTPUT
    Serial.printf("[DIAG_CSV] %lu,%s,%s,%s,%u,%u,%u,%d,%u,%u\n",
        (unsigned long)now,
        stateName(voice.getState()),
        modeName(voice.getMode()),
        WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED",
        (unsigned)ESP.getFreeHeap(),
        (unsigned)ESP.getFreePsram(),
        (unsigned)voice.getEngine().getAudioLevel(),
        (voice.isSpeaking() || voice.isBusy()) ? 1 : 0,
        (unsigned)s_errors,
        (unsigned)s_reconnects);
#else
    // Format série structuré conforme aux spécifications :
    // [DIAG] Voice=READY [DIAG] Mode=IDLE [DIAG] WiFi=CONNECTED [DIAG] RAM_FREE=152320 [DIAG] PSRAM_FREE=7845120 [DIAG] MIC_RMS=215 [DIAG] TTS_BUSY=0
    Serial.printf("[DIAG] Voice=%s [DIAG] Mode=%s [DIAG] WiFi=%s [DIAG] RAM_FREE=%u [DIAG] PSRAM_FREE=%u [DIAG] MIC_RMS=%u [DIAG] TTS_BUSY=%d\n",
        stateName(voice.getState()),
        modeName(voice.getMode()),
        WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED",
        (unsigned)ESP.getFreeHeap(),
        (unsigned)ESP.getFreePsram(),
        (unsigned)voice.getEngine().getAudioLevel(),
        (voice.isSpeaking() || voice.isBusy()) ? 1 : 0);
#endif
}

// =============================================================================
// Setup et boucle principale
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println("\n========================================================");
    Serial.println("  VoiceKit v0.5.1 — Demonstration VoiceDiagnostics      ");
    Serial.println("  Outil de diagnostic vocal & systeme pour M5 CoreS3     ");
    Serial.println("========================================================");

    // Initialisation matérielle M5Unified
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = true;
    M5.begin(cfg);
    M5.Display.setRotation(1);

    // Enregistrement des callbacks de surveillance VoiceKit
    voice.onReady([]() {
        if (s_everConnected && !s_seenReady) {
            s_reconnects++;
        }
        s_everConnected = true;
        s_seenReady = true;
        addEvent("Session Gemini READY", COLOR_GREEN);
    });

    voice.onStateChanged([](VoiceState s) {
        if (s == VoiceState::DISCONNECTED || s == VoiceState::ERROR_STATE) {
            s_seenReady = false;
        }
        char buf[40];
        snprintf(buf, sizeof(buf), "Etat: %s", stateName(s));
        addEvent(buf, (s == VoiceState::ERROR_STATE) ? COLOR_WARN : COLOR_FG);
    });

    voice.onError([](const String& err) {
        s_errors++;
        s_lastErrorStr = err;
        char buf[48];
        snprintf(buf, sizeof(buf), "ERR: %s", err.c_str());
        addEvent(buf, COLOR_WARN);
    });

    voice.onSpeechStarted([]() {
        s_speechStarts++;
        addEvent("TTS: Debut restitution", COLOR_GREEN);
    });

    voice.onSpeechFinished([]() {
        s_speechEnds++;
        addEvent("TTS: Fin restitution", COLOR_FG);
    });

    voice.onListeningStarted([]() {
        s_listeningStarts++;
        addEvent("STT: Debut acquisition micro", COLOR_ACCENT);
    });

    voice.onListeningFinished([]() {
        s_listeningEnds++;
        addEvent("STT: Fin acquisition micro", COLOR_FG);
    });

    voice.onTranscription([](const String& transcript, bool isFinal) {
        if (isFinal) {
            s_sttFinals++;
            s_latestTranscriptStr = transcript;
            char buf[48];
            snprintf(buf, sizeof(buf), "STT: %s", transcript.c_str());
            addEvent(buf, COLOR_FG);
        }
    });

    voice.onConversationTurnMetrics([](const ConversationMetrics& m) {
        s_hasConvMetrics = true;
        s_lastLatencyMs = m.firstAudioLatencyMs;
        s_lastDroppedSamples = m.droppedSamples;
        char buf[48];
        snprintf(buf, sizeof(buf), "Conv: %lums lat, %ludrop", (unsigned long)m.firstAudioLatencyMs, (unsigned long)m.droppedSamples);
        addEvent(buf, COLOR_FG);
    });

    // Configuration de la façade VoiceKit
    VoiceKitConfig config;
    config.apiKey       = GEMINI_KEY;
    config.wifiMode     = WiFiMode::MANAGED_WIFI;
    config.wifiSSID     = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    config.voice        = "Aoede";
    config.language     = "fr-FR";
    config.volume       = 65;

    addEvent("Initialisation VoiceKit...", COLOR_MUTED);

    if (!voice.begin(config)) {
        s_errors++;
        s_lastErrorStr = voice.getLastError();
        addEvent("Echec voice.begin", COLOR_WARN);
    }

#if ENABLE_CSV_OUTPUT
    Serial.println("[DIAG_CSV_HEADER] ms,state,mode,wifi,ram_free,psram_free,mic_rms,tts_busy,errors,reconnects");
#endif

    drawUI();
}

void loop() {
    M5.update();
    voice.update();

    // Gestion des appuis tactiles M5Unified
    if (M5.Touch.getCount()) {
        auto t = M5.Touch.getDetail();
        if (t.wasPressed()) {
            // Sélection des onglets supérieurs (y in 23..48)
            if (t.y >= 23 && t.y <= 48) {
                if (t.x >= 6 && t.x < 80)        s_currentPage = Page::SYSTEM;
                else if (t.x >= 80 && t.x < 158) s_currentPage = Page::AUDIO;
                else if (t.x >= 158 && t.x < 236)s_currentPage = Page::NETWORK;
                else if (t.x >= 236 && t.x < 314)s_currentPage = Page::EVENTS;
                s_forceFullRedraw = true;
            }
            // Boutons d'action inférieurs (y in 194..234)
            else if (t.y >= 194 && t.y <= 234) {
                if (t.x < 160) {
                    // TEST TTS : Prononciation d'une phrase française authentique
                    if (voice.isReady() && !voice.isBusy()) {
                        if (!voice.say("Bonjour. Ceci est un test de diagnostic vocal VoiceKit sur Core S trois.")) {
                            addEvent("TTS refuse (say echec)", COLOR_WARN);
                        } else {
                            addEvent("Test TTS lance", COLOR_ACCENT);
                        }
                    } else {
                        addEvent("Test TTS indisponible (moteur occupe)", COLOR_WARN);
                    }
                } else {
                    // STOP AUDIO
                    voice.stop();
                    addEvent("Arret audio (voice.stop)", COLOR_WARN);
                }
            }
            drawUI();
        }
    }

    const uint32_t now = millis();

    // Rafraîchissement affichage limité à 5 Hz (200 ms)
    if (now - s_lastDrawMs >= UI_REFRESH_INTERVAL_MS) {
        s_lastDrawMs = now;
        drawUI();
    }

    // Journalisation de diagnostic série (1 Hz hors streaming)
    if (now - s_lastLogMs >= DIAG_LOG_INTERVAL_MS) {
        logDiagnostic();
    }
}
