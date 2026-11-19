#pragma once

#include <stdint.h>
#include "SensorData.h"
#include "EnvSensor.h"

/**
 * @file SensorManager.h
 * @brief Gestionnaire d'acquisition périodique non bloquante pour le capteur environnemental.
 *
 * Responsabilités :
 * - Initialisation du BME688 et sondage contrôlé.
 * - Acquisition périodique non bloquante (beginReading -> endReading).
 * - Validation physique des échantillons et conservation du dernier instantané cohérent.
 * - Détection de péremption (STALE), d'erreur I2C (ERROR) et de déconnexion (DISCONNECTED).
 * - Reconnexion automatique contrôlée sans bloquer la boucle principale.
 */

class SensorManager {
public:
    explicit SensorManager(IEnvSensor& sensor);
    ~SensorManager() = default;

    bool begin(uint32_t nowMs);
    void update(uint32_t nowMs, uint32_t readStartUs = 0, uint32_t (*microsFn)() = nullptr);

    // Instantané cohérent (copie atomique logique)
    SensorData getSnapshot() const { return m_lastValid; }
    SensorState getState() const { return m_state; }
    const SensorDiagnostics& getDiagnostics() const { return m_diag; }

    uint32_t getLastSampleAgeMs(uint32_t nowMs) const {
        return computeSampleAgeMs(m_lastValid, nowMs);
    }

    bool hasValidData() const { return m_lastValid.valid; }
    bool isDataFresh(uint32_t nowMs) const {
        return isSampleFresh(m_lastValid, nowMs, m_staleThresholdMs);
    }
    bool isOperational() const {
        return (m_state == SensorState::READY || m_state == SensorState::READING);
    }

    // Configuration ajustable (utile pour tests)
    void setPollIntervalMs(uint32_t ms) { m_pollIntervalMs = ms; }
    void setStaleThresholdMs(uint32_t ms) { m_staleThresholdMs = ms; }
    void setReconnectIntervalMs(uint32_t ms) { m_reconnectIntervalMs = ms; }

    uint32_t getPollIntervalMs() const { return m_pollIntervalMs; }
    uint32_t getStaleThresholdMs() const { return m_staleThresholdMs; }

private:
    void setState(SensorState newState);
    void handleFailure(uint32_t nowMs, bool isI2cError);
    void updateStaleStatus(uint32_t nowMs);

    IEnvSensor& m_sensor;
    SensorState m_state = SensorState::INITIALIZING;
    SensorData m_lastValid;
    SensorDiagnostics m_diag;

    uint32_t m_pollIntervalMs = AppConfig::SENSOR_POLL_INTERVAL_MS;
    uint32_t m_staleThresholdMs = AppConfig::SENSOR_STALE_THRESHOLD_MS;
    uint32_t m_reconnectIntervalMs = AppConfig::SENSOR_RECONNECT_INTERVAL_MS;

    uint32_t m_lastPollStartMs = 0;
    uint32_t m_readReadyAtMs = 0;
    uint32_t m_readStartedAtMs = 0;
    uint32_t m_lastReconnectTryMs = 0;
    bool m_readingInProgress = false;
    bool m_hasPolledOnce = false;
};
