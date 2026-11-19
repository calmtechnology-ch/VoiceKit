#include "SensorManager.h"

SensorManager::SensorManager(IEnvSensor& sensor)
    : m_sensor(sensor) {
}

void SensorManager::setState(SensorState newState) {
    m_state = newState;
}

bool SensorManager::begin(uint32_t nowMs) {
    setState(SensorState::INITIALIZING);
    m_readingInProgress = false;
    m_hasPolledOnce = false;
    m_lastPollStartMs = nowMs;
    m_lastReconnectTryMs = nowMs;

    if (m_sensor.begin()) {
        m_diag.i2cAddress = m_sensor.getI2cAddress();
        m_diag.consecutiveErrors = 0;
        setState(SensorState::READY);
        // Lancer immédiatement la première lecture asynchrone
        uint32_t readyAt = 0;
        if (m_sensor.beginReading(nowMs, readyAt)) {
            m_readingInProgress = true;
            m_readStartedAtMs = nowMs;
            m_readReadyAtMs = readyAt;
            m_lastPollStartMs = nowMs;
            m_hasPolledOnce = true;
            setState(SensorState::READING);
        }
        return true;
    }

    m_diag.i2cErrorCount++;
    m_diag.consecutiveErrors = AppConfig::SENSOR_DISCONNECT_ERR_COUNT;
    setState(SensorState::DISCONNECTED);
    return false;
}

void SensorManager::handleFailure(uint32_t nowMs, bool isI2cError) {
    m_readingInProgress = false;
    m_diag.readErrorCount++;
    if (isI2cError) {
        m_diag.i2cErrorCount++;
    }
    if (m_diag.consecutiveErrors < 255) {
        m_diag.consecutiveErrors++;
    }

    if (m_diag.consecutiveErrors >= AppConfig::SENSOR_DISCONNECT_ERR_COUNT || !m_sensor.isConnected()) {
        m_lastReconnectTryMs = nowMs;
        setState(SensorState::DISCONNECTED);
    } else {
        setState(SensorState::ERROR);
    }
}

void SensorManager::updateStaleStatus(uint32_t nowMs) {
    if (m_state == SensorState::READY && m_lastValid.valid) {
        if (!isSampleFresh(m_lastValid, nowMs, m_staleThresholdMs)) {
            setState(SensorState::STALE);
        }
    }
}

void SensorManager::update(uint32_t nowMs, uint32_t readStartUs, uint32_t (*microsFn)()) {
    // 1. Reconnexion contrôlée si capteur déconnecté ou en erreur persistante
    if (m_state == SensorState::DISCONNECTED || m_state == SensorState::INITIALIZING) {
        if (static_cast<uint32_t>(nowMs - m_lastReconnectTryMs) >= m_reconnectIntervalMs) {
            m_lastReconnectTryMs = nowMs;
            m_diag.reconnectAttempts++;
            if (m_sensor.begin()) {
                m_diag.i2cAddress = m_sensor.getI2cAddress();
                m_diag.consecutiveErrors = 0;
                m_diag.reconnectSuccessCount++;
                m_hasPolledOnce = false;
                setState(SensorState::READY);
            }
        }
        return;
    }

    // 2. Finalisation d'une lecture asynchrone en cours
    if (m_readingInProgress) {
        // Timeout de sécurité
        if (static_cast<uint32_t>(nowMs - m_readStartedAtMs) >= AppConfig::SENSOR_READ_TIMEOUT_MS) {
            handleFailure(nowMs, true);
            return;
        }

        // Attendre sans bloquer que la mesure BME688 soit prête
        if (static_cast<int32_t>(nowMs - m_readReadyAtMs) < 0) {
            return;
        }

        uint32_t t0 = microsFn ? microsFn() : readStartUs;
        SensorData candidate;
        bool ok = m_sensor.endReading(candidate);
        if (microsFn) {
            m_diag.lastReadDurationUs = static_cast<uint32_t>(microsFn() - t0);
        }

        m_readingInProgress = false;

        if (ok && validateSensorSample(candidate)) {
            candidate.timestampMs = nowMs;
            candidate.valid = true;
            candidate.gasValid = isGasReadingValid(candidate.gasResistanceOhms);
            // Remplacement atomique logique de l'instantané complet
            m_lastValid = candidate;
            m_diag.readSuccessCount++;
            m_diag.consecutiveErrors = 0;
            setState(SensorState::READY);
        } else {
            handleFailure(nowMs, !ok);
        }
        return;
    }

    // 3. Vérification de fraîcheur des données
    updateStaleStatus(nowMs);

    // 4. Déclenchement périodique de la prochaine acquisition non bloquante
    if (!m_hasPolledOnce || static_cast<uint32_t>(nowMs - m_lastPollStartMs) >= m_pollIntervalMs) {
        m_lastPollStartMs = nowMs;
        m_hasPolledOnce = true;

        uint32_t readyAt = 0;
        if (m_sensor.beginReading(nowMs, readyAt)) {
            m_readingInProgress = true;
            m_readStartedAtMs = nowMs;
            m_readReadyAtMs = readyAt;
            setState(SensorState::READING);
        } else {
            handleFailure(nowMs, true);
        }
    }
}
