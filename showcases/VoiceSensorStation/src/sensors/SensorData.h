#pragma once

#include <stdint.h>
#include <cmath>
#include "AppConfig.h"

/**
 * @file SensorData.h
 * @brief Structures de données, états et validation pour le capteur BME688 (ENV Pro U169).
 *        Conçu en C++17 pur (indépendant d'Arduino) pour permettre les tests unitaires natifs.
 */

enum class SensorState : uint8_t {
    INITIALIZING = 0,
    READY,
    READING,
    STALE,
    ERROR,
    DISCONNECTED
};

inline const char* sensorStateToString(SensorState state) {
    switch (state) {
        case SensorState::INITIALIZING: return "INITIALIZING";
        case SensorState::READY:        return "READY";
        case SensorState::READING:      return "READING";
        case SensorState::STALE:        return "STALE";
        case SensorState::ERROR:        return "ERROR";
        case SensorState::DISCONNECTED: return "DISCONNECTED";
        default:                        return "UNKNOWN";
    }
}

inline const char* sensorStateToFrenchLabel(SensorState state) {
    switch (state) {
        case SensorState::INITIALIZING: return "Initialisation capteur...";
        case SensorState::READY:        return "Capteur OK";
        case SensorState::READING:      return "Lecture capteur...";
        case SensorState::STALE:        return "Mesures périmées";
        case SensorState::ERROR:        return "Erreur capteur";
        case SensorState::DISCONNECTED: return "Capteur indisponible";
        default:                        return "État inconnu";
    }
}

/**
 * @struct SensorData
 * @brief Instantané cohérent et horodaté des mesures du BME688.
 *
 * NOTE IMPORTANTE : gasResistanceOhms représente la résistance électrique brute
 * de la couche sensible aux gaz du BME688 (en ohms). Elle ne constitue pas une
 * concentration directe de CO2, de COV en ppm, ni un indice certifié IAQ.
 */
struct SensorData {
    float temperatureC = 0.0f;
    float humidityPercent = 0.0f;
    float pressureHpa = 0.0f;
    float gasResistanceOhms = 0.0f;

    uint32_t timestampMs = 0;
    bool valid = false;
    bool gasValid = false;
};

struct SensorDiagnostics {
    uint32_t readSuccessCount = 0;
    uint32_t readErrorCount = 0;
    uint32_t i2cErrorCount = 0;
    uint32_t reconnectAttempts = 0;
    uint32_t reconnectSuccessCount = 0;
    uint8_t  consecutiveErrors = 0;
    uint32_t lastReadDurationUs = 0;
    uint8_t  i2cAddress = 0;
};

inline bool isFiniteFloat(float v) {
    return !std::isnan(v) && !std::isinf(v);
}

/**
 * @brief Vérifie que les mesures de température, humidité et pression sont
 *        numériquement finies et dans les bornes physiques du BME688.
 */
inline bool validateSensorSample(const SensorData& d) {
    if (!isFiniteFloat(d.temperatureC) ||
        d.temperatureC < AppConfig::TEMP_MIN_C ||
        d.temperatureC > AppConfig::TEMP_MAX_C) {
        return false;
    }
    if (!isFiniteFloat(d.humidityPercent) ||
        d.humidityPercent < AppConfig::HUMIDITY_MIN_PCT ||
        d.humidityPercent > AppConfig::HUMIDITY_MAX_PCT) {
        return false;
    }
    if (!isFiniteFloat(d.pressureHpa) ||
        d.pressureHpa < AppConfig::PRESSURE_MIN_HPA ||
        d.pressureHpa > AppConfig::PRESSURE_MAX_HPA) {
        return false;
    }
    return true;
}

inline bool isGasReadingValid(float gasResistanceOhms) {
    return isFiniteFloat(gasResistanceOhms) && gasResistanceOhms > 0.0f;
}

/**
 * @brief Retourne l'âge en millisecondes d'un instantané valide (UINT32_MAX si invalide).
 */
inline uint32_t computeSampleAgeMs(const SensorData& d, uint32_t nowMs) {
    if (!d.valid) {
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(nowMs - d.timestampMs);
}

/**
 * @brief Indique si un instantané est valide et plus récent que maxAgeMs.
 */
inline bool isSampleFresh(const SensorData& d, uint32_t nowMs,
                          uint32_t maxAgeMs = AppConfig::SENSOR_STALE_THRESHOLD_MS) {
    if (!d.valid) {
        return false;
    }
    return computeSampleAgeMs(d, nowMs) <= maxAgeMs;
}
