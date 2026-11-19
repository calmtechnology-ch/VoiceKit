#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @file app_config.h
 * @brief Paramètres applicatifs de VoiceAssistant v0.1.0 pour M5Stack CoreS3.
 */

namespace AppConfig {
    // Métadonnées applicatives
    constexpr const char* APP_NAME          = "VoiceAssistant";
    constexpr const char* APP_VERSION       = "v0.1.0";
    constexpr const char* VOICEKIT_VERSION  = "0.6.7.1";

    // Écran CoreS3 (320x240 paysage)
    constexpr uint32_t SCREEN_WIDTH   = 320;
    constexpr uint32_t SCREEN_HEIGHT  = 240;
    constexpr size_t   BUFFER_PIXELS  = 320 * 20; // 6400 pixels (~12.8 Ko DMA)

    // Configuration par défaut Gemini Live
    constexpr const char* GEMINI_MODEL    = "models/gemini-2.5-flash-native-audio-latest";
    constexpr const char* GEMINI_VOICE    = "Aoede";

    // Configuration par défaut OpenAI Realtime
    constexpr const char* OPENAI_MODEL    = "gpt-realtime-2.1-mini";
    constexpr const char* OPENAI_VOICE    = "alloy";

    // Langue par défaut
    constexpr const char* DEFAULT_LANGUAGE = "fr-FR";

    // Consigne système pour l'assistant vocal (Section 7)
    constexpr const char* SYSTEM_INSTRUCTION =
        "Tu es VoiceAssistant, un assistant vocal fonctionnant sur un M5Stack CoreS3. "
        "Tu réponds de manière naturelle, précise et concise dans la langue sélectionnée. "
        "Tes réponses sont destinées à être prononcées oralement. Pour une question simple, "
        "réponds en une ou deux phrases. Si l'utilisateur demande une explication détaillée, "
        "tu peux développer. N'affirme jamais avoir effectué une action matérielle ou consulté "
        "une donnée réelle sans disposer de cette capacité.";

    // Volume audio (0-100 %)
    constexpr uint8_t DEFAULT_VOLUME = 80;

    // Paramètres Interaction & VAD (Section 4.2)
    constexpr uint32_t DEBOUNCE_MS                 = 120;
    constexpr uint32_t MIC_TIMEOUT_MS              = 15000;
    constexpr uint32_t SERVER_TIMEOUT_MS           = 15000;
    constexpr uint32_t MIN_CAPTURE_DURATION_MS     = 350;
    constexpr uint32_t MIN_SPEECH_DURATION_MS      = 250;
    constexpr uint32_t MIN_CONSECUTIVE_SPEECH_MS   = 120;
    constexpr uint32_t SPEECH_THRESHOLD_FLOOR_RMS  = 380;
    constexpr float    NOISE_MULTIPLIER            = 1.8f;
    constexpr uint32_t NOISE_OFFSET_RMS            = 150;
}
