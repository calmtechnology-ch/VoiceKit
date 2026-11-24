#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @file app_config.h
 * @brief Paramètres applicatifs pour TwoClickConversationLVGL v0.6.7.1.
 */

// Sélection du fournisseur IA (OpenAI Realtime par défaut, basculable vers Gemini Live)
#define USE_OPENAI 0
#define USE_GEMINI 1

namespace AppConfig {
    // Écran CoreS3 (320x240)
    constexpr uint32_t SCREEN_WIDTH  = 320;
    constexpr uint32_t SCREEN_HEIGHT = 240;
    constexpr size_t   BUFFER_PIXELS = 320 * 20; // 6400 pixels (~12.8 Ko DMA)

    // Configuration OpenAI Realtime
    constexpr const char* OPENAI_MODEL    = "gpt-realtime-2.1-mini";
    constexpr const char* OPENAI_VOICE    = "alloy";
    constexpr const char* OPENAI_LANGUAGE = "fr-FR";

    // Configuration Gemini Live (si USE_GEMINI activé)
    constexpr const char* GEMINI_MODEL    = "models/gemini-3.8-live";
    constexpr const char* GEMINI_VOICE    = "Aoede";
    constexpr const char* GEMINI_LANGUAGE = "fr-FR";

    // Directives système pour l'IA
    constexpr const char* CONVERSATION_INSTRUCTION =
        "Tu es un assistant vocal fluide et concis pour M5Stack CoreS3. "
        "Réponds brièvement en français naturel, sans balises markdown ni émojis.";

    // Volume audio (0-100%)
    constexpr uint8_t DEFAULT_VOLUME = 80;

    // Paramètres Interaction & VAD
    constexpr uint32_t DEBOUNCE_MS                 = 120;
    constexpr uint32_t MIC_TIMEOUT_MS              = 15000;
    constexpr uint32_t SERVER_TIMEOUT_MS           = 15000;
    constexpr uint32_t MIN_CAPTURE_DURATION_MS     = 350;
    constexpr uint32_t MIN_SPEECH_DURATION_MS      = 250;
    constexpr uint32_t MIN_CONSECUTIVE_SPEECH_MS   = 120;
    constexpr uint32_t SPEECH_THRESHOLD_FLOOR_RMS  = 380;
}
