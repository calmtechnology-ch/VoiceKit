/**
 * @file VoiceOrb.cpp
 * @brief Implementation de l'orbe vocal interactif LVGL 9.
 */

#include "VoiceOrb.h"
#include <math.h>

static void set_circle_geometry(lv_obj_t* obj, int32_t diameter) {
    if (!obj) return;
    if (diameter < 2) diameter = 2;
    if (diameter > AppConfig::ORB_BOX_SIZE) diameter = AppConfig::ORB_BOX_SIZE;
    lv_obj_set_size(obj, diameter, diameter);
    int32_t offset = (AppConfig::ORB_BOX_SIZE - diameter) / 2;
    lv_obj_set_pos(obj, offset, offset);
}

VoiceOrb::VoiceOrb() = default;
VoiceOrb::~VoiceOrb() = default;

bool VoiceOrb::init(lv_obj_t* parent) {
    if (!parent) return false;

    _container = lv_obj_create(parent);
    lv_obj_remove_style_all(_container);
    lv_obj_set_size(_container, AppConfig::ORB_BOX_SIZE, AppConfig::ORB_BOX_SIZE);
    lv_obj_set_pos(_container, AppConfig::ORB_CENTER_X - AppConfig::ORB_BOX_SIZE / 2,
                               AppConfig::ORB_CENTER_Y - AppConfig::ORB_BOX_SIZE / 2);
    lv_obj_set_scrollable(_container, false);

    // Anneau externe
    _ring_outer = lv_obj_create(_container);
    lv_obj_remove_style_all(_ring_outer);
    lv_obj_set_style_radius(_ring_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_ring_outer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_ring_outer, 2, 0);
    lv_obj_set_scrollable(_ring_outer, false);

    // Anneau median
    _ring_middle = lv_obj_create(_container);
    lv_obj_remove_style_all(_ring_middle);
    lv_obj_set_style_radius(_ring_middle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_ring_middle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_ring_middle, 2, 0);
    lv_obj_set_scrollable(_ring_middle, false);

    // Noyau central
    _core = lv_obj_create(_container);
    lv_obj_remove_style_all(_core);
    lv_obj_set_style_radius(_core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_core, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(_core, false);

    // Label d'alerte / icone
    _label_icon = lv_label_create(_container);
    lv_label_set_text(_label_icon, "!");
    lv_obj_set_style_text_color(_label_icon, lv_color_hex(AppConfig::COLOR_TEXT_PRI_HEX), 0);
    lv_obj_center(_label_icon);
    lv_obj_set_hidden(_label_icon, true);

    applyColors();
    _initialized = true;
    _forceRedraw = true;
    return true;
}

void VoiceOrb::applyColors() {
    if (!_initialized) return;

    uint32_t colorHex = AppConfig::COLOR_CYAN_HEX;
    switch (_state) {
        case OrbVisualState::READY:       colorHex = AppConfig::COLOR_CYAN_HEX; break;
        case OrbVisualState::LISTENING:   colorHex = AppConfig::COLOR_CORAL_HEX; break;
        case OrbVisualState::PROCESSING:  colorHex = AppConfig::COLOR_PURPLE_HEX; break;
        case OrbVisualState::SPEAKING:    colorHex = AppConfig::COLOR_TURQUOISE_HEX; break;
        case OrbVisualState::ERROR_STATE: colorHex = AppConfig::COLOR_RED_HEX; break;
    }

    lv_color_t color = lv_color_hex(colorHex);
    lv_obj_set_style_bg_color(_core, color, 0);
    lv_obj_set_style_border_color(_ring_middle, color, 0);
    lv_obj_set_style_border_color(_ring_outer, color, 0);

    if (_state == OrbVisualState::ERROR_STATE) {
        lv_obj_set_hidden(_label_icon, false);
    } else {
        lv_obj_set_hidden(_label_icon, true);
    }
}

void VoiceOrb::setState(OrbVisualState state) {
    if (_state != state) {
        _prevState = _state;
        _state = state;
        applyColors();
        _forceRedraw = true;
    }
}

void VoiceOrb::forceRedraw() {
    _forceRedraw = true;
}

void VoiceOrb::renderReady(uint32_t nowMs) {
    // Respiration lente : periode 2400 ms
    float phase = (float)(nowMs % 2400) * (2.0f * 3.14159f / 2400.0f);
    float sineVal = sinf(phase); // -1.0 .. +1.0

    int32_t coreDiam = 36 + (int32_t)(sineVal * 4.0f); // 32 .. 40 px
    int32_t midDiam  = 50 + (int32_t)(sineVal * 5.0f); // 45 .. 55 px
    int32_t outDiam  = 64 + (int32_t)(sineVal * 6.0f); // 58 .. 70 px

    lv_opa_t midOpa = (lv_opa_t)(100 + (int32_t)(sineVal * 40.0f));
    lv_opa_t outOpa = (lv_opa_t)(60  + (int32_t)(sineVal * 30.0f));

    set_circle_geometry(_core, coreDiam);
    set_circle_geometry(_ring_middle, midDiam);
    set_circle_geometry(_ring_outer, outDiam);

    lv_obj_set_style_border_opa(_ring_middle, midOpa, 0);
    lv_obj_set_style_border_opa(_ring_outer, outOpa, 0);
}

void VoiceOrb::renderListening(uint32_t nowMs, uint32_t audioLevel) {
    // Lissage RMS authentique
    _smoothedAudio = (_smoothedAudio * 3 + audioLevel) / 4;

    // Pulsation rapide + reaction audio reelle
    float phase = (float)(nowMs % 800) * (2.0f * 3.14159f / 800.0f);
    float pulse = sinf(phase);

    int32_t audioBoost = (int32_t)(_smoothedAudio / 800); // proportionnel au RMS
    if (audioBoost > 14) audioBoost = 14;

    int32_t coreDiam = 38 + (int32_t)(pulse * 3.0f) + (audioBoost / 2);
    int32_t midDiam  = 52 + (int32_t)(pulse * 4.0f) + audioBoost;
    int32_t outDiam  = 66 + audioBoost;

    set_circle_geometry(_core, coreDiam);
    set_circle_geometry(_ring_middle, midDiam);
    set_circle_geometry(_ring_outer, outDiam);

    lv_obj_set_style_border_opa(_ring_middle, (lv_opa_t)(140 + audioBoost * 6), 0);
    lv_obj_set_style_border_opa(_ring_outer, (lv_opa_t)(100 + audioBoost * 8), 0);
}

void VoiceOrb::renderProcessing(uint32_t nowMs) {
    // Animation douce de reflexion : periode 1200 ms
    float phase = (float)(nowMs % 1200) * (2.0f * 3.14159f / 1200.0f);
    float sineVal = sinf(phase);

    int32_t coreDiam = 36;
    int32_t midDiam  = 52 + (int32_t)(sineVal * 6.0f);
    int32_t outDiam  = 66 - (int32_t)(sineVal * 6.0f);

    set_circle_geometry(_core, coreDiam);
    set_circle_geometry(_ring_middle, midDiam);
    set_circle_geometry(_ring_outer, outDiam);

    lv_opa_t midOpa = (lv_opa_t)(120 + (int32_t)(sineVal * 50.0f));
    lv_opa_t outOpa = (lv_opa_t)(120 - (int32_t)(sineVal * 50.0f));

    lv_obj_set_style_border_opa(_ring_middle, midOpa, 0);
    lv_obj_set_style_border_opa(_ring_outer, outOpa, 0);
}

void VoiceOrb::renderSpeaking(uint32_t nowMs) {
    // Ondes en expansion vers l'exterieur : periode 1000 ms
    float progress = (float)(nowMs % 1000) / 1000.0f; // 0.0 .. 1.0

    int32_t coreDiam = 36 + (int32_t)(sinf(progress * 3.14159f * 2.0f) * 2.0f);
    int32_t midDiam  = 44 + (int32_t)(progress * 16.0f); // 44 -> 60 px
    int32_t outDiam  = 54 + (int32_t)(progress * 18.0f); // 54 -> 72 px

    lv_opa_t midOpa = (lv_opa_t)(180 * (1.0f - progress * 0.7f));
    lv_opa_t outOpa = (lv_opa_t)(160 * (1.0f - progress));

    set_circle_geometry(_core, coreDiam);
    set_circle_geometry(_ring_middle, midDiam);
    set_circle_geometry(_ring_outer, outDiam);

    lv_obj_set_style_border_opa(_ring_middle, midOpa, 0);
    lv_obj_set_style_border_opa(_ring_outer, outOpa, 0);
}

void VoiceOrb::renderError() {
    set_circle_geometry(_core, 40);
    set_circle_geometry(_ring_middle, 56);
    set_circle_geometry(_ring_outer, 68);

    lv_obj_set_style_border_opa(_ring_middle, LV_OPA_70, 0);
    lv_obj_set_style_border_opa(_ring_outer, LV_OPA_40, 0);
}

void VoiceOrb::update(uint32_t nowMs, uint32_t audioLevel, bool throttle) {
    if (!_initialized) return;

    uint32_t interval = throttle ? (AppConfig::ORB_FRAME_INTERVAL_MS * 2) : AppConfig::ORB_FRAME_INTERVAL_MS;
    if (!_forceRedraw && (nowMs - _lastFrameMs < interval)) {
        return;
    }

    _lastFrameMs = nowMs;
    _forceRedraw = false;

    switch (_state) {
        case OrbVisualState::READY:
            renderReady(nowMs);
            break;
        case OrbVisualState::LISTENING:
            renderListening(nowMs, audioLevel);
            break;
        case OrbVisualState::PROCESSING:
            renderProcessing(nowMs);
            break;
        case OrbVisualState::SPEAKING:
            renderSpeaking(nowMs);
            break;
        case OrbVisualState::ERROR_STATE:
            renderError();
            break;
    }
}

