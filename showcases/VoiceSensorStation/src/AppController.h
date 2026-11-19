#pragma once

#include <Arduino.h>
#include "AppConfig.h"
#include "sensors/EnvSensor.h"
#include "sensors/SensorManager.h"
#include "voice/StationVoiceController.h"
#include "ui/SensorStationUI.h"

/**
 * @file AppController.h
 * @brief Orchestrateur principal de VoiceSensorStation v0.1.0.
 *
 * Coordonne l'initialisation, le cycle principal non bloquant et les diagnostics série
 * des sous-systèmes :
 * - Bme688EnvSensor + SensorManager (bus I2C externe Port A CoreS3)
 * - StationVoiceController (VoiceKit v0.6.7.1 + commandes locales + OpenAI/Gemini)
 * - SensorStationUI (interface tactile LVGL 9)
 */

class AppController {
public:
    AppController();
    ~AppController() = default;

    bool begin();
    void update(uint32_t nowMs);
    void printDiagnostics(uint32_t nowMs) const;

    SensorManager& getSensorManager() { return m_sensorManager; }
    StationVoiceController& getVoiceController() { return m_voiceController; }
    SensorStationUI& getUI() { return m_ui; }

private:
    static uint32_t espMicros();

    Bme688EnvSensor m_envSensor;
    SensorManager m_sensorManager;
    StationVoiceController m_voiceController;
    SensorStationUI m_ui;

    uint32_t m_lastDiagPrintMs = 0;
    uint32_t m_lastLvglHandlerUs = 0;
};
