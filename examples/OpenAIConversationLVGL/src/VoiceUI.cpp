/**
 * @file VoiceUI.cpp
 * @brief Implementation de l'interface utilisateur LVGL 9 pour OpenAIConversationLVGL.
 */

#include "VoiceUI.h"

// Allocation du buffer partiel dans la memoire interne DMA
alignas(4) uint8_t VoiceUI::s_drawBuf[AppConfig::BUFFER_PIXELS * sizeof(uint16_t)];

// Pointeur statique pour les callbacks C de LVGL
static VoiceUI* s_activeVoiceUI = nullptr;

VoiceUI::VoiceUI() {
    s_activeVoiceUI = this;
}

VoiceUI::~VoiceUI() {
    if (s_activeVoiceUI == this) {
        s_activeVoiceUI = nullptr;
    }
}

void VoiceUI::flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // Note d'architecture (Audit v0.1.0.1) :
    // M5.Display.pushImage() realise un transfert synchrone sur le bus SPI vers le controleur ILI9342C.
    // L'appel a lv_display_flush_ready(disp) est realise immediatement a la sortie de endWrite()
    // pour clore le cycle de rafraichissement sans bloquer la boucle cooperative.
    uint32_t t0 = micros();
    M5.Display.startWrite();
    M5.Display.pushImage(area->x1, area->y1, w, h, (const uint16_t*)px_map);
    M5.Display.endWrite();
    uint32_t durUs = micros() - t0;

    if (s_activeVoiceUI && s_activeVoiceUI->_diag) {
        s_activeVoiceUI->_diag->recordFlush(durUs);
        s_activeVoiceUI->_diag->logLvglFlush(w, h, durUs);
    }

    lv_display_flush_ready(disp);
}

void VoiceUI::touchReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    if (M5.Touch.getCount() > 0) {
        auto detail = M5.Touch.getDetail(0);
        if (detail.isPressed()) {
            data->state = LV_INDEV_STATE_PRESSED;
            data->point.x = detail.x;
            data->point.y = detail.y;
            if (s_activeVoiceUI) {
                s_activeVoiceUI->_physicalTouchActive = true;
                if (s_activeVoiceUI->_diag) {
                    s_activeVoiceUI->_diag->recordTouch();
                }
            }
            return;
        }
    }

    data->state = LV_INDEV_STATE_RELEASED;
    if (s_activeVoiceUI) {
        s_activeVoiceUI->_physicalTouchActive = false;
    }
}

void VoiceUI::buttonEventCallback(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    VoiceUI* ui = (VoiceUI*)lv_event_get_user_data(e);
    if (!ui || !ui->_ptt) return;

    static uint32_t s_contactIdCounter = 0;
    lv_point_t point = {160, 215};
    lv_indev_t* indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &point);
    }

    if (code == LV_EVENT_PRESSED) {
        s_contactIdCounter++;
        ui->_ptt->onButtonPress(s_contactIdCounter, point.x, point.y);
    } else if (code == LV_EVENT_RELEASED) {
        ui->_ptt->onButtonRelease(s_contactIdCounter, point.x, point.y);
    } else if (code == LV_EVENT_PRESS_LOST) {
        ui->_ptt->onButtonPressLost(s_contactIdCounter, point.x, point.y);
    }
}

static uint32_t lvglTickCallback() {
    return (uint32_t)millis();
}

bool VoiceUI::init(PTTController* ptt, Diagnostics* diag) {
    _ptt = ptt;
    _diag = diag;

    // 1. Initialisation coeur LVGL 9
    lv_init();
    lv_tick_set_cb(lvglTickCallback);

    // 2. Configuration affichage M5GFX
    M5.Display.waitDisplay();
    M5.Display.setRotation(1);
    M5.Display.setBrightness(220);
    M5.Display.fillScreen(AppConfig::COLOR_BG_HEX);

    int32_t screenW = M5.Display.width();
    int32_t screenH = M5.Display.height();

    _disp = lv_display_create(screenW, screenH);
    if (!_disp) {
        Serial.println("[VoiceUI][ERR] Echec lv_display_create()");
        return false;
    }

    lv_display_set_buffers(_disp, s_drawBuf, nullptr, sizeof(s_drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(_disp, flushCallback);

    // 3. Configuration pilote tactile LVGL 9
    _indev = lv_indev_create();
    if (!_indev) {
        Serial.println("[VoiceUI][ERR] Echec lv_indev_create()");
        return false;
    }
    lv_indev_set_type(_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(_indev, touchReadCallback);

    // 4. Construction de l'arborescence de widgets
    buildWidgets();

    _initialized = true;
    Serial.printf("[VoiceUI] LVGL 9 initialise avec succes (%dx%d, buffer partiel %d lignes, police francaise Segoe UI).\n",
                  (int)screenW, (int)screenH, (int)AppConfig::BUFFER_LINES);
    return true;
}

void VoiceUI::buildWidgets() {
    _scr = lv_screen_active();
    lv_obj_set_style_bg_color(_scr, lv_color_hex(AppConfig::COLOR_BG_HEX), 0);
    lv_obj_set_style_bg_opa(_scr, LV_OPA_COVER, 0);

    // -------------------------------------------------------------
    // En-tete (Y 0..30)
    // -------------------------------------------------------------
    _header = lv_obj_create(_scr);
    lv_obj_remove_style_all(_header);
    lv_obj_set_size(_header, AppConfig::SCREEN_WIDTH, AppConfig::HEADER_HEIGHT);
    lv_obj_set_pos(_header, 0, AppConfig::HEADER_Y);
    lv_obj_set_scrollable(_header, false);

    // Titre de l'application
    _label_title = lv_label_create(_header);
    lv_label_set_text(_label_title, "OpenAI Realtime");
    lv_obj_set_style_text_font(_label_title, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(_label_title, lv_color_hex(AppConfig::COLOR_TEXT_PRI_HEX), 0);
    lv_obj_set_pos(_label_title, 12, 6);

    // Statut Wi-Fi
    _label_wifi = lv_label_create(_header);
    lv_label_set_text(_label_wifi, "Wi-Fi --");
    lv_obj_set_style_text_font(_label_wifi, &font_french_12, 0);
    lv_obj_set_style_text_color(_label_wifi, lv_color_hex(AppConfig::COLOR_TEXT_MUTED_HEX), 0);
    lv_obj_set_pos(_label_wifi, 175, 8);

    // Badge d'etat court
    _badge_state = lv_obj_create(_header);
    lv_obj_remove_style_all(_badge_state);
    lv_obj_set_size(_badge_state, 68, 20);
    lv_obj_set_pos(_badge_state, 240, 5);
    lv_obj_set_style_radius(_badge_state, 10, 0);
    lv_obj_set_style_bg_color(_badge_state, lv_color_hex(0x2C3E50), 0);
    lv_obj_set_style_bg_opa(_badge_state, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(_badge_state, false);

    _label_badge = lv_label_create(_badge_state);
    lv_label_set_text(_label_badge, "ATTENTE");
    lv_obj_set_style_text_font(_label_badge, &font_french_12, 0);
    lv_obj_set_style_text_color(_label_badge, lv_color_hex(AppConfig::COLOR_TEXT_PRI_HEX), 0);
    lv_obj_center(_label_badge);

    // -------------------------------------------------------------
    // Orbe vocal anime (Y 35..108)
    // -------------------------------------------------------------
    _orb.init(_scr);

    // -------------------------------------------------------------
    // Texte d'etat principal (Y 110..132)
    // -------------------------------------------------------------
    _label_state_text = lv_label_create(_scr);
    lv_obj_set_size(_label_state_text, AppConfig::SCREEN_WIDTH - 20, AppConfig::STATE_TEXT_HEIGHT);
    lv_obj_set_pos(_label_state_text, 10, AppConfig::STATE_TEXT_Y);
    lv_obj_set_style_text_font(_label_state_text, &font_french_14, 0);
    lv_obj_set_style_text_color(_label_state_text, lv_color_hex(AppConfig::COLOR_CYAN_HEX), 0);
    lv_obj_set_style_text_align(_label_state_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_label_state_text, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(_label_state_text, "Connexion à OpenAI Realtime...");

    // -------------------------------------------------------------
    // Carte transcription Utilisateur (Y 136..164)
    // -------------------------------------------------------------
    _card_user = lv_obj_create(_scr);
    lv_obj_remove_style_all(_card_user);
    lv_obj_set_size(_card_user, AppConfig::USER_CARD_W, AppConfig::USER_CARD_H);
    lv_obj_set_pos(_card_user, AppConfig::USER_CARD_X, AppConfig::USER_CARD_Y);
    lv_obj_set_style_radius(_card_user, 6, 0);
    lv_obj_set_style_bg_color(_card_user, lv_color_hex(AppConfig::COLOR_CARD_BG_HEX), 0);
    lv_obj_set_style_bg_opa(_card_user, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_card_user, lv_color_hex(AppConfig::COLOR_CARD_BORDER_HEX), 0);
    lv_obj_set_style_border_width(_card_user, 1, 0);
    lv_obj_set_scrollable(_card_user, false);

    _label_user_content = lv_label_create(_card_user);
    lv_obj_set_size(_label_user_content, AppConfig::USER_CARD_W - 16, AppConfig::USER_CARD_H - 6);
    lv_obj_set_pos(_label_user_content, 8, 5);
    lv_obj_set_style_text_font(_label_user_content, &font_french_12, 0);
    lv_obj_set_style_text_color(_label_user_content, lv_color_hex(AppConfig::COLOR_TEXT_SEC_HEX), 0);
    lv_label_set_long_mode(_label_user_content, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(_label_user_content, "Vous : (en attente d'elocution...)");

    // -------------------------------------------------------------
    // Carte transcription Assistant (Y 168..196)
    // -------------------------------------------------------------
    _card_asst = lv_obj_create(_scr);
    lv_obj_remove_style_all(_card_asst);
    lv_obj_set_size(_card_asst, AppConfig::ASST_CARD_W, AppConfig::ASST_CARD_H);
    lv_obj_set_pos(_card_asst, AppConfig::ASST_CARD_X, AppConfig::ASST_CARD_Y);
    lv_obj_set_style_radius(_card_asst, 6, 0);
    lv_obj_set_style_bg_color(_card_asst, lv_color_hex(AppConfig::COLOR_CARD_BG_HEX), 0);
    lv_obj_set_style_bg_opa(_card_asst, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_card_asst, lv_color_hex(AppConfig::COLOR_CARD_BORDER_HEX), 0);
    lv_obj_set_style_border_width(_card_asst, 1, 0);
    lv_obj_set_scrollable(_card_asst, false);

    _label_asst_content = lv_label_create(_card_asst);
    lv_obj_set_size(_label_asst_content, AppConfig::ASST_CARD_W - 16, AppConfig::ASST_CARD_H - 6);
    lv_obj_set_pos(_label_asst_content, 8, 5);
    lv_obj_set_style_text_font(_label_asst_content, &font_french_12, 0);
    lv_obj_set_style_text_color(_label_asst_content, lv_color_hex(AppConfig::COLOR_TEXT_PRI_HEX), 0);
    lv_label_set_long_mode(_label_asst_content, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(_label_asst_content, "Assistant : (reponse concise en attente...)");

    // -------------------------------------------------------------
    // Grand Bouton Principal (Y 200..235)
    // -------------------------------------------------------------
    _btn_main = lv_button_create(_scr);
    lv_obj_set_size(_btn_main, AppConfig::MAIN_BTN_W, AppConfig::MAIN_BTN_H);
    lv_obj_set_pos(_btn_main, AppConfig::MAIN_BTN_X, AppConfig::MAIN_BTN_Y);
    lv_obj_set_style_radius(_btn_main, 8, 0);
    lv_obj_set_style_shadow_width(_btn_main, 0, 0);
    lv_obj_set_style_border_width(_btn_main, 0, 0);
    lv_obj_add_event_cb(_btn_main, buttonEventCallback, LV_EVENT_ALL, this);

    _label_btn = lv_label_create(_btn_main);
    lv_obj_set_style_text_font(_label_btn, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(_label_btn, lv_color_hex(AppConfig::COLOR_TEXT_PRI_HEX), 0);
    lv_label_set_text(_label_btn, "CONNEXION EN COURS...");
    lv_obj_center(_label_btn);

    updateButtonVisuals(PTTState::DISABLED);
}

void VoiceUI::updateButtonVisuals(PTTState state) {
    if (!_btn_main || !_label_btn) return;

    uint32_t btnColorHex = 0x2C3E50;
    const char* btnText = "PATIENTEZ...";
    bool clickable = true;

    switch (state) {
        case PTTState::DISABLED:
            btnColorHex = 0x2C3E50;
            btnText = "CONNEXION EN COURS...";
            clickable = false;
            break;
        case PTTState::READY:
            btnColorHex = 0x0984E3; // Bleu lumineux
            btnText = "PARLER (MAINTENIR)";
            break;
        case PTTState::STARTING:
        case PTTState::LISTENING:
            btnColorHex = AppConfig::COLOR_CORAL_HEX; // Corail
            btnText = "RELACHEZ POUR ENVOYER";
            break;
        case PTTState::STOPPING_CAPTURE:
        case PTTState::PROCESSING:
            btnColorHex = AppConfig::COLOR_STOP_HEX; // Ambre / Orange STOP
            btnText = "STOP (INTERROMPRE)";
            break;
        case PTTState::SPEAKING:
            btnColorHex = AppConfig::COLOR_STOP_HEX;
            btnText = "STOP (INTERROMPRE)";
            break;
        case PTTState::INTERRUPTING:
            btnColorHex = 0x7F8C8D;
            btnText = "INTERRUPTION EN COURS...";
            clickable = false;
            break;
        case PTTState::RECOVERING:
            btnColorHex = 0x7F8C8D;
            btnText = "REPRISE EN COURS...";
            clickable = false;
            break;
        case PTTState::ERROR_STATE:
            btnColorHex = AppConfig::COLOR_RED_HEX;
            btnText = "REINITIALISER";
            break;
    }

    lv_obj_set_style_bg_color(_btn_main, lv_color_hex(btnColorHex), 0);
    lv_label_set_text(_label_btn, btnText);
    lv_obj_center(_label_btn);

    if (clickable) {
        lv_obj_clear_state(_btn_main, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(_btn_main, LV_STATE_DISABLED);
    }
}

void VoiceUI::updateStateLabel(PTTState state) {
    if (!_label_state_text || !_label_badge || !_badge_state) return;

    const char* stateText = "Prêt à dialoguer";
    const char* badgeText = "PRÊT";
    uint32_t badgeColorHex = AppConfig::COLOR_CYAN_HEX;
    uint32_t stateColorHex = AppConfig::COLOR_CYAN_HEX;

    switch (state) {
        case PTTState::DISABLED:
            stateText = "Connexion Wi-Fi / OpenAI en cours...";
            badgeText = "ATTENTE";
            badgeColorHex = 0x2C3E50;
            stateColorHex = AppConfig::COLOR_TEXT_MUTED_HEX;
            break;
        case PTTState::READY:
            stateText = "Prêt à dialoguer (maintenez PARLER)";
            badgeText = "PRÊT";
            badgeColorHex = 0x0984E3;
            stateColorHex = AppConfig::COLOR_CYAN_HEX;
            break;
        case PTTState::STARTING:
        case PTTState::LISTENING:
            stateText = "Écoute en cours... Parlez maintenant";
            badgeText = "ÉCOUTE";
            badgeColorHex = AppConfig::COLOR_CORAL_HEX;
            stateColorHex = AppConfig::COLOR_CORAL_HEX;
            break;
        case PTTState::STOPPING_CAPTURE:
        case PTTState::PROCESSING:
            stateText = "Réflexion OpenAI en cours...";
            badgeText = "ANALYSE";
            badgeColorHex = AppConfig::COLOR_PURPLE_HEX;
            stateColorHex = AppConfig::COLOR_PURPLE_HEX;
            break;
        case PTTState::SPEAKING:
            stateText = "L'assistant vous répond...";
            badgeText = "PAROLE";
            badgeColorHex = AppConfig::COLOR_TURQUOISE_HEX;
            stateColorHex = AppConfig::COLOR_TURQUOISE_HEX;
            break;
        case PTTState::INTERRUPTING:
            stateText = "Interruption de la session...";
            badgeText = "ARRÊT";
            badgeColorHex = AppConfig::COLOR_STOP_HEX;
            stateColorHex = AppConfig::COLOR_STOP_HEX;
            break;
        case PTTState::RECOVERING:
            stateText = "Récupération contrôlée...";
            badgeText = "REPRISE";
            badgeColorHex = 0x7F8C8D;
            stateColorHex = AppConfig::COLOR_TEXT_SEC_HEX;
            break;
        case PTTState::ERROR_STATE:
            stateText = "Erreur survenue (appuyez pour relancer)";
            badgeText = "ERREUR";
            badgeColorHex = AppConfig::COLOR_RED_HEX;
            stateColorHex = AppConfig::COLOR_RED_HEX;
            break;
    }

    lv_label_set_text(_label_state_text, stateText);
    lv_obj_set_style_text_color(_label_state_text, lv_color_hex(stateColorHex), 0);

    lv_label_set_text(_label_badge, badgeText);
    lv_obj_set_style_bg_color(_badge_state, lv_color_hex(badgeColorHex), 0);
    lv_obj_center(_label_badge);
}

void VoiceUI::syncWithPTT(PTTState pttState) {
    if (_lastPttState != pttState) {
        _lastPttState = pttState;
        updateButtonVisuals(pttState);
        updateStateLabel(pttState);

        // Synchronisation de l'apparence de l'orbe vocal
        switch (pttState) {
            case PTTState::READY:
                _orb.setState(OrbVisualState::READY);
                break;
            case PTTState::STARTING:
            case PTTState::LISTENING:
                _orb.setState(OrbVisualState::LISTENING);
                break;
            case PTTState::STOPPING_CAPTURE:
            case PTTState::PROCESSING:
                _orb.setState(OrbVisualState::PROCESSING);
                break;
            case PTTState::SPEAKING:
                _orb.setState(OrbVisualState::SPEAKING);
                break;
            case PTTState::ERROR_STATE:
                _orb.setState(OrbVisualState::ERROR_STATE);
                break;
            default:
                break;
        }
    }
}

void VoiceUI::setUserText(const String& text) {
    if (!_label_user_content) return;
    if (text.isEmpty()) {
        lv_label_set_text(_label_user_content, "Vous : (en attente d'elocution...)");
    } else {
        String formatted = "Vous : " + text;
        lv_label_set_text(_label_user_content, formatted.c_str());
    }
}

void VoiceUI::setAssistantText(const String& text) {
    if (!_label_asst_content) return;
    if (text.isEmpty()) {
        lv_label_set_text(_label_asst_content, "Assistant : (reponse concise en attente...)");
    } else {
        String formatted = "Assistant : " + text;
        lv_label_set_text(_label_asst_content, formatted.c_str());
    }
}

void VoiceUI::setErrorMessage(const String& err) {
    if (!_label_state_text) return;
    if (err.isEmpty()) {
        updateStateLabel(_lastPttState);
    } else {
        String msg = "Erreur : " + err;
        lv_label_set_text(_label_state_text, msg.c_str());
        lv_obj_set_style_text_color(_label_state_text, lv_color_hex(AppConfig::COLOR_RED_HEX), 0);
    }
}

void VoiceUI::setWifiStatus(bool connected, int8_t rssi) {
    if (_wifiConnected != connected || _wifiRssi != rssi) {
        _wifiConnected = connected;
        _wifiRssi = rssi;
        if (_label_wifi) {
            if (connected) {
                char buf[16];
                snprintf(buf, sizeof(buf), "Wi-Fi %d dBm", (int)rssi);
                lv_label_set_text(_label_wifi, buf);
                lv_obj_set_style_text_color(_label_wifi, lv_color_hex(AppConfig::COLOR_CYAN_HEX), 0);
            } else {
                lv_label_set_text(_label_wifi, "Wi-Fi --");
                lv_obj_set_style_text_color(_label_wifi, lv_color_hex(AppConfig::COLOR_TEXT_MUTED_HEX), 0);
            }
        }
    }
}

void VoiceUI::update(uint32_t nowMs, uint32_t audioLevel, bool throttle) {
    if (!_initialized) return;

    // 1. Mise a jour de l'animation de l'orbe
    _orb.update(nowMs, audioLevel, throttle);

    // 2. Execution de l'ordonnanceur de timers et de rendu LVGL 9
    uint32_t t0 = micros();
    lv_timer_handler();
    uint32_t renderDurUs = micros() - t0;

    if (_diag) {
        _diag->recordLvglRender(renderDurUs);
    }
}

