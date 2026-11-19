#include "VoiceControlUI.h"
#include "FrenchFonts.h"
#include "commands/TimeCommands.h"
#include <cstdio>
#include <cstring>

// Palette claire à fort contraste pour M5Stack CoreS3 (320x240)
static constexpr uint32_t COL_BG          = 0xF1F5F9;
static constexpr uint32_t COL_SURFACE     = 0xFFFFFF;
static constexpr uint32_t COL_BORDER      = 0xCBD5E1;
static constexpr uint32_t COL_TEXT_DARK   = 0x0F172A;
static constexpr uint32_t COL_TEXT_MUTED  = 0x64748B;
static constexpr uint32_t COL_PRIMARY     = 0x2563EB;
static constexpr uint32_t COL_LISTENING   = 0xEA580C;
static constexpr uint32_t COL_PROCESSING  = 0x7C3AED;
static constexpr uint32_t COL_SPEAKING    = 0x0D9488;
static constexpr uint32_t COL_SUCCESS     = 0x16A34A;
static constexpr uint32_t COL_WARNING     = 0xD97706;
static constexpr uint32_t COL_DANGER      = 0xDC2626;
static constexpr uint32_t COL_DISABLED    = 0x94A3B8;
static constexpr uint32_t COL_PROVIDER_BG = 0xE2E8F0;

void VoiceControlUI::setLabelIfChanged(lv_obj_t* label, char* cache, size_t cacheSize, const char* text) {
    if (!label || !cache || !text) return;
    if (strncmp(cache, text, cacheSize) == 0) {
        return;
    }
    strncpy(cache, text, cacheSize - 1);
    cache[cacheSize - 1] = '\0';
    lv_label_set_text(label, cache);
}

void VoiceControlUI::onTalkBtnEvent(lv_event_t* e) {
    auto* self = static_cast<VoiceControlUI*>(lv_event_get_user_data(e));
    if (self && self->m_voice) {
        self->m_voice->onTalkClick();
    }
}

void VoiceControlUI::onStopBtnEvent(lv_event_t* e) {
    auto* self = static_cast<VoiceControlUI*>(lv_event_get_user_data(e));
    if (self && self->m_voice) {
        self->m_voice->triggerStop();
    }
}

void VoiceControlUI::init(VoiceController& voiceController) {
    m_voice = &voiceController;
    m_uiLang = VoiceControlConfig::getUiLanguage();

    buildMainScreen();
    lv_screen_load(m_screen);
}

void VoiceControlUI::buildMainScreen() {
    const Localization::Strings& str = Localization::get(m_uiLang);

    m_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(COL_BG), 0);
    lv_obj_remove_flag(m_screen, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Barre supérieure (y=0, h=28) : Titre + Fournisseur actif (lecture seule)
    lv_obj_t* topBar = lv_obj_create(m_screen);
    lv_obj_set_pos(topBar, 0, 0);
    lv_obj_set_size(topBar, VC_SCREEN_WIDTH, 28);
    lv_obj_set_style_bg_color(topBar, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_side(topBar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(topBar, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(topBar, 1, 0);
    lv_obj_set_style_radius(topBar, 0, 0);
    lv_obj_set_style_pad_all(topBar, 0, 0);
    lv_obj_remove_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblTitle = lv_label_create(topBar);
    lv_obj_align(lblTitle, LV_ALIGN_LEFT_MID, 10, 0);
    lv_label_set_text(lblTitle, str.appTitle);
    lv_obj_set_style_text_font(lblTitle, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(lblTitle, lv_color_hex(COL_TEXT_DARK), 0);

    // Badge fournisseur IA actif (OpenAI ou Gemini, non cliquable)
    lv_obj_t* badgeProvider = lv_obj_create(topBar);
    lv_obj_set_size(badgeProvider, 72, 20);
    lv_obj_align(badgeProvider, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(badgeProvider, lv_color_hex(COL_PROVIDER_BG), 0);
    lv_obj_set_style_border_width(badgeProvider, 0, 0);
    lv_obj_set_style_radius(badgeProvider, 10, 0);
    lv_obj_set_style_pad_all(badgeProvider, 0, 0);
    lv_obj_remove_flag(badgeProvider, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lblProvider = lv_label_create(badgeProvider);
    lv_obj_center(lblProvider);
    lv_label_set_text(lblProvider, VoiceControlConfig::getConfiguredProviderDisplay());
    lv_obj_set_style_text_font(lblProvider, &font_french_12, 0);
    lv_obj_set_style_text_color(lblProvider, lv_color_hex(COL_TEXT_DARK), 0);

    // 2. Carte Horloge Suisse (y=34, h=102, x=10, w=300)
    lv_obj_t* clockCard = lv_obj_create(m_screen);
    lv_obj_set_pos(clockCard, 10, 34);
    lv_obj_set_size(clockCard, 300, 102);
    lv_obj_set_style_bg_color(clockCard, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_color(clockCard, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(clockCard, 1, 0);
    lv_obj_set_style_radius(clockCard, 10, 0);
    lv_obj_set_style_pad_all(clockCard, 6, 0);
    lv_obj_remove_flag(clockCard, LV_OBJ_FLAG_SCROLLABLE);

    // Heure suisse en gros caractères (24h)
    m_lblTime = lv_label_create(clockCard);
    lv_obj_align(m_lblTime, LV_ALIGN_TOP_MID, 0, 2);
    lv_label_set_text(m_lblTime, "--:--:--");
    lv_obj_set_style_text_font(m_lblTime, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(m_lblTime, lv_color_hex(COL_TEXT_DARK), 0);

    // Date du jour (format suisse JJ.MM.AAAA)
    m_lblDate = lv_label_create(clockCard);
    lv_obj_align(m_lblDate, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(m_lblDate, str.dateNotSynced);
    lv_obj_set_style_text_font(m_lblDate, &font_french_14, 0);
    lv_obj_set_style_text_color(m_lblDate, lv_color_hex(COL_TEXT_MUTED), 0);

    // Ligne inférieure de la carte : Fuseau horaire (gauche) & État NTP (droite)
    m_lblTz = lv_label_create(clockCard);
    lv_obj_align(m_lblTz, LV_ALIGN_BOTTOM_LEFT, 4, -2);
    lv_label_set_text(m_lblTz, VC_TIMEZONE_NAME);
    lv_obj_set_style_text_font(m_lblTz, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblTz, lv_color_hex(COL_TEXT_MUTED), 0);

    m_lblNtp = lv_label_create(clockCard);
    lv_obj_align(m_lblNtp, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
    lv_label_set_text(m_lblNtp, str.ntpWaitingWifi);
    lv_obj_set_style_text_font(m_lblNtp, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblNtp, lv_color_hex(COL_WARNING), 0);

    // 3. Barre d'état vocal (y=142, h=32, x=10, w=300)
    lv_obj_t* voiceBar = lv_obj_create(m_screen);
    lv_obj_set_pos(voiceBar, 10, 142);
    lv_obj_set_size(voiceBar, 300, 32);
    lv_obj_set_style_bg_color(voiceBar, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_border_color(voiceBar, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(voiceBar, 1, 0);
    lv_obj_set_style_radius(voiceBar, 8, 0);
    lv_obj_set_style_pad_all(voiceBar, 4, 0);
    lv_obj_remove_flag(voiceBar, LV_OBJ_FLAG_SCROLLABLE);

    m_pillVoice = lv_obj_create(voiceBar);
    lv_obj_set_size(m_pillVoice, 104, 22);
    lv_obj_align(m_pillVoice, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(m_pillVoice, lv_color_hex(COL_DISABLED), 0);
    lv_obj_set_style_border_width(m_pillVoice, 0, 0);
    lv_obj_set_style_radius(m_pillVoice, 6, 0);
    lv_obj_set_style_pad_all(m_pillVoice, 0, 0);
    lv_obj_remove_flag(m_pillVoice, LV_OBJ_FLAG_SCROLLABLE);

    m_lblVoiceSt = lv_label_create(m_pillVoice);
    lv_obj_center(m_lblVoiceSt);
    lv_label_set_text(m_lblVoiceSt, str.stateLabelInit);
    lv_obj_set_style_text_font(m_lblVoiceSt, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblVoiceSt, lv_color_hex(0xFFFFFF), 0);

    m_lblStatus = lv_label_create(voiceBar);
    lv_obj_set_width(m_lblStatus, 178);
    lv_obj_align(m_lblStatus, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_label_set_long_mode(m_lblStatus, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(m_lblStatus, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(m_lblStatus, str.statusConnecting);
    lv_obj_set_style_text_font(m_lblStatus, &font_french_12, 0);
    lv_obj_set_style_text_color(m_lblStatus, lv_color_hex(COL_TEXT_MUTED), 0);

    // 4. Boutons PARLER et STOP (y=182, h=48)
    m_btnTalk = lv_button_create(m_screen);
    lv_obj_set_pos(m_btnTalk, 10, 182);
    lv_obj_set_size(m_btnTalk, 188, 48);
    lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(COL_DISABLED), 0);
    lv_obj_set_style_radius(m_btnTalk, 10, 0);
    lv_obj_add_event_cb(m_btnTalk, onTalkBtnEvent, LV_EVENT_CLICKED, this);

    m_lblTalk = lv_label_create(m_btnTalk);
    lv_obj_center(m_lblTalk);
    lv_label_set_text(m_lblTalk, str.btnTalk);
    lv_obj_set_style_text_font(m_lblTalk, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(m_lblTalk, lv_color_hex(0xFFFFFF), 0);

    m_btnStop = lv_button_create(m_screen);
    lv_obj_set_pos(m_btnStop, 208, 182);
    lv_obj_set_size(m_btnStop, 102, 48);
    lv_obj_set_style_bg_color(m_btnStop, lv_color_hex(COL_DANGER), 0);
    lv_obj_set_style_radius(m_btnStop, 10, 0);
    lv_obj_add_event_cb(m_btnStop, onStopBtnEvent, LV_EVENT_CLICKED, this);

    lv_obj_t* lblStop = lv_label_create(m_btnStop);
    lv_obj_center(lblStop);
    lv_label_set_text(lblStop, str.btnStop);
    lv_obj_set_style_text_font(lblStop, &font_french_bold_16, 0);
    lv_obj_set_style_text_color(lblStop, lv_color_hex(0xFFFFFF), 0);
}

void VoiceControlUI::updateClockDisplay(uint32_t nowMs) {
    if (!m_voice) return;

    // Actualisation indépendante de l'activité vocale (vérification toutes les 200 ms, rendu chaque seconde)
    if (nowMs - m_lastClockTickMs < 200) {
        return;
    }
    m_lastClockTickMs = nowMs;

    const Localization::Strings& str = Localization::get(m_uiLang);
    NtpUiStatus ntpSt = m_voice->getNtpStatus();

    if (ntpSt != m_lastNtpSt) {
        m_lastNtpSt = ntpSt;
        setLabelIfChanged(m_lblNtp, m_cacheNtp, sizeof(m_cacheNtp),
                          Localization::getNtpStatusLabel(ntpSt, m_uiLang));
        uint32_t ntpCol = COL_WARNING;
        if (ntpSt == NtpUiStatus::SYNCED) {
            ntpCol = COL_SUCCESS;
        } else if (ntpSt == NtpUiStatus::UNAVAILABLE) {
            ntpCol = COL_DANGER;
        }
        lv_obj_set_style_text_color(m_lblNtp, lv_color_hex(ntpCol), 0);
    }

    struct tm localTm{};
    bool isDst = false;
    if (m_voice->getCurrentSwissTime(localTm, &isDst)) {
        if (localTm.tm_sec != m_lastDisplayedSec) {
            m_lastDisplayedSec = localTm.tm_sec;

            char timeBuf[16];
            SwissTime::formatTimeHHMMSS(localTm, timeBuf, sizeof(timeBuf));
            setLabelIfChanged(m_lblTime, m_cacheTime, sizeof(m_cacheTime), timeBuf);

            char dateOnly[16];
            SwissTime::formatDateDDMMYYYY(localTm, dateOnly, sizeof(dateOnly));
            char dateFull[48];
            snprintf(dateFull, sizeof(dateFull), "%s %s",
                     Localization::getWeekdayTitle(localTm.tm_wday, m_uiLang),
                     dateOnly);
            setLabelIfChanged(m_lblDate, m_cacheDate, sizeof(m_cacheDate), dateFull);

            char tzBuf[32];
            snprintf(tzBuf, sizeof(tzBuf), "%s (%s)", VC_TIMEZONE_NAME, isDst ? "CEST" : "CET");
            setLabelIfChanged(m_lblTz, m_cacheTz, sizeof(m_cacheTz), tzBuf);
        }
    } else {
        setLabelIfChanged(m_lblTime, m_cacheTime, sizeof(m_cacheTime), "--:--:--");
        setLabelIfChanged(m_lblDate, m_cacheDate, sizeof(m_cacheDate), str.dateNotSynced);
        setLabelIfChanged(m_lblTz, m_cacheTz, sizeof(m_cacheTz), VC_TIMEZONE_NAME);
        m_lastDisplayedSec = -1;
    }
}

void VoiceControlUI::updateVoiceDisplay() {
    if (!m_voice) return;

    const Localization::Strings& str = Localization::get(m_uiLang);

    // 1. Messages asynchrones éventuels
    String msg;
    if (m_voice->consumeStatusMessage(msg) && !msg.isEmpty()) {
        setLabelIfChanged(m_lblStatus, m_cacheStatus, sizeof(m_cacheStatus), msg.c_str());
    }

    // 2. Pill d'état vocal (READY, LISTENING, PROCESSING, SPEAKING, ERROR)
    VoiceUiStatus uiSt = m_voice->getVoiceUiStatus();
    if (uiSt != m_lastVoiceUi) {
        m_lastVoiceUi = uiSt;
        setLabelIfChanged(m_lblVoiceSt, m_cacheVoiceSt, sizeof(m_cacheVoiceSt),
                          Localization::getVoiceStateLabel(uiSt, m_uiLang));

        uint32_t pillCol = COL_DISABLED;
        switch (uiSt) {
            case VoiceUiStatus::READY:       pillCol = COL_SUCCESS;    break;
            case VoiceUiStatus::LISTENING:   pillCol = COL_LISTENING;  break;
            case VoiceUiStatus::PROCESSING:  pillCol = COL_PROCESSING; break;
            case VoiceUiStatus::SPEAKING:    pillCol = COL_SPEAKING;   break;
            case VoiceUiStatus::ERROR_STATE: pillCol = COL_DANGER;     break;
            case VoiceUiStatus::INIT:
            default:                         pillCol = COL_DISABLED;   break;
        }
        lv_obj_set_style_bg_color(m_pillVoice, lv_color_hex(pillCol), 0);
    }

    // 3. Bouton PARLER (PTT 2 clics)
    const bool listening = m_voice->isListening();
    const bool talkEnabled = m_voice->isSessionReady() || listening || (uiSt == VoiceUiStatus::ERROR_STATE);

    if (listening != m_lastTalkListening || talkEnabled != m_lastTalkEnabled) {
        m_lastTalkListening = listening;
        m_lastTalkEnabled = talkEnabled;

        if (listening) {
            setLabelIfChanged(m_lblTalk, m_cacheTalkBtn, sizeof(m_cacheTalkBtn), str.btnStopCapture);
            lv_obj_set_style_bg_color(m_btnTalk, lv_color_hex(COL_LISTENING), 0);
        } else {
            setLabelIfChanged(m_lblTalk, m_cacheTalkBtn, sizeof(m_cacheTalkBtn), str.btnTalk);
            lv_obj_set_style_bg_color(m_btnTalk,
                                      lv_color_hex(talkEnabled ? COL_PRIMARY : COL_DISABLED), 0);
        }
    }
}

void VoiceControlUI::update(uint32_t nowMs) {
    updateClockDisplay(nowMs);
    updateVoiceDisplay();
}
