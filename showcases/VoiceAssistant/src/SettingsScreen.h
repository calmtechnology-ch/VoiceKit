#pragma once

#include <lvgl.h>
#include <functional>
#include "AppController.h"

/**
 * @class SettingsScreen
 * @brief Gestionnaire de l'écran des paramètres de VoiceAssistant v0.1.0 (LVGL 9).
 *        Permet de configurer le fournisseur IA, la langue vocale, la voix compatible,
 *        le volume, de sauvegarder en NVS et d'effacer l'historique local.
 */
class SettingsScreen {
public:
    SettingsScreen() = default;
    ~SettingsScreen() = default;

    /**
     * @brief Construit l'écran des paramètres.
     * @param parent Objet conteneur parent LVGL.
     * @param controller Référence au contrôleur applicatif.
     * @param onBackClicked Callback invoqué lors du clic sur RETOUR.
     */
    void init(lv_obj_t* parent, AppController& controller, std::function<void()> onBackClicked);

    void show();
    void hide();
    bool isVisible() const;
    void refresh();
    void update();

    void setFeedbackMessage(const char* msg);
    bool isSettingsActionAllowed() const;

private:
    lv_obj_t* m_container = nullptr;
    AppController* m_controller = nullptr;
    std::function<void()> m_onBack;
    bool m_waitingConfirmation = false;
    bool m_controlsBlocked = false;

    // Widgets LVGL 9
    lv_obj_t* m_btnGemini = nullptr;
    lv_obj_t* m_btnOpenAi = nullptr;
    lv_obj_t* m_lblGemini = nullptr;
    lv_obj_t* m_lblOpenAi = nullptr;

    lv_obj_t* m_btnLang = nullptr;
    lv_obj_t* m_lblLangValue = nullptr;

    lv_obj_t* m_btnVoice = nullptr;
    lv_obj_t* m_lblVoiceValue = nullptr;

    lv_obj_t* m_btnSaveNvs = nullptr;
    lv_obj_t* m_lblVolumeValue = nullptr;
    lv_obj_t* m_lblFeedback = nullptr;
    lv_obj_t* m_lblSysInfo = nullptr;

    size_t m_selectedLangIdx = 0;
    size_t m_selectedVoiceIdx = 0;

    void updateProviderButtons();
    void updateLangLabel();
    void updateVoiceLabel();
    void updateVolumeLabel();
    void updateControlsAvailability();

    void onProviderClicked(VoiceProviderType prov);
    void onLangCycleClicked();
    void onVoiceCycleClicked();
    void onVolumeChange(int delta);
    void onSaveNvsClicked();
    void onClearHistoryClicked();

    static void eventHandler(lv_event_t* e);
};
