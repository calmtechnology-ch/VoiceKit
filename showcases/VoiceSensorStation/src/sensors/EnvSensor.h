#pragma once

#include <stdint.h>
#include "SensorData.h"

/**
 * @file EnvSensor.h
 * @brief Abstraction capteur environnemental non bloquant et pilote BME688 (M5Stack ENV Pro U169).
 */

class IEnvSensor {
public:
    virtual ~IEnvSensor() = default;

    /**
     * @brief Initialise le bus / capteur. Retourne true si détecté et configuré.
     */
    virtual bool begin() = 0;

    /**
     * @brief Vérifie la présence sur le bus I2C (ping léger sans réinitialisation).
     */
    virtual bool isConnected() = 0;

    /**
     * @brief Démarre une acquisition asynchrone (mode forcé BME688).
     * @param nowMs Horodatage courant en ms.
     * @param outReadyAtMs Reçoit l'horodatage estimé auquel endReading() peut être appelé.
     * @return true si l'acquisition a été lancée, false en cas d'erreur I2C.
     */
    virtual bool beginReading(uint32_t nowMs, uint32_t& outReadyAtMs) = 0;

    /**
     * @brief Lit les registres du capteur une fois la mesure achevée.
     * @param outSample Instantané rempli en cas de succès.
     * @return true si la lecture I2C a réussi.
     */
    virtual bool endReading(SensorData& outSample) = 0;

    /**
     * @brief Retourne l'adresse I2C active (0 si non détecté).
     */
    virtual uint8_t getI2cAddress() const = 0;
};

#ifdef ARDUINO

#include <Wire.h>
#include <Adafruit_BME680.h>

class Bme688EnvSensor : public IEnvSensor {
public:
    explicit Bme688EnvSensor(TwoWire& wire = Wire);
    ~Bme688EnvSensor() override = default;

    void setPins(int sdaPin, int sclPin, uint32_t freqHz = AppConfig::EXT_I2C_FREQ_HZ);

    bool begin() override;
    bool isConnected() override;
    bool beginReading(uint32_t nowMs, uint32_t& outReadyAtMs) override;
    bool endReading(SensorData& outSample) override;
    uint8_t getI2cAddress() const override { return m_address; }

private:
    bool probeAddress(uint8_t addr);
    void configureOversamplingAndHeater();

    TwoWire& m_wire;
    Adafruit_BME680 m_bme;
    int m_sdaPin = AppConfig::EXT_I2C_SDA_FALLBACK;
    int m_sclPin = AppConfig::EXT_I2C_SCL_FALLBACK;
    uint32_t m_freqHz = AppConfig::EXT_I2C_FREQ_HZ;
    uint8_t m_address = 0;
    bool m_busInitialized = false;
    bool m_sensorInitialized = false;
};

#endif // ARDUINO
