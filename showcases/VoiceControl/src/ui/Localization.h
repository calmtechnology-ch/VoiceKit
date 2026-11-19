#pragma once

#include <stdint.h>
#include "config/VoiceControlConfig.h"

/**
 * @file Localization.h
 * @brief Chaînes localisées FR / EN pour l'interface LVGL 9 et les messages d'état.
 */

enum class NtpUiStatus : uint8_t {
    WAITING_WIFI = 0,
    SYNCING,
    SYNCED,
    UNAVAILABLE
};

enum class VoiceUiStatus : uint8_t {
    INIT = 0,
    READY,
    LISTENING,
    PROCESSING,
    SPEAKING,
    ERROR_STATE
};

namespace Localization {

struct Strings {
    const char* appTitle;
    const char* btnTalk;
    const char* btnStopCapture;
    const char* btnStop;
    const char* ntpWaitingWifi;
    const char* ntpSyncing;
    const char* ntpSynced;
    const char* ntpUnavailable;
    const char* dateNotSynced;
    const char* statusConnecting;
    const char* statusReady;
    const char* statusListening;
    const char* statusProcessing;
    const char* statusSpeaking;
    const char* statusStopped;
    const char* statusSilence;
    const char* statusError;
    const char* statusOffline;
    const char* stateLabelInit;
    const char* stateLabelReady;
    const char* stateLabelListening;
    const char* stateLabelProcessing;
    const char* stateLabelSpeaking;
    const char* stateLabelError;
};

const Strings& get(VoiceControlConfig::LanguageCode lang = VoiceControlConfig::getUiLanguage());

const char* getNtpStatusLabel(NtpUiStatus status,
                              VoiceControlConfig::LanguageCode lang = VoiceControlConfig::getUiLanguage());

const char* getVoiceStateLabel(VoiceUiStatus status,
                               VoiceControlConfig::LanguageCode lang = VoiceControlConfig::getUiLanguage());

/**
 * @brief Nom du jour de la semaine avec majuscule initiale (0 = Dimanche/Sunday .. 6 = Samedi/Saturday).
 */
const char* getWeekdayTitle(int tmWday,
                            VoiceControlConfig::LanguageCode lang = VoiceControlConfig::getUiLanguage());

} // namespace Localization
