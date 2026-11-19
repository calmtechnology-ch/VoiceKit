#include "EnvSensor.h"

#ifdef ARDUINO

#include <Arduino.h>

Bme688EnvSensor::Bme688EnvSensor(TwoWire& wire)
    : m_wire(wire), m_bme(&wire) {
}

void Bme688EnvSensor::setPins(int sdaPin, int sclPin, uint32_t freqHz) {
    m_sdaPin = sdaPin;
    m_sclPin = sclPin;
    m_freqHz = freqHz;
}

bool Bme688EnvSensor::probeAddress(uint8_t addr) {
    if (addr == 0) return false;
    m_wire.beginTransmission(addr);
    return (m_wire.endTransmission() == 0);
}

void Bme688EnvSensor::configureOversamplingAndHeater() {
    m_bme.setTemperatureOversampling(BME680_OS_4X);
    m_bme.setHumidityOversampling(BME680_OS_2X);
    m_bme.setPressureOversampling(BME680_OS_4X);
    m_bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    // Chauffe modérée (300 °C pendant 100 ms) pour mesure de résistance de gaz non bloquante
    m_bme.setGasHeater(300, 100);
}

bool Bme688EnvSensor::begin() {
    if (!m_busInitialized) {
        if (!m_wire.begin(m_sdaPin, m_sclPin, m_freqHz)) {
            Serial.printf("[SENSOR][I2C] Échec initialisation bus I2C externe (SDA=%d, SCL=%d)\n",
                          m_sdaPin, m_sclPin);
            return false;
        }
        m_wire.setTimeOut(50);
        m_busInitialized = true;
    }

    m_sensorInitialized = false;
    m_address = 0;

    // Sonder d'abord l'adresse standard ENV Pro U169 (0x77), puis repli sur 0x76
    const uint8_t candidates[2] = {
        AppConfig::BME688_ADDR_PRIMARY,
        AppConfig::BME688_ADDR_SECONDARY
    };

    for (uint8_t addr : candidates) {
        if (probeAddress(addr)) {
            if (m_bme.begin(addr, true)) {
                m_address = addr;
                configureOversamplingAndHeater();
                m_sensorInitialized = true;
                Serial.printf("[SENSOR] BME688 détecté et initialisé sur I2C externe (0x%02X, SDA=%d, SCL=%d)\n",
                              m_address, m_sdaPin, m_sclPin);
                return true;
            }
        }
    }

    Serial.printf("[SENSOR] Aucun BME688 détecté sur Port A (SDA=%d, SCL=%d, adresses 0x77/0x76)\n",
                  m_sdaPin, m_sclPin);
    return false;
}

bool Bme688EnvSensor::isConnected() {
    if (!m_busInitialized || m_address == 0) {
        return false;
    }
    return probeAddress(m_address);
}

bool Bme688EnvSensor::beginReading(uint32_t nowMs, uint32_t& outReadyAtMs) {
    if (!m_sensorInitialized || !isConnected()) {
        return false;
    }
    unsigned long endTime = m_bme.beginReading();
    if (endTime == 0) {
        return false;
    }
    // Adafruit_BME680::beginReading() retourne un timestamp millis() cible
    outReadyAtMs = static_cast<uint32_t>(endTime);
    if (static_cast<int32_t>(outReadyAtMs - nowMs) < 0) {
        outReadyAtMs = nowMs;
    }
    return true;
}

bool Bme688EnvSensor::endReading(SensorData& outSample) {
    if (!m_sensorInitialized || !isConnected()) {
        return false;
    }
    if (!m_bme.endReading()) {
        return false;
    }

    outSample.temperatureC = m_bme.temperature;
    outSample.humidityPercent = m_bme.humidity;
    outSample.pressureHpa = m_bme.pressure / 100.0f;
    outSample.gasResistanceOhms = static_cast<float>(m_bme.gas_resistance);
    outSample.gasValid = isGasReadingValid(outSample.gasResistanceOhms);
    outSample.valid = validateSensorSample(outSample);
    return outSample.valid;
}

#endif // ARDUINO
