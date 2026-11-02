/**
 * @file VoiceUI.h
 * @brief Gestionnaire d'interface utilisateur LVGL 9 pour OpenAIConversationLVGL.
 */

#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <lvgl.h>
#include "AppConfig.h"
#include "FrenchFonts.h"
#include "VoiceOrb.h"
#include "PTTController.h"
#include "Diagnostics.h"

class VoiceUI {
public:
    VoiceUI();
    ~VoiceUI();

    bool init(PTTController* ptt, Diagnostics* diag);

    // Mises a jour d'etat de l'interface
    void syncWithPTT(PTTState pttState);
    void setUserText(const String& text);
    void setAssistantText(const String& text);
    void setErrorMessage(const String& err);
    void setWifiStatus(bool connected, int8_t rssi);

    // Boucle d'ordonnancement non-bloquante
    void update(uint32_t nowMs, uint32_t audioLevel = 0, bool throttle = false);

    // Accesseurs
    PTTController* getPttController() const { return _ptt; }
    bool isPhysicalTouchActive() const { return _physicalTouchActive; }

private:
    PTTController* _ptt = nullptr;
    Diagnostics* _diag = nullptr;
    VoiceOrb _orb;

    lv_display_t* _disp = nullptr;
    lv_indev_t* _indev = nullptr;

    // Elements graphiques LVGL 9
    lv_obj_t* _scr = nullptr;
    lv_obj_t* _header = nullptr;
    lv_obj_t* _label_title = nullptr;
    lv_obj_t* _label_wifi = nullptr;
    lv_obj_t* _badge_state = nullptr;
    lv_obj_t* _label_badge = nullptr;

    lv_obj_t* _label_state_text = nullptr;

    lv_obj_t* _card_user = nullptr;
    lv_obj_t* _label_user_content = nullptr;

    lv_obj_t* _card_asst = nullptr;
    lv_obj_t* _label_asst_content = nullptr;

    lv_obj_t* _btn_main = nullptr;
    lv_obj_t* _label_btn = nullptr;

    PTTState _lastPttState = PTTState::DISABLED;
    bool _wifiConnected = false;
    int8_t _wifiRssi = 0;
    bool _physicalTouchActive = false;
    bool _initialized = false;

    // Buffers d'affichage statiques
    alignas(4) static uint8_t s_drawBuf[AppConfig::BUFFER_PIXELS * sizeof(uint16_t)];

    static void flushCallback(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static void touchReadCallback(lv_indev_t* indev, lv_indev_data_t* data);
    static void buttonEventCallback(lv_event_t* e);

    void buildWidgets();
    void updateButtonVisuals(PTTState state);
    void updateStateLabel(PTTState state);
};

