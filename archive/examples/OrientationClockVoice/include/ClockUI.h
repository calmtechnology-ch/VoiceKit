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
    void setDate(const char* date_str);
    void setAlarm(const char* alarm_str, bool enabled);
    void setStatus(const char* status_str, uint32_t color = 0x94A3B8);
    void showAlarmEditor(int hour, int minute, bool enabled);
    void updateAlarmEditor(int hour, int minute, bool enabled);
    void hideAlarmEditor();
    bool isAlarmEditorVisible() const { return alarm_editor_visible; }
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
    void layoutAlarmEditor();

    lv_display_t* disp;
    lv_obj_t* time_label;
    lv_obj_t* date_label;
    lv_obj_t* alarm_label;
    lv_obj_t* status_label;
    lv_obj_t* voice_indicator;
    lv_obj_t* alarm_editor;
    lv_obj_t* editor_time_label;
    lv_obj_t* editor_state_label;
    lv_obj_t* editor_buttons[6];
    DeviceOrientation current_orientation;
    bool initialized;
    bool alarm_editor_visible;

    static constexpr uint32_t BUFFER_LINES  = 40;
    static constexpr uint32_t BUFFER_PIXELS = 320 * BUFFER_LINES;
    alignas(4) static uint8_t draw_buf[BUFFER_PIXELS * sizeof(uint16_t)];
};
