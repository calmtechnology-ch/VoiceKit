#include "ClockUI.h"

alignas(4) uint8_t ClockUI::draw_buf[ClockUI::BUFFER_PIXELS * sizeof(uint16_t)];

ClockUI::ClockUI()
    : disp(nullptr),
      time_label(nullptr),
      voice_indicator(nullptr),
      current_orientation(DeviceOrientation::LANDSCAPE_NORMAL),
      initialized(false) {
}

void ClockUI::flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    M5.Display.startWrite();
    M5.Display.pushImageDMA(area->x1, area->y1, w, h, (const uint16_t*)px_map);
    M5.Display.waitDMA();
    M5.Display.endWrite();

    lv_display_flush_ready(disp);
}

static uint32_t custom_tick_get(void) {
    return (uint32_t)millis();
}

bool ClockUI::init(DeviceOrientation initial_orientation) {
    lv_init();
    lv_tick_set_cb(custom_tick_get);

    current_orientation = initial_orientation;

    M5.Display.waitDMA();
    M5.Display.waitDisplay();

    uint8_t m5_rot = getM5Rotation(initial_orientation);
    M5.Display.setRotation(m5_rot);
    M5.Display.setBrightness(220);
    M5.Display.fillScreen(0x0000);

    int32_t w = M5.Display.width();
    int32_t h = M5.Display.height();

    disp = lv_display_create(w, h);
    if (!disp) {
        Serial.println("[ClockUI] ERROR: Failed to create LVGL display");
        initialized = false;
        return false;
    }

    lv_display_set_buffers(disp, draw_buf, nullptr, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, flushCallback);

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Heure principale centrée
    time_label = lv_label_create(scr);
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(time_label, "--:--");
    
    // Indicateur vocal discret : petit point de 6x6 px dans le coin supérieur droit
    voice_indicator = lv_obj_create(scr);
    lv_obj_set_size(voice_indicator, 8, 8);
    lv_obj_set_style_radius(voice_indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(voice_indicator, 0, 0);
    lv_obj_align(voice_indicator, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_flag(voice_indicator, LV_OBJ_FLAG_HIDDEN);

    adaptFontSize();
    lv_obj_center(time_label);

    initialized = true;
    Serial.printf("[ClockUI] LVGL 9 display initialized (%dx%d, orientation: %s, m5_rot: %d).\n",
                  w, h, orientationToString(initial_orientation), m5_rot);
    return true;
}

void ClockUI::setVoiceIndicator(VoiceState state) {
    if (!initialized || !voice_indicator) return;

    switch (state) {
        case VoiceState::READY:
            // Pret mais inactif : indicateur masque pour epurer le design
            lv_obj_add_flag(voice_indicator, LV_OBJ_FLAG_HIDDEN);
            break;

        case VoiceState::GENERATING:
            // En attente de generation : petit point bleu discret
            lv_obj_clear_flag(voice_indicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(voice_indicator, lv_color_hex(0x4285F4), 0);
            break;

        case VoiceState::SPEAKING:
            // Parole active : petit point vert/blanc lumineux
            lv_obj_clear_flag(voice_indicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(voice_indicator, lv_color_hex(0x34A853), 0);
            break;

        case VoiceState::ERROR_STATE:
            // Erreur : petit point rouge
            lv_obj_clear_flag(voice_indicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(voice_indicator, lv_color_hex(0xEA4335), 0);
            break;

        default:
            lv_obj_add_flag(voice_indicator, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}

void ClockUI::adaptFontSize() {
    if (!time_label || !disp) return;

    int32_t screen_w = lv_display_get_horizontal_resolution(disp);
    if (screen_w < 200) {
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_36, 0);
    } else {
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);
    }
}

void ClockUI::setOrientation(DeviceOrientation orient) {
    if (!initialized || !disp) return;
    if (orient == current_orientation) return;
    current_orientation = orient;

    M5.Display.waitDMA();
    M5.Display.waitDisplay();

    uint8_t m5_rot = getM5Rotation(orient);
    M5.Display.setRotation(m5_rot);

    int32_t w = M5.Display.width();
    int32_t h = M5.Display.height();

    lv_display_set_resolution(disp, w, h);
    adaptFontSize();
    if (time_label) {
        lv_obj_center(time_label);
        lv_obj_invalidate(time_label);
    }
    if (voice_indicator) {
        lv_obj_align(voice_indicator, LV_ALIGN_TOP_RIGHT, -10, 10);
    }

    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    M5.Display.waitDMA();
}

void ClockUI::setTime(const char* time_str) {
    if (!initialized || !time_label || !time_str) return;
    lv_label_set_text(time_label, time_str);
    lv_obj_center(time_label);
}

void ClockUI::update() {
    if (initialized) {
        lv_timer_handler();
    }
}

int32_t ClockUI::getDisplayWidth() const {
    if (disp) return lv_display_get_horizontal_resolution(disp);
    return M5.Display.width();
}

int32_t ClockUI::getDisplayHeight() const {
    if (disp) return lv_display_get_vertical_resolution(disp);
    return M5.Display.height();
}

uint8_t ClockUI::getM5RotationIndex() const {
    return M5.Display.getRotation();
}
