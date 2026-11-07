#pragma once

#include <Arduino.h>

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #include "secrets.example.h"
#endif

#ifndef WIFI_SSID
    #define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
    #define WIFI_PASSWORD ""
#endif
#ifndef GEMINI_KEY
    #ifdef GEMINI_API_KEY
        #define GEMINI_KEY GEMINI_API_KEY
    #else
        #define GEMINI_KEY ""
    #endif
#endif
#ifndef OPENAI_KEY
    #ifdef OPENAI_API_KEY
        #define OPENAI_KEY OPENAI_API_KEY
    #else
        #define OPENAI_KEY ""
    #endif
#endif

namespace AppConfig {
    // Volume de restitution audio du CoreS3 (0 à 100)
    static constexpr uint8_t DEFAULT_VOLUME = 75;

    // Timeout de réception de transcription finale STT (ms)
    static constexpr uint32_t STT_FINAL_TIMEOUT_MS = 6000;

    // Capacité maximale de l'historique conversationnel ReplyMemory (5 par défaut)
    static constexpr size_t MEMORY_CAPACITY = 5;

    // Sélection du fournisseur vocal : 0 = Gemini Live, 1 = OpenAI Realtime
    static constexpr uint8_t VOICE_PROVIDER = 0;
}

