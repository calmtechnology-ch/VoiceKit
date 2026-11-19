#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @file AppConfig.h
 * @brief Paramètres centralisés de VoiceSensorStation v0.1.0.1 pour M5Stack CoreS3.
 */

namespace AppConfig {
    // Métadonnées applicatives
    constexpr const char* APP_NAME          = "VoiceSensorStation";
    constexpr const char* APP_VERSION       = "v0.1.0.1";
    constexpr const char* VOICEKIT_VERSION  = "0.6.7.1";

    // Écran M5Stack CoreS3 (320x240 paysage)
    constexpr uint32_t SCREEN_WIDTH   = 320;
    constexpr uint32_t SCREEN_HEIGHT  = 240;
    constexpr size_t   BUFFER_PIXELS  = 320 * 20; // 6400 pixels (~12.8 Ko)

    // Bus I2C externe Port A du CoreS3 (M5Stack ENV Pro U169 / BME688)
    // Broches par défaut CoreS3 Port A : SDA = GPIO 2, SCL = GPIO 1
    constexpr int      EXT_I2C_SDA_FALLBACK   = 2;
    constexpr int      EXT_I2C_SCL_FALLBACK   = 1;
    constexpr uint32_t EXT_I2C_FREQ_HZ        = 100000;
    constexpr uint8_t  BME688_ADDR_PRIMARY    = 0x77;
    constexpr uint8_t  BME688_ADDR_SECONDARY  = 0x76;

    // Paramètres d'acquisition environnementale (non bloquante)
    constexpr uint32_t SENSOR_POLL_INTERVAL_MS      = 2000;  // 1 acquisition toutes les 2 s
    constexpr uint32_t SENSOR_STALE_THRESHOLD_MS    = 10000; // Mesure périmée après 10 s
    constexpr uint32_t SENSOR_RECONNECT_INTERVAL_MS = 3000;  // Tentative reconnexion toutes les 3 s
    constexpr uint32_t SENSOR_READ_TIMEOUT_MS       = 1500;  // Timeout lecture asynchrone BME688
    constexpr uint8_t  SENSOR_DISCONNECT_ERR_COUNT  = 3;     // Erreurs consécutives avant DISCONNECTED

    // Bornes physiques de validation des mesures BME688
    constexpr float TEMP_MIN_C       = -40.0f;
    constexpr float TEMP_MAX_C       = 85.0f;
    constexpr float HUMIDITY_MIN_PCT = 0.0f;
    constexpr float HUMIDITY_MAX_PCT = 100.0f;
    constexpr float PRESSURE_MIN_HPA = 300.0f;
    constexpr float PRESSURE_MAX_HPA = 1100.0f;

    // Fournisseurs IA par défaut (VoiceKit v0.6.7.1)
    constexpr const char* GEMINI_MODEL    = "models/gemini-2.5-flash-native-audio-latest";
    constexpr const char* GEMINI_VOICE    = "Aoede";
    constexpr const char* OPENAI_MODEL    = "gpt-realtime-2.1-mini";
    constexpr const char* OPENAI_VOICE    = "alloy";

    // Langue par défaut
    constexpr const char* DEFAULT_LANGUAGE = "fr-FR";

    // Consigne système combinant TTS mot à mot (commandes locales) et réponses IA courtes
    constexpr const char* SYSTEM_INSTRUCTION =
        "Tu es VoiceSensorStation, une station environnementale vocale. "
        "Si le message commence par 'Prononce exactement et uniquement ce texte : ', "
        "ton unique role est de prononcer tres exactement ce texte mot a mot sans aucun ajout ni commentaire. "
        "Sinon, reponds en francais, avec des phrases courtes et precises (une ou deux phrases pour les questions simples). "
        "Utilise uniquement les mesures fournies par les capteurs. N'invente jamais de valeurs. "
        "Si une mesure est indisponible, indique-le. "
        "La resistance de gaz du BME688 ne represente pas directement une concentration de CO2 "
        "ni un indice certifie de qualite de l'air.";

    // Volume audio initial (0-100 %)
    constexpr uint8_t DEFAULT_VOLUME = 75;

    // Paramètres d'interaction PTT 2 clics & VAD local
    constexpr uint32_t DEBOUNCE_MS                 = 120;
    constexpr uint32_t MIC_TIMEOUT_MS              = 15000;
    constexpr uint32_t SERVER_TIMEOUT_MS           = 15000;
    constexpr uint32_t MIN_CAPTURE_DURATION_MS     = 350;
    constexpr uint32_t MIN_SPEECH_DURATION_MS      = 250;
    constexpr uint32_t MIN_CONSECUTIVE_SPEECH_MS   = 120;
    constexpr uint32_t SPEECH_THRESHOLD_FLOOR_RMS  = 380;
    constexpr float    NOISE_MULTIPLIER            = 1.8f;
    constexpr uint32_t NOISE_OFFSET_RMS            = 150;

    // Période du rapport de diagnostics série (ms)
    constexpr uint32_t SERIAL_DIAG_INTERVAL_MS     = 5000;
}
