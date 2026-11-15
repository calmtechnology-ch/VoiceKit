#include "ClockUI.h"

alignas(4) uint8_t ClockUI::draw_buf[ClockUI::BUFFER_PIXELS * sizeof(uint16_t)];

ClockUI::ClockUI()
    : disp(nullptr),
      time_label(nullptr),
      date_label(nullptr),
      alarm_label(nullptr),
      status_label(nullptr),
      voice_indicator(nullptr),
      alarm_editor(nullptr),
      editor_time_label(nullptr),
      editor_state_label(nullptr),
      editor_buttons{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
      current_orientation(DeviceOrientation::LANDSCAPE_NORMAL),
      initialized(false),
      alarm_editor_visible(false) {
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

    // Heure principale centree
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
    lv_obj_set_hidden(voice_indicator, true);

    adaptFontSize();
    date_label = lv_label_create(scr);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(date_label, "Synchronisation...");

    alarm_label = lv_label_create(scr);
    lv_obj_set_style_text_font(alarm_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(alarm_label, "Reveil off 07:00");

    status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x94A3B8), 0);
    lv_label_set_text(status_label, "Touchez pour entendre l'heure");

    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -10);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_align(alarm_label, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Ecran modal V2.1 de reglage du reveil. Les zones tactiles sont traitees
    // dans main.cpp afin de rester independantes du pilote d'entree LVGL.
    alarm_editor = lv_obj_create(scr);
    lv_obj_set_style_bg_color(alarm_editor, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(alarm_editor, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(alarm_editor, 0, 0);
    lv_obj_set_style_radius(alarm_editor, 0, 0);
    lv_obj_set_style_pad_all(alarm_editor, 0, 0);

    lv_obj_t* editor_title = lv_label_create(alarm_editor);
    lv_obj_set_style_text_color(editor_title, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_text_font(editor_title, &lv_font_montserrat_14, 0);
    lv_label_set_text(editor_title, "REGLAGE DU REVEIL");
    lv_obj_align(editor_title, LV_ALIGN_TOP_MID, 0, 10);

    editor_time_label = lv_label_create(alarm_editor);
    lv_obj_set_style_text_color(editor_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(editor_time_label, &lv_font_montserrat_48, 0);
    lv_label_set_text(editor_time_label, "07:00");

    editor_state_label = lv_label_create(alarm_editor);
    lv_obj_set_style_text_font(editor_state_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(editor_state_label, "DESACTIVE");

    static const char* BUTTON_TEXTS[6] = {"-H", "+H", "-M", "+M", "ON", "OK"};
    for (int i = 0; i < 6; ++i) {
        editor_buttons[i] = lv_obj_create(alarm_editor);
        lv_obj_set_style_bg_color(editor_buttons[i], lv_color_hex(i == 5 ? 0x2563EB : 0x1E293B), 0);
        lv_obj_set_style_border_width(editor_buttons[i], 1, 0);
        lv_obj_set_style_border_color(editor_buttons[i], lv_color_hex(0x475569), 0);
        lv_obj_set_style_radius(editor_buttons[i], 6, 0);
        lv_obj_t* label = lv_label_create(editor_buttons[i]);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_label_set_text(label, BUTTON_TEXTS[i]);
        lv_obj_center(label);
    }
    layoutAlarmEditor();
    lv_obj_set_hidden(alarm_editor, true);

    initialized = true;
    Serial.printf("[ClockUI] LVGL 9 display initialized (%dx%d, orientation: %s, m5_rot: %d).\n",
                  w, h, orientationToString(initial_orientation), m5_rot);
    return true;
}

void ClockUI::setVoiceIndicator(VoiceState state) {
    if (!initialized || !voice_indicator) return;

    switch (state) {
        case VoiceState::READY:
        case VoiceState::IDLE:
            // Pret mais inactif : indicateur masque pour epurer le design
            lv_obj_set_hidden(voice_indicator, true);
            setStatus("Pret - touchez pour l'heure", 0x94A3B8);
            break;

        case VoiceState::CONNECTING_WIFI:
            // Reveil et connexion Wi-Fi : petit point orange / ambre
            lv_obj_set_hidden(voice_indicator, false);
            lv_obj_set_style_bg_color(voice_indicator, lv_color_hex(0xFBBC05), 0);
            setStatus("Connexion Wi-Fi...", 0xFBBC05);
            break;

        case VoiceState::CONNECTING_SERVICE:
        case VoiceState::GENERATING:
            // En attente de generation / connexion Gemini : petit point bleu discret
            lv_obj_set_hidden(voice_indicator, false);
            lv_obj_set_style_bg_color(voice_indicator, lv_color_hex(0x4285F4), 0);
            setStatus("Preparation de la voix...", 0x4285F4);
            break;

        case VoiceState::SPEAKING:
            // Parole active : petit point vert/blanc lumineux
            lv_obj_set_hidden(voice_indicator, false);
            lv_obj_set_style_bg_color(voice_indicator, lv_color_hex(0x34A853), 0);
            setStatus("Annonce en cours...", 0x34A853);
            break;

        case VoiceState::ERROR_STATE:
            // Erreur : petit point rouge
            lv_obj_set_hidden(voice_indicator, false);
            lv_obj_set_style_bg_color(voice_indicator, lv_color_hex(0xEA4335), 0);
            setStatus("Voix indisponible", 0xEA4335);
            break;

        default:
            lv_obj_set_hidden(voice_indicator, true);
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
        lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -10);
        lv_obj_invalidate(time_label);
    }
    if (date_label) lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 18);
    if (alarm_label) lv_obj_align(alarm_label, LV_ALIGN_BOTTOM_MID, 0, -34);
    if (status_label) lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    if (voice_indicator) {
        lv_obj_align(voice_indicator, LV_ALIGN_TOP_RIGHT, -10, 10);
    }
    layoutAlarmEditor();

    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    M5.Display.waitDMA();
}

void ClockUI::setTime(const char* time_str) {
    if (!initialized || !time_label || !time_str) return;
    lv_label_set_text(time_label, time_str);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -10);
}

void ClockUI::setDate(const char* date_str) {
    if (!initialized || !date_label || !date_str) return;
    lv_label_set_text(date_label, date_str);
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 18);
}

void ClockUI::setAlarm(const char* alarm_str, bool enabled) {
    if (!initialized || !alarm_label || !alarm_str) return;
    lv_label_set_text(alarm_label, alarm_str);
    lv_obj_set_style_text_color(alarm_label,
                                lv_color_hex(enabled ? 0xFBBF24 : 0x64748B), 0);
}

void ClockUI::setStatus(const char* status_str, uint32_t color) {
    if (!initialized || !status_label || !status_str) return;
    lv_label_set_text(status_label, status_str);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void ClockUI::layoutAlarmEditor() {
    if (!alarm_editor || !disp) return;
    int32_t w = lv_display_get_horizontal_resolution(disp);
    int32_t h = lv_display_get_vertical_resolution(disp);
    lv_obj_set_size(alarm_editor, w, h);
    lv_obj_align(alarm_editor, LV_ALIGN_CENTER, 0, 0);
    if (editor_time_label) lv_obj_align(editor_time_label, LV_ALIGN_CENTER, 0, -24);
    if (editor_state_label) lv_obj_align(editor_state_label, LV_ALIGN_CENTER, 0, 24);

    const int32_t margin = 4;
    int32_t button_w = (w - margin * 7) / 6;
    int32_t button_h = 38;
    for (int i = 0; i < 6; ++i) {
        if (!editor_buttons[i]) continue;
        lv_obj_set_size(editor_buttons[i], button_w, button_h);
        lv_obj_set_pos(editor_buttons[i], margin + i * (button_w + margin), h - button_h - 8);
    }
}

void ClockUI::showAlarmEditor(int hour, int minute, bool enabled) {
    if (!initialized || !alarm_editor) return;
    alarm_editor_visible = true;
    updateAlarmEditor(hour, minute, enabled);
    lv_obj_set_hidden(alarm_editor, false);
    lv_obj_move_foreground(alarm_editor);
}

void ClockUI::updateAlarmEditor(int hour, int minute, bool enabled) {
    if (!alarm_editor || !editor_time_label || !editor_state_label) return;
    char time_text[8];
    snprintf(time_text, sizeof(time_text), "%02d:%02d", hour, minute);
    lv_label_set_text(editor_time_label, time_text);
    lv_label_set_text(editor_state_label, enabled ? "ACTIVE" : "DESACTIVE");
    lv_obj_set_style_text_color(editor_state_label,
                                lv_color_hex(enabled ? 0xFBBF24 : 0x64748B), 0);
}

void ClockUI::hideAlarmEditor() {
    if (!alarm_editor) return;
    alarm_editor_visible = false;
    lv_obj_set_hidden(alarm_editor, true);
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
