/**
 * @file VoiceAssistantUI.cpp
 * @brief Implémentation de l'interface graphique LVGL 9 pour VoiceAssistant v0.1.0.
 */

#include "VoiceAssistantUI.h"
#include "FrenchFonts.h"

void VoiceAssistantUI::init(AppController& controller) {
    m_controller = &controller;

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF4F6F9), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // =========================================================================
    // 1. Conteneur principal de la vue Conversation (320x240)
    // =========================================================================
    m_contConv = lv_obj_create(scr);
    lv_obj_set_pos(m_contConv, 0, 0);
    lv_obj_set_size(m_contConv, 320, 240);
    lv_obj_set_style_bg_color(m_contConv, lv_color_hex(0xF4F6F9), 0);
    lv_obj_set_style_pad_all(m_contConv, 0, 0);
    lv_obj_set_style_border_width(m_contConv, 0, 0);
    lv_obj_set_style_radius(m_contConv, 0, 0);
    lv_obj_remove_flag(m_contConv, LV_OBJ_FLAG_SCROLLABLE);

    // 1.1 Barre supérieure (y: 0, h: 24, w: 320)
    lv_obj_t* headerPnl = lv_obj_create(m_contConv);
    lv_obj_set_pos(headerPnl, 0, 0);
    lv_obj_set_size(headerPnl, 320, 24);
    lv_obj_set_style_bg_color(headerPnl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(headerPnl, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(headerPnl, 1, 0);
    lv_obj_set_style_border_side(headerPnl, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(headerPnl, 0, 0);
    lv_obj_set_style_pad_hor(headerPnl, 8, 0);
    lv_obj_set_style_pad_ver(headerPnl, 2, 0);
    lv_obj_remove_flag(headerPnl, LV_OBJ_FLAG_SCROLLABLE);

    m_lblTitle = lv_label_create(headerPnl);
    lv_label_set_text(m_lblTitle, AppConfig::APP_NAME);
    lv_obj_set_style_text_font(m_lblTitle, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(m_lblTitle, lv_color_hex(0x1A1A24), 0);
    lv_obj_align(m_lblTitle, LV_ALIGN_LEFT_MID, 0, 0);

    m_lblProviderPill = lv_label_create(headerPnl);
    lv_label_set_text(m_lblProviderPill, "Gemini");
    lv_obj_set_style_text_font(m_lblProviderPill, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblProviderPill, lv_color_hex(0x1976D2), 0);
    lv_obj_set_style_bg_color(m_lblProviderPill, lv_color_hex(0xE3F2FD), 0);
    lv_obj_set_style_bg_opa(m_lblProviderPill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m_lblProviderPill, 3, 0);
    lv_obj_set_style_pad_hor(m_lblProviderPill, 6, 0);
    lv_obj_set_style_pad_ver(m_lblProviderPill, 1, 0);
    lv_obj_align(m_lblProviderPill, LV_ALIGN_CENTER, 20, 0);

    m_lblWifiStatus = lv_label_create(headerPnl);
    lv_label_set_text(m_lblWifiStatus, "WiFi OK");
    lv_obj_set_style_text_font(m_lblWifiStatus, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblWifiStatus, lv_color_hex(0x388E3C), 0);
    lv_obj_align(m_lblWifiStatus, LV_ALIGN_RIGHT_MID, 0, 0);

    // 1.2 Carte d'état vocal & VAD (y: 26, h: 22, w: 304)
    m_cardState = lv_obj_create(m_contConv);
    lv_obj_set_pos(m_cardState, 8, 26);
    lv_obj_set_size(m_cardState, 304, 22);
    lv_obj_set_style_bg_color(m_cardState, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(m_cardState, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(m_cardState, 1, 0);
    lv_obj_set_style_radius(m_cardState, 4, 0);
    lv_obj_set_style_pad_hor(m_cardState, 6, 0);
    lv_obj_set_style_pad_ver(m_cardState, 2, 0);
    lv_obj_remove_flag(m_cardState, LV_OBJ_FLAG_SCROLLABLE);

    m_lblState = lv_label_create(m_cardState);
    lv_label_set_text(m_lblState, "Assistant prêt");
    lv_obj_set_style_text_font(m_lblState, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblState, lv_color_hex(0x2E7D32), 0);
    lv_obj_align(m_lblState, LV_ALIGN_LEFT_MID, 0, 0);

    m_lblVad = lv_label_create(m_cardState);
    lv_label_set_text(m_lblVad, "VAD: PRÊT");
    lv_obj_set_style_text_font(m_lblVad, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblVad, lv_color_hex(0x1976D2), 0);
    lv_obj_align(m_lblVad, LV_ALIGN_RIGHT_MID, 0, 0);

    // 1.3 Carte VOUS (y: 50, h: 56, w: 304)
    m_cardUser = lv_obj_create(m_contConv);
    lv_obj_set_pos(m_cardUser, 8, 50);
    lv_obj_set_size(m_cardUser, 304, 56);
    lv_obj_set_style_bg_color(m_cardUser, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(m_cardUser, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(m_cardUser, 1, 0);
    lv_obj_set_style_radius(m_cardUser, 6, 0);
    lv_obj_set_style_pad_all(m_cardUser, 4, 0);
    lv_obj_remove_flag(m_cardUser, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblUserHead = lv_label_create(m_cardUser);
    lv_label_set_text(lblUserHead, "VOUS");
    lv_obj_set_style_text_font(lblUserHead, &font_french_12, 0);
    lv_obj_set_style_text_color(lblUserHead, lv_color_hex(0x757575), 0);
    lv_obj_set_pos(lblUserHead, 2, 0);

    m_lblUserText = lv_label_create(m_cardUser);
    lv_obj_set_pos(m_lblUserText, 2, 14);
    lv_obj_set_size(m_lblUserText, 290, 36);
    lv_label_set_long_mode(m_lblUserText, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(m_lblUserText, "Appuyez sur PARLER pour poser une question.");
    lv_obj_set_style_text_font(m_lblUserText, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblUserText, lv_color_hex(0x212121), 0);

    // 1.4 Carte ASSISTANT (y: 110, h: 60, w: 304) — Défilable verticalement
    m_cardAssistant = lv_obj_create(m_contConv);
    lv_obj_set_pos(m_cardAssistant, 8, 110);
    lv_obj_set_size(m_cardAssistant, 304, 60);
    lv_obj_set_style_bg_color(m_cardAssistant, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(m_cardAssistant, lv_color_hex(0xBBDEFB), 0);
    lv_obj_set_style_border_width(m_cardAssistant, 1, 0);
    lv_obj_set_style_radius(m_cardAssistant, 6, 0);
    lv_obj_set_style_pad_all(m_cardAssistant, 4, 0);
    lv_obj_remove_flag(m_cardAssistant, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblAstHead = lv_label_create(m_cardAssistant);
    lv_label_set_text(lblAstHead, "ASSISTANT");
    lv_obj_set_style_text_font(lblAstHead, &font_french_12, 0);
    lv_obj_set_style_text_color(lblAstHead, lv_color_hex(0x1976D2), 0);
    lv_obj_set_pos(lblAstHead, 2, 0);

    // Conteneur interne avec défilement vertical pour réponses longues (>= 10 lignes)
    m_contAssistantScroll = lv_obj_create(m_cardAssistant);
    lv_obj_set_pos(m_contAssistantScroll, 0, 14);
    lv_obj_set_size(m_contAssistantScroll, 296, 40);
    lv_obj_set_style_bg_opa(m_contAssistantScroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_contAssistantScroll, 0, 0);
    lv_obj_set_style_pad_all(m_contAssistantScroll, 0, 0);
    lv_obj_set_style_pad_right(m_contAssistantScroll, 4, 0);
    lv_obj_add_flag(m_contAssistantScroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(m_contAssistantScroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(m_contAssistantScroll, LV_SCROLLBAR_MODE_AUTO);

    m_lblAssistantText = lv_label_create(m_contAssistantScroll);
    lv_obj_set_pos(m_lblAssistantText, 0, 0);
    lv_obj_set_width(m_lblAssistantText, 280);
    lv_obj_set_height(m_lblAssistantText, LV_SIZE_CONTENT);
    lv_label_set_long_mode(m_lblAssistantText, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(m_lblAssistantText, "Bonjour ! Je suis à votre écoute.");
    lv_obj_set_style_text_font(m_lblAssistantText, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblAssistantText, lv_color_hex(0x212121), 0);

    // 1.5 Boutons de contrôle vocal (y: 174, h: 34)
    // Bouton PARLER / TERMINER (w: 196)
    m_btnTalk = lv_button_create(m_contConv);
    lv_obj_set_pos(m_btnTalk, 8, 174);
    lv_obj_set_size(m_btnTalk, 196, 34);
    lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0x1976D2), 0);
    lv_obj_set_style_radius(m_btnTalk, 6, 0);
    lv_obj_add_event_cb(m_btnTalk, onTalkButtonClicked, LV_EVENT_CLICKED, this);

    m_lblBtnTalk = lv_label_create(m_btnTalk);
    lv_label_set_text(m_lblBtnTalk, "PARLER");
    lv_obj_set_style_text_font(m_lblBtnTalk, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(m_lblBtnTalk, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(m_lblBtnTalk);

    // Bouton STOP (w: 100)
    m_btnStop = lv_button_create(m_contConv);
    lv_obj_set_pos(m_btnStop, 212, 174);
    lv_obj_set_size(m_btnStop, 100, 34);
    lv_obj_set_style_bg_color(m_btnStop, lv_color_hex(0xD32F2F), 0);
    lv_obj_set_style_radius(m_btnStop, 6, 0);
    lv_obj_add_event_cb(m_btnStop, onStopButtonClicked, LV_EVENT_CLICKED, this);

    lv_obj_t* lblBtnStop = lv_label_create(m_btnStop);
    lv_label_set_text(lblBtnStop, "STOP");
    lv_obj_set_style_text_font(lblBtnStop, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(lblBtnStop, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblBtnStop);

    // 1.6 Barre d'onglets inférieure (y: 212, h: 24, w: 304)
    m_btnTabConv = lv_button_create(m_contConv);
    lv_obj_set_pos(m_btnTabConv, 8, 212);
    lv_obj_set_size(m_btnTabConv, 148, 24);
    lv_obj_set_style_bg_color(m_btnTabConv, lv_color_hex(0x1976D2), 0);
    lv_obj_set_style_radius(m_btnTabConv, 4, 0);
    lv_obj_set_style_pad_all(m_btnTabConv, 0, 0);
    lv_obj_add_event_cb(m_btnTabConv, onTabConvClicked, LV_EVENT_CLICKED, this);

    m_lblTabConv = lv_label_create(m_btnTabConv);
    lv_label_set_text(m_lblTabConv, "Conversation");
    lv_obj_set_style_text_font(m_lblTabConv, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblTabConv, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(m_lblTabConv);

    m_btnTabSettings = lv_button_create(m_contConv);
    lv_obj_set_pos(m_btnTabSettings, 164, 212);
    lv_obj_set_size(m_btnTabSettings, 148, 24);
    lv_obj_set_style_bg_color(m_btnTabSettings, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_radius(m_btnTabSettings, 4, 0);
    lv_obj_set_style_pad_all(m_btnTabSettings, 0, 0);
    lv_obj_add_event_cb(m_btnTabSettings, onTabSettingsClicked, LV_EVENT_CLICKED, this);

    m_lblTabSettings = lv_label_create(m_btnTabSettings);
    lv_label_set_text(m_lblTabSettings, "Paramètres");
    lv_obj_set_style_text_font(m_lblTabSettings, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblTabSettings, lv_color_hex(0x424242), 0);
    lv_obj_center(m_lblTabSettings);

    // =========================================================================
    // 2. Initialisation de l'écran des paramètres
    // =========================================================================
    m_settingsScreen.init(scr, controller, [this]() {
        showConversation();
    });

    updateVoiceState(m_controller->getState());
}

void VoiceAssistantUI::showConversation() {
    if (m_contConv) {
        lv_obj_remove_flag(m_contConv, LV_OBJ_FLAG_HIDDEN);
    }
    m_settingsScreen.hide();
    refreshHeader();
}

void VoiceAssistantUI::showSettings() {
    if (m_contConv) {
        lv_obj_add_flag(m_contConv, LV_OBJ_FLAG_HIDDEN);
    }
    m_settingsScreen.show();
}

void VoiceAssistantUI::update() {
    if (!m_controller) return;

    // Mise à jour de l'écran Paramètres (confirmation et feedback)
    m_settingsScreen.update();

    // Effacement immédiat de l'affichage si demandé par le bouton Effacer
    if (m_controller->consumeHistoryCleared()) {
        if (m_lblUserText) {
            lv_label_set_text(m_lblUserText, "Appuyez sur PARLER pour poser une question.");
        }
        if (m_lblAssistantText) {
            lv_label_set_text(m_lblAssistantText, "Bonjour ! Je suis à votre écoute.");
        }
        if (m_contAssistantScroll) {
            lv_obj_scroll_to_y(m_contAssistantScroll, 0, LV_ANIM_OFF);
        }
    }

    // Consommation des messages différés vers les labels
    String text;
    if (m_controller->consumeNewUserText(text)) {
        if (m_lblUserText) {
            lv_label_set_text(m_lblUserText, text.c_str());
        }
    }

    if (m_controller->consumeNewAssistantText(text)) {
        if (m_lblAssistantText) {
            lv_label_set_text(m_lblAssistantText, text.c_str());
        }
    }

    if (m_controller->consumeNewVadText(text)) {
        if (m_lblVad) {
            lv_label_set_text(m_lblVad, text.c_str());
        }
    }

    if (m_controller->consumeNewStatusMessage(text)) {
        if (m_lblState) {
            lv_label_set_text(m_lblState, text.c_str());
        }
    }

    VoiceInteractionState state;
    if (m_controller->hasStateChanged(state)) {
        updateVoiceState(state);
    }

    // Rafraîchissement périodique de l'en-tête (toutes les 500 ms)
    uint32_t nowMs = millis();
    if (nowMs - m_lastHeaderRefreshMs >= 500) {
        m_lastHeaderRefreshMs = nowMs;
        refreshHeader();
        // S'assurer que le bouton PARLER reflète la disponibilité session en READY
        if (m_controller->getState() == VoiceInteractionState::READY) {
            updateVoiceState(VoiceInteractionState::READY);
        }
    }
}

void VoiceAssistantUI::refreshHeader() {
    if (!m_controller) return;

    // 1. Pilule du fournisseur
    if (m_lblProviderPill) {
        bool isGemini = (m_controller->getProvider() == VoiceProviderType::GEMINI);
        lv_label_set_text(m_lblProviderPill, isGemini ? "Gemini" : "OpenAI");
    }

    // 2. Statut Wi-Fi / Session
    if (m_lblWifiStatus) {
        bool wifiOk = m_controller->isWifiConnected();
        bool sessionOk = m_controller->isSessionConnected();

        if (!wifiOk) {
            lv_label_set_text(m_lblWifiStatus, "WiFi NO");
            lv_obj_set_style_text_color(m_lblWifiStatus, lv_color_hex(0xD32F2F), 0);
        } else if (!sessionOk) {
            lv_label_set_text(m_lblWifiStatus, "Session...");
            lv_obj_set_style_text_color(m_lblWifiStatus, lv_color_hex(0xF57C00), 0);
        } else {
            lv_label_set_text(m_lblWifiStatus, "En ligne");
            lv_obj_set_style_text_color(m_lblWifiStatus, lv_color_hex(0x2E7D32), 0);
        }
    }
}

void VoiceAssistantUI::updateVoiceState(VoiceInteractionState state) {
    if (!m_lblState || !m_btnTalk || !m_lblBtnTalk || !m_controller) return;

    bool sessionReady = m_controller->isSessionConnected() &&
                        m_controller->isWifiConnected() &&
                        !m_controller->isProfileChangeInProgress();

    switch (state) {
        case VoiceInteractionState::DISABLED:
            lv_label_set_text(m_lblState, "Non disponible (hors ligne)");
            lv_obj_set_style_text_color(m_lblState, lv_color_hex(0x757575), 0);
            lv_label_set_text(m_lblBtnTalk, "ATTENTE");
            lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0x9E9E9E), 0);
            lv_obj_add_state(m_btnTalk, LV_STATE_DISABLED);
            break;

        case VoiceInteractionState::READY:
            if (!sessionReady) {
                lv_label_set_text(m_lblState, "Connexion session IA...");
                lv_obj_set_style_text_color(m_lblState, lv_color_hex(0xF57C00), 0);
                lv_label_set_text(m_lblBtnTalk, "ATTENTE");
                lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0x9E9E9E), 0);
                lv_obj_add_state(m_btnTalk, LV_STATE_DISABLED);
            } else {
                lv_label_set_text(m_lblState, "Assistant prêt (2 clics)");
                lv_obj_set_style_text_color(m_lblState, lv_color_hex(0x2E7D32), 0);
                lv_label_set_text(m_lblBtnTalk, "PARLER");
                lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0x1976D2), 0);
                lv_obj_remove_state(m_btnTalk, LV_STATE_DISABLED);
            }
            break;

        case VoiceInteractionState::STARTING:
            lv_label_set_text(m_lblState, "Ouverture du micro...");
            lv_obj_set_style_text_color(m_lblState, lv_color_hex(0xF57C00), 0);
            lv_label_set_text(m_lblBtnTalk, "OUVERTURE...");
            lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0xF57C00), 0);
            lv_obj_add_state(m_btnTalk, LV_STATE_DISABLED);
            break;

        case VoiceInteractionState::LISTENING:
            lv_label_set_text(m_lblState, "Écoute en cours...");
            lv_obj_set_style_text_color(m_lblState, lv_color_hex(0xD50000), 0);
            lv_label_set_text(m_lblBtnTalk, "TERMINER");
            lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0xD50000), 0);
            lv_obj_remove_state(m_btnTalk, LV_STATE_DISABLED);
            break;

        case VoiceInteractionState::STOPPING_CAPTURE:
            lv_label_set_text(m_lblState, "Validation de la parole...");
            lv_obj_set_style_text_color(m_lblState, lv_color_hex(0xFF8F00), 0);
            lv_label_set_text(m_lblBtnTalk, "VALIDATION...");
            lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0xFF8F00), 0);
            lv_obj_add_state(m_btnTalk, LV_STATE_DISABLED);
            break;

        case VoiceInteractionState::PROCESSING:
            lv_label_set_text(m_lblState, "Réflexion en cours...");
            lv_obj_set_style_text_color(m_lblState, lv_color_hex(0x0288D1), 0);
            lv_label_set_text(m_lblBtnTalk, "TRAITEMENT...");
            lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0x0288D1), 0);
            lv_obj_add_state(m_btnTalk, LV_STATE_DISABLED);
            break;

        case VoiceInteractionState::SPEAKING:
            lv_label_set_text(m_lblState, "Réponse vocale en cours...");
            lv_obj_set_style_text_color(m_lblState, lv_color_hex(0x7B1FA2), 0);
            lv_label_set_text(m_lblBtnTalk, "PAROLE EN COURS");
            lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0x9E9E9E), 0);
            lv_obj_add_state(m_btnTalk, LV_STATE_DISABLED);
            break;

        case VoiceInteractionState::INTERRUPTING:
            lv_label_set_text(m_lblState, "Arrêt en cours...");
            lv_obj_set_style_text_color(m_lblState, lv_color_hex(0xC62828), 0);
            lv_label_set_text(m_lblBtnTalk, "ARRÊT...");
            lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0xC62828), 0);
            lv_obj_add_state(m_btnTalk, LV_STATE_DISABLED);
            break;

        case VoiceInteractionState::RECOVERING:
            lv_label_set_text(m_lblState, "Reconnexion / reprise...");
            lv_obj_set_style_text_color(m_lblState, lv_color_hex(0xE65100), 0);
            lv_label_set_text(m_lblBtnTalk, "REPRISE...");
            lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0xE65100), 0);
            lv_obj_add_state(m_btnTalk, LV_STATE_DISABLED);
            break;

        case VoiceInteractionState::ERROR_STATE:
            lv_label_set_text(m_lblState, "Erreur");
            lv_obj_set_style_text_color(m_lblState, lv_color_hex(0xBF360C), 0);
            lv_label_set_text(m_lblBtnTalk, "ERREUR");
            lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(0xBF360C), 0);
            lv_obj_add_state(m_btnTalk, LV_STATE_DISABLED);
            break;
    }
}

void VoiceAssistantUI::onTalkButtonClicked(lv_event_t* e) {
    auto* self = static_cast<VoiceAssistantUI*>(lv_event_get_user_data(e));
    if (self && self->m_controller) {
        self->m_controller->onTalkClick();
    }
}

void VoiceAssistantUI::onStopButtonClicked(lv_event_t* e) {
    auto* self = static_cast<VoiceAssistantUI*>(lv_event_get_user_data(e));
    if (self && self->m_controller) {
        self->m_controller->triggerStop();
    }
}

void VoiceAssistantUI::onTabConvClicked(lv_event_t* e) {
    auto* self = static_cast<VoiceAssistantUI*>(lv_event_get_user_data(e));
    if (self) {
        self->showConversation();
    }
}

void VoiceAssistantUI::onTabSettingsClicked(lv_event_t* e) {
    auto* self = static_cast<VoiceAssistantUI*>(lv_event_get_user_data(e));
    if (self) {
        self->showSettings();
    }
}
