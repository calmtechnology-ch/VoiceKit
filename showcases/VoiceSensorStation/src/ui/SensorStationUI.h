#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "AppConfig.h"
#include "sensors/SensorManager.h"
#include "voice/StationVoiceController.h"

/**
 * @file SensorStationUI.h
 * @brief Interface graphique LVGL 9 (thème clair, 320x240) de VoiceSensorStation v0.1.0.1.
 *
 * - Écran principal : tableau de bord environnemental (Température, Humidité, Pression, Gaz),
 *   barre d'état capteur/assistant et boutons tactiles [PARLER] [STOP] [RÉGLAGES].
 * - Écran réglages : toujours accessible (même hors ligne), fournisseur OpenAI/Gemini,
 *   langue, voix, volume, état capteur, sauvegarde NVS, accès diagnostics.
 * - Écran diagnostics : métriques matérielles, capteur, vocal et mémoire en temps réel.
 *
 * Règle stricte : toutes les fonctions de cette classe sont appelées uniquement depuis
 * la tâche principale (setup / loop).
 */

class SensorStationUI {
public:
    SensorStationUI() = default;
    ~SensorStationUI() = default;

    void init(SensorManager& sensorManager, StationVoiceController& voiceController);
    void update(uint32_t nowMs);

    uint32_t getLastUpdateDurationUs() const { return m_lastUpdateDurationUs; }

private:
    enum class ActiveScreen : uint8_t {
        DASHBOARD = 0,
        SETTINGS,
        DIAGNOSTICS
    };

    void buildDashboardScreen();
    void buildSettingsScreen();
    void buildDiagnosticsScreen();
    void showScreen(ActiveScreen screen);

    void updateDashboard(uint32_t nowMs);
    void updateSettings(uint32_t nowMs);
    void updateDiagnostics(uint32_t nowMs);

    static void setLabelIfChanged(lv_obj_t* label, char* cache, size_t cacheSize, const char* text);
    static void formatCommaFloat(char* dst, size_t dstSize, float value, int decimals);

    // Callbacks LVGL
    static void onTalkClicked(lv_event_t* e);
    static void onStopClicked(lv_event_t* e);
    static void onSettingsClicked(lv_event_t* e);
    static void onBackToDashClicked(lv_event_t* e);
    static void onBackToSettingsClicked(lv_event_t* e);
    static void onDiagClicked(lv_event_t* e);
    static void onProviderGeminiClicked(lv_event_t* e);
    static void onProviderOpenAiClicked(lv_event_t* e);
    static void onCycleLanguageClicked(lv_event_t* e);
    static void onCycleVoiceClicked(lv_event_t* e);
    static void onVolumeChanged(lv_event_t* e);
    static void onSaveNvsClicked(lv_event_t* e);

    SensorManager* m_sensors = nullptr;
    StationVoiceController* m_voice = nullptr;
    ActiveScreen m_activeScreen = ActiveScreen::DASHBOARD;

    // Écrans LVGL
    lv_obj_t* m_scrDashboard = nullptr;
    lv_obj_t* m_scrSettings = nullptr;
    lv_obj_t* m_scrDiag = nullptr;

    // Widgets Tableau de bord
    lv_obj_t* m_lblWifi = nullptr;
    lv_obj_t* m_lblClock = nullptr;
    lv_obj_t* m_lblTempVal = nullptr;
    lv_obj_t* m_lblHumVal = nullptr;
    lv_obj_t* m_lblPresVal = nullptr;
    lv_obj_t* m_lblGasVal = nullptr;
    lv_obj_t* m_lblSensorStatus = nullptr;
    lv_obj_t* m_lblVoiceStatus = nullptr;
    lv_obj_t* m_btnTalk = nullptr;
    lv_obj_t* m_lblBtnTalk = nullptr;
    lv_obj_t* m_btnStop = nullptr;
    lv_obj_t* m_btnSettings = nullptr;

    // Widgets Réglages
    lv_obj_t* m_btnGemini = nullptr;
    lv_obj_t* m_btnOpenAi = nullptr;
    lv_obj_t* m_lblLangVal = nullptr;
    lv_obj_t* m_lblVoiceVal = nullptr;
    lv_obj_t* m_sliderVol = nullptr;
    lv_obj_t* m_lblVolVal = nullptr;
    lv_obj_t* m_lblSettingsSensor = nullptr;
    lv_obj_t* m_lblSettingsFeedback = nullptr;

    // Widgets Diagnostics
    lv_obj_t* m_lblDiagBody = nullptr;

    // Caches anti-clignotement (mise à jour uniquement si texte modifié)
    char m_cacheWifi[20] = "";
    char m_cacheClock[12] = "";
    char m_cacheTemp[16] = "";
    char m_cacheHum[16] = "";
    char m_cachePres[16] = "";
    char m_cacheGas[16] = "";
    char m_cacheSensorSt[40] = "";
    char m_cacheVoiceSt[40] = "";
    char m_cacheTalkBtn[20] = "";
    char m_cacheSettingsSensor[64] = "";
    char m_cacheSettingsFeedback[96] = "";
    char m_cacheDiag[512] = "";

    VoiceInteractionState m_lastRenderedVoiceState = VoiceInteractionState::DISABLED;
    uint32_t m_lastRenderedTalkBg = 0;
    bool m_lastRenderedWifi = false;
    SensorState m_lastRenderedSensorState = SensorState::INITIALIZING;

    uint32_t m_lastUiRefreshMs = 0;
    uint32_t m_lastDiagRefreshMs = 0;
    uint32_t m_lastUpdateDurationUs = 0;
};
