#pragma once

#include <lvgl.h>
#include <stdint.h>
#include <stddef.h>
#include "config/VoiceControlConfig.h"
#include "ui/Localization.h"
#include "voice/VoiceController.h"

/**
 * @file VoiceControlUI.h
 * @brief Interface LVGL 9 (320x240) pour VoiceControl v0.1.0 sur M5Stack CoreS3.
 *
 * Affiche exclusivement :
 * - Titre VoiceControl et badge fournisseur actif (OpenAI ou Gemini, lecture seule)
 * - Heure suisse en gros caractères (actualisée chaque seconde) et date au format JJ.MM.AAAA
 * - État de synchronisation NTP et fuseau horaire (CET/CEST)
 * - État vocal (READY, LISTENING, PROCESSING, SPEAKING, ERROR)
 * - Boutons PARLER (PTT 2 clics) et STOP (interruption immédiate)
 *
 * Aucun menu de paramètres ni sélecteur de fournisseur/modèle.
 */

class VoiceControlUI {
public:
    VoiceControlUI() = default;
    ~VoiceControlUI() = default;

    void init(VoiceController& voiceController);
    void update(uint32_t nowMs);

private:
    static void onTalkBtnEvent(lv_event_t* e);
    static void onStopBtnEvent(lv_event_t* e);
    static void setLabelIfChanged(lv_obj_t* label, char* cache, size_t cacheSize, const char* text);

    void buildMainScreen();
    void updateClockDisplay(uint32_t nowMs);
    void updateVoiceDisplay();

    VoiceController* m_voice = nullptr;
    VoiceControlConfig::LanguageCode m_uiLang = VoiceControlConfig::LanguageCode::FR;

    // Widgets LVGL 9
    lv_obj_t* m_screen       = nullptr;
    lv_obj_t* m_lblTime      = nullptr;
    lv_obj_t* m_lblDate      = nullptr;
    lv_obj_t* m_lblTz        = nullptr;
    lv_obj_t* m_lblNtp       = nullptr;
    lv_obj_t* m_pillVoice    = nullptr;
    lv_obj_t* m_lblVoiceSt   = nullptr;
    lv_obj_t* m_lblStatus    = nullptr;
    lv_obj_t* m_btnTalk      = nullptr;
    lv_obj_t* m_lblTalk      = nullptr;
    lv_obj_t* m_btnStop      = nullptr;

    // Caches différentiels (zéro redessin inutile)
    char m_cacheTime[16]     = "";
    char m_cacheDate[48]     = "";
    char m_cacheTz[32]       = "";
    char m_cacheNtp[32]      = "";
    char m_cacheVoiceSt[24]  = "";
    char m_cacheStatus[128]  = "";
    char m_cacheTalkBtn[24]  = "";

    uint32_t m_lastClockTickMs = 0;
    int m_lastDisplayedSec     = -1;
    NtpUiStatus m_lastNtpSt    = NtpUiStatus::WAITING_WIFI;
    VoiceUiStatus m_lastVoiceUi = VoiceUiStatus::INIT;
    bool m_lastTalkEnabled     = false;
    bool m_lastTalkListening   = false;
};
