/**
 * @file main.cpp
 * @brief VoiceControl v0.1.0 — Centre de commandes vocales (Heure et date) pour M5Stack CoreS3.
 *
 * Matériel : M5Stack CoreS3 (ESP32-S3, écran 320x240)
 * Pile     : VoiceKit v0.6.7.1 + LVGL 9.2.2
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <lvgl.h>

#include "lv_conf.h"
#include "FrenchFonts.h"
#include "config/VoiceControlConfig.h"
#include "voice/VoiceController.h"
#include "ui/VoiceControlUI.h"

// Buffer de rendu partiel LVGL 9 (320 x 20 pixels ~12.8 Ko)
alignas(4) static uint8_t s_drawBuf[VC_LVGL_BUFFER_PIXELS * sizeof(uint16_t)];

static VoiceController s_voiceController;
static VoiceControlUI  s_ui;

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

#if VC_DIAG_ENABLED
    Serial.printf("\n[VC][BOOT] %s %s\n", VC_APP_NAME, VC_APP_VERSION);
    Serial.printf("[VC][CONFIG] Language: %s\n", VoiceControlConfig::getSystemLanguageCodeStr());
    Serial.printf("[VC][CONFIG] Provider: %s\n", VoiceControlConfig::getConfiguredProviderUpper());
    Serial.printf("[VC][CONFIG] Model: %s\n", VoiceControlConfig::getConfiguredModel());
#endif

    M5.Display.waitDisplay();
    M5.Display.setBrightness(200);
    M5.Display.setSwapBytes(true);
    M5.Display.fillScreen(TFT_WHITE);

    // 1. Initialisation LVGL 9
    lv_init();
    lv_tick_set_cb(lvglTickCb);

    lv_display_t* disp = lv_display_create(VC_SCREEN_WIDTH, VC_SCREEN_HEIGHT);
    lv_display_set_buffers(disp, s_drawBuf, nullptr, sizeof(s_drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, dispFlushCb);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchReadCb);

    // 2. Interface utilisable immédiatement (avant connexion Wi-Fi / NTP / IA)
    s_ui.init(s_voiceController);
    lv_timer_handler();

    // 3. Démarrage non bloquant du contrôleur vocal et de la synchronisation NTP
    s_voiceController.begin();
}

// =============================================================================
// Boucle principale non bloquante (loop)
// =============================================================================

void loop() {
    M5.update();
    uint32_t nowMs = millis();

    s_voiceController.update(nowMs);
    s_ui.update(nowMs);
    lv_timer_handler();

    delay(5);
}
