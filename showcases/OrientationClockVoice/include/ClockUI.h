#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <lvgl.h>
#include "DeviceOrientation.h"
#include <VoxCoreVoice.h>

/**
 * @class ClockUI
 * @brief Interface d'affichage de l'horloge minimaliste sous LVGL 9,
 *        avec rotation matérielle M5GFX et indicateur vocal discret.
 */
class ClockUI {
public:
    ClockUI();

    bool init(DeviceOrientation initial_orientation = DeviceOrientation::LANDSCAPE_NORMAL);
    void setTime(const char* time_str);
    void setOrientation(DeviceOrientation orient);
    void update();

    /**
     * @brief Met à jour l'indicateur vocal minimaliste (point discret dans le coin supérieur).
     */
    void setVoiceIndicator(VoiceState state);

    DeviceOrientation getOrientation() const { return current_orientation; }
    bool isInitialized() const { return initialized; }
    int32_t getDisplayWidth() const;
    int32_t getDisplayHeight() const;
    uint8_t getM5RotationIndex() const;

private:
    static void flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    void adaptFontSize();

    lv_display_t* disp;
    lv_obj_t* time_label;
    lv_obj_t* voice_indicator;
    DeviceOrientation current_orientation;
    bool initialized;

    static constexpr uint32_t BUFFER_LINES  = 40;
    static constexpr uint32_t BUFFER_PIXELS = 320 * BUFFER_LINES;
    alignas(4) static uint8_t draw_buf[BUFFER_PIXELS * sizeof(uint16_t)];
};
