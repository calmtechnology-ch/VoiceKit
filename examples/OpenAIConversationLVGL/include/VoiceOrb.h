/**
 * @file VoiceOrb.h
 * @brief Orbe vocal interactif anime LVGL 9 pour M5Stack CoreS3.
 */

#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "AppConfig.h"

enum class OrbVisualState : uint8_t {
    READY,
    LISTENING,
    PROCESSING,
    SPEAKING,
    ERROR_STATE
};

class VoiceOrb {
public:
    VoiceOrb();
    ~VoiceOrb();

    bool init(lv_obj_t* parent);
    void setState(OrbVisualState state);
    OrbVisualState getState() const { return _state; }

    void update(uint32_t nowMs, uint32_t audioLevel = 0, bool throttle = false);
    void forceRedraw();

private:
    lv_obj_t* _container = nullptr;
    lv_obj_t* _ring_outer = nullptr;
    lv_obj_t* _ring_middle = nullptr;
    lv_obj_t* _core = nullptr;
    lv_obj_t* _label_icon = nullptr;

    OrbVisualState _state = OrbVisualState::READY;
    OrbVisualState _prevState = OrbVisualState::READY;

    uint32_t _lastFrameMs = 0;
    uint32_t _smoothedAudio = 0;
    bool _initialized = false;
    bool _forceRedraw = true;

    void applyColors();
    void renderReady(uint32_t nowMs);
    void renderListening(uint32_t nowMs, uint32_t audioLevel);
    void renderProcessing(uint32_t nowMs);
    void renderSpeaking(uint32_t nowMs);
    void renderError();
};

