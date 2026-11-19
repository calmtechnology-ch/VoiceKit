#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file VoiceControlConfig.h
 * @brief Configuration centralisée unique de VoiceControl v0.1.0 pour M5Stack
 * CoreS3.
 *
 * Tous les paramètres applicatifs (langue, fournisseur IA, modèles, prompt
 * système, synchronisation NTP, fuseau horaire suisse et diagnostics) sont
 * définis ici uniquement. Aucun de ces paramètres n'est modifiable depuis
 * l'interface LVGL du CoreS3. Les clés API et identifiants Wi-Fi restent dans
 * include/secrets.h (exclu de Git).
 */

// =============================================================================
// 1. Métadonnées applicatives
// =============================================================================
#define VC_APP_NAME "VoiceControl"
#define VC_APP_VERSION "v0.1.0"
#define VC_VOICEKIT_VERSION "0.6.7.1"

// =============================================================================
// 2. Langue système unique ("fr" ou "en")
//    Détermine par défaut la langue de l'interface LVGL, du STT et du TTS.
// =============================================================================
#ifndef VC_LANGUAGE
#define VC_LANGUAGE "fr"
#endif

// Surcharges optionnelles (laisser vide "" pour hériter automatiquement de
// VC_LANGUAGE)
#ifndef VC_UI_LANGUAGE
#define VC_UI_LANGUAGE ""
#endif

#ifndef VC_STT_LANGUAGE
#define VC_STT_LANGUAGE ""
#endif

#ifndef VC_TTS_LANGUAGE
#define VC_TTS_LANGUAGE ""
#endif

// =============================================================================
// 3. Fournisseur IA et modèles (sélectionnés au démarrage, sans bascule
// dynamique)
// =============================================================================
#define VC_PROVIDER_OPENAI 1
#define VC_PROVIDER_GEMINI 2

// Choisir VC_PROVIDER_OPENAI ou VC_PROVIDER_GEMINI
#ifndef VC_AI_PROVIDER
#define VC_AI_PROVIDER VC_PROVIDER_GEMINI
#endif

#ifndef VC_OPENAI_MODEL
#define VC_OPENAI_MODEL "gpt-realtime-2.1-mini"
#endif

#ifndef VC_OPENAI_VOICE
#define VC_OPENAI_VOICE "alloy"
#endif

#ifndef VC_GEMINI_MODEL
#define VC_GEMINI_MODEL "models/gemini-2.5-flash-native-audio-latest"
#endif

#ifndef VC_GEMINI_VOICE
#define VC_GEMINI_VOICE "Aoede"
#endif

// =============================================================================
// 4. Prompt système (FR et EN)
// =============================================================================
#ifndef VC_SYSTEM_PROMPT_FR
#define VC_SYSTEM_PROMPT_FR                                                      \
  "Tu es VoiceControl, un assistant vocal spécialisé uniquement dans l'heure " \
  "et la date.\n"                                                                \
  "Tu réponds en français, avec des phrases courtes et naturelles.\n"            \
  "Tu peux comprendre les demandes concernant l'heure actuelle en Suisse, la "   \
  "date, le jour, l'année et les calculs horaires simples.\n"                   \
  "Tu ne dois jamais inventer l'heure ni la date.\n"                             \
  "Les valeurs horaires proviennent exclusivement des fonctions locales "        \
  "fournies par l'application.\n"                                                \
  "Si une demande ne concerne pas l'heure ou la date, réponds : Je peux "       \
  "uniquement répondre aux questions concernant l'heure et la date.\n"          \
  "Ne réponds jamais à une question horaire en utilisant tes propres "         \
  "connaissances temporelles."
#endif

#ifndef VC_SYSTEM_PROMPT_EN
#define VC_SYSTEM_PROMPT_EN                                                    \
  "You are VoiceControl, a voice assistant specialized exclusively in time "   \
  "and date.\n"                                                                \
  "You reply in English with short and natural sentences.\n"                   \
  "You can understand requests about the current time in Switzerland, the "    \
  "date, the day of the week, the year, and simple time calculations.\n"       \
  "You must never invent the time or the date.\n"                              \
  "All time values come exclusively from the local functions provided by the " \
  "application.\n"                                                             \
  "If a request is not about time or date, reply: I can only answer "          \
  "questions about the time and date.\n"                                       \
  "Never answer a time question using your own temporal knowledge."
#endif

// Directive complémentaire garantissant la restitution TTS mot à mot via
// VoxCoreVoice::say()
#define VC_TTS_VERBATIM_PREFIX_FR                                              \
  "Prononce exactement et uniquement ce texte : "
#define VC_TTS_VERBATIM_RULE_FR                                                  \
  "\nSi le message commence par 'Prononce exactement et uniquement ce texte "    \
  ": ', "                                                                        \
  "ton unique rôle est de prononcer très exactement ce texte mot à mot sans " \
  "aucun ajout ni commentaire."

#define VC_TTS_VERBATIM_PREFIX_EN                                              \
  "Speak aloud strictly and verbatim this exact text without translating it: "
#define VC_TTS_VERBATIM_RULE_EN                                                \
  "\nIf the message starts with 'Speak aloud strictly and verbatim this "      \
  "exact text without translating it: ', "                                     \
  "your sole role is to speak aloud that exact text word for word without "    \
  "any addition or commentary."

// =============================================================================
// 5. Synchronisation NTP et fuseau horaire suisse (Europe/Zurich)
//    Hiver : CET = UTC+1 | Été : CEST = UTC+2
// =============================================================================
#ifndef VC_TIMEZONE_NAME
#define VC_TIMEZONE_NAME "Europe/Zurich"
#endif

#ifndef VC_TIMEZONE_POSIX
#define VC_TIMEZONE_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

#ifndef VC_NTP_SERVER_1
#define VC_NTP_SERVER_1 "pool.ntp.org"
#endif

#ifndef VC_NTP_SERVER_2
#define VC_NTP_SERVER_2 "ch.pool.ntp.org"
#endif

#ifndef VC_NTP_SERVER_3
#define VC_NTP_SERVER_3 "time.google.com"
#endif

#ifndef VC_NTP_MIN_VALID_EPOCH
#define VC_NTP_MIN_VALID_EPOCH 1700000000L
#endif

#ifndef VC_NTP_TIMEOUT_MS
#define VC_NTP_TIMEOUT_MS 15000U
#endif

#ifndef VC_NTP_RETRY_INTERVAL_MS
#define VC_NTP_RETRY_INTERVAL_MS 30000U
#endif

// =============================================================================
// 6. Paramètres matériels, audio, PTT à deux clics et diagnostics
// =============================================================================
#define VC_SCREEN_WIDTH 320U
#define VC_SCREEN_HEIGHT 240U
#define VC_LVGL_BUFFER_PIXELS (VC_SCREEN_WIDTH * 20U)

#ifndef VC_DEFAULT_VOLUME
#define VC_DEFAULT_VOLUME 75U
#endif

// Paramètres validés PTT 2 clics & VAD local pour CoreS3 (ES7210)
#define VC_PTT_DEBOUNCE_MS 120U
#define VC_PTT_MIC_TIMEOUT_MS 15000U
#define VC_PTT_SERVER_TIMEOUT_MS 15000U
#define VC_VAD_MIN_CAPTURE_DURATION_MS 350U
#define VC_VAD_MIN_SPEECH_DURATION_MS 250U
#define VC_VAD_MIN_CONSECUTIVE_SPEECH_MS 120U
#define VC_VAD_SPEECH_FLOOR_RMS 380U
#define VC_VAD_NOISE_MULTIPLIER 1.8f
#define VC_VAD_NOISE_OFFSET_RMS 150U

// Paramètres de diagnostics série
#ifndef VC_DIAG_ENABLED
#define VC_DIAG_ENABLED 1
#endif

#ifndef VC_DIAG_HEARTBEAT_MS
#define VC_DIAG_HEARTBEAT_MS 30000U
#endif

// =============================================================================
// 7. Résolution de configuration (langue dérivée & fournisseur actif)
// =============================================================================
namespace VoiceControlConfig {

enum class LanguageCode : uint8_t { FR = 0, EN = 1 };

enum class ProviderId : uint8_t { OPENAI = 0, GEMINI = 1 };

inline bool isEnglishTag(const char *tag) {
  if (!tag || tag[0] == '\0')
    return false;
  return (tag[0] == 'e' || tag[0] == 'E') && (tag[1] == 'n' || tag[1] == 'N');
}

inline LanguageCode getSystemLanguage() {
  return isEnglishTag(VC_LANGUAGE) ? LanguageCode::EN : LanguageCode::FR;
}

inline LanguageCode getUiLanguage() {
  if (VC_UI_LANGUAGE[0] != '\0') {
    return isEnglishTag(VC_UI_LANGUAGE) ? LanguageCode::EN : LanguageCode::FR;
  }
  return getSystemLanguage();
}

inline const char *getSystemLanguageCodeStr() {
  return (getSystemLanguage() == LanguageCode::EN) ? "en" : "fr";
}

inline const char *getSttLanguageBcp47() {
  if (VC_STT_LANGUAGE[0] != '\0') {
    return VC_STT_LANGUAGE;
  }
  return (getSystemLanguage() == LanguageCode::EN) ? "en-US" : "fr-CH";
}

inline const char *getTtsLanguageBcp47() {
  if (VC_TTS_LANGUAGE[0] != '\0') {
    return VC_TTS_LANGUAGE;
  }
  return (getSystemLanguage() == LanguageCode::EN) ? "en-US" : "fr-CH";
}

inline ProviderId getConfiguredProvider() {
#if VC_AI_PROVIDER == VC_PROVIDER_GEMINI
  return ProviderId::GEMINI;
#else
  return ProviderId::OPENAI;
#endif
}

inline const char *getConfiguredProviderUpper() {
  return (getConfiguredProvider() == ProviderId::GEMINI) ? "GEMINI" : "OPENAI";
}

inline const char *getConfiguredProviderDisplay() {
  return (getConfiguredProvider() == ProviderId::GEMINI) ? "Gemini" : "OpenAI";
}

inline const char *getConfiguredModel() {
  return (getConfiguredProvider() == ProviderId::GEMINI) ? VC_GEMINI_MODEL
                                                         : VC_OPENAI_MODEL;
}

inline const char *getConfiguredVoice() {
  return (getConfiguredProvider() == ProviderId::GEMINI) ? VC_GEMINI_VOICE
                                                         : VC_OPENAI_VOICE;
}

inline const char *getBaseSystemPrompt() {
  return (getSystemLanguage() == LanguageCode::EN) ? VC_SYSTEM_PROMPT_EN
                                                   : VC_SYSTEM_PROMPT_FR;
}

inline const char *getVerbatimPrefix() {
  return (getSystemLanguage() == LanguageCode::EN) ? VC_TTS_VERBATIM_PREFIX_EN
                                                   : VC_TTS_VERBATIM_PREFIX_FR;
}

inline const char *getVerbatimRule() {
  return (getSystemLanguage() == LanguageCode::EN) ? VC_TTS_VERBATIM_RULE_EN
                                                   : VC_TTS_VERBATIM_RULE_FR;
}

} // namespace VoiceControlConfig
