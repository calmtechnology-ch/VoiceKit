/**
 * @file main.cpp
 * @brief VoiceSensorStation v0.1.0 — Station environnementale vocale (Showcase VoiceKit).
 *
 * Matériel : M5Stack CoreS3 (ESP32-S3, 16 Mo Flash, 8 Mo PSRAM, écran 320x240)
 *            + capteur M5Stack ENV Pro U169 (Bosch BME688 sur bus I2C externe Port A).
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <lvgl.h>

#include "AppConfig.h"
#include "lv_conf.h"
#include "FrenchFonts.h"
#include "AppController.h"

// Buffer de rendu partiel LVGL 9 (320 x 20 pixels ~12.8 Ko)
alignas(4) static uint8_t s_drawBuf[AppConfig::BUFFER_PIXELS * sizeof(uint16_t)];

static AppController s_app;

// =============================================================================
// Pilotes Affichage & Tactile LVGL 9 pour M5Stack CoreS3
// =============================================================================

static void dispFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
    uint32_t h = static_cast<uint32_t>(area->y2 - area->y1 + 1);
    M5.Display.startWrite();
    M5.Display.pushImage(area->x1, area->y1, w, h, reinterpret_cast<const uint16_t*>(px_map));
    M5.Display.endWrite();
    lv_display_flush_ready(disp);
}

static void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    if (M5.Touch.getCount() > 0) {
        auto detail = M5.Touch.getDetail(0);
        if (detail.isPressed()) {
            data->state = LV_INDEV_STATE_PRESSED;
            data->point.x = detail.x;
            data->point.y = detail.y;
            return;
        }
    }
    data->state = LV_INDEV_STATE_RELEASED;
}

static uint32_t lvglTickCb() {
    return static_cast<uint32_t>(millis());
}

// =============================================================================
// Initialisation (setup)
// =============================================================================

void setup() {
    auto m5cfg = M5.config();
    M5.begin(m5cfg);

    Serial.begin(115200);
    delay(200);

    Serial.println("\n========================================================");
    Serial.println("  VoiceSensorStation v0.1.0 — Station environnementale  ");
    Serial.println("  M5Stack CoreS3 + ENV Pro U169 (BME688) | VoiceKit     ");
    Serial.println("========================================================");

    M5.Display.waitDisplay();
    M5.Display.setBrightness(200);
    M5.Display.setSwapBytes(true);
    M5.Display.fillScreen(TFT_WHITE);

    // 1. Initialisation LVGL 9
    lv_init();
    lv_tick_set_cb(lvglTickCb);

    lv_display_t* disp = lv_display_create(AppConfig::SCREEN_WIDTH, AppConfig::SCREEN_HEIGHT);
    lv_display_set_buffers(disp, s_drawBuf, nullptr, sizeof(s_drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, dispFlushCb);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchReadCb);

    // 2. Initialisation de l'application (capteur, UI, vocal)
    s_app.begin();
}

// =============================================================================
// Boucle Principale non bloquante (loop)
// =============================================================================

void loop() {
    M5.update();
    s_app.update(millis());
    delay(5);
}
