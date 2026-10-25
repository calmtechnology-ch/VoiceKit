/**
 * @file main.cpp
 * @brief Exemple MinimalVoiceKit — Démonstration de l'API simplifiée VoiceKit v0.5.0
 *        pour M5Stack CoreS3 avec Google Gemini Live.
 * 
 * Cet exemple illustre l'usage de la nouvelle classe façade `VoiceKit` permettant
 * d'utiliser simplement et successivement, avec une seule instance :
 * 1. TTS : Synthèse vocale de phrases (« say ») en PCM 24 kHz haute fluidité.
 * 2. STT Manuel : Dictée push-to-talk (« listen » / « stopListening ») en PCM 16 kHz.
 * 3. ContinuousSTT : Reconnaissance continue mains libres avec VAD intégré (« startContinuousListening »).
 * 4. Conversation : Dialogue vocal bidirectionnel complet avec Gemini Live (« startConversation »).
 * 
 * Interface : Écran tactile M5Stack CoreS3 (320 × 240) sans dépendance LVGL.
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoiceKit.h>
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

// Instance unique de la façade simplifiée VoiceKit v0.5.0
static VoiceKit voice;

// Modes de test dans l'interface
enum class DemoTab : uint8_t {
    TAB_TTS = 0,
    TAB_STT,
    TAB_CONTINUOUS,
    TAB_CONVERSATION
};

static DemoTab s_activeTab = DemoTab::TAB_TTS;
static bool s_needsRedraw = true;
static bool s_manualTouchActive = false;

// Variables d'état et textes
static String s_interimText = "";
static String s_finalText = "";
static String s_assistantText = "";
static String s_statusMessage = "Connexion Wi-Fi & Gemini Live...";
static String s_errorMessage = "";

// Phrase de démonstration TTS
static const char* TTS_DEMO_TEXT = "Bonjour ! VoiceKit version zero point cinq simplifie l API vocale sur CoreS3.";

static void drawUI();

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  VoiceKit v0.5.0 — Exemple MinimalVoiceKit             ");
    Serial.println("  Demonstration de la facade VoiceKit simplifiee        ");
    Serial.println("  M5Stack CoreS3 (ES7210 Mic + AW88298 Speaker)         ");
    Serial.println("========================================================");

    // 1. Initialisation matérielle CoreS3
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = true;
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    drawUI();

    // 2. Configuration unifiée VoiceKitConfig
    VoiceKitConfig config;
    config.apiKey = GEMINI_KEY;
    config.wifiMode = WiFiMode::MANAGED_WIFI;
    config.wifiSSID = WIFI_SSID;
    config.wifiPassword = WIFI_PASSWORD;
    config.volume = 70;
    config.voice = "Aoede";
    config.language = "fr-FR";
    config.style = VoiceStyle::NATURAL;
    config.tone = VoiceTone::NEUTRAL;

    // 3. Enregistrement des callbacks de la façade simplifiée
    voice.onReady([]() {
        Serial.println("[MinimalVoiceKit] Callback onReady : Moteur vocal connecte et PRET !");
        s_statusMessage = "Moteur connect\xc3\xa9"" et pr\xc3\xaat";
        s_needsRedraw = true;
    });

    voice.onError([](const String& err) {
        Serial.printf("[MinimalVoiceKit] Callback onError : %s\n", err.c_str());
        s_errorMessage = err;
        s_statusMessage = "Erreur survenue";
        s_needsRedraw = true;
    });

    voice.onSpeechStarted([]() {
        Serial.println("[MinimalVoiceKit] Callback onSpeechStarted : Restitution audio en cours");
        s_statusMessage = "Parole en cours (haut-parleur)...";
        s_needsRedraw = true;
    });

    voice.onSpeechFinished([]() {
        Serial.println("[MinimalVoiceKit] Callback onSpeechFinished : Restitution audio terminee");
        s_statusMessage = "Restitution audio termin\xc3\xa9""e";
        s_needsRedraw = true;
    });

    voice.onTranscription([](const String& text, bool isFinal) {
        if (isFinal) {
            Serial.printf(">>> [TRANSCRIPTION FINALE] %s\n", text.c_str());
            s_finalText = text;
            s_interimText = "";
            s_statusMessage = "Transcription finale re\xc3\xa7""ue";
        } else {
            Serial.printf("... [interim] %s\n", text.c_str());
            s_interimText = text;
        }
        s_needsRedraw = true;
    });

    voice.onAssistantText([](const String& text) {
        Serial.printf("[MinimalVoiceKit] Reponse Gemini : %s\n", text.c_str());
        s_assistantText = text;
        s_needsRedraw = true;
    });

    voice.onConversationFinished([]() {
        Serial.println("[MinimalVoiceKit] Callback onConversationFinished : Fin du tour");
        s_statusMessage = "Tour de conversation achev\xc3\xa9";
        s_needsRedraw = true;
    });

    // 4. Initialisation de la façade
    if (!voice.begin(config)) {
        Serial.printf("[MinimalVoiceKit][FATAL] Echec voice.begin() : %s\n", voice.getLastError().c_str());
        s_errorMessage = voice.getLastError();
        s_statusMessage = "Echec initialisation";
    }
}

static void drawUI() {
    M5.Display.fillScreen(TFT_BLACK);

    // 1. Barre supérieure : Titre et badge de statut
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_LIGHTGRAY, TFT_BLACK);
    M5.Display.drawString("VoiceKit v0.5.0 - MinimalVoiceKit", 8, 8);

    const char* badgeStr = "PRET";
    uint16_t badgeCol = TFT_GREEN;
    if (!voice.isReady()) {
        badgeStr = "CONNEXION";
        badgeCol = TFT_ORANGE;
    }
    if (voice.isSpeaking()) {
        badgeStr = "PAROLE";
        badgeCol = TFT_CYAN;
    } else if (voice.isListening()) {
        badgeStr = "ECOUTE";
        badgeCol = TFT_RED;
    } else if (voice.isTranscribing()) {
        badgeStr = "TRAITEMENT";
        badgeCol = TFT_MAGENTA;
    } else if (!s_errorMessage.isEmpty()) {
        badgeStr = "ERREUR";
        badgeCol = TFT_RED;
    }

    int badgeW = 90;
    int badgeX = 320 - badgeW - 8;
    M5.Display.fillRoundRect(badgeX, 5, badgeW, 18, 4, badgeCol);
    M5.Display.setTextColor(TFT_BLACK, badgeCol);
    M5.Display.drawCenterString(badgeStr, badgeX + (badgeW / 2), 8);

    // 2. Onglets des 4 Modes (Y = 28..54)
    static const char* TAB_TITLES[4] = { "1. TTS", "2. STT", "3. CONTINU", "4. CONV" };
    int tabW = 74;
    int tabGap = 6;
    int startX = 8;

    for (int i = 0; i < 4; ++i) {
        int x = startX + i * (tabW + tabGap);
        bool isSel = ((uint8_t)s_activeTab == i);
        uint16_t bg = isSel ? M5.Display.color565(20, 110, 210) : M5.Display.color565(40, 45, 55);
        uint16_t border = isSel ? TFT_WHITE : M5.Display.color565(70, 75, 85);

        M5.Display.fillRoundRect(x, 28, tabW, 24, 4, bg);
        M5.Display.drawRoundRect(x, 28, tabW, 24, 4, border);
        M5.Display.setTextColor(TFT_WHITE, bg);
        M5.Display.drawCenterString(TAB_TITLES[i], x + (tabW / 2), 34);
    }

    // 3. Cadre central de visualisation (Y = 58..168, H = 110)
    int boxX = 8;
    int boxY = 58;
    int boxW = 304;
    int boxH = 110;
    uint16_t boxBg = M5.Display.color565(15, 20, 28);
    M5.Display.fillRoundRect(boxX, boxY, boxW, boxH, 6, boxBg);
    M5.Display.drawRoundRect(boxX, boxY, boxW, boxH, 6, M5.Display.color565(50, 60, 75));

    // Police avec prise en charge intégrale des accents UTF-8
    M5.Display.setFont(&fonts::efontJA_12);
    M5.Display.setTextSize(1);
    M5.Display.setTextWrap(true);

    int textY = boxY + 6;

    if (s_activeTab == DemoTab::TAB_TTS) {
        M5.Display.setTextColor(M5.Display.color565(120, 190, 255), boxBg);
        M5.Display.drawString("Mode 1 : Synth\xc3\xa8""se Vocale (say)", boxX + 8, textY);
        textY += 18;

        M5.Display.setTextColor(TFT_LIGHTGRAY, boxBg);
        M5.Display.drawString("Phrase : ", boxX + 8, textY);
        textY += 16;
        M5.Display.setTextColor(TFT_WHITE, boxBg);
        M5.Display.drawString(TTS_DEMO_TEXT, boxX + 8, textY);
        textY += 34;

        M5.Display.setTextColor(M5.Display.color565(80, 220, 120), boxBg);
        M5.Display.drawString(s_statusMessage.c_str(), boxX + 8, textY);
    } else if (s_activeTab == DemoTab::TAB_STT) {
        M5.Display.setTextColor(M5.Display.color565(255, 200, 80), boxBg);
        M5.Display.drawString("Mode 2 : Dict\xc3\xa9""e Manuelle (listen/stop)", boxX + 8, textY);
        textY += 18;

        if (s_finalText.length() > 0) {
            M5.Display.setTextColor(TFT_GREEN, boxBg);
            M5.Display.drawString("Texte : ", boxX + 8, textY);
            M5.Display.setTextColor(TFT_WHITE, boxBg);
            M5.Display.drawString(s_finalText.c_str(), boxX + 54, textY);
        } else {
            M5.Display.setTextColor(TFT_DARKGRAY, boxBg);
            M5.Display.drawString("(Maintenez DICTER et parlez)", boxX + 8, textY);
        }
        textY += 32;

        if (s_interimText.length() > 0) {
            M5.Display.setTextColor(TFT_YELLOW, boxBg);
            M5.Display.drawString("[En direct] : ", boxX + 8, textY);
            M5.Display.setTextColor(TFT_WHITE, boxBg);
            M5.Display.drawString(s_interimText.c_str(), boxX + 80, textY);
        } else {
            M5.Display.setTextColor(M5.Display.color565(140, 160, 180), boxBg);
            M5.Display.drawString(s_statusMessage.c_str(), boxX + 8, textY);
        }
    } else if (s_activeTab == DemoTab::TAB_CONTINUOUS) {
        M5.Display.setTextColor(M5.Display.color565(140, 230, 120), boxBg);
        char hdr[64];
        snprintf(hdr, sizeof(hdr), "Mode 3 : Dict\xc3\xa9""e Continue (VAD) | Segments: %u",
                 (unsigned)voice.getContinuousController().getSegmentCount());
        M5.Display.drawString(hdr, boxX + 8, textY);
        textY += 18;

        if (s_finalText.length() > 0) {
            M5.Display.setTextColor(TFT_GREEN, boxBg);
            M5.Display.drawString("Dernier : ", boxX + 8, textY);
            M5.Display.setTextColor(TFT_WHITE, boxBg);
            M5.Display.drawString(s_finalText.c_str(), boxX + 65, textY);
        } else {
            M5.Display.setTextColor(TFT_DARKGRAY, boxBg);
            M5.Display.drawString("(Activez le continu pour parler sans bouton)", boxX + 8, textY);
        }
        textY += 32;

        if (s_interimText.length() > 0) {
            M5.Display.setTextColor(TFT_YELLOW, boxBg);
            M5.Display.drawString("[Direct] : ", boxX + 8, textY);
            M5.Display.setTextColor(TFT_WHITE, boxBg);
            M5.Display.drawString(s_interimText.c_str(), boxX + 65, textY);
        } else {
            M5.Display.setTextColor(M5.Display.color565(140, 160, 180), boxBg);
            if (voice.isContinuousListening()) {
                if (voice.isContinuousListeningPaused()) {
                    M5.Display.drawString("Dict\xc3\xa9""e continue en PAUSE", boxX + 8, textY);
                } else {
                    M5.Display.drawString("Ecoute continue active (parlez librement)", boxX + 8, textY);
                }
            } else {
                M5.Display.drawString("Dict\xc3\xa9""e continue inactive", boxX + 8, textY);
            }
        }
    } else if (s_activeTab == DemoTab::TAB_CONVERSATION) {
        M5.Display.setTextColor(M5.Display.color565(255, 120, 220), boxBg);
        M5.Display.drawString("Mode 4 : Conversation Gemini Live (Mode B)", boxX + 8, textY);
        textY += 18;

        if (s_interimText.length() > 0 || s_finalText.length() > 0) {
            M5.Display.setTextColor(TFT_YELLOW, boxBg);
            M5.Display.drawString("Vous : ", boxX + 8, textY);
            M5.Display.setTextColor(TFT_WHITE, boxBg);
            String uTxt = s_interimText.length() > 0 ? s_interimText : s_finalText;
            M5.Display.drawString(uTxt.c_str(), boxX + 46, textY);
        } else {
            M5.Display.setTextColor(TFT_DARKGRAY, boxBg);
            M5.Display.drawString("Vous : (Maintenez PARLER)", boxX + 8, textY);
        }
        textY += 28;

        if (s_assistantText.length() > 0) {
            M5.Display.setTextColor(TFT_CYAN, boxBg);
            M5.Display.drawString("Gemini : ", boxX + 8, textY);
            M5.Display.setTextColor(TFT_WHITE, boxBg);
            M5.Display.drawString(s_assistantText.c_str(), boxX + 60, textY);
        } else {
            M5.Display.setTextColor(TFT_DARKGRAY, boxBg);
            M5.Display.drawString("Gemini : (En attente de dialogue)", boxX + 8, textY);
        }
    }

    // 4. Barre de boutons d'action inférieure (Y = 176..232, H = 56)
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);

    int btnY = 176;
    int btnH = 56;

    if (s_activeTab == DemoTab::TAB_TTS) {
        // Bouton DIRE LA PHRASE (X=8..218)
        uint16_t bCol = voice.isSpeaking() ? TFT_DARKGRAY : M5.Display.color565(30, 120, 210);
        M5.Display.fillRoundRect(8, btnY, 210, btnH, 8, bCol);
        M5.Display.drawRoundRect(8, btnY, 210, btnH, 8, TFT_WHITE);
        M5.Display.setTextColor(TFT_WHITE, bCol);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString("DIRE LA PHRASE (TTS)", 8 + 105, btnY + 28);

        // Bouton STOP (X=226..312)
        M5.Display.fillRoundRect(226, btnY, 86, btnH, 8, M5.Display.color565(180, 40, 40));
        M5.Display.drawRoundRect(226, btnY, 86, btnH, 8, TFT_WHITE);
        M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(180, 40, 40));
        M5.Display.drawString("STOP", 226 + 43, btnY + 28);
        M5.Display.setTextDatum(TL_DATUM);
    } else if (s_activeTab == DemoTab::TAB_STT) {
        // Bouton DICTER PUSH-TO-TALK (X=8..218)
        uint16_t bCol = voice.isListening() ? M5.Display.color565(210, 30, 30) : M5.Display.color565(30, 130, 60);
        const char* bTxt = voice.isListening() ? "ENREGISTREMENT..." : "MAINTENIR POUR DICTER";
        M5.Display.fillRoundRect(8, btnY, 210, btnH, 8, bCol);
        M5.Display.drawRoundRect(8, btnY, 210, btnH, 8, TFT_WHITE);
        M5.Display.setTextColor(TFT_WHITE, bCol);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString(bTxt, 8 + 105, btnY + 28);

        // Bouton STOP (X=226..312)
        M5.Display.fillRoundRect(226, btnY, 86, btnH, 8, M5.Display.color565(180, 40, 40));
        M5.Display.drawRoundRect(226, btnY, 86, btnH, 8, TFT_WHITE);
        M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(180, 40, 40));
        M5.Display.drawString("STOP", 226 + 43, btnY + 28);
        M5.Display.setTextDatum(TL_DATUM);
    } else if (s_activeTab == DemoTab::TAB_CONTINUOUS) {
        // Bouton DEMARRER / ARRETER CONTINU (X=8..150)
        bool isCont = voice.isContinuousListening();
        uint16_t bCol = isCont ? M5.Display.color565(170, 40, 40) : M5.Display.color565(30, 130, 70);
        const char* bTxt = isCont ? "ARRETER CONTINU" : "DEMARRER CONTINU";
        M5.Display.fillRoundRect(8, btnY, 142, btnH, 8, bCol);
        M5.Display.drawRoundRect(8, btnY, 142, btnH, 8, TFT_WHITE);
        M5.Display.setTextColor(TFT_WHITE, bCol);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString(bTxt, 8 + 71, btnY + 28);

        // Bouton PAUSE / REPRENDRE (X=156..236)
        bool isPsd = voice.isContinuousListeningPaused();
        uint16_t pCol = isPsd ? M5.Display.color565(20, 120, 210) : M5.Display.color565(200, 130, 20);
        const char* pTxt = isPsd ? "REPRENDRE" : "PAUSE";
        M5.Display.fillRoundRect(156, btnY, 80, btnH, 8, pCol);
        M5.Display.drawRoundRect(156, btnY, 80, btnH, 8, TFT_WHITE);
        M5.Display.setTextColor(TFT_WHITE, pCol);
        M5.Display.drawString(pTxt, 156 + 40, btnY + 28);

        // Bouton STOP (X=242..312)
        M5.Display.fillRoundRect(242, btnY, 70, btnH, 8, M5.Display.color565(180, 40, 40));
        M5.Display.drawRoundRect(242, btnY, 70, btnH, 8, TFT_WHITE);
        M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(180, 40, 40));
        M5.Display.drawString("STOP", 242 + 35, btnY + 28);
        M5.Display.setTextDatum(TL_DATUM);
    } else if (s_activeTab == DemoTab::TAB_CONVERSATION) {
        // Bouton ACTIVER / ARRETER CONVERSATION (X=8..150)
        bool isConv = voice.isConversationActive();
        uint16_t bCol = isConv ? M5.Display.color565(170, 40, 40) : M5.Display.color565(140, 40, 180);
        const char* bTxt = isConv ? "QUITTER CONV" : "ACTIVER CONV";
        M5.Display.fillRoundRect(8, btnY, 142, btnH, 8, bCol);
        M5.Display.drawRoundRect(8, btnY, 142, btnH, 8, TFT_WHITE);
        M5.Display.setTextColor(TFT_WHITE, bCol);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString(bTxt, 8 + 71, btnY + 28);

        // Bouton PARLER (X=156..236)
        uint16_t talkCol = voice.isListening() ? M5.Display.color565(210, 30, 30) : M5.Display.color565(30, 110, 200);
        M5.Display.fillRoundRect(156, btnY, 80, btnH, 8, talkCol);
        M5.Display.drawRoundRect(156, btnY, 80, btnH, 8, TFT_WHITE);
        M5.Display.setTextColor(TFT_WHITE, talkCol);
        M5.Display.drawString(voice.isListening() ? "PARLEZ..." : "PARLER", 156 + 40, btnY + 28);

        // Bouton STOP (X=242..312)
        M5.Display.fillRoundRect(242, btnY, 70, btnH, 8, M5.Display.color565(180, 40, 40));
        M5.Display.drawRoundRect(242, btnY, 70, btnH, 8, TFT_WHITE);
        M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(180, 40, 40));
        M5.Display.drawString("STOP", 242 + 35, btnY + 28);
        M5.Display.setTextDatum(TL_DATUM);
    }

    s_needsRedraw = false;
}

void loop() {
    M5.update();
    voice.update();

    // 1. Commandes de diagnostic via moniteur série (115200 baud)
    if (Serial.available()) {
        char c = Serial.read();
        if (c == '1') {
            s_activeTab = DemoTab::TAB_TTS;
            s_needsRedraw = true;
            voice.say(TTS_DEMO_TEXT);
        } else if (c == '2') {
            s_activeTab = DemoTab::TAB_STT;
            s_needsRedraw = true;
        } else if (c == '3') {
            s_activeTab = DemoTab::TAB_CONTINUOUS;
            s_needsRedraw = true;
            if (voice.isContinuousListening()) voice.stopContinuousListening();
            else voice.startContinuousListening();
        } else if (c == '4') {
            s_activeTab = DemoTab::TAB_CONVERSATION;
            s_needsRedraw = true;
            if (voice.isConversationActive()) voice.stopConversation();
            else voice.startConversation();
        } else if (c == 'P' || c == 'p') {
            if (voice.isReady()) voice.listen();
        } else if (c == 'R' || c == 'r') {
            if (voice.isListening()) voice.stopListening();
        } else if (c == 'S' || c == 's') {
            voice.stop();
            s_needsRedraw = true;
        }
    }

    // 2. Gestion de l'écran tactile M5Stack CoreS3
    auto touch = M5.Touch.getDetail();

    if (touch.wasPressed()) {
        int tx = touch.x;
        int ty = touch.y;

        // Clic sur l'un des 4 onglets (Y = 28..52)
        if (ty >= 26 && ty <= 54) {
            int tabW = 74;
            int tabGap = 6;
            int startX = 8;
            for (int i = 0; i < 4; ++i) {
                int x = startX + i * (tabW + tabGap);
                if (tx >= x && tx <= (x + tabW)) {
                    // Arrêt propre du mode précédent si nécessaire
                    if (s_activeTab == DemoTab::TAB_CONTINUOUS && voice.isContinuousListening()) {
                        voice.stopContinuousListening();
                    } else if (s_activeTab == DemoTab::TAB_CONVERSATION && voice.isConversationActive()) {
                        voice.stopConversation();
                    } else {
                        voice.stop();
                    }
                    s_activeTab = (DemoTab)i;
                    s_interimText = "";
                    s_finalText = "";
                    s_assistantText = "";
                    s_statusMessage = "Mode chang\xc3\xa9";
                    s_needsRedraw = true;
                    break;
                }
            }
        }
        // Clic sur la barre inférieure d'actions (Y = 176..232)
        else if (ty >= 174 && ty <= 234) {
            if (s_activeTab == DemoTab::TAB_TTS) {
                if (tx >= 8 && tx <= 218) {
                    // Action : DIRE LA PHRASE
                    Serial.println("[Touch] Clic DIRE LA PHRASE -> voice.say()");
                    s_interimText = "";
                    s_finalText = "";
                    bool ok = voice.say(TTS_DEMO_TEXT);
                    if (ok) s_statusMessage = "Requ\xc3\xaa""te TTS accept\xc3\xa9""e";
                    else s_statusMessage = "Requ\xc3\xaa""te TTS refus\xc3\xa9""e (occup\xc3\xa9)";
                    s_needsRedraw = true;
                } else if (tx >= 226 && tx <= 312) {
                    // Action : STOP
                    Serial.println("[Touch] Clic STOP");
                    voice.stop();
                    s_statusMessage = "Arr\xc3\xaat effectu\xc3\xa9";
                    s_needsRedraw = true;
                }
            } else if (s_activeTab == DemoTab::TAB_STT) {
                if (tx >= 8 && tx <= 218) {
                    // Action : PUSH-TO-TALK (Appui)
                    if (voice.isReady()) {
                        Serial.println("[Touch] Début dictée manuelle -> voice.listen()");
                        s_manualTouchActive = true;
                        s_interimText = "";
                        s_finalText = "";
                        bool ok = voice.listen();
                        if (ok) s_statusMessage = "Enregistrement en cours...";
                        s_needsRedraw = true;
                    }
                } else if (tx >= 226 && tx <= 312) {
                    // Action : STOP
                    Serial.println("[Touch] Clic STOP");
                    voice.stop();
                    s_manualTouchActive = false;
                    s_statusMessage = "Arr\xc3\xaat effectu\xc3\xa9";
                    s_needsRedraw = true;
                }
            } else if (s_activeTab == DemoTab::TAB_CONTINUOUS) {
                if (tx >= 8 && tx <= 150) {
                    // Toggle DEMARRER / ARRETER CONTINU
                    if (voice.isContinuousListening()) {
                        Serial.println("[Touch] stopContinuousListening()");
                        voice.stopContinuousListening();
                        s_statusMessage = "Dict\xc3\xa9""e continue arr\xc3\xaa""t\xc3\xa9""e";
                    } else {
                        Serial.println("[Touch] startContinuousListening()");
                        s_interimText = "";
                        s_finalText = "";
                        voice.startContinuousListening();
                        s_statusMessage = "Dict\xc3\xa9""e continue lanc\xc3\xa9""e";
                    }
                    s_needsRedraw = true;
                } else if (tx >= 156 && tx <= 236) {
                    // Toggle PAUSE / REPRENDRE
                    if (voice.isContinuousListeningPaused()) {
                        Serial.println("[Touch] resumeContinuousListening()");
                        voice.resumeContinuousListening();
                        s_statusMessage = "Dict\xc3\xa9""e reprise";
                    } else {
                        Serial.println("[Touch] pauseContinuousListening()");
                        voice.pauseContinuousListening();
                        s_statusMessage = "Dict\xc3\xa9""e suspendue (pause)";
                    }
                    s_needsRedraw = true;
                } else if (tx >= 242 && tx <= 312) {
                    // Action : STOP
                    Serial.println("[Touch] Clic STOP");
                    voice.stop();
                    s_statusMessage = "Arr\xc3\xaat complet";
                    s_needsRedraw = true;
                }
            } else if (s_activeTab == DemoTab::TAB_CONVERSATION) {
                if (tx >= 8 && tx <= 150) {
                    // Toggle ACTIVER / ARRETER CONVERSATION
                    if (voice.isConversationActive()) {
                        Serial.println("[Touch] stopConversation()");
                        voice.stopConversation();
                        s_statusMessage = "Conversation arr\xc3\xaa""t\xc3\xa9""e";
                    } else {
                        Serial.println("[Touch] startConversation()");
                        s_interimText = "";
                        s_finalText = "";
                        s_assistantText = "";
                        voice.startConversation();
                        s_statusMessage = "Conversation activ\xc3\xa9""e";
                    }
                    s_needsRedraw = true;
                } else if (tx >= 156 && tx <= 236) {
                    // Bouton PARLER (en conversation)
                    if (voice.isConversationActive() && voice.isReady()) {
                        Serial.println("[Touch] PARLER en conversation -> listen()");
                        s_manualTouchActive = true;
                        s_interimText = "";
                        s_finalText = "";
                        voice.getEngine().startListening();
                        s_needsRedraw = true;
                    }
                } else if (tx >= 242 && tx <= 312) {
                    // Action : STOP
                    Serial.println("[Touch] Clic STOP");
                    voice.stop();
                    s_manualTouchActive = false;
                    s_statusMessage = "Arr\xc3\xaat effectu\xc3\xa9";
                    s_needsRedraw = true;
                }
            }
        }
    } else if (touch.wasReleased()) {
        // Relâchement du Push-to-Talk (STT ou Conversation)
        if (s_manualTouchActive) {
            s_manualTouchActive = false;
            if (voice.isListening()) {
                Serial.println("[Touch] Relachement PTT -> stopListening()");
                voice.stopListening();
                s_statusMessage = "Attente transcription...";
                s_needsRedraw = true;
            }
        }
    }

    // 3. Rafraîchissement périodique de l'interface
    if (s_needsRedraw) {
        drawUI();
    }

    delay(10);
}
