#include "SensorStationUI.h"
#include "FrenchFonts.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <time.h>
#include <esp_heap_caps.h>

// Palette thème clair (lisible, moderne, fort contraste sur CoreS3 320x240)
static constexpr uint32_t COL_BG          = 0xF1F5F9;
static constexpr uint32_t COL_CARD_BG     = 0xFFFFFF;
static constexpr uint32_t COL_CARD_BORDER = 0xCBD5E1;
static constexpr uint32_t COL_TEXT_DARK   = 0x0F172A;
static constexpr uint32_t COL_TEXT_MUTED  = 0x64748B;
static constexpr uint32_t COL_PRIMARY     = 0x2563EB;
static constexpr uint32_t COL_DANGER      = 0xDC2626;
static constexpr uint32_t COL_SUCCESS     = 0x16A34A;
static constexpr uint32_t COL_WARNING     = 0xD97706;
static constexpr uint32_t COL_SLATE       = 0x475569;
static constexpr uint32_t COL_DISABLED    = 0x94A3B8;

void SensorStationUI::setLabelIfChanged(lv_obj_t* label, char* cache, size_t cacheSize, const char* text) {
    if (!label || !cache || !text) return;
    if (strncmp(cache, text, cacheSize) == 0) {
        return;
    }
    strncpy(cache, text, cacheSize - 1);
    cache[cacheSize - 1] = '\0';
    lv_label_set_text(label, cache);
}

void SensorStationUI::formatCommaFloat(char* dst, size_t dstSize, float value, int decimals) {
    if (decimals <= 0) {
        snprintf(dst, dstSize, "%.0f", roundf(value));
    } else {
        snprintf(dst, dstSize, "%.1f", value);
    }
    for (size_t i = 0; dst[i] != '\0'; ++i) {
        if (dst[i] == '.') dst[i] = ',';
    }
}

void SensorStationUI::init(SensorManager& sensorManager, StationVoiceController& voiceController) {
    m_sensors = &sensorManager;
    m_voice = &voiceController;

    buildDashboardScreen();
    buildSettingsScreen();
    buildDiagnosticsScreen();

    showScreen(ActiveScreen::DASHBOARD);
}

static lv_obj_t* createMetricCard(lv_obj_t* parent, int32_t x, int32_t y,
                                  const char* title, const char* unit,
                                  uint32_t accentColor, lv_obj_t** outValLabel) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, 148, 60);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD_BG), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_CARD_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblTitle = lv_label_create(card);
    lv_obj_set_pos(lblTitle, 2, 0);
    lv_label_set_text(lblTitle, title);
    lv_obj_set_style_text_font(lblTitle, &font_french_12, 0);
    lv_obj_set_style_text_color(lblTitle, lv_color_hex(COL_TEXT_MUTED), 0);

    lv_obj_t* lblVal = lv_label_create(card);
    lv_obj_set_pos(lblVal, 2, 20);
    lv_label_set_text(lblVal, "--");
    lv_obj_set_style_text_font(lblVal, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lblVal, lv_color_hex(accentColor), 0);

    lv_obj_t* lblUnit = lv_label_create(card);
    lv_obj_align(lblUnit, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
    lv_label_set_text(lblUnit, unit);
    lv_obj_set_style_text_font(lblUnit, &font_french_12, 0);
    lv_obj_set_style_text_color(lblUnit, lv_color_hex(COL_TEXT_MUTED), 0);

    *outValLabel = lblVal;
    return card;
}

void SensorStationUI::buildDashboardScreen() {
    m_scrDashboard = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(m_scrDashboard, lv_color_hex(COL_BG), 0);
    lv_obj_remove_flag(m_scrDashboard, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Barre supérieure (y=0, h=26)
    lv_obj_t* topBar = lv_obj_create(m_scrDashboard);
    lv_obj_set_pos(topBar, 0, 0);
    lv_obj_set_size(topBar, AppConfig::SCREEN_WIDTH, 26);
    lv_obj_set_style_bg_color(topBar, lv_color_hex(COL_CARD_BG), 0);
    lv_obj_set_style_border_width(topBar, 0, 0);
    lv_obj_set_style_radius(topBar, 0, 0);
    lv_obj_set_style_pad_all(topBar, 0, 0);
    lv_obj_remove_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblApp = lv_label_create(topBar);
    lv_obj_set_pos(lblApp, 8, 5);
    lv_label_set_text(lblApp, "VoiceSensorStation");
    lv_obj_set_style_text_font(lblApp, &font_french_14, 0);
    lv_obj_set_style_text_color(lblApp, lv_color_hex(COL_TEXT_DARK), 0);

    m_lblWifi = lv_label_create(topBar);
    lv_obj_set_pos(m_lblWifi, 198, 6);
    lv_label_set_text(m_lblWifi, "WiFi ...");
    lv_obj_set_style_text_font(m_lblWifi, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblWifi, lv_color_hex(COL_TEXT_MUTED), 0);

    m_lblClock = lv_label_create(topBar);
    lv_obj_align(m_lblClock, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_label_set_text(m_lblClock, "--:--");
    lv_obj_set_style_text_font(m_lblClock, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblClock, lv_color_hex(COL_TEXT_DARK), 0);

    // 2. Grille 2x2 des 4 paramètres environnementaux (y=32..158)
    createMetricCard(m_scrDashboard, 8,   32, "TEMPÉRATURE", "°C",   0xB91C1C, &m_lblTempVal);
    createMetricCard(m_scrDashboard, 164, 32, "HUMIDITÉ",    "%",    0x0369A1, &m_lblHumVal);
    createMetricCard(m_scrDashboard, 8,   98, "PRESSION",    "hPa",  0x1D4ED8, &m_lblPresVal);
    createMetricCard(m_scrDashboard, 164, 98, "GAZ (BME688)","kOhm", 0x047857, &m_lblGasVal);

    // 3. Barre d'état capteur & assistant (y=164, h=24)
    lv_obj_t* statusPanel = lv_obj_create(m_scrDashboard);
    lv_obj_set_pos(statusPanel, 8, 164);
    lv_obj_set_size(statusPanel, 304, 24);
    lv_obj_set_style_bg_color(statusPanel, lv_color_hex(COL_CARD_BG), 0);
    lv_obj_set_style_border_color(statusPanel, lv_color_hex(COL_CARD_BORDER), 0);
    lv_obj_set_style_border_width(statusPanel, 1, 0);
    lv_obj_set_style_radius(statusPanel, 6, 0);
    lv_obj_set_style_pad_all(statusPanel, 0, 0);
    lv_obj_remove_flag(statusPanel, LV_OBJ_FLAG_SCROLLABLE);

    m_lblSensorStatus = lv_label_create(statusPanel);
    lv_obj_align(m_lblSensorStatus, LV_ALIGN_LEFT_MID, 6, 0);
    lv_label_set_text(m_lblSensorStatus, "Capteur init...");
    lv_obj_set_style_text_font(m_lblSensorStatus, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblSensorStatus, lv_color_hex(COL_TEXT_MUTED), 0);

    m_lblVoiceStatus = lv_label_create(statusPanel);
    lv_obj_align(m_lblVoiceStatus, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_label_set_text(m_lblVoiceStatus, "Connexion IA...");
    lv_obj_set_style_text_font(m_lblVoiceStatus, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblVoiceStatus, lv_color_hex(COL_TEXT_MUTED), 0);

    // 4. Barre de boutons tactiles (y=194, h=40)
    m_btnTalk = lv_button_create(m_scrDashboard);
    lv_obj_set_pos(m_btnTalk, 8, 194);
    lv_obj_set_size(m_btnTalk, 114, 40);
    lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(COL_DISABLED), 0);
    lv_obj_set_style_radius(m_btnTalk, 8, 0);
    lv_obj_add_event_cb(m_btnTalk, onTalkClicked, LV_EVENT_CLICKED, this);

    m_lblBtnTalk = lv_label_create(m_btnTalk);
    lv_label_set_text(m_lblBtnTalk, "PARLER");
    lv_obj_set_style_text_font(m_lblBtnTalk, &font_french_14, 0);
    lv_obj_set_style_text_color(m_lblBtnTalk, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(m_lblBtnTalk);

    m_btnStop = lv_button_create(m_scrDashboard);
    lv_obj_set_pos(m_btnStop, 128, 194);
    lv_obj_set_size(m_btnStop, 78, 40);
    lv_obj_set_style_bg_color(m_btnStop, lv_color_hex(COL_DANGER), 0);
    lv_obj_set_style_radius(m_btnStop, 8, 0);
    lv_obj_add_event_cb(m_btnStop, onStopClicked, LV_EVENT_CLICKED, this);

    lv_obj_t* lblStop = lv_label_create(m_btnStop);
    lv_label_set_text(lblStop, "STOP");
    lv_obj_set_style_text_font(lblStop, &font_french_14, 0);
    lv_obj_set_style_text_color(lblStop, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblStop);

    m_btnSettings = lv_button_create(m_scrDashboard);
    lv_obj_set_pos(m_btnSettings, 212, 194);
    lv_obj_set_size(m_btnSettings, 100, 40);
    lv_obj_set_style_bg_color(m_btnSettings, lv_color_hex(COL_SLATE), 0);
    lv_obj_set_style_radius(m_btnSettings, 8, 0);
    lv_obj_add_event_cb(m_btnSettings, onSettingsClicked, LV_EVENT_CLICKED, this);

    lv_obj_t* lblSet = lv_label_create(m_btnSettings);
    lv_label_set_text(lblSet, "RÉGLAGES");
    lv_obj_set_style_text_font(lblSet, &font_french_14, 0);
    lv_obj_set_style_text_color(lblSet, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblSet);
}

void SensorStationUI::buildSettingsScreen() {
    m_scrSettings = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(m_scrSettings, lv_color_hex(COL_BG), 0);
    lv_obj_remove_flag(m_scrSettings, LV_OBJ_FLAG_SCROLLABLE);

    // En-tête
    lv_obj_t* topBar = lv_obj_create(m_scrSettings);
    lv_obj_set_pos(topBar, 0, 0);
    lv_obj_set_size(topBar, AppConfig::SCREEN_WIDTH, 28);
    lv_obj_set_style_bg_color(topBar, lv_color_hex(COL_CARD_BG), 0);
    lv_obj_set_style_border_width(topBar, 0, 0);
    lv_obj_set_style_radius(topBar, 0, 0);
    lv_obj_set_style_pad_all(topBar, 0, 0);
    lv_obj_remove_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblTitle = lv_label_create(topBar);
    lv_obj_set_pos(lblTitle, 8, 6);
    lv_label_set_text(lblTitle, "Réglages — v0.1.0");
    lv_obj_set_style_text_font(lblTitle, &font_french_14, 0);
    lv_obj_set_style_text_color(lblTitle, lv_color_hex(COL_TEXT_DARK), 0);

    lv_obj_t* btnBack = lv_button_create(topBar);
    lv_obj_set_pos(btnBack, 244, 2);
    lv_obj_set_size(btnBack, 70, 24);
    lv_obj_set_style_bg_color(btnBack, lv_color_hex(COL_SLATE), 0);
    lv_obj_set_style_radius(btnBack, 6, 0);
    lv_obj_add_event_cb(btnBack, onBackToDashClicked, LV_EVENT_CLICKED, this);

    lv_obj_t* lblBack = lv_label_create(btnBack);
    lv_label_set_text(lblBack, "Retour");
    lv_obj_set_style_text_font(lblBack, &font_french_12, 0);
    lv_obj_set_style_text_color(lblBack, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblBack);

    // Ligne 1 : Fournisseur IA (Gemini / OpenAI)
    lv_obj_t* lblProv = lv_label_create(m_scrSettings);
    lv_obj_set_pos(lblProv, 10, 40);
    lv_label_set_text(lblProv, "Fournisseur IA :");
    lv_obj_set_style_text_font(lblProv, &font_french_12, 0);
    lv_obj_set_style_text_color(lblProv, lv_color_hex(COL_TEXT_DARK), 0);

    m_btnGemini = lv_button_create(m_scrSettings);
    lv_obj_set_pos(m_btnGemini, 118, 34);
    lv_obj_set_size(m_btnGemini, 92, 28);
    lv_obj_set_style_radius(m_btnGemini, 6, 0);
    lv_obj_add_event_cb(m_btnGemini, onProviderGeminiClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* lblG = lv_label_create(m_btnGemini);
    lv_label_set_text(lblG, "Gemini");
    lv_obj_set_style_text_font(lblG, &font_french_12, 0);
    lv_obj_set_style_text_color(lblG, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblG);

    m_btnOpenAi = lv_button_create(m_scrSettings);
    lv_obj_set_pos(m_btnOpenAi, 218, 34);
    lv_obj_set_size(m_btnOpenAi, 92, 28);
    lv_obj_set_style_radius(m_btnOpenAi, 6, 0);
    lv_obj_add_event_cb(m_btnOpenAi, onProviderOpenAiClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* lblO = lv_label_create(m_btnOpenAi);
    lv_label_set_text(lblO, "OpenAI");
    lv_obj_set_style_text_font(lblO, &font_french_12, 0);
    lv_obj_set_style_text_color(lblO, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblO);

    // Ligne 2 : Langue & Voix
    lv_obj_t* btnLang = lv_button_create(m_scrSettings);
    lv_obj_set_pos(btnLang, 10, 70);
    lv_obj_set_size(btnLang, 146, 30);
    lv_obj_set_style_bg_color(btnLang, lv_color_hex(COL_CARD_BG), 0);
    lv_obj_set_style_border_color(btnLang, lv_color_hex(COL_CARD_BORDER), 0);
    lv_obj_set_style_border_width(btnLang, 1, 0);
    lv_obj_set_style_radius(btnLang, 6, 0);
    lv_obj_add_event_cb(btnLang, onCycleLanguageClicked, LV_EVENT_CLICKED, this);
    m_lblLangVal = lv_label_create(btnLang);
    lv_label_set_text(m_lblLangVal, "Langue : fr-FR");
    lv_obj_set_style_text_font(m_lblLangVal, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblLangVal, lv_color_hex(COL_TEXT_DARK), 0);
    lv_obj_center(m_lblLangVal);

    lv_obj_t* btnVoice = lv_button_create(m_scrSettings);
    lv_obj_set_pos(btnVoice, 164, 70);
    lv_obj_set_size(btnVoice, 146, 30);
    lv_obj_set_style_bg_color(btnVoice, lv_color_hex(COL_CARD_BG), 0);
    lv_obj_set_style_border_color(btnVoice, lv_color_hex(COL_CARD_BORDER), 0);
    lv_obj_set_style_border_width(btnVoice, 1, 0);
    lv_obj_set_style_radius(btnVoice, 6, 0);
    lv_obj_add_event_cb(btnVoice, onCycleVoiceClicked, LV_EVENT_CLICKED, this);
    m_lblVoiceVal = lv_label_create(btnVoice);
    lv_label_set_text(m_lblVoiceVal, "Voix : Aoede");
    lv_obj_set_style_text_font(m_lblVoiceVal, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblVoiceVal, lv_color_hex(COL_TEXT_DARK), 0);
    lv_obj_center(m_lblVoiceVal);

    // Ligne 3 : Volume (local, toujours utilisable hors ligne)
    m_lblVolVal = lv_label_create(m_scrSettings);
    lv_obj_set_pos(m_lblVolVal, 10, 112);
    lv_label_set_text(m_lblVolVal, "Volume : 75 %");
    lv_obj_set_style_text_font(m_lblVolVal, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblVolVal, lv_color_hex(COL_TEXT_DARK), 0);

    m_sliderVol = lv_slider_create(m_scrSettings);
    lv_obj_set_pos(m_sliderVol, 118, 114);
    lv_obj_set_size(m_sliderVol, 188, 12);
    lv_slider_set_range(m_sliderVol, 0, 100);
    lv_slider_set_value(m_sliderVol, AppConfig::DEFAULT_VOLUME, LV_ANIM_OFF);
    lv_obj_add_event_cb(m_sliderVol, onVolumeChanged, LV_EVENT_VALUE_CHANGED, this);

    // Ligne 4 : État capteur + message de retour
    m_lblSettingsSensor = lv_label_create(m_scrSettings);
    lv_obj_set_pos(m_lblSettingsSensor, 10, 144);
    lv_label_set_text(m_lblSettingsSensor, "Capteur : Initialisation...");
    lv_obj_set_style_text_font(m_lblSettingsSensor, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblSettingsSensor, lv_color_hex(COL_TEXT_MUTED), 0);

    m_lblSettingsFeedback = lv_label_create(m_scrSettings);
    lv_obj_set_pos(m_lblSettingsFeedback, 10, 166);
    lv_label_set_text(m_lblSettingsFeedback, "Réglages locaux disponibles hors ligne.");
    lv_obj_set_style_text_font(m_lblSettingsFeedback, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblSettingsFeedback, lv_color_hex(COL_PRIMARY), 0);

    // Ligne 5 : Boutons Sauver NVS & Diagnostics
    lv_obj_t* btnSave = lv_button_create(m_scrSettings);
    lv_obj_set_pos(btnSave, 10, 196);
    lv_obj_set_size(btnSave, 146, 36);
    lv_obj_set_style_bg_color(btnSave, lv_color_hex(COL_PRIMARY), 0);
    lv_obj_set_style_radius(btnSave, 8, 0);
    lv_obj_add_event_cb(btnSave, onSaveNvsClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* lblSave = lv_label_create(btnSave);
    lv_label_set_text(lblSave, "Sauver profil");
    lv_obj_set_style_text_font(lblSave, &font_french_14, 0);
    lv_obj_set_style_text_color(lblSave, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblSave);

    lv_obj_t* btnDiag = lv_button_create(m_scrSettings);
    lv_obj_set_pos(btnDiag, 164, 196);
    lv_obj_set_size(btnDiag, 146, 36);
    lv_obj_set_style_bg_color(btnDiag, lv_color_hex(COL_SLATE), 0);
    lv_obj_set_style_radius(btnDiag, 8, 0);
    lv_obj_add_event_cb(btnDiag, onDiagClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* lblDiag = lv_label_create(btnDiag);
    lv_label_set_text(lblDiag, "Diagnostics");
    lv_obj_set_style_text_font(lblDiag, &font_french_14, 0);
    lv_obj_set_style_text_color(lblDiag, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblDiag);
}

void SensorStationUI::buildDiagnosticsScreen() {
    m_scrDiag = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(m_scrDiag, lv_color_hex(COL_BG), 0);
    lv_obj_remove_flag(m_scrDiag, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* topBar = lv_obj_create(m_scrDiag);
    lv_obj_set_pos(topBar, 0, 0);
    lv_obj_set_size(topBar, AppConfig::SCREEN_WIDTH, 28);
    lv_obj_set_style_bg_color(topBar, lv_color_hex(COL_CARD_BG), 0);
    lv_obj_set_style_border_width(topBar, 0, 0);
    lv_obj_set_style_radius(topBar, 0, 0);
    lv_obj_set_style_pad_all(topBar, 0, 0);
    lv_obj_remove_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblTitle = lv_label_create(topBar);
    lv_obj_set_pos(lblTitle, 8, 6);
    lv_label_set_text(lblTitle, "Diagnostics — v0.1.0");
    lv_obj_set_style_text_font(lblTitle, &font_french_14, 0);
    lv_obj_set_style_text_color(lblTitle, lv_color_hex(COL_TEXT_DARK), 0);

    lv_obj_t* btnBack = lv_button_create(topBar);
    lv_obj_set_pos(btnBack, 244, 2);
    lv_obj_set_size(btnBack, 70, 24);
    lv_obj_set_style_bg_color(btnBack, lv_color_hex(COL_SLATE), 0);
    lv_obj_set_style_radius(btnBack, 6, 0);
    lv_obj_add_event_cb(btnBack, onBackToSettingsClicked, LV_EVENT_CLICKED, this);

    lv_obj_t* lblBack = lv_label_create(btnBack);
    lv_label_set_text(lblBack, "Retour");
    lv_obj_set_style_text_font(lblBack, &font_french_12, 0);
    lv_obj_set_style_text_color(lblBack, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lblBack);

    lv_obj_t* card = lv_obj_create(m_scrDiag);
    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 304, 198);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD_BG), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_CARD_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    m_lblDiagBody = lv_label_create(card);
    lv_obj_set_size(m_lblDiagBody, 286, 182);
    lv_label_set_long_mode(m_lblDiagBody, LV_LABEL_LONG_WRAP);
    lv_label_set_text(m_lblDiagBody, "Collecte des diagnostics...");
    lv_obj_set_style_text_font(m_lblDiagBody, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblDiagBody, lv_color_hex(COL_TEXT_DARK), 0);
}

void SensorStationUI::showScreen(ActiveScreen screen) {
    m_activeScreen = screen;
    if (screen == ActiveScreen::DASHBOARD && m_scrDashboard) {
        lv_screen_load(m_scrDashboard);
    } else if (screen == ActiveScreen::SETTINGS && m_scrSettings) {
        lv_screen_load(m_scrSettings);
    } else if (screen == ActiveScreen::DIAGNOSTICS && m_scrDiag) {
        lv_screen_load(m_scrDiag);
    }
}

void SensorStationUI::update(uint32_t nowMs) {
    if (!m_sensors || !m_voice) return;

    // Consommer les messages de résultat de profil
    bool profOk = false;
    String profMsg;
    if (m_voice->consumeProfileResult(profOk, profMsg)) {
        setLabelIfChanged(m_lblSettingsFeedback, m_cacheSettingsFeedback,
                          sizeof(m_cacheSettingsFeedback), profMsg.c_str());
        if (m_lblSettingsFeedback) {
            lv_obj_set_style_text_color(m_lblSettingsFeedback,
                                        lv_color_hex(profOk ? COL_SUCCESS : COL_DANGER), 0);
        }
    }

    // Rafraîchissement cadencé (toutes les 200 ms) pour éviter toute surcharge LVGL
    if (nowMs - m_lastUiRefreshMs < 200) {
        return;
    }
    m_lastUiRefreshMs = nowMs;

    uint32_t t0 = micros();
    if (m_activeScreen == ActiveScreen::DASHBOARD) {
        updateDashboard(nowMs);
    } else if (m_activeScreen == ActiveScreen::SETTINGS) {
        updateSettings(nowMs);
    } else {
        updateDiagnostics(nowMs);
    }
    m_lastUpdateDurationUs = static_cast<uint32_t>(micros() - t0);
}

void SensorStationUI::updateDashboard(uint32_t nowMs) {
    // 1. Wi-Fi & Horloge
    bool wifiOk = m_voice->isWifiConnected();
    setLabelIfChanged(m_lblWifi, m_cacheWifi, sizeof(m_cacheWifi), wifiOk ? "WiFi OK" : "Hors ligne");
    if (wifiOk != m_lastRenderedWifi) {
        m_lastRenderedWifi = wifiOk;
        lv_obj_set_style_text_color(m_lblWifi, lv_color_hex(wifiOk ? COL_SUCCESS : COL_WARNING), 0);
    }

    char clockBuf[12] = "--:--";
    time_t nowSec = time(nullptr);
    if (nowSec >= 1704067200) { // >= 2024-01-01
        struct tm tmLocal;
        localtime_r(&nowSec, &tmLocal);
        snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d", tmLocal.tm_hour, tmLocal.tm_min);
    }
    setLabelIfChanged(m_lblClock, m_cacheClock, sizeof(m_cacheClock), clockBuf);

    // 2. Valeurs environnementales (instantané cohérent)
    SensorData snap = m_sensors->getSnapshot();
    SensorState sState = m_sensors->getState();

    char tBuf[16], hBuf[16], pBuf[16], gBuf[16];
    if (snap.valid) {
        formatCommaFloat(tBuf, sizeof(tBuf), snap.temperatureC, 1);
        formatCommaFloat(hBuf, sizeof(hBuf), snap.humidityPercent, 0);
        formatCommaFloat(pBuf, sizeof(pBuf), snap.pressureHpa, 1);
        if (snap.gasValid) {
            formatCommaFloat(gBuf, sizeof(gBuf), snap.gasResistanceOhms / 1000.0f, 1);
        } else {
            strncpy(gBuf, "--", sizeof(gBuf));
        }
    } else {
        strncpy(tBuf, "--", sizeof(tBuf));
        strncpy(hBuf, "--", sizeof(hBuf));
        strncpy(pBuf, "--", sizeof(pBuf));
        strncpy(gBuf, "--", sizeof(gBuf));
    }

    setLabelIfChanged(m_lblTempVal, m_cacheTemp, sizeof(m_cacheTemp), tBuf);
    setLabelIfChanged(m_lblHumVal,  m_cacheHum,  sizeof(m_cacheHum),  hBuf);
    setLabelIfChanged(m_lblPresVal, m_cachePres, sizeof(m_cachePres), pBuf);
    setLabelIfChanged(m_lblGasVal,  m_cacheGas,  sizeof(m_cacheGas),  gBuf);

    // 3. Statut capteur (indique l'âge si périmé ou déconnecté avec dernière mesure)
    char sStatusBuf[40];
    uint32_t sensorColor = COL_SUCCESS;
    if (sState == SensorState::READY || sState == SensorState::READING) {
        strncpy(sStatusBuf, "Capteur OK", sizeof(sStatusBuf));
        sensorColor = COL_SUCCESS;
    } else if (sState == SensorState::STALE) {
        uint32_t ageS = m_sensors->getLastSampleAgeMs(nowMs) / 1000;
        snprintf(sStatusBuf, sizeof(sStatusBuf), "Mesure anc. (%lus)", (unsigned long)ageS);
        sensorColor = COL_WARNING;
    } else if (sState == SensorState::INITIALIZING) {
        strncpy(sStatusBuf, "Capteur init...", sizeof(sStatusBuf));
        sensorColor = COL_TEXT_MUTED;
    } else {
        if (snap.valid) {
            uint32_t ageS = m_sensors->getLastSampleAgeMs(nowMs) / 1000;
            snprintf(sStatusBuf, sizeof(sStatusBuf), "Capteur KO (%lus)", (unsigned long)ageS);
        } else {
            strncpy(sStatusBuf, "Capteur indisponible", sizeof(sStatusBuf));
        }
        sensorColor = COL_DANGER;
    }
    setLabelIfChanged(m_lblSensorStatus, m_cacheSensorSt, sizeof(m_cacheSensorSt), sStatusBuf);
    if (sState != m_lastRenderedSensorState) {
        m_lastRenderedSensorState = sState;
        lv_obj_set_style_text_color(m_lblSensorStatus, lv_color_hex(sensorColor), 0);
    }

    // 4. Statut vocal & bouton PARLER / TERMINER
    const char* vShort = m_voice->getAssistantStatusShort();
    setLabelIfChanged(m_lblVoiceStatus, m_cacheVoiceSt, sizeof(m_cacheVoiceSt), vShort);

    VoiceInteractionState vState = m_voice->getInteractionState();
    const char* talkLabel = "PARLER";
    uint32_t talkBg = COL_DISABLED;

    if (!wifiOk) {
        talkLabel = "HORS LIGNE";
        talkBg = COL_DISABLED;
    } else if (vState == VoiceInteractionState::READY && m_voice->isSessionReady()) {
        talkLabel = "PARLER";
        talkBg = COL_PRIMARY;
    } else if (vState == VoiceInteractionState::LISTENING || vState == VoiceInteractionState::STARTING) {
        talkLabel = "TERMINER";
        talkBg = COL_DANGER;
    } else if (vState == VoiceInteractionState::PROCESSING || vState == VoiceInteractionState::STOPPING_CAPTURE) {
        talkLabel = "ANALYSE...";
        talkBg = COL_WARNING;
    } else if (vState == VoiceInteractionState::SPEAKING) {
        talkLabel = "PAROLE...";
        talkBg = 0x7C3AED;
    } else if (vState == VoiceInteractionState::ERROR_STATE) {
        talkLabel = "RÉESSAYER";
        talkBg = COL_WARNING;
    }

    setLabelIfChanged(m_lblBtnTalk, m_cacheTalkBtn, sizeof(m_cacheTalkBtn), talkLabel);
    if (vState != m_lastRenderedVoiceState || !wifiOk) {
        m_lastRenderedVoiceState = vState;
        lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(talkBg), 0);
    }
}

void SensorStationUI::updateSettings(uint32_t nowMs) {
    bool isGemini = (m_voice->getProvider() == VoiceProviderType::GEMINI);
    lv_obj_set_style_bg_color(m_btnGemini, lv_color_hex(isGemini ? COL_PRIMARY : COL_DISABLED), 0);
    lv_obj_set_style_bg_color(m_btnOpenAi, lv_color_hex(!isGemini ? COL_PRIMARY : COL_DISABLED), 0);

    char langBuf[32];
    snprintf(langBuf, sizeof(langBuf), "Langue : %s", m_voice->getLanguage().c_str());
    lv_label_set_text(m_lblLangVal, langBuf);

    char voiceBuf[32];
    snprintf(voiceBuf, sizeof(voiceBuf), "Voix : %s", m_voice->getVoiceId().c_str());
    lv_label_set_text(m_lblVoiceVal, voiceBuf);

    uint8_t vol = m_voice->getVolume();
    char volBuf[24];
    snprintf(volBuf, sizeof(volBuf), "Volume : %u %%", (unsigned)vol);
    lv_label_set_text(m_lblVolVal, volBuf);

    char sensorBuf[64];
    SensorState st = m_sensors->getState();
    const SensorDiagnostics& diag = m_sensors->getDiagnostics();
    if (m_sensors->hasValidData()) {
        uint32_t ageMs = m_sensors->getLastSampleAgeMs(nowMs);
        snprintf(sensorBuf, sizeof(sensorBuf), "Capteur : %s (0x%02X, âge %.1fs)",
                 sensorStateToString(st), diag.i2cAddress, ageMs / 1000.0f);
    } else {
        snprintf(sensorBuf, sizeof(sensorBuf), "Capteur : %s (erreurs I2C: %lu)",
                 sensorStateToString(st), (unsigned long)diag.i2cErrorCount);
    }
    setLabelIfChanged(m_lblSettingsSensor, m_cacheSettingsSensor, sizeof(m_cacheSettingsSensor), sensorBuf);
}

void SensorStationUI::updateDiagnostics(uint32_t nowMs) {
    if (nowMs - m_lastDiagRefreshMs < 800) {
        return;
    }
    m_lastDiagRefreshMs = nowMs;

    const SensorDiagnostics& sd = m_sensors->getDiagnostics();
    const VoiceDiagnostics& vd = m_voice->getDiagnostics();
    uint32_t ageMs = m_sensors->hasValidData() ? m_sensors->getLastSampleAgeMs(nowMs) : 0;

    char buf[512];
    snprintf(buf, sizeof(buf),
             "[CAPTEUR BME688]\n"
             "État: %s | I2C: 0x%02X | Âge: %lu ms\n"
             "Lectures: %lu | Erreurs: %lu (I2C: %lu)\n\n"
             "[VOCAL & IA]\n"
             "Fournisseur: %s | PTT: %s\n"
             "Cmd locales: %lu | IA: %lu | STOP: %lu | Err: %lu\n\n"
             "[SYSTÈME]\n"
             "WiFi: %s | Uptime: %lu s\n"
             "Heap: %lu Ko | PSRAM: %lu Ko",
             sensorStateToString(m_sensors->getState()),
             sd.i2cAddress,
             (unsigned long)ageMs,
             (unsigned long)sd.readSuccessCount,
             (unsigned long)sd.readErrorCount,
             (unsigned long)sd.i2cErrorCount,
             m_voice->getProviderName(),
             m_voice->getInteractionStateString(),
             (unsigned long)vd.localCommandCount,
             (unsigned long)vd.aiQueryCount,
             (unsigned long)vd.stopInterruptCount,
             (unsigned long)vd.voiceErrorCount,
             m_voice->isWifiConnected() ? "CONNECTÉ" : "HORS LIGNE",
             (unsigned long)(nowMs / 1000),
             (unsigned long)(esp_get_free_heap_size() / 1024),
             (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    setLabelIfChanged(m_lblDiagBody, m_cacheDiag, sizeof(m_cacheDiag), buf);
}

// =============================================================================
// Callbacks LVGL
// =============================================================================

void SensorStationUI::onTalkClicked(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (self && self->m_voice) self->m_voice->onTalkClick();
}

void SensorStationUI::onStopClicked(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (self && self->m_voice) self->m_voice->triggerStop();
}

void SensorStationUI::onSettingsClicked(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (self) self->showScreen(ActiveScreen::SETTINGS);
}

void SensorStationUI::onBackToDashClicked(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (self) self->showScreen(ActiveScreen::DASHBOARD);
}

void SensorStationUI::onBackToSettingsClicked(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (self) self->showScreen(ActiveScreen::SETTINGS);
}

void SensorStationUI::onDiagClicked(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (self) {
        self->m_lastDiagRefreshMs = 0;
        self->showScreen(ActiveScreen::DIAGNOSTICS);
    }
}

void SensorStationUI::onProviderGeminiClicked(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (!self || !self->m_voice) return;
    setLabelIfChanged(self->m_lblSettingsFeedback, self->m_cacheSettingsFeedback,
                      sizeof(self->m_cacheSettingsFeedback), "Bascule vers Gemini...");
    self->m_voice->switchProvider(VoiceProviderType::GEMINI);
}

void SensorStationUI::onProviderOpenAiClicked(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (!self || !self->m_voice) return;
    setLabelIfChanged(self->m_lblSettingsFeedback, self->m_cacheSettingsFeedback,
                      sizeof(self->m_cacheSettingsFeedback), "Bascule vers OpenAI...");
    self->m_voice->switchProvider(VoiceProviderType::OPENAI);
}

void SensorStationUI::onCycleLanguageClicked(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (!self || !self->m_voice) return;
    auto langs = self->m_voice->getAvailableLanguages();
    if (langs.empty()) return;
    String cur = self->m_voice->getLanguage();
    size_t nextIdx = 0;
    for (size_t i = 0; i < langs.size(); ++i) {
        if (cur.equalsIgnoreCase(langs[i].bcp47)) {
            nextIdx = (i + 1) % langs.size();
            break;
        }
    }
    self->m_voice->setLanguage(langs[nextIdx].bcp47);
}

void SensorStationUI::onCycleVoiceClicked(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (!self || !self->m_voice) return;
    auto voices = self->m_voice->getAvailableVoices();
    if (voices.empty()) return;
    String cur = self->m_voice->getVoiceId();
    size_t nextIdx = 0;
    for (size_t i = 0; i < voices.size(); ++i) {
        if (cur.equalsIgnoreCase(voices[i].id)) {
            nextIdx = (i + 1) % voices.size();
            break;
        }
    }
    self->m_voice->setVoice(voices[nextIdx].id);
}

void SensorStationUI::onVolumeChanged(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (!self || !self->m_voice || !self->m_sliderVol) return;
    int32_t val = lv_slider_get_value(self->m_sliderVol);
    if (val < 0) val = 0;
    if (val > 100) val = 100;
    self->m_voice->setVolume(static_cast<uint8_t>(val));
}

void SensorStationUI::onSaveNvsClicked(lv_event_t* e) {
    auto* self = static_cast<SensorStationUI*>(lv_event_get_user_data(e));
    if (!self || !self->m_voice) return;
    bool ok = self->m_voice->saveProfileToNvs();
    setLabelIfChanged(self->m_lblSettingsFeedback, self->m_cacheSettingsFeedback,
                      sizeof(self->m_cacheSettingsFeedback),
                      ok ? "Profil sauvegardé en NVS." : "Échec sauvegarde NVS.");
    lv_obj_set_style_text_color(self->m_lblSettingsFeedback,
                                lv_color_hex(ok ? COL_SUCCESS : COL_DANGER), 0);
}
