/**
 * @file main.cpp
 * @brief VoiceKit v0.6.5 — Exemple de Référence IntegratedVoiceDemo
 *        Démonstration de l'orchestrateur IntegratedVoicePipeline
 *        sur M5Stack CoreS3 (ESP32-S3, 8 Mo PSRAM, 320x240 LCD tactile + LVGL 9).
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <lvgl.h>
#include <WiFi.h>

#include <VoxCoreVoice.h>
#include <IntegratedVoicePipeline.h>
#include "FrenchFonts.h"

#if __has_include("secrets.h")
    #include "secrets.h"
#endif

#ifndef WIFI_SSID
    #define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
    #ifdef WIFI_PASS
        #define WIFI_PASSWORD WIFI_PASS
    #else
        #define WIFI_PASSWORD ""
    #endif
#endif

#ifndef GEMINI_API_KEY
    #ifdef GEMINI_KEY
        #define GEMINI_API_KEY GEMINI_KEY
    #else
        #define GEMINI_API_KEY ""
    #endif
#endif

#ifndef OPENAI_API_KEY
    #define OPENAI_API_KEY ""
#endif

// =============================================================================
// Objets Globaux
// =============================================================================

static VoxCoreVoice             s_voice;
static ToolRegistry             s_tools;
static IntegratedVoicePipeline  s_pipeline;
static lv_display_t*            s_disp = nullptr;

// =============================================================================
// LVGL 9 Configuration et Tampons
// =============================================================================

static constexpr uint32_t SCREEN_WIDTH   = 320;
static constexpr uint32_t SCREEN_HEIGHT  = 240;
static constexpr uint32_t DRAW_BUF_LINES = 20;

alignas(4) static uint8_t s_drawBuf[SCREEN_WIDTH * DRAW_BUF_LINES * sizeof(uint16_t)];

// Widgets UI
static lv_obj_t* s_lblStateBadge    = nullptr;
static lv_obj_t* s_lblUserText      = nullptr;
static lv_obj_t* s_lblResponseText  = nullptr;
static lv_obj_t* s_pnlConfirm       = nullptr;
static lv_obj_t* s_lblConfirmPrompt = nullptr;
static lv_obj_t* s_btnPtt           = nullptr;
static lv_obj_t* s_lblPtt           = nullptr;
static lv_obj_t* s_btnStop          = nullptr;

// Tampons de texte pour découplage thread-safe
static char s_userTextBuf[256]     = "Appuyez sur PARLER pour dicter une commande.";
static char s_responseTextBuf[256] = "En attente...";
static bool s_userTextDirty        = true;
static bool s_responseTextDirty    = true;
static String s_pendingConfirmText = "";
static bool s_showConfirmModal     = false;

// =============================================================================
// Callbacks LVGL 9
// =============================================================================

static void dispFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    M5.Display.startWrite();
    M5.Display.pushImage(area->x1, area->y1, w, h, (const uint16_t*)px_map);
    M5.Display.endWrite();

    lv_display_flush_ready(disp);
}

static void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
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

static void updatePttUi(bool isListening, bool isSpeaking, bool isReady) {
    if (!s_btnPtt || !s_lblPtt) return;
    if (isListening) {
        lv_label_set_text(s_lblPtt, "Relâcher pour envoyer");
        lv_obj_set_style_bg_color(s_btnPtt, lv_color_hex(0xC62828), 0);
    } else if (isSpeaking) {
        lv_label_set_text(s_lblPtt, "Réponse en cours...");
        lv_obj_set_style_bg_color(s_btnPtt, lv_color_hex(0x2E7D32), 0);
    } else if (isReady) {
        lv_label_set_text(s_lblPtt, "Parler (PTT)");
        lv_obj_set_style_bg_color(s_btnPtt, lv_color_hex(0x1565C0), 0);
    } else {
        lv_label_set_text(s_lblPtt, "En attente service...");
        lv_obj_set_style_bg_color(s_btnPtt, lv_color_hex(0x424242), 0);
    }
}

static void onPttButtonEvent(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        if (!s_voice.isReady() && s_voice.getState() != VoiceState::READY) {
            Serial.println("[UI] PTT Pressé mais service vocal pas encore prêt.");
            snprintf(s_userTextBuf, sizeof(s_userTextBuf), "Service vocal non prêt (connexion en cours)...");
            s_userTextDirty = true;
            return;
        }
        Serial.println("[UI] PTT Pressed -> startListening()");
        s_pipeline.startListening();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_pipeline.getState() == PipelineState::LISTENING) {
            Serial.println("[UI] PTT Released -> stopListening()");
            s_pipeline.stopListening();
        }
    }
}

static void onStopButtonEvent(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        Serial.println("[UI] STOP Clicked -> cancel()");
        s_pipeline.cancel();
    }
}

static void onConfirmOkEvent(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        Serial.println("[UI] Action Confirmée par l'utilisateur !");
        s_pipeline.confirmPendingAction();
    }
}

static void onConfirmCancelEvent(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        Serial.println("[UI] Action Refusée par l'utilisateur !");
        s_pipeline.rejectPendingAction();
    }
}

// =============================================================================
// Construction de l'Interface Graphique LVGL 9
// =============================================================================

static void createUi() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x121212), 0);

    // --- 1. En-tête ---
    lv_obj_t* header = lv_obj_create(scr);
    lv_obj_set_size(header, 320, 32);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblTitle = lv_label_create(header);
    lv_label_set_text(lblTitle, "VoiceKit v0.6.5.1 Pipeline");
    lv_obj_set_style_text_font(lblTitle, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(lblTitle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lblTitle, LV_ALIGN_LEFT_MID, 6, 0);

    s_lblStateBadge = lv_label_create(header);
    lv_label_set_text(s_lblStateBadge, "CONNEXION...");
    lv_obj_set_style_text_font(s_lblStateBadge, &font_french_12, 0);
    lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0xFFAB00), 0);
    lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x4E3400), 0);
    lv_obj_set_style_bg_opa(s_lblStateBadge, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_lblStateBadge, 6, 0);
    lv_obj_set_style_pad_ver(s_lblStateBadge, 2, 0);
    lv_obj_set_style_radius(s_lblStateBadge, 4, 0);
    lv_obj_align(s_lblStateBadge, LV_ALIGN_RIGHT_MID, -6, 0);

    // --- 2. Zone Centrale (Transcription et Réponse) ---
    lv_obj_t* body = lv_obj_create(scr);
    lv_obj_set_size(body, 308, 146);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x181818), 0);
    lv_obj_set_style_border_color(body, lv_color_hex(0x282828), 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_radius(body, 6, 0);
    lv_obj_set_style_pad_all(body, 8, 0);

    s_lblUserText = lv_label_create(body);
    lv_obj_set_width(s_lblUserText, 290);
    lv_label_set_long_mode(s_lblUserText, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_lblUserText, s_userTextBuf);
    lv_obj_set_style_text_font(s_lblUserText, &font_french_14, 0);
    lv_obj_set_style_text_color(s_lblUserText, lv_color_hex(0x90CAF9), 0);
    lv_obj_align(s_lblUserText, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lblResponseText = lv_label_create(body);
    lv_obj_set_width(s_lblResponseText, 290);
    lv_label_set_long_mode(s_lblResponseText, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_lblResponseText, s_responseTextBuf);
    lv_obj_set_style_text_font(s_lblResponseText, &font_french_14, 0);
    lv_obj_set_style_text_color(s_lblResponseText, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_lblResponseText, LV_ALIGN_TOP_LEFT, 0, 60);

    // --- 3. Modal de Confirmation pour Action SENSITIVE ---
    s_pnlConfirm = lv_obj_create(scr);
    lv_obj_set_size(s_pnlConfirm, 280, 110);
    lv_obj_center(s_pnlConfirm);
    lv_obj_set_style_bg_color(s_pnlConfirm, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_color(s_pnlConfirm, lv_color_hex(0xFF9800), 0);
    lv_obj_set_style_border_width(s_pnlConfirm, 2, 0);
    lv_obj_set_style_radius(s_pnlConfirm, 8, 0);
    lv_obj_set_style_pad_all(s_pnlConfirm, 8, 0);
    lv_obj_add_flag(s_pnlConfirm, LV_OBJ_FLAG_HIDDEN);

    s_lblConfirmPrompt = lv_label_create(s_pnlConfirm);
    lv_obj_set_width(s_lblConfirmPrompt, 260);
    lv_label_set_long_mode(s_lblConfirmPrompt, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_lblConfirmPrompt, "Confirmer l'action sensible ?");
    lv_obj_set_style_text_font(s_lblConfirmPrompt, &font_french_14, 0);
    lv_obj_set_style_text_color(s_lblConfirmPrompt, lv_color_hex(0xFFB74D), 0);
    lv_obj_align(s_lblConfirmPrompt, LV_ALIGN_TOP_MID, 0, 2);

    lv_obj_t* btnOk = lv_button_create(s_pnlConfirm);
    lv_obj_set_size(btnOk, 110, 36);
    lv_obj_align(btnOk, LV_ALIGN_BOTTOM_LEFT, 10, -4);
    lv_obj_set_style_bg_color(btnOk, lv_color_hex(0x388E3C), 0);
    lv_obj_add_event_cb(btnOk, onConfirmOkEvent, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lblOk = lv_label_create(btnOk);
    lv_label_set_text(lblOk, "Confirmer");
    lv_obj_set_style_text_font(lblOk, &font_french_bold_16, 0);
    lv_obj_center(lblOk);

    lv_obj_t* btnCancel = lv_button_create(s_pnlConfirm);
    lv_obj_set_size(btnCancel, 110, 36);
    lv_obj_align(btnCancel, LV_ALIGN_BOTTOM_RIGHT, -10, -4);
    lv_obj_set_style_bg_color(btnCancel, lv_color_hex(0xD32F2F), 0);
    lv_obj_add_event_cb(btnCancel, onConfirmCancelEvent, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lblCancel = lv_label_create(btnCancel);
    lv_label_set_text(lblCancel, "Refuser");
    lv_obj_set_style_text_font(lblCancel, &font_french_bold_16, 0);
    lv_obj_center(lblCancel);

    // --- 4. Barre Inférieure (Boutons PTT et STOP) ---
    s_btnPtt = lv_button_create(scr);
    lv_obj_set_size(s_btnPtt, 195, 48);
    lv_obj_align(s_btnPtt, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_set_style_bg_color(s_btnPtt, lv_color_hex(0x424242), 0);
    lv_obj_set_style_radius(s_btnPtt, 6, 0);
    lv_obj_add_event_cb(s_btnPtt, onPttButtonEvent, LV_EVENT_ALL, nullptr);

    s_lblPtt = lv_label_create(s_btnPtt);
    lv_label_set_text(s_lblPtt, "En attente service...");
    lv_obj_set_style_text_font(s_lblPtt, &font_french_14, 0);
    lv_obj_center(s_lblPtt);

    s_btnStop = lv_button_create(scr);
    lv_obj_set_size(s_btnStop, 105, 48);
    lv_obj_align(s_btnStop, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    lv_obj_set_style_bg_color(s_btnStop, lv_color_hex(0xD32F2F), 0);
    lv_obj_set_style_radius(s_btnStop, 6, 0);
    lv_obj_add_event_cb(s_btnStop, onStopButtonEvent, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* lblStop = lv_label_create(s_btnStop);
    lv_label_set_text(lblStop, "STOP");
    lv_obj_set_style_text_font(lblStop, &font_french_bold_16, 0);
    lv_obj_center(lblStop);
}

// =============================================================================
// Enregistrement des Outils de Démonstration (ToolRegistry)
// =============================================================================

static bool registerDemoTools() {
    bool ok = true;

    // 1. Outil Lecture Seule : État général du système
    ok &= s_tools.registerTool(
        "system.get_status",
        "Obtenir le statut general et la batterie",
        []() {
            int battery = M5.Power.getBatteryLevel();
            String res = "Système opérationnel, batterie à " + String(battery) + " pour cent.";
            return ToolResult::success(res);
        },
        ToolSecurityPolicy::READ_ONLY
    );

    // 2. Outil Lecture Seule : Mémoire libre
    ok &= s_tools.registerTool(
        "system.get_free_memory",
        "Obtenir la memoire interne et PSRAM disponible",
        []() {
            uint32_t freeInt = (uint32_t)ESP.getFreeHeap() / 1024;
            uint32_t freePsram = (uint32_t)ESP.getFreePsram() / 1024;
            String res = "Mémoire interne libre " + String(freeInt) + " Ko, PSRAM libre " + String(freePsram) + " Ko.";
            return ToolResult::success(res);
        },
        ToolSecurityPolicy::READ_ONLY
    );

    // 3. Outil Écriture : Réglage luminosité
    ToolDefinition brightDef("system.set_brightness", "Regler la luminosite de l'ecran (0 a 100)", ToolSecurityPolicy::WRITE);
    brightDef.addParam(ToolParam("level", ToolParamType::INT, 0, 100, true, "Niveau de 0 a 100"));
    brightDef.setHandler([](const ToolArgs& args) {
        int level = args.getInt("level", 80);
        if (level < 0) level = 0;
        if (level > 100) level = 100;
        uint8_t hwLevel = (uint8_t)((level * 255) / 100);
        M5.Display.setBrightness(hwLevel);
        return ToolResult::success("Luminosité réglée à " + String(level) + " pour cent.");
    });
    ok &= s_tools.registerTool(brightDef);

    // 4. Outil Sensible : Réinitialisation écran (Requiert confirmation explicite !)
    ok &= s_tools.registerTool(
        "system.reset_screen",
        "Reinitialiser l'affichage graphique de l'ecran",
        []() {
            M5.Display.fillScreen(TFT_BLACK);
            lv_obj_invalidate(lv_screen_active());
            return ToolResult::success("Écran réinitialisé avec succès.");
        },
        ToolSecurityPolicy::SENSITIVE
    );

    // Mappages en langage naturel français vers VoiceCommandDispatcher
    VoiceCommandDispatcher& disp = s_pipeline.getDispatcher();
    ok &= disp.registerMapping("system.get_status", {"statut", "etat", "état", "etat du systeme", "état du système", "quel est le statut"});
    ok &= disp.registerMapping("system.get_free_memory", {"memoire", "mémoire", "memoire libre", "mémoire libre", "espace memoire", "ram"});
    ok &= disp.registerMapping("system.set_brightness", {"luminosite", "luminosité", "regle la luminosite", "règle la luminosité à", "mets la luminosite a"});
    ok &= disp.registerMapping("system.reset_screen", {"reinitialise l'ecran", "réinitialise l'écran", "reset ecran", "reinitialiser ecran"});

    return ok;
}

// =============================================================================
// Commandes Série de Diagnostic et Contrôle
// =============================================================================

static void printHelp() {
    Serial.println("\n--- Commandes Série IntegratedVoiceDemo ---");
    Serial.println("  p       : Démarrer l'écoute vocale (PTT ON)");
    Serial.println("  r       : Arrêter l'écoute vocale (PTT OFF -> Transcrire)");
    Serial.println("  s       : STOP prioritaire");
    Serial.println("  c       : Confirmer l'action sensible en attente");
    Serial.println("  x       : Refuser l'action sensible en attente");
    Serial.println("  t <txt> : Injecter un texte simulé (ex: 't statut')");
    Serial.println("  o <0..3>: Régler la rotation écran (0=Paysage normal, 2=Paysage inv, 1=Portrait)");
    Serial.println("  ?       : Afficher cette aide");
    Serial.println("-------------------------------------------\n");
}

static void handleSerialCommand(const String& cmd) {
    if (cmd.length() == 0) return;
    char first = cmd[0];

    if (first == 'p' || first == 'P') {
        Serial.println("[CMD] P -> Démarrer écoute");
        s_pipeline.startListening();
    } else if (first == 'r' || first == 'R') {
        Serial.println("[CMD] R -> Arrêter écoute");
        s_pipeline.stopListening();
    } else if (first == 's' || first == 'S') {
        Serial.println("[CMD] S -> STOP prioritaire");
        s_pipeline.cancel();
    } else if (first == 'c' || first == 'C') {
        Serial.println("[CMD] C -> Confirmer action");
        s_pipeline.confirmPendingAction();
    } else if (first == 'x' || first == 'X') {
        Serial.println("[CMD] X -> Refuser action");
        s_pipeline.rejectPendingAction();
    } else if ((first == 't' || first == 'T') && cmd.length() > 2) {
        String sim = cmd.substring(2);
        sim.trim();
        Serial.printf("[CMD] T -> Test texte : \"%s\"\n", sim.c_str());
        snprintf(s_userTextBuf, sizeof(s_userTextBuf), "Vous (simulé) : %s", sim.c_str());
        s_userTextDirty = true;
        s_pipeline.feedSimulatedTranscription(sim, true);
    } else if ((first == 'o' || first == 'O') && cmd.length() > 2) {
        int r = cmd.substring(2).toInt();
        if (r >= 0 && r <= 3) {
            M5.Display.setRotation(r);
            Serial.printf("[DISPLAY] Rotation réglee à %d (%dx%d)\n", r, (int)M5.Display.width(), (int)M5.Display.height());
            if (s_disp != nullptr) {
                lv_display_set_resolution(s_disp, M5.Display.width(), M5.Display.height());
            }
            lv_obj_invalidate(lv_screen_active());
        }
    } else if (first == '?') {
        printHelp();
    }
}

// =============================================================================
// Initialisation (setup)
// =============================================================================

void setup() {
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = true;
    M5.begin(cfg);

    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  VoiceKit v0.6.5 — Integrated Voice Pipeline Demo       ");
    Serial.println("  M5Stack CoreS3 (ESP32-S3, 8 Mo PSRAM, LVGL 9)          ");
    Serial.println("========================================================");

    // 1. Initialisation Affichage M5GFX (Orientation Paysage 320x240) et LVGL 9
    // Sur M5Stack CoreS3, rotation 0 = 320x240 Paysage Normal (caméra en haut).
    M5.Display.waitDisplay();
    M5.Display.setRotation(0);
    M5.Display.setBrightness(220);
    M5.Display.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });

    int32_t screenW = M5.Display.width();
    int32_t screenH = M5.Display.height();

    s_disp = lv_display_create(screenW, screenH);
    lv_display_set_buffers(s_disp, s_drawBuf, nullptr, sizeof(s_drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(s_disp, dispFlushCb);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchReadCb);

    createUi();
    lv_timer_handler();

    // 2. Configuration du Moteur Vocal VoxCoreVoice en mode MANAGED_WIFI
    VoiceKitConfig vcfg;
    if (strlen(GEMINI_API_KEY) > 0) {
        vcfg.provider = VoiceProviderType::GEMINI;
        vcfg.apiKey = GEMINI_API_KEY;
    } else if (strlen(OPENAI_API_KEY) > 0) {
        vcfg.provider = VoiceProviderType::OPENAI;
        vcfg.apiKey = OPENAI_API_KEY;
    }
    vcfg.wifiMode = WiFiMode::MANAGED_WIFI;
    vcfg.wifiSSID = WIFI_SSID;
    vcfg.wifiPassword = WIFI_PASSWORD;
    vcfg.volume = 75;

    bool voiceOk = s_voice.begin(vcfg);
    if (!voiceOk) {
        Serial.println("[MAIN] ERREUR : Echec initialisation VoxCoreVoice !");
        if (s_lblStateBadge) {
            lv_label_set_text(s_lblStateBadge, "ERR INIT VOCAL");
            lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0xFF5252), 0);
            lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x490000), 0);
        }
        snprintf(s_responseTextBuf, sizeof(s_responseTextBuf), "Erreur: Echec initialisation du moteur vocal.");
        s_responseTextDirty = true;
    }

    // 3. Enregistrement des outils locaux
    bool toolsOk = registerDemoTools();
    if (!toolsOk) {
        Serial.println("[MAIN] ERREUR : Echec enregistrement outils ou mappages !");
        if (s_lblStateBadge) {
            lv_label_set_text(s_lblStateBadge, "ERR OUTILS");
            lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0xFF5252), 0);
            lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x490000), 0);
        }
        snprintf(s_responseTextBuf, sizeof(s_responseTextBuf), "Erreur: Echec enregistrement des outils locaux.");
        s_responseTextDirty = true;
    }

    // 4. Configuration et Démarrage du Pipeline Intégré
    VoicePipelineConfig pcfg;
    pcfg.enableVoiceUx = true;
    pcfg.enableLocalTools = true;
    pcfg.enableAiProcessing = true;
    pcfg.requireConfirmationForSensitive = true;
    pcfg.speakToolResult = true;
    pcfg.speakConfirmationRequired = true;

    bool pipeOk = s_pipeline.begin(s_voice, s_tools, pcfg);
    if (!pipeOk) {
        Serial.println("[MAIN] ERREUR : Echec initialisation IntegratedVoicePipeline !");
        if (s_lblStateBadge) {
            lv_label_set_text(s_lblStateBadge, "ERR PIPELINE");
            lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0xFF5252), 0);
            lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x490000), 0);
        }
        snprintf(s_responseTextBuf, sizeof(s_responseTextBuf), "Erreur: Echec demarrage du pipeline integre.");
        s_responseTextDirty = true;
    }

    // 5. Abonnements aux Événements du Pipeline & Moteur Vocal
    s_voice.onReady([]() {
        Serial.println("[MAIN] VoiceKit service vocal connecté et PRÊT !");
        if (s_lblStateBadge && s_pipeline.getState() == PipelineState::READY) {
            lv_label_set_text(s_lblStateBadge, "PRÊT");
            lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0x00E676), 0);
            lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x003311), 0);
        }
        updatePttUi(false, false, true);
    });

    s_voice.onStateChanged([](VoiceState st) {
        if (!s_voice.isReady() && s_pipeline.getState() == PipelineState::READY) {
            if (s_lblStateBadge) {
                switch (st) {
                    case VoiceState::CONNECTING_WIFI:
                        lv_label_set_text(s_lblStateBadge, "WIFI...");
                        lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0xFF9800), 0);
                        lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x4E2600), 0);
                        break;
                    case VoiceState::CONNECTING_SERVICE:
                        lv_label_set_text(s_lblStateBadge, "CONNEXION IA...");
                        lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0xFFEB3B), 0);
                        lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x4E4B00), 0);
                        break;
                    case VoiceState::ERROR_STATE:
                        lv_label_set_text(s_lblStateBadge, "ERREUR");
                        lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0xFF5252), 0);
                        lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x490000), 0);
                        break;
                    default:
                        break;
                }
            }
        }
    });

    s_pipeline.onStateChanged([](PipelineState oldState, PipelineState newState) {
        Serial.printf("[PIPELINE] Transition : %s -> %s\n",
                      pipelineStateToString(oldState), pipelineStateToString(newState));
        if (s_lblStateBadge != nullptr) {
            lv_label_set_text(s_lblStateBadge, pipelineStateToString(newState));
            switch (newState) {
                case PipelineState::READY:
                    lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0x00E676), 0);
                    lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x003311), 0);
                    s_showConfirmModal = false;
                    updatePttUi(false, false, s_voice.isReady());
                    break;
                case PipelineState::LISTENING:
                    lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0x40C4FF), 0);
                    lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x002B49), 0);
                    updatePttUi(true, false, false);
                    break;
                case PipelineState::TRANSCRIBING:
                    lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0xE040FB), 0);
                    lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x38003D), 0);
                    updatePttUi(false, false, false);
                    break;
                case PipelineState::WAITING_CONFIRMATION:
                    lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0xFFAB00), 0);
                    lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x4E3400), 0);
                    s_showConfirmModal = true;
                    updatePttUi(false, false, false);
                    break;
                case PipelineState::SPEAKING:
                    lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0x69F0AE), 0);
                    lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x00391E), 0);
                    updatePttUi(false, true, false);
                    break;
                case PipelineState::ERROR:
                    lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0xFF5252), 0);
                    lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x490000), 0);
                    updatePttUi(false, false, false);
                    break;
                default:
                    lv_obj_set_style_text_color(s_lblStateBadge, lv_color_hex(0xFFD740), 0);
                    lv_obj_set_style_bg_color(s_lblStateBadge, lv_color_hex(0x3B3000), 0);
                    break;
            }
        }
    });

    s_pipeline.onTranscription([](uint32_t turnId, const String& text, bool isFinal) {
        snprintf(s_userTextBuf, sizeof(s_userTextBuf), "Vous : %s%s", text.c_str(), isFinal ? "" : " ...");
        s_userTextDirty = true;
    });

    s_pipeline.onToolResult([](uint32_t turnId, const String& toolId, const ToolResult& res) {
        if (res.isSuccess()) {
            snprintf(s_responseTextBuf, sizeof(s_responseTextBuf), "Outil [%s] :\n%s", toolId.c_str(), res.output.c_str());
        } else {
            snprintf(s_responseTextBuf, sizeof(s_responseTextBuf), "Erreur [%s] :\n%s", toolId.c_str(), res.error.c_str());
        }
        s_responseTextDirty = true;
    });

    s_pipeline.onAiResponse([](uint32_t turnId, const String& response) {
        snprintf(s_responseTextBuf, sizeof(s_responseTextBuf), "IA :\n%s", response.c_str());
        s_responseTextDirty = true;
    });

    s_pipeline.onConfirmationRequired([](uint32_t turnId, const String& toolId, const String& token, const String& prompt) {
        s_pendingConfirmText = "Action SENSIBLE [" + toolId + "]\n" + prompt;
        s_showConfirmModal = true;
    });

    s_pipeline.onError([](uint32_t turnId, const String& error) {
        snprintf(s_responseTextBuf, sizeof(s_responseTextBuf), "Erreur :\n%s", error.c_str());
        s_responseTextDirty = true;
    });

    s_pipeline.onTurnFinished([](uint32_t turnId, PipelineResult result, const String& summary) {
        Serial.printf("[PIPELINE] Fin tour #%u : Résultat=%s (\"%s\")\n",
                      (unsigned)turnId, pipelineResultToString(result), summary.c_str());
    });

    printHelp();
    Serial.println("[MAIN] Initialisation terminée avec succès. Connexion réseau en cours...");
}

// =============================================================================
// Boucle Principale Coopérative (loop)
// =============================================================================

void loop() {
    M5.update();
    s_pipeline.update();

    // Commandes reçues via la liaison série
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        handleSerialCommand(line);
    }

    // Rafraîchissement des textes de l'UI
    if (s_userTextDirty && s_lblUserText != nullptr) {
        lv_label_set_text(s_lblUserText, s_userTextBuf);
        s_userTextDirty = false;
    }
    if (s_responseTextDirty && s_lblResponseText != nullptr) {
        lv_label_set_text(s_lblResponseText, s_responseTextBuf);
        s_responseTextDirty = false;
    }

    // Gestion de l'affichage de la fenêtre modale de confirmation
    if (s_pnlConfirm != nullptr) {
        if (s_showConfirmModal && lv_obj_has_flag(s_pnlConfirm, LV_OBJ_FLAG_HIDDEN)) {
            if (!s_pendingConfirmText.isEmpty() && s_lblConfirmPrompt != nullptr) {
                lv_label_set_text(s_lblConfirmPrompt, s_pendingConfirmText.c_str());
            }
            lv_obj_clear_flag(s_pnlConfirm, LV_OBJ_FLAG_HIDDEN);
        } else if (!s_showConfirmModal && !lv_obj_has_flag(s_pnlConfirm, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(s_pnlConfirm, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_timer_handler();
    delay(5);
}
