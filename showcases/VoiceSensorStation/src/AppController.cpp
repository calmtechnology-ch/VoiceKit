#include "AppController.h"
#include <M5Unified.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

AppController::AppController()
    : m_envSensor(Wire),
      m_sensorManager(m_envSensor) {
}

uint32_t AppController::espMicros() {
    return static_cast<uint32_t>(micros());
}

bool AppController::begin() {
    Serial.printf("[APP] Démarrage %s %s (VoiceKit %s)\n",
                  AppConfig::APP_NAME, AppConfig::APP_VERSION, AppConfig::VOICEKIT_VERSION);

    // 1. Détection des broches I2C externes Port A du CoreS3 via M5Unified (SDA=2, SCL=1)
    int sdaPin = M5.getPin(m5::pin_name_t::port_a_sda);
    int sclPin = M5.getPin(m5::pin_name_t::port_a_scl);
    if (sdaPin < 0) sdaPin = AppConfig::EXT_I2C_SDA_FALLBACK;
    if (sclPin < 0) sclPin = AppConfig::EXT_I2C_SCL_FALLBACK;
    m_envSensor.setPins(sdaPin, sclPin, AppConfig::EXT_I2C_FREQ_HZ);

    // 2. Initialisation du capteur environnemental (indépendant du Wi-Fi et de l'IA)
    uint32_t nowMs = millis();
    if (!m_sensorManager.begin(nowMs)) {
        Serial.println("[APP][WARN] Capteur BME688 absent au démarrage -> reconnexion périodique active.");
    }

    // 3. Initialisation de l'interface LVGL 9 (thème clair)
    m_ui.init(m_sensorManager, m_voiceController);

    // 4. Initialisation du contrôleur vocal VoiceKit (non bloquant)
    if (!m_voiceController.begin(m_sensorManager)) {
        Serial.println("[APP][WARN] Service vocal non démarré -> mode station locale actif.");
    }

    m_lastDiagPrintMs = millis();
    return true;
}

void AppController::update(uint32_t nowMs) {
    // 1. Acquisition environnementale non bloquante (toujours active, même hors ligne)
    m_sensorManager.update(nowMs, 0, &AppController::espMicros);

    // 2. Moteur vocal VoiceKit, PTT 2 clics et commandes locales
    m_voiceController.update(nowMs);

    // 3. Mise à jour différentielle de l'interface LVGL 9 (thread principal uniquement)
    m_ui.update(nowMs);

    uint32_t tLvgl = micros();
    lv_timer_handler();
    m_lastLvglHandlerUs = static_cast<uint32_t>(micros() - tLvgl);

    // 4. Rapport de diagnostics périodique sur port série
    if (nowMs - m_lastDiagPrintMs >= AppConfig::SERIAL_DIAG_INTERVAL_MS) {
        m_lastDiagPrintMs = nowMs;
        printDiagnostics(nowMs);
    }
}

void AppController::printDiagnostics(uint32_t nowMs) const {
    const SensorData snap = m_sensorManager.getSnapshot();
    const SensorState sState = m_sensorManager.getState();
    const SensorDiagnostics& sd = m_sensorManager.getDiagnostics();
    const VoiceDiagnostics& vd = m_voiceController.getDiagnostics();

    Serial.println("\n========================================");
    Serial.printf("%s %s\n", AppConfig::APP_NAME, AppConfig::APP_VERSION);
    Serial.println("========================================\n");

    Serial.println("[SENSOR]");
    Serial.printf("State           : %s\n", sensorStateToString(sState));
    Serial.printf("I2C address     : 0x%02X\n", sd.i2cAddress);
    if (snap.valid) {
        Serial.printf("Temperature     : %.1f C\n", snap.temperatureC);
        Serial.printf("Humidity        : %.1f %%\n", snap.humidityPercent);
        Serial.printf("Pressure        : %.1f hPa\n", snap.pressureHpa);
        if (snap.gasValid) {
            Serial.printf("Gas resistance  : %.1f kOhm\n", snap.gasResistanceOhms / 1000.0f);
        } else {
            Serial.println("Gas resistance  : N/A");
        }
        Serial.printf("Last sample age : %lu ms\n", (unsigned long)m_sensorManager.getLastSampleAgeMs(nowMs));
    } else {
        Serial.println("Measurements    : N/A (no valid sample)");
    }
    Serial.printf("Reads / Errors  : %lu ok | %lu err | %lu I2C err | %lu reconnects\n\n",
                  (unsigned long)sd.readSuccessCount,
                  (unsigned long)sd.readErrorCount,
                  (unsigned long)sd.i2cErrorCount,
                  (unsigned long)sd.reconnectSuccessCount);

    Serial.println("[VOICE]");
    Serial.printf("Provider        : %s\n", m_voiceController.getProviderName());
    Serial.printf("State           : %s\n", voiceStateToString(m_voiceController.getVoiceState()));
    Serial.printf("Session         : %s\n", liveSessionStateToString(m_voiceController.getSessionState()));
    Serial.printf("PTT             : %s\n", m_voiceController.getInteractionStateString());
    Serial.printf("Counters        : %lu local | %lu AI | %lu silence | %lu STOP | %lu err\n\n",
                  (unsigned long)vd.localCommandCount,
                  (unsigned long)vd.aiQueryCount,
                  (unsigned long)vd.silenceRejectCount,
                  (unsigned long)vd.stopInterruptCount,
                  (unsigned long)vd.voiceErrorCount);

    Serial.println("[SYSTEM]");
    Serial.printf("WiFi            : %s\n", m_voiceController.isWifiConnected() ? "CONNECTED" : "DISCONNECTED");
    Serial.printf("Free heap       : %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    Serial.printf("Free PSRAM      : %lu bytes\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    Serial.printf("Uptime          : %lu s\n\n", (unsigned long)(nowMs / 1000));

    Serial.println("[PERFORMANCE]");
    Serial.printf("Sensor read     : %.2f ms\n", sd.lastReadDurationUs / 1000.0f);
    Serial.printf("LVGL update     : %.2f ms (UI %.2f ms + flush %.2f ms)\n",
                  (m_ui.getLastUpdateDurationUs() + m_lastLvglHandlerUs) / 1000.0f,
                  m_ui.getLastUpdateDurationUs() / 1000.0f,
                  m_lastLvglHandlerUs / 1000.0f);
    Serial.printf("Voice update    : %.2f ms\n", vd.lastUpdateDurationUs / 1000.0f);
    Serial.println("========================================");
}
