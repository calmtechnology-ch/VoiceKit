/**
 * @file main.cpp
 * @brief Exemple TwoClickConversationLVGL — VoiceKit v0.6.7.0 Experimental
 *        Démonstration du PTT à deux clics, validation locale de parole (VAD)
 *        et STOP prioritaire avec LVGL 9 sur M5Stack CoreS3.
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <VoxCoreVoice.h>
#include <VoiceInteractionController.h>
#include <WiFi.h>

#include "app_config.h"
#include "lv_conf.h"
#include <lvgl.h>
#include "FrenchFonts.h"

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #include "secrets.example.h"
#endif

#ifndef OPENAI_API_KEY
    #define OPENAI_API_KEY ""
#endif

#ifndef GEMINI_API_KEY
    #define GEMINI_API_KEY ""
#endif

#ifndef WIFI_SSID
    #define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
    #define WIFI_PASSWORD ""
#endif

// =============================================================================
// Instances Globales
// =============================================================================

static VoxCoreVoice s_voice;
static VoiceInteractionConfig s_interactionConfig;
static VoiceInteractionController s_interaction(s_voice, s_interactionConfig);

// Buffers DMA pour LVGL 9
alignas(4) static uint8_t s_drawBuf[AppConfig::BUFFER_PIXELS * sizeof(uint16_t)];

// Widgets LVGL
static lv_obj_t* s_lblConn = nullptr;
static lv_obj_t* s_lblVad = nullptr;
static lv_obj_t* s_lblState = nullptr;
static lv_obj_t* s_lblUserText = nullptr;
static lv_obj_t* s_lblAssistantText = nullptr;
static lv_obj_t* s_btnTalk = nullptr;
static lv_obj_t* s_lblBtnTalk = nullptr;
static lv_obj_t* s_btnStop = nullptr;

// Buffers de texte pour découplage thread-safe vers LVGL
static portMUX_TYPE s_textMux = portMUX_INITIALIZER_UNLOCKED;
static char s_pendingUserText[512] = "";
static bool s_hasPendingUserText = false;
static char s_pendingAssistantText[512] = "";
static bool s_hasPendingAssistantText = false;
static char s_pendingVadText[64] = "VAD: En attente";
static bool s_hasPendingVadText = false;

static void setTextThreadSafe(char* dst, size_t dstSize, const char* src, bool& flag) {
    if (!dst || dstSize == 0 || !src) return;
    portENTER_CRITICAL(&s_textMux);
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
    flag = true;
    portEXIT_CRITICAL(&s_textMux);
}

// =============================================================================
// Callbacks Pilotes Affichage & Tactile LVGL 9
// =============================================================================

static void disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    M5.Display.startWrite();
    M5.Display.pushImage(area->x1, area->y1, w, h, (const uint16_t*)px_map);
    M5.Display.endWrite();
    lv_display_flush_ready(disp);
}

static void touch_read(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    if (M5.Touch.getCount() > 0) {
        auto detail = M5.Touch.getDetail(0);
        if (detail.isPressed()) {
            data->state = LV_INDEV_STATE_PRESSED;
            data->point.x = detail.x;
            data->point.y = detail.y;
            return;
        }
    }
    data->state = LV_INDEV_STATE_RELEASED;
}

static uint32_t lvgl_tick_cb() {
    return static_cast<uint32_t>(millis());
}

// =============================================================================
// Callbacks Événements Boutons LVGL
// =============================================================================

static void onTalkButtonClicked(lv_event_t* e) {
    (void)e;
    Serial.println("[UI][CLICK] Bouton PARLER / TERMINER cliqué");
    s_interaction.onTalkClick();
}

static void onStopButtonClicked(lv_event_t* e) {
    (void)e;
    Serial.println("[UI][CLICK] Bouton STOP cliqué");
    s_interaction.triggerStop();
}

// =============================================================================
// Construction de l'Interface Graphique (LVGL 9)
// =============================================================================

static void init_ui() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x121218), 0);

    // 1. Barre supérieure : Connexion & VAD (y: 4, h: 22)
    s_lblConn = lv_label_create(scr);
    lv_obj_set_pos(s_lblConn, 8, 6);
    lv_label_set_text(s_lblConn, "WiFi: ... | Session: ...");
    lv_obj_set_style_text_font(s_lblConn, &font_french_12, 0);
    lv_obj_set_style_text_color(s_lblConn, lv_color_hex(0x9E9E9E), 0);

    s_lblVad = lv_label_create(scr);
    lv_obj_set_pos(s_lblVad, 210, 6);
    lv_label_set_text(s_lblVad, "VAD: PRÊT");
    lv_obj_set_style_text_font(s_lblVad, &font_french_12, 0);
    lv_obj_set_style_text_color(s_lblVad, lv_color_hex(0x00E676), 0);

    // 2. Indicateur d'état vocal (y: 28, h: 24)
    s_lblState = lv_label_create(scr);
    lv_obj_set_pos(s_lblState, 8, 28);
    lv_label_set_text(s_lblState, "ÉTAT : INITIALISATION...");
    lv_obj_set_style_text_font(s_lblState, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(s_lblState, lv_color_hex(0xFFEB3B), 0);

    // 3. Cadre texte Utilisateur (y: 56, h: 56, w: 304)
    lv_obj_t* pnlUser = lv_obj_create(scr);
    lv_obj_set_pos(pnlUser, 8, 56);
    lv_obj_set_size(pnlUser, 304, 56);
    lv_obj_set_style_bg_color(pnlUser, lv_color_hex(0x1E1E28), 0);
    lv_obj_set_style_border_color(pnlUser, lv_color_hex(0x333344), 0);
    lv_obj_set_style_radius(pnlUser, 6, 0);
    lv_obj_set_style_pad_all(pnlUser, 4, 0);
    lv_obj_clear_flag(pnlUser, LV_OBJ_FLAG_SCROLLABLE);

    s_lblUserText = lv_label_create(pnlUser);
    lv_obj_set_size(s_lblUserText, 292, 46);
    lv_label_set_long_mode(s_lblUserText, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(s_lblUserText, "Moi : (En attente de parole...)");
    lv_obj_set_style_text_font(s_lblUserText, &font_french_12, 0);
    lv_obj_set_style_text_color(s_lblUserText, lv_color_hex(0xBBDEFB), 0);

    // 4. Cadre texte Assistant (y: 118, h: 62, w: 304)
    lv_obj_t* pnlAssistant = lv_obj_create(scr);
    lv_obj_set_pos(pnlAssistant, 8, 118);
    lv_obj_set_size(pnlAssistant, 304, 62);
    lv_obj_set_style_bg_color(pnlAssistant, lv_color_hex(0x1A2228), 0);
    lv_obj_set_style_border_color(pnlAssistant, lv_color_hex(0x283844), 0);
    lv_obj_set_style_radius(pnlAssistant, 6, 0);
    lv_obj_set_style_pad_all(pnlAssistant, 4, 0);
    lv_obj_clear_flag(pnlAssistant, LV_OBJ_FLAG_SCROLLABLE);

    s_lblAssistantText = lv_label_create(pnlAssistant);
    lv_obj_set_size(s_lblAssistantText, 292, 52);
    lv_label_set_long_mode(s_lblAssistantText, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(s_lblAssistantText, "IA : Bonjour ! Cliquez sur PARLER pour démarrer.");
    lv_obj_set_style_text_font(s_lblAssistantText, &font_french_12, 0);
    lv_obj_set_style_text_color(s_lblAssistantText, lv_color_hex(0xC8E6C9), 0);

    // 5. Bouton Principal PARLER / TERMINER (y: 186, h: 48, w: 196)
    s_btnTalk = lv_button_create(scr);
    lv_obj_set_pos(s_btnTalk, 8, 186);
    lv_obj_set_size(s_btnTalk, 196, 48);
    lv_obj_set_style_bg_color(s_btnTalk, lv_color_hex(0x1976D2), 0);
    lv_obj_set_style_radius(s_btnTalk, 8, 0);
    lv_obj_add_event_cb(s_btnTalk, onTalkButtonClicked, LV_EVENT_CLICKED, nullptr);

    s_lblBtnTalk = lv_label_create(s_btnTalk);
    lv_label_set_text(s_lblBtnTalk, "PARLER");
    lv_obj_set_style_text_font(s_lblBtnTalk, &font_french_bold_16, 0);
    lv_obj_center(s_lblBtnTalk);

    // 6. Bouton STOP prioritaire (y: 186, h: 48, w: 102)
    s_btnStop = lv_button_create(scr);
    lv_obj_set_pos(s_btnStop, 210, 186);
    lv_obj_set_size(s_btnStop, 102, 48);
    lv_obj_set_style_bg_color(s_btnStop, lv_color_hex(0xD32F2F), 0);
    lv_obj_set_style_radius(s_btnStop, 8, 0);
    lv_obj_add_event_cb(s_btnStop, onStopButtonClicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* lblBtnStop = lv_label_create(s_btnStop);
    lv_label_set_text(lblBtnStop, "STOP");
    lv_obj_set_style_text_font(lblBtnStop, &font_french_bold_16, 0);
    lv_obj_center(lblBtnStop);
}

// =============================================================================
// Synchronisation de l'IHM avec VoiceInteractionState
// =============================================================================

static void updateUiState(VoiceInteractionState st) {
    if (!s_lblState || !s_btnTalk || !s_lblBtnTalk) return;

    switch (st) {
        case VoiceInteractionState::DISABLED:
            lv_label_set_text(s_lblState, "ÉTAT : DÉSACTIVÉ");
            lv_obj_set_style_text_color(s_lblState, lv_color_hex(0x9E9E9E), 0);
            lv_label_set_text(s_lblBtnTalk, "ATTENTE");
            lv_obj_set_style_bg_color(s_btnTalk, lv_color_hex(0x424242), 0);
            break;
        case VoiceInteractionState::READY:
            lv_label_set_text(s_lblState, "ÉTAT : PRÊT (2 clics)");
            lv_obj_set_style_text_color(s_lblState, lv_color_hex(0x00E676), 0);
            lv_label_set_text(s_lblBtnTalk, "PARLER");
            lv_obj_set_style_bg_color(s_btnTalk, lv_color_hex(0x1976D2), 0);
            break;
        case VoiceInteractionState::STARTING:
            lv_label_set_text(s_lblState, "ÉTAT : OUVERTURE MICRO...");
            lv_obj_set_style_text_color(s_lblState, lv_color_hex(0xFF9800), 0);
            lv_label_set_text(s_lblBtnTalk, "OUVERTURE");
            lv_obj_set_style_bg_color(s_btnTalk, lv_color_hex(0xF57C00), 0);
            break;
        case VoiceInteractionState::LISTENING:
            lv_label_set_text(s_lblState, "ÉTAT : ÉCOUTE EN COURS...");
            lv_obj_set_style_text_color(s_lblState, lv_color_hex(0xFF1744), 0);
            lv_label_set_text(s_lblBtnTalk, "TERMINER");
            lv_obj_set_style_bg_color(s_btnTalk, lv_color_hex(0xD50000), 0);
            break;
        case VoiceInteractionState::STOPPING_CAPTURE:
            lv_label_set_text(s_lblState, "ÉTAT : VALIDATION VAD...");
            lv_obj_set_style_text_color(s_lblState, lv_color_hex(0xFFB300), 0);
            lv_label_set_text(s_lblBtnTalk, "VALIDATION");
            lv_obj_set_style_bg_color(s_btnTalk, lv_color_hex(0xFF8F00), 0);
            break;
        case VoiceInteractionState::PROCESSING:
            lv_label_set_text(s_lblState, "ÉTAT : TRAITEMENT IA...");
            lv_obj_set_style_text_color(s_lblState, lv_color_hex(0x29B6F6), 0);
            lv_label_set_text(s_lblBtnTalk, "TRAITEMENT");
            lv_obj_set_style_bg_color(s_btnTalk, lv_color_hex(0x0288D1), 0);
            break;
        case VoiceInteractionState::SPEAKING:
            lv_label_set_text(s_lblState, "ÉTAT : ASSISTANT PARLE");
            lv_obj_set_style_text_color(s_lblState, lv_color_hex(0xAB47BC), 0);
            lv_label_set_text(s_lblBtnTalk, "STOP PAROLE");
            lv_obj_set_style_bg_color(s_btnTalk, lv_color_hex(0x7B1FA2), 0);
            break;
        case VoiceInteractionState::INTERRUPTING:
            lv_label_set_text(s_lblState, "ÉTAT : ARRÊT STOP...");
            lv_obj_set_style_text_color(s_lblState, lv_color_hex(0xE53935), 0);
            lv_label_set_text(s_lblBtnTalk, "ARRÊT...");
            lv_obj_set_style_bg_color(s_btnTalk, lv_color_hex(0xC62828), 0);
            break;
        case VoiceInteractionState::RECOVERING:
            lv_label_set_text(s_lblState, "ÉTAT : RÉCUPÉRATION...");
            lv_obj_set_style_text_color(s_lblState, lv_color_hex(0xFFA726), 0);
            lv_label_set_text(s_lblBtnTalk, "REPRISE");
            lv_obj_set_style_bg_color(s_btnTalk, lv_color_hex(0xE65100), 0);
            break;
        case VoiceInteractionState::ERROR_STATE:
            lv_label_set_text(s_lblState, "ÉTAT : ERREUR");
            lv_obj_set_style_text_color(s_lblState, lv_color_hex(0xFF3D00), 0);
            lv_label_set_text(s_lblBtnTalk, "RÉINITIALISER");
            lv_obj_set_style_bg_color(s_btnTalk, lv_color_hex(0xBF360C), 0);
            break;
    }
}

// =============================================================================
// Initialisation (setup)
// =============================================================================

void setup() {
    auto m5cfg = M5.config();
    M5.begin(m5cfg);

    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  VoiceKit v0.6.7.0 — TwoClickConversationLVGL          ");
    Serial.println("  Interaction Vocale Réutilisable à Deux Clics (PTT)    ");
    Serial.println("  Validation VAD Locale & STOP Prioritaire sur CoreS3   ");
    Serial.println("========================================================");

    // Initialisation et rétroéclairage de l'écran CoreS3
    M5.Display.waitDisplay();
    M5.Display.setBrightness(200);
    M5.Display.fillScreen(TFT_BLACK);

    // 1. Initialisation de LVGL 9
    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);

    lv_display_t* disp = lv_display_create(AppConfig::SCREEN_WIDTH, AppConfig::SCREEN_HEIGHT);
    lv_display_set_buffers(disp, s_drawBuf, nullptr, sizeof(s_drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, disp_flush);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);

    // 2. Construction de l'interface
    init_ui();

    // 3. Configuration de VoiceInteractionController
    s_interactionConfig.debounceMs = AppConfig::DEBOUNCE_MS;
    s_interactionConfig.micTimeoutMs = AppConfig::MIC_TIMEOUT_MS;
    s_interactionConfig.serverResponseTimeoutMs = AppConfig::SERVER_TIMEOUT_MS;
    s_interactionConfig.minCaptureDurationMs = AppConfig::MIN_CAPTURE_DURATION_MS;
    s_interactionConfig.minSpeechDurationMs = AppConfig::MIN_SPEECH_DURATION_MS;
    s_interactionConfig.minConsecutiveSpeechMs = AppConfig::MIN_CONSECUTIVE_SPEECH_MS;
    s_interactionConfig.speechThresholdFloorRms = AppConfig::SPEECH_THRESHOLD_FLOOR_RMS;
    s_interaction.setConfig(s_interactionConfig);

    // Initialisation du contrôleur
    s_interaction.begin();

    // 4. Configuration des callbacks d'observation de l'interaction
    s_interaction.onStateChanged([](VoiceInteractionState oldSt, VoiceInteractionState newSt) {
        Serial.printf("[PTT_STATE] %s -> %s\n", voiceInteractionStateToString(oldSt), voiceInteractionStateToString(newSt));
    });

    s_interaction.onSilenceDetected([](const char* reason) {
        Serial.printf("[VAD][REJECT] Silence ou bruit bref détecté [motif: %s]\n", reason ? reason : "INCONNU");
        char vadBuf[64];
        snprintf(vadBuf, sizeof(vadBuf), "VAD: REJET (%s)", reason ? reason : "");
        setTextThreadSafe(s_pendingVadText, sizeof(s_pendingVadText), vadBuf, s_hasPendingVadText);

        char userBuf[128];
        snprintf(userBuf, sizeof(userBuf), "Moi : (Aucune parole détectée : %s)", reason ? reason : "");
        setTextThreadSafe(s_pendingUserText, sizeof(s_pendingUserText), userBuf, s_hasPendingUserText);
    });

    s_interaction.onCaptureValidated([](const VoiceCaptureMetrics& m) {
        if (m.isValid) {
            Serial.printf("[VAD][VALID] Parole confirmée [durée: %u ms, parole: %u ms, peak: %u RMS]\n",
                          (unsigned)m.captureDurationMs, (unsigned)m.cumulativeSpeechMs, (unsigned)m.peakRms);
            char vadBuf[64];
            snprintf(vadBuf, sizeof(vadBuf), "VAD: OK (%ums)", (unsigned)m.cumulativeSpeechMs);
            setTextThreadSafe(s_pendingVadText, sizeof(s_pendingVadText), vadBuf, s_hasPendingVadText);
        }
    });

    // 5. Configuration de VoiceKit
    VoiceKitConfig vConfig;
#if USE_OPENAI
    vConfig.provider = VoiceProviderType::OPENAI;
    vConfig.apiKey = OPENAI_API_KEY;
    vConfig.openAiApiKey = OPENAI_API_KEY;
    vConfig.model = AppConfig::OPENAI_MODEL;
    vConfig.voice = AppConfig::OPENAI_VOICE;
    vConfig.language = AppConfig::OPENAI_LANGUAGE;
#elif USE_GEMINI
    vConfig.provider = VoiceProviderType::GEMINI;
    vConfig.apiKey = GEMINI_API_KEY;
    vConfig.geminiApiKey = GEMINI_API_KEY;
    vConfig.model = AppConfig::GEMINI_MODEL;
    vConfig.voice = AppConfig::GEMINI_VOICE;
    vConfig.language = AppConfig::GEMINI_LANGUAGE;
#endif

    vConfig.wifiMode = WiFiMode::MANAGED_WIFI;
    vConfig.wifiSSID = WIFI_SSID;
    vConfig.wifiPassword = WIFI_PASSWORD;
    vConfig.enableFastConnect = true;
    vConfig.volume = AppConfig::DEFAULT_VOLUME;
    vConfig.enableConversation = true;
    vConfig.conversationInstruction = AppConfig::CONVERSATION_INSTRUCTION;

    // 6. Enregistrement des callbacks de VoiceKit avec relais explicite vers VoiceInteractionController
    s_voice.onAudioChunk([](const int16_t* samples, size_t count, uint32_t rms) {
        s_interaction.processAudioChunk(samples, count, rms);
    });

    s_voice.onReady([]() {
        Serial.println("[VOICE] Session live connectée et prête !");
        s_interaction.notifyReady();
    });

    s_voice.onStateChanged([](VoiceState st) {
        s_interaction.notifyVoiceStateChanged(st);
    });

    s_voice.onListeningStarted([]() {
        s_interaction.notifyListeningStarted();
    });

    s_voice.onListeningFinished([]() {
        s_interaction.notifyListeningFinished();
    });

    s_voice.onSpeechStarted([]() {
        s_interaction.notifySpeechStarted();
    });

    s_voice.onSpeechFinished([]() {
        s_interaction.notifySpeechFinished();
    });

    s_voice.onError([](const String& err) {
        Serial.printf("[VOICE][ERR] %s\n", err.c_str());
        s_interaction.notifyError(err);
    });

    // Callbacks de texte conversationnel
    s_voice.onUserText([](const String& text, bool isFinal) {
        if (s_interaction.isLastTurnCancelled()) {
            return; // Ignorer les transcriptions tardives après STOP
        }
        if (isFinal) {
            Serial.printf("[USER] %s\n", text.c_str());
        }
        String out = "Moi : " + text;
        setTextThreadSafe(s_pendingUserText, sizeof(s_pendingUserText), out.c_str(), s_hasPendingUserText);
    });

    s_voice.onAssistantText([](const String& text) {
        if (s_interaction.isLastTurnCancelled()) {
            return; // Ignorer les réponses tardives après STOP
        }
        String out = "IA : " + text;
        setTextThreadSafe(s_pendingAssistantText, sizeof(s_pendingAssistantText), out.c_str(), s_hasPendingAssistantText);
    });

    // 7. Démarrage de VoiceKit
    if (!s_voice.begin(vConfig)) {
        Serial.printf("[MAIN][ERR] Échec voice.begin() : %s\n", s_voice.getLastError().c_str());
        s_interaction.notifyError(s_voice.getLastError());
    }

    Serial.println("[MAIN] Initialisation achevée. Prêt pour l'interaction vocale.");
}

// =============================================================================
// Boucle Principale non bloquante (loop)
// =============================================================================

void loop() {
    M5.update();

    uint32_t nowMs = millis();

    // 1. Mise à jour cyclique obligatoire des moteurs vocaux
    s_voice.update();
    s_interaction.update(nowMs);

    // 2. Traitement des textes différés vers LVGL 9 dans le thread principal
    if (s_hasPendingUserText) {
        char buf[512];
        portENTER_CRITICAL(&s_textMux);
        strncpy(buf, s_pendingUserText, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        s_hasPendingUserText = false;
        portEXIT_CRITICAL(&s_textMux);
        if (s_lblUserText) {
            lv_label_set_text(s_lblUserText, buf);
        }
    }

    if (s_hasPendingAssistantText) {
        char buf[512];
        portENTER_CRITICAL(&s_textMux);
        strncpy(buf, s_pendingAssistantText, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        s_hasPendingAssistantText = false;
        portEXIT_CRITICAL(&s_textMux);
        if (s_lblAssistantText) {
            lv_label_set_text(s_lblAssistantText, buf);
        }
    }

    if (s_hasPendingVadText) {
        char buf[64];
        portENTER_CRITICAL(&s_textMux);
        strncpy(buf, s_pendingVadText, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        s_hasPendingVadText = false;
        portEXIT_CRITICAL(&s_textMux);
        if (s_lblVad) {
            lv_label_set_text(s_lblVad, buf);
        }
    }

    // 3. Mise à jour de l'indicateur de statut connexion (périodique toutes les 500 ms)
    static uint32_t s_lastConnUpdate = 0;
    if (nowMs - s_lastConnUpdate >= 500) {
        s_lastConnUpdate = nowMs;
        if (s_lblConn) {
            bool wifiOk = (WiFi.status() == WL_CONNECTED);
            bool sessionOk = (s_voice.getSession().getState() == LiveSessionState::CONNECTED);
            char connBuf[64];
            snprintf(connBuf, sizeof(connBuf), "WiFi: %s | Session: %s",
                     wifiOk ? "OK" : "NO",
                     sessionOk ? "CONNECTED" : "WAIT");
            lv_label_set_text(s_lblConn, connBuf);
        }
    }

    // 4. Mise à jour de l'état graphique
    updateUiState(s_interaction.getState());

    // 5. Exécution du timer LVGL
    lv_timer_handler();
    delay(5);
}
