/**
 * @file SettingsScreen.cpp
 * @brief Implémentation de l'écran Paramètres de VoiceAssistant v0.1.0.
 */

#include "SettingsScreen.h"
#include "FrenchFonts.h"
#include <WiFi.h>

struct LangOption {
    const char* bcp47;
    const char* label;
};

static constexpr LangOption kSupportedLangs[] = {
    {"fr-FR", "Français (fr-FR)"},
    {"en-US", "English (en-US)"},
    {"de-DE", "Deutsch (de-DE)"},
    {"it-IT", "Italiano (it-IT)"},
    {"es-ES", "Español (es-ES)"}
};
static constexpr size_t kSupportedLangCount = sizeof(kSupportedLangs) / sizeof(kSupportedLangs[0]);

enum SettingsEventId {
    EVT_BACK = 1,
    EVT_PROVIDER_GEMINI,
    EVT_PROVIDER_OPENAI,
    EVT_LANG_CYCLE,
    EVT_VOICE_CYCLE,
    EVT_VOL_DOWN,
    EVT_VOL_UP,
    EVT_SAVE_NVS,
    EVT_CLEAR_HIST
};

void SettingsScreen::init(lv_obj_t* parent, AppController& controller, std::function<void()> onBackClicked) {
    m_controller = &controller;
    m_onBack = std::move(onBackClicked);

    // Conteneur principal plein écran (320x240)
    m_container = lv_obj_create(parent);
    lv_obj_set_pos(m_container, 0, 0);
    lv_obj_set_size(m_container, 320, 240);
    lv_obj_set_style_bg_color(m_container, lv_color_hex(0xF4F6F9), 0);
    lv_obj_set_style_pad_all(m_container, 0, 0);
    lv_obj_set_style_border_width(m_container, 0, 0);
    lv_obj_set_style_radius(m_container, 0, 0);
    lv_obj_remove_flag(m_container, LV_OBJ_FLAG_SCROLLABLE);

    // Masqué par défaut
    lv_obj_add_flag(m_container, LV_OBJ_FLAG_HIDDEN);

    // 1. Barre supérieure d'en-tête (y: 2, h: 26)
    lv_obj_t* headerPnl = lv_obj_create(m_container);
    lv_obj_set_pos(headerPnl, 0, 0);
    lv_obj_set_size(headerPnl, 320, 26);
    lv_obj_set_style_bg_color(headerPnl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(headerPnl, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(headerPnl, 1, 0);
    lv_obj_set_style_border_side(headerPnl, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(headerPnl, 0, 0);
    lv_obj_set_style_pad_hor(headerPnl, 8, 0);
    lv_obj_set_style_pad_ver(headerPnl, 2, 0);
    lv_obj_remove_flag(headerPnl, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblTitle = lv_label_create(headerPnl);
    lv_label_set_text(lblTitle, "Paramètres VoiceAssistant");
    lv_obj_set_style_text_font(lblTitle, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(lblTitle, lv_color_hex(0x1A1A24), 0);
    lv_obj_align(lblTitle, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* btnBack = lv_button_create(headerPnl);
    lv_obj_set_size(btnBack, 74, 20);
    lv_obj_align(btnBack, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btnBack, lv_color_hex(0x1976D2), 0);
    lv_obj_set_style_radius(btnBack, 4, 0);
    lv_obj_set_style_pad_all(btnBack, 0, 0);
    lv_obj_add_event_cb(btnBack, eventHandler, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnBack, (void*)(uintptr_t)EVT_BACK);

    lv_obj_t* lblBack = lv_label_create(btnBack);
    lv_label_set_text(lblBack, "RETOUR");
    lv_obj_set_style_text_font(lblBack, &font_french_12, 0);
    lv_obj_set_style_text_color(lblBack, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblBack);

    // 2. Zone de configuration principale (y: 28, h: 180)
    // Carte 1 : Fournisseur IA (y: 30, h: 36, w: 304)
    lv_obj_t* cardProv = lv_obj_create(m_container);
    lv_obj_set_pos(cardProv, 8, 30);
    lv_obj_set_size(cardProv, 304, 36);
    lv_obj_set_style_bg_color(cardProv, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(cardProv, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(cardProv, 1, 0);
    lv_obj_set_style_radius(cardProv, 6, 0);
    lv_obj_set_style_pad_all(cardProv, 4, 0);
    lv_obj_remove_flag(cardProv, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblProvTitle = lv_label_create(cardProv);
    lv_label_set_text(lblProvTitle, "IA :");
    lv_obj_set_style_text_font(lblProvTitle, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(lblProvTitle, lv_color_hex(0x424242), 0);
    lv_obj_align(lblProvTitle, LV_ALIGN_LEFT_MID, 4, 0);

    m_btnGemini = lv_button_create(cardProv);
    lv_obj_set_size(m_btnGemini, 116, 26);
    lv_obj_set_pos(m_btnGemini, 44, 1);
    lv_obj_set_style_radius(m_btnGemini, 4, 0);
    lv_obj_set_style_pad_all(m_btnGemini, 0, 0);
    lv_obj_add_event_cb(m_btnGemini, eventHandler, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(m_btnGemini, (void*)(uintptr_t)EVT_PROVIDER_GEMINI);

    m_lblGemini = lv_label_create(m_btnGemini);
    lv_label_set_text(m_lblGemini, "Gemini Live");
    lv_obj_set_style_text_font(m_lblGemini, &font_french_12, 0);
    lv_obj_center(m_lblGemini);

    m_btnOpenAi = lv_button_create(cardProv);
    lv_obj_set_size(m_btnOpenAi, 126, 26);
    lv_obj_set_pos(m_btnOpenAi, 166, 1);
    lv_obj_set_style_radius(m_btnOpenAi, 4, 0);
    lv_obj_set_style_pad_all(m_btnOpenAi, 0, 0);
    lv_obj_add_event_cb(m_btnOpenAi, eventHandler, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(m_btnOpenAi, (void*)(uintptr_t)EVT_PROVIDER_OPENAI);

    m_lblOpenAi = lv_label_create(m_btnOpenAi);
    lv_label_set_text(m_lblOpenAi, "OpenAI Realtime");
    lv_obj_set_style_text_font(m_lblOpenAi, &font_french_12, 0);
    lv_obj_center(m_lblOpenAi);

    // Carte 2 : Langue et Voix (y: 70, h: 56, w: 304)
    lv_obj_t* cardLangVoice = lv_obj_create(m_container);
    lv_obj_set_pos(cardLangVoice, 8, 70);
    lv_obj_set_size(cardLangVoice, 304, 56);
    lv_obj_set_style_bg_color(cardLangVoice, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(cardLangVoice, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(cardLangVoice, 1, 0);
    lv_obj_set_style_radius(cardLangVoice, 6, 0);
    lv_obj_set_style_pad_all(cardLangVoice, 4, 0);
    lv_obj_remove_flag(cardLangVoice, LV_OBJ_FLAG_SCROLLABLE);

    // Ligne 1 : Langue
    lv_obj_t* lblLangTitle = lv_label_create(cardLangVoice);
    lv_label_set_text(lblLangTitle, "Langue :");
    lv_obj_set_style_text_font(lblLangTitle, &font_french_12, 0);
    lv_obj_set_style_text_color(lblLangTitle, lv_color_hex(0x616161), 0);
    lv_obj_set_pos(lblLangTitle, 4, 4);

    m_btnLang = lv_button_create(cardLangVoice);
    lv_obj_set_pos(m_btnLang, 66, 1);
    lv_obj_set_size(m_btnLang, 226, 22);
    lv_obj_set_style_bg_color(m_btnLang, lv_color_hex(0xEEF2F6), 0);
    lv_obj_set_style_radius(m_btnLang, 4, 0);
    lv_obj_set_style_pad_all(m_btnLang, 0, 0);
    lv_obj_add_event_cb(m_btnLang, eventHandler, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(m_btnLang, (void*)(uintptr_t)EVT_LANG_CYCLE);

    m_lblLangValue = lv_label_create(m_btnLang);
    lv_label_set_text(m_lblLangValue, "Français (fr-FR)");
    lv_obj_set_style_text_font(m_lblLangValue, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblLangValue, lv_color_hex(0x1976D2), 0);
    lv_obj_center(m_lblLangValue);

    // Ligne 2 : Voix
    lv_obj_t* lblVoiceTitle = lv_label_create(cardLangVoice);
    lv_label_set_text(lblVoiceTitle, "Voix :");
    lv_obj_set_style_text_font(lblVoiceTitle, &font_french_12, 0);
    lv_obj_set_style_text_color(lblVoiceTitle, lv_color_hex(0x616161), 0);
    lv_obj_set_pos(lblVoiceTitle, 4, 28);

    m_btnVoice = lv_button_create(cardLangVoice);
    lv_obj_set_pos(m_btnVoice, 66, 26);
    lv_obj_set_size(m_btnVoice, 226, 22);
    lv_obj_set_style_bg_color(m_btnVoice, lv_color_hex(0xEEF2F6), 0);
    lv_obj_set_style_radius(m_btnVoice, 4, 0);
    lv_obj_set_style_pad_all(m_btnVoice, 0, 0);
    lv_obj_add_event_cb(m_btnVoice, eventHandler, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(m_btnVoice, (void*)(uintptr_t)EVT_VOICE_CYCLE);

    m_lblVoiceValue = lv_label_create(m_btnVoice);
    lv_label_set_text(m_lblVoiceValue, "Aoede");
    lv_obj_set_style_text_font(m_lblVoiceValue, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblVoiceValue, lv_color_hex(0x1976D2), 0);
    lv_obj_center(m_lblVoiceValue);

    // Carte 3 : Volume et Actions rapides (y: 130, h: 42, w: 304)
    lv_obj_t* cardVolAction = lv_obj_create(m_container);
    lv_obj_set_pos(cardVolAction, 8, 130);
    lv_obj_set_size(cardVolAction, 304, 42);
    lv_obj_set_style_bg_color(cardVolAction, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(cardVolAction, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(cardVolAction, 1, 0);
    lv_obj_set_style_radius(cardVolAction, 6, 0);
    lv_obj_set_style_pad_all(cardVolAction, 4, 0);
    lv_obj_remove_flag(cardVolAction, LV_OBJ_FLAG_SCROLLABLE);

    // Contrôles Volume
    lv_obj_t* lblVolTitle = lv_label_create(cardVolAction);
    lv_label_set_text(lblVolTitle, "Vol :");
    lv_obj_set_style_text_font(lblVolTitle, &font_french_12, 0);
    lv_obj_set_style_text_color(lblVolTitle, lv_color_hex(0x616161), 0);
    lv_obj_align(lblVolTitle, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* btnVolDown = lv_button_create(cardVolAction);
    lv_obj_set_size(btnVolDown, 28, 26);
    lv_obj_set_pos(btnVolDown, 36, 4);
    lv_obj_set_style_bg_color(btnVolDown, lv_color_hex(0xCFD8DC), 0);
    lv_obj_set_style_radius(btnVolDown, 4, 0);
    lv_obj_set_style_pad_all(btnVolDown, 0, 0);
    lv_obj_add_event_cb(btnVolDown, eventHandler, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnVolDown, (void*)(uintptr_t)EVT_VOL_DOWN);

    lv_obj_t* lblVolDown = lv_label_create(btnVolDown);
    lv_label_set_text(lblVolDown, "-");
    lv_obj_set_style_text_font(lblVolDown, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(lblVolDown, lv_color_hex(0x263238), 0);
    lv_obj_center(lblVolDown);

    m_lblVolumeValue = lv_label_create(cardVolAction);
    lv_label_set_text(m_lblVolumeValue, "80 %");
    lv_obj_set_style_text_font(m_lblVolumeValue, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblVolumeValue, lv_color_hex(0x1A1A24), 0);
    lv_obj_set_pos(m_lblVolumeValue, 70, 8);

    lv_obj_t* btnVolUp = lv_button_create(cardVolAction);
    lv_obj_set_size(btnVolUp, 28, 26);
    lv_obj_set_pos(btnVolUp, 108, 4);
    lv_obj_set_style_bg_color(btnVolUp, lv_color_hex(0xCFD8DC), 0);
    lv_obj_set_style_radius(btnVolUp, 4, 0);
    lv_obj_set_style_pad_all(btnVolUp, 0, 0);
    lv_obj_add_event_cb(btnVolUp, eventHandler, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnVolUp, (void*)(uintptr_t)EVT_VOL_UP);

    lv_obj_t* lblVolUp = lv_label_create(btnVolUp);
    lv_label_set_text(lblVolUp, "+");
    lv_obj_set_style_text_font(lblVolUp, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(lblVolUp, lv_color_hex(0x263238), 0);
    lv_obj_center(lblVolUp);

    // Bouton Sauvegarder NVS
    lv_obj_t* btnSaveNvs = lv_button_create(cardVolAction);
    lv_obj_set_size(btnSaveNvs, 72, 26);
    lv_obj_set_pos(btnSaveNvs, 144, 4);
    lv_obj_set_style_bg_color(btnSaveNvs, lv_color_hex(0x00897B), 0);
    lv_obj_set_style_radius(btnSaveNvs, 4, 0);
    lv_obj_set_style_pad_all(btnSaveNvs, 0, 0);
    lv_obj_add_event_cb(btnSaveNvs, eventHandler, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnSaveNvs, (void*)(uintptr_t)EVT_SAVE_NVS);

    lv_obj_t* lblSave = lv_label_create(btnSaveNvs);
    lv_label_set_text(lblSave, "Sauver");
    lv_obj_set_style_text_font(lblSave, &font_french_12, 0);
    lv_obj_set_style_text_color(lblSave, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblSave);

    // Bouton Effacer Historique
    lv_obj_t* btnClearHist = lv_button_create(cardVolAction);
    lv_obj_set_size(btnClearHist, 72, 26);
    lv_obj_set_pos(btnClearHist, 222, 4);
    lv_obj_set_style_bg_color(btnClearHist, lv_color_hex(0x757575), 0);
    lv_obj_set_style_radius(btnClearHist, 4, 0);
    lv_obj_set_style_pad_all(btnClearHist, 0, 0);
    lv_obj_add_event_cb(btnClearHist, eventHandler, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(btnClearHist, (void*)(uintptr_t)EVT_CLEAR_HIST);

    lv_obj_t* lblClear = lv_label_create(btnClearHist);
    lv_label_set_text(lblClear, "Effacer");
    lv_obj_set_style_text_font(lblClear, &font_french_12, 0);
    lv_obj_set_style_text_color(lblClear, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblClear);

    // 3. Carte Feedback & Statut (y: 176, h: 32, w: 304)
    lv_obj_t* cardStatus = lv_obj_create(m_container);
    lv_obj_set_pos(cardStatus, 8, 176);
    lv_obj_set_size(cardStatus, 304, 32);
    lv_obj_set_style_bg_color(cardStatus, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(cardStatus, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_width(cardStatus, 1, 0);
    lv_obj_set_style_radius(cardStatus, 6, 0);
    lv_obj_set_style_pad_all(cardStatus, 4, 0);
    lv_obj_remove_flag(cardStatus, LV_OBJ_FLAG_SCROLLABLE);

    m_lblFeedback = lv_label_create(cardStatus);
    lv_label_set_text(m_lblFeedback, "Prêt");
    lv_obj_set_style_text_font(m_lblFeedback, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblFeedback, lv_color_hex(0x2E7D32), 0);
    lv_obj_align(m_lblFeedback, LV_ALIGN_LEFT_MID, 4, 0);

    // 4. Barre d'informations système au bas (y: 214, h: 22)
    m_lblSysInfo = lv_label_create(m_container);
    lv_obj_set_pos(m_lblSysInfo, 8, 214);
    lv_label_set_text(m_lblSysInfo, "VoiceKit v0.6.7.1 | VoiceAssistant v0.1.0");
    lv_obj_set_style_text_font(m_lblSysInfo, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblSysInfo, lv_color_hex(0x9E9E9E), 0);

    refresh();
}

void SettingsScreen::show() {
    if (m_container) {
        lv_obj_remove_flag(m_container, LV_OBJ_FLAG_HIDDEN);
        refresh();
    }
}

void SettingsScreen::hide() {
    if (m_container) {
        lv_obj_add_flag(m_container, LV_OBJ_FLAG_HIDDEN);
    }
}

bool SettingsScreen::isVisible() const {
    if (!m_container) return false;
    return !lv_obj_has_flag(m_container, LV_OBJ_FLAG_HIDDEN);
}

void SettingsScreen::update() {
    if (!m_controller) return;

    bool success = false;
    String msg;
    if (m_controller->consumeProfileChangeResult(success, msg)) {
        m_waitingConfirmation = false;
        if (success) {
            setFeedbackMessage("Modification appliquée");
        } else {
            setFeedbackMessage(msg.isEmpty() ? "Échec modification" : msg.c_str());
        }
        refresh();
    }
}

void SettingsScreen::refresh() {
    if (!m_controller) return;
    updateProviderButtons();
    updateLangLabel();
    updateVoiceLabel();
    updateVolumeLabel();

    if (m_lblSysInfo) {
        char sysBuf[96];
        bool wifiOk = m_controller->isWifiConnected();
        snprintf(sysBuf, sizeof(sysBuf), "WiFi: %s | VoiceKit: %s | App: %s",
                 wifiOk ? "OK" : "NO", AppConfig::VOICEKIT_VERSION, AppConfig::APP_VERSION);
        lv_label_set_text(m_lblSysInfo, sysBuf);
    }
}

void SettingsScreen::setFeedbackMessage(const char* msg) {
    if (m_lblFeedback && msg) {
        lv_label_set_text(m_lblFeedback, msg);
    }
}

void SettingsScreen::updateProviderButtons() {
    if (!m_btnGemini || !m_btnOpenAi || !m_controller) return;

    bool isGemini = (m_controller->getProvider() == VoiceProviderType::GEMINI);

    // Gemini
    lv_obj_set_style_bg_color(m_btnGemini,
        isGemini ? lv_color_hex(0x1976D2) : lv_color_hex(0xEEF2F6), 0);
    lv_obj_set_style_text_color(m_lblGemini,
        isGemini ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x424242), 0);

    // OpenAI
    lv_obj_set_style_bg_color(m_btnOpenAi,
        !isGemini ? lv_color_hex(0x1976D2) : lv_color_hex(0xEEF2F6), 0);
    lv_obj_set_style_text_color(m_lblOpenAi,
        !isGemini ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x424242), 0);
}

void SettingsScreen::updateLangLabel() {
    if (!m_lblLangValue || !m_controller) return;

    String curLang = m_controller->getLanguage();
    const char* displayLabel = curLang.c_str();

    for (size_t i = 0; i < kSupportedLangCount; ++i) {
        if (curLang.equalsIgnoreCase(kSupportedLangs[i].bcp47)) {
            displayLabel = kSupportedLangs[i].label;
            m_selectedLangIdx = i;
            break;
        }
    }
    lv_label_set_text(m_lblLangValue, displayLabel);
}

void SettingsScreen::updateVoiceLabel() {
    if (!m_lblVoiceValue || !m_controller) return;
    String curVoice = m_controller->getVoiceId();
    lv_label_set_text(m_lblVoiceValue, curVoice.isEmpty() ? "(Par défaut)" : curVoice.c_str());

    std::vector<VoiceDescriptor> voices = m_controller->getAvailableVoices();
    for (size_t i = 0; i < voices.size(); ++i) {
        if (curVoice.equalsIgnoreCase(voices[i].id)) {
            m_selectedVoiceIdx = i;
            break;
        }
    }
}

void SettingsScreen::updateVolumeLabel() {
    if (!m_lblVolumeValue || !m_controller) return;
    char volBuf[16];
    snprintf(volBuf, sizeof(volBuf), "%u %%", (unsigned)m_controller->getVolume());
    lv_label_set_text(m_lblVolumeValue, volBuf);
}

void SettingsScreen::onProviderClicked(VoiceProviderType prov) {
    if (!m_controller) return;
    if (m_waitingConfirmation || m_controller->isProfileChangeInProgress()) {
        setFeedbackMessage("Modification déjà en cours");
        return;
    }
    if (m_controller->getProvider() == prov) {
        setFeedbackMessage("Fournisseur déjà actif");
        return;
    }
    setFeedbackMessage("Modification en cours...");
    m_waitingConfirmation = true;
    bool ok = m_controller->switchProvider(prov);
    if (!ok) {
        m_waitingConfirmation = false;
        setFeedbackMessage("Échec demande modification");
        refresh();
    }
}

void SettingsScreen::onLangCycleClicked() {
    if (!m_controller) return;
    if (m_waitingConfirmation || m_controller->isProfileChangeInProgress()) {
        setFeedbackMessage("Modification déjà en cours");
        return;
    }
    size_t nextIdx = (m_selectedLangIdx + 1) % kSupportedLangCount;
    const char* targetBcp47 = kSupportedLangs[nextIdx].bcp47;
    setFeedbackMessage("Modification en cours...");
    m_waitingConfirmation = true;
    bool ok = m_controller->setLanguage(targetBcp47);
    if (!ok) {
        m_waitingConfirmation = false;
        setFeedbackMessage("Échec demande modification");
        refresh();
    }
}

void SettingsScreen::onVoiceCycleClicked() {
    if (!m_controller) return;
    if (m_waitingConfirmation || m_controller->isProfileChangeInProgress()) {
        setFeedbackMessage("Modification déjà en cours");
        return;
    }
    std::vector<VoiceDescriptor> voices = m_controller->getAvailableVoices();
    if (voices.empty()) return;

    size_t nextIdx = (m_selectedVoiceIdx + 1) % voices.size();
    String newVoiceId = voices[nextIdx].id;
    setFeedbackMessage("Modification en cours...");
    m_waitingConfirmation = true;
    bool ok = m_controller->setVoice(newVoiceId);
    if (!ok) {
        m_waitingConfirmation = false;
        setFeedbackMessage("Échec demande modification");
        refresh();
    }
}

void SettingsScreen::onVolumeChange(int delta) {
    if (!m_controller) return;
    int curVol = (int)m_controller->getVolume();
    int newVol = curVol + delta;
    if (newVol < 0) newVol = 0;
    if (newVol > 100) newVol = 100;
    m_controller->setVolume((uint8_t)newVol);
    updateVolumeLabel();
}

void SettingsScreen::onSaveNvsClicked() {
    if (!m_controller) return;
    if (m_waitingConfirmation || m_controller->isProfileChangeInProgress()) {
        setFeedbackMessage("Attendre fin modification");
        return;
    }
    bool ok = m_controller->saveProfileToNvs();
    setFeedbackMessage(ok ? "Profil sauvegardé en NVS" : "Erreur sauvegarde NVS");
}

void SettingsScreen::onClearHistoryClicked() {
    if (!m_controller) return;
    m_controller->clearHistory();
    setFeedbackMessage("Historique local effacé");
}

void SettingsScreen::eventHandler(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    uintptr_t evtId = (uintptr_t)lv_obj_get_user_data(target);

    switch (evtId) {
        case EVT_BACK:
            if (self->m_onBack) self->m_onBack();
            break;
        case EVT_PROVIDER_GEMINI:
            self->onProviderClicked(VoiceProviderType::GEMINI);
            break;
        case EVT_PROVIDER_OPENAI:
            self->onProviderClicked(VoiceProviderType::OPENAI);
            break;
        case EVT_LANG_CYCLE:
            self->onLangCycleClicked();
            break;
        case EVT_VOICE_CYCLE:
            self->onVoiceCycleClicked();
            break;
        case EVT_VOL_DOWN:
            self->onVolumeChange(-10);
            break;
        case EVT_VOL_UP:
            self->onVolumeChange(10);
            break;
        case EVT_SAVE_NVS:
            self->onSaveNvsClicked();
            break;
        case EVT_CLEAR_HIST:
            self->onClearHistoryClicked();
            break;
        default:
            break;
    }
}
