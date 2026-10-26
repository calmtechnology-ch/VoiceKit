/**
 * @file main.cpp
 * @brief Exemple VoiceSettingsDemo — Démonstrateur de configuration vocale et UTF-8
 *        pour M5Stack CoreS3 avec VoiceKit v0.5.3.1 et Google Gemini Live.
 * 
 * Fonctionnalités démontrées :
 * - Alternance successive entre plusieurs voix Gemini (Aoede, Charon, Fenrir, Kore, Puck).
 * - Prise en charge multilingue (français fr-FR, allemand de-DE, anglais en-US).
 * - Configuration des styles (naturel, professionnel, expressif, narratif) et tons (chaleureux, sérieux, joyeux, calme).
 * - Restitution intégrale de phrases accentuées françaises (ligatures œ, accents, cédilles, guillemets).
 * - Reconnexion de session sécurisée sans conflit pendant les changements de profil.
 * - Interface tactile fluide sur écran M5Stack CoreS3 (320x240) et commandes série ('N', 'R', 'S').
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>
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

struct VoiceScenario {
    const char* label;
    const char* voice;
    const char* language;
    VoiceStyle style;
    VoiceTone tone;
    const char* accent;
    const char* text;
};

static const VoiceScenario SCENARIOS[] = {
    {
        "1. Francais - Phrase obligatoire UTF-8",
        "Aoede",
        "fr-FR",
        VoiceStyle::NATURAL,
        VoiceTone::WARM,
        "parisien",
        "« À Noël, Élise prépare un café crème. L'été à Genève est agréable. François préfère un cœur léger. »"
    },
    {
        "2. Francais - Style Professionnel & Serieux",
        "Charon",
        "fr-FR",
        VoiceStyle::PROFESSIONAL,
        VoiceTone::SERIOUS,
        "",
        "« Le compte rendu de la séance plénière confirme une hausse des investissements de vingt-quatre pour cent. »"
    },
    {
        "3. Francais - Style Expressif & Enjoue",
        "Fenrir",
        "fr-FR",
        VoiceStyle::EXPRESSIVE,
        VoiceTone::CHEERFUL,
        "meridional",
        "« Formidable ! Quelle magnifique journée ensoleillée pour célébrer cette splendide réussite ! »"
    },
    {
        "4. Allemand - Style Calme & Naturel",
        "Kore",
        "de-DE",
        VoiceStyle::NATURAL,
        VoiceTone::CALM,
        "",
        "« Guten Tag! Das Wetter in Berlin ist heute wunderbar und die Vögel singen fröhlich im Garten. »"
    },
    {
        "5. Anglais - Style Narratif & Chaleureux",
        "Puck",
        "en-US",
        VoiceStyle::NARRATIVE,
        VoiceTone::WARM,
        "",
        "« Once upon a time, high in the misty mountains, an ancient clock began to strike twelve. »"
    }
};

static constexpr size_t NUM_SCENARIOS = sizeof(SCENARIOS) / sizeof(SCENARIOS[0]);
static size_t s_currentScenario = 0;

static VoxCoreVoice voice;
static String s_statusText = "INITIALISATION...";
static uint16_t s_statusColor = TFT_ORANGE;
static String s_errorMessage = "";
static bool s_needsRedraw = true;
static bool s_profileSwitchPending = false;

// Coordonnées du bouton tactile de navigation (bas d'écran)
static constexpr int BTN_X = 15;
static constexpr int BTN_Y = 175;
static constexpr int BTN_W = 290;
static constexpr int BTN_H = 55;

static void updateStatus(const String& txt, uint16_t color) {
    s_statusText = txt;
    s_statusColor = color;
    s_needsRedraw = true;
}

static void drawUI() {
    M5.Display.fillScreen(TFT_BLACK);

    // 1. En-tête : Titre & version
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString("VoiceKit v0.4.1 — VoiceSettingsDemo", 10, 8);

    // Badge d'état visuel en haut à droite
    M5.Display.fillRoundRect(200, 5, 110, 18, 4, s_statusColor);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(TFT_BLACK, s_statusColor);
    M5.Display.drawString(s_statusText, 255, 14);
    M5.Display.setTextDatum(TL_DATUM);

    const VoiceScenario& sc = SCENARIOS[s_currentScenario];

    // 2. Carte : Configuration vocale en cours
    M5.Display.fillRoundRect(10, 30, 300, 68, 6, M5.Display.color565(25, 30, 45));
    M5.Display.drawRoundRect(10, 30, 300, 68, 6, M5.Display.color565(70, 90, 140));

    M5.Display.setTextColor(TFT_CYAN, M5.Display.color565(25, 30, 45));
    M5.Display.drawString(sc.label, 18, 36);

    char buf1[80];
    snprintf(buf1, sizeof(buf1), "Voix: %-7s | Langue: %s", sc.voice, sc.language);
    M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(25, 30, 45));
    M5.Display.drawString(buf1, 18, 52);

    char buf2[90];
    snprintf(buf2, sizeof(buf2), "Style: %s | Ton: %s",
             voiceStyleToString(sc.style), voiceToneToString(sc.tone));
    M5.Display.drawString(buf2, 18, 66);

    if (sc.accent && strlen(sc.accent) > 0) {
        char buf3[60];
        snprintf(buf3, sizeof(buf3), "Accent: %s", sc.accent);
        M5.Display.setTextColor(TFT_YELLOW, M5.Display.color565(25, 30, 45));
        M5.Display.drawString(buf3, 18, 80);
    }

    // 3. Carte : Texte en cours de test
    M5.Display.fillRoundRect(10, 104, 300, 64, 6, M5.Display.color565(20, 20, 25));
    M5.Display.drawRoundRect(10, 104, 300, 64, 6, M5.Display.color565(50, 50, 60));

    M5.Display.setTextColor(TFT_LIGHTGRAY, M5.Display.color565(20, 20, 25));
    M5.Display.drawString("Texte prononce (mot a mot UTF-8) :", 16, 109);

    // Découpage sommaire pour affichage 2 lignes
    String t(sc.text);
    if (t.length() > 50) {
        String l1 = t.substring(0, 48);
        String l2 = t.substring(48);
        M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(20, 20, 25));
        M5.Display.drawString(l1, 16, 126);
        M5.Display.drawString(l2, 16, 142);
    } else {
        M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(20, 20, 25));
        M5.Display.drawString(t, 16, 132);
    }

    // Erreur éventuelle
    if (!s_errorMessage.isEmpty()) {
        M5.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5.Display.drawString(s_errorMessage, 16, 158);
    }

    // 4. Bouton tactile d'action
    uint16_t btnColor = voice.isSpeaking() ? TFT_MAGENTA : M5.Display.color565(30, 130, 60);
    M5.Display.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 10, btnColor);
    M5.Display.drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 10, TFT_WHITE);

    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, btnColor);
    M5.Display.drawString("PROFIL SUIVANT", BTN_X + (BTN_W / 2), BTN_Y + 20);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGRAY, btnColor);
    M5.Display.drawString("Appuyez pour tester (ou touche 'N')", BTN_X + (BTN_W / 2), BTN_Y + 42);
    M5.Display.setTextDatum(TL_DATUM);

    s_needsRedraw = false;
}

static void applyAndRunScenario(size_t index) {
    if (index >= NUM_SCENARIOS) return;
    s_currentScenario = index;
    const VoiceScenario& sc = SCENARIOS[s_currentScenario];

    Serial.printf("\n========================================================\n");
    Serial.printf("  Lancement du scenario [%u/%u] : %s\n", (unsigned)(s_currentScenario + 1), (unsigned)NUM_SCENARIOS, sc.label);
    Serial.printf("  Voix: %s | Langue: %s | Style: %s | Ton: %s\n",
                  sc.voice, sc.language, voiceStyleToString(sc.style), voiceToneToString(sc.tone));
    if (sc.accent && strlen(sc.accent) > 0) {
        Serial.printf("  Accent: %s\n", sc.accent);
    }
    Serial.printf("  Phrase : %s\n", sc.text);
    Serial.printf("========================================================\n");

    s_errorMessage = "";
    updateStatus("APPLICATION...", TFT_ORANGE);

    // Modification sécurisée du profil vocal
    bool ok = voice.setVoiceProfile(sc.voice, sc.language, sc.style, sc.tone, sc.accent ? sc.accent : "");
    if (!ok) {
        s_errorMessage = "Moteur occupe : modification differee";
        updateStatus("OCCUPE", TFT_RED);
        return;
    }

    s_profileSwitchPending = true;
    s_needsRedraw = true;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    delay(400);

    Serial.println("\n========================================================");
    Serial.println("  VoiceKit v0.4.1 — VoiceSettingsDemo (CoreS3)          ");
    Serial.println("  Test voix Gemini, BCP-47, Styles, Tons et UTF-8       ");
    Serial.println("========================================================");

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    drawUI();

    // Configuration de base VoiceKit v0.4.1
    VoiceKitConfig config;
    config.apiKey = GEMINI_KEY;
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    config.volume = 70;

    // Initialisation avec le premier scénario
    const VoiceScenario& sc0 = SCENARIOS[0];
    config.voice = sc0.voice;
    config.language = sc0.language;
    config.style = sc0.style;
    config.tone = sc0.tone;
    config.accent = sc0.accent;

    // Callbacks VoiceKit
    voice.onReady([]() {
        Serial.println("[VoiceSettingsDemo] Session prete et connectee !");
        updateStatus("PRET", TFT_GREEN);

        if (s_profileSwitchPending) {
            s_profileSwitchPending = false;
            const VoiceScenario& sc = SCENARIOS[s_currentScenario];
            Serial.printf("[VoiceSettingsDemo] Prononciation du scenario : \"%s\"\n", sc.text);
            voice.say(sc.text);
        }
    });

    voice.onSpeechStarted([]() {
        Serial.println("[VoiceSettingsDemo] Restitution sonore en cours (AW88298)");
        updateStatus("ENONCIATION", TFT_MAGENTA);
    });

    voice.onSpeechFinished([]() {
        Serial.println("[VoiceSettingsDemo] Restitution sonore achevee");
        updateStatus("PRET", TFT_GREEN);
    });

    voice.onError([](const String& err) {
        Serial.printf("[VoiceSettingsDemo] ERREUR : %s\n", err.c_str());
        s_errorMessage = err;
        updateStatus("ERREUR", TFT_RED);
    });

    voice.onStateChanged([](VoiceState st) {
        Serial.printf("[VoiceSettingsDemo] Etat VoiceKit : %s\n", voiceStateToString(st));
    });

    if (!voice.begin(config)) {
        Serial.println("[VoiceSettingsDemo] Echec initialisation VoiceKit !");
        updateStatus("ECHEC INIT", TFT_RED);
    }
}

void loop() {
    M5.update();
    voice.update();

    // Gestion du bouton tactile pour passer au scénario suivant
    auto touch = M5.Touch.getDetail();
    if (touch.wasPressed()) {
        if (touch.x >= BTN_X && touch.x <= (BTN_X + BTN_W) &&
            touch.y >= BTN_Y && touch.y <= (BTN_Y + BTN_H)) {
            size_t next = (s_currentScenario + 1) % NUM_SCENARIOS;
            applyAndRunScenario(next);
        }
    }

    // Commandes série interactives
    if (Serial.available()) {
        char cmd = Serial.read();
        if (cmd == 'n' || cmd == 'N' || cmd == ' ') {
            size_t next = (s_currentScenario + 1) % NUM_SCENARIOS;
            applyAndRunScenario(next);
        } else if (cmd == 'r' || cmd == 'R') {
            applyAndRunScenario(s_currentScenario);
        } else if (cmd == 's' || cmd == 'S') {
            voice.stop();
            updateStatus("STOPPE", TFT_YELLOW);
        }
    }

    if (s_needsRedraw) {
        drawUI();
    }

    delay(10);
}
