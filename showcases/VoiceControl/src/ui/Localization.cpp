#include "Localization.h"

namespace Localization {

static const Strings kFrenchStrings = {
    "VoiceControl",
    "PARLER",
    "VALIDER",
    "STOP",
    "NTP : Attente Wi-Fi",
    "NTP : Synchro...",
    "NTP : OK",
    "NTP : Indisponible",
    "Horloge non synchronisée",
    "Connexion...",
    "Prêt",
    "Écoute en cours...",
    "Traitement...",
    "Réponse vocale...",
    "Interrompu (STOP)",
    "Silence ignoré",
    "Erreur vocale",
    "Wi-Fi déconnecté",
    "INIT",
    "READY",
    "LISTENING",
    "PROCESSING",
    "SPEAKING",
    "ERROR"
};

static const Strings kEnglishStrings = {
    "VoiceControl",
    "TALK",
    "SEND",
    "STOP",
    "NTP: Waiting Wi-Fi",
    "NTP: Syncing...",
    "NTP: OK",
    "NTP: Unavailable",
    "Clock not synced",
    "Connecting...",
    "Ready",
    "Listening...",
    "Processing...",
    "Speaking...",
    "Stopped (STOP)",
    "Silence ignored",
    "Voice error",
    "Wi-Fi disconnected",
    "INIT",
    "READY",
    "LISTENING",
    "PROCESSING",
    "SPEAKING",
    "ERROR"
};

const Strings& get(VoiceControlConfig::LanguageCode lang) {
    return (lang == VoiceControlConfig::LanguageCode::EN) ? kEnglishStrings : kFrenchStrings;
}

const char* getNtpStatusLabel(NtpUiStatus status, VoiceControlConfig::LanguageCode lang) {
    const Strings& s = get(lang);
    switch (status) {
        case NtpUiStatus::WAITING_WIFI: return s.ntpWaitingWifi;
        case NtpUiStatus::SYNCING:      return s.ntpSyncing;
        case NtpUiStatus::SYNCED:       return s.ntpSynced;
        case NtpUiStatus::UNAVAILABLE:
        default:                        return s.ntpUnavailable;
    }
}

const char* getVoiceStateLabel(VoiceUiStatus status, VoiceControlConfig::LanguageCode lang) {
    const Strings& s = get(lang);
    switch (status) {
        case VoiceUiStatus::READY:       return s.stateLabelReady;
        case VoiceUiStatus::LISTENING:   return s.stateLabelListening;
        case VoiceUiStatus::PROCESSING:  return s.stateLabelProcessing;
        case VoiceUiStatus::SPEAKING:    return s.stateLabelSpeaking;
        case VoiceUiStatus::ERROR_STATE: return s.stateLabelError;
        case VoiceUiStatus::INIT:
        default:                         return s.stateLabelInit;
    }
}

const char* getWeekdayTitle(int tmWday, VoiceControlConfig::LanguageCode lang) {
    static const char* const kDaysFr[7] = {
        "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"
    };
    static const char* const kDaysEn[7] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    if (tmWday < 0 || tmWday > 6) {
        return "";
    }
    return (lang == VoiceControlConfig::LanguageCode::EN) ? kDaysEn[tmWday] : kDaysFr[tmWday];
}

} // namespace Localization
