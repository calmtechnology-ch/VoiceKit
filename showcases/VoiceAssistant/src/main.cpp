/**
 * @file main.cpp
 * @brief Application VoiceAssistant v0.1.0 — Showcase VoiceKit pour M5Stack CoreS3.
 *
 * Assistant vocal conversationnel fluide avec interface tactile LVGL 9 (320x240),
 * Push-to-Talk (PTT) à deux clics, validation locale de la parole (VAD),
 * arrêt prioritaire STOP, support Google Gemini Live et OpenAI Realtime.
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <lvgl.h>

#include "app_config.h"
#include "lv_conf.h"
#include "FrenchFonts.h"
#include "AppController.h"
#include "VoiceAssistantUI.h"

// Buffer de rendu DMA pour LVGL 9 (320x20 pixels ~12.8 Ko)
alignas(4) static uint8_t s_drawBuf[AppConfig::BUFFER_PIXELS * sizeof(uint16_t)];

// Instances globales applicatives
static AppController s_controller;
static VoiceAssistantUI s_ui;

// =============================================================================
// Pilotes Affichage & Tactile LVGL 9 pour M5Stack CoreS3
// =============================================================================

static void disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    M5.Display.startWrite();
    M5.Display.pushImage(area->x1, area->y1, w, h, (const uint16_t*)px_map);
    M5.Display.endWrite();
    lv_display_flush_ready(disp);
}

static void touch_read(lv_indev_t* indev, lv_indev_data_t* data) {
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

static uint32_t lvgl_tick_cb() {
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
    Serial.println("  VoiceAssistant v0.1.0 — Application Showcase VoiceKit ");
    Serial.println("  M5Stack CoreS3 (ESP32-S3) | VoiceKit v0.6.7.1         ");
    Serial.println("  LVGL 9 (320x240) | PTT 2 Clics | VAD Local | STOP     ");
    Serial.println("========================================================");

    // Initialisation et rétroéclairage de l'écran CoreS3
    M5.Display.waitDisplay();
    M5.Display.setBrightness(200);
    M5.Display.fillScreen(TFT_BLACK);

    // 1. Initialisation de LVGL 9
    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);

    lv_display_t* disp = lv_display_create(AppConfig::SCREEN_WIDTH, AppConfig::SCREEN_HEIGHT);
    lv_display_set_buffers(disp, s_drawBuf, nullptr, sizeof(s_drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, disp_flush);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);

    // 2. Construction de l'interface graphique
    s_ui.init(s_controller);

    // 3. Démarrage du contrôleur applicatif VoiceKit
    if (!s_controller.begin()) {
        Serial.println("[ERROR] Échec de démarrage du contrôleur applicatif !");
    }

    Serial.println("[APP] VoiceAssistant prêt pour l'interaction.");
}

// =============================================================================
// Boucle Principale non bloquante (loop)
// =============================================================================

void loop() {
    M5.update();

    uint32_t nowMs = millis();

    // 1. Mise à jour cyclique obligatoire du moteur vocal et de l'interaction
    s_controller.update(nowMs);

    // 2. Mise à jour de l'interface graphique dans le thread principal
    s_ui.update();

    // 3. Gestionnaire de timers et tâches LVGL 9
    lv_timer_handler();

    delay(5);
}
