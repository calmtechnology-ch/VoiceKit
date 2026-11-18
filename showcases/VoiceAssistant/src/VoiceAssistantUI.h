#pragma once

#include <lvgl.h>
#include "AppController.h"
#include "SettingsScreen.h"

/**
 * @class VoiceAssistantUI
 * @brief Gestionnaire de l'interface tactile complète de VoiceAssistant (LVGL 9).
 *        Supervise l'écran Conversation, les cartes de dialogue, les boutons tactiles
 *        PARLER / TERMINER et STOP, ainsi que la transition avec l'écran Paramètres.
 */
class VoiceAssistantUI {
public:
    VoiceAssistantUI() = default;
    ~VoiceAssistantUI() = default;

    /**
     * @brief Construit tous les écrans et widgets LVGL 9.
     * @param controller Référence au contrôleur applicatif.
     */
    void init(AppController& controller);

    /**
     * @brief Met à jour l'IHM depuis la boucle principale loop() (thread-safe).
     */
    void update();

    /**
     * @brief Affiche l'écran de conversation.
     */
    void showConversation();

    /**
     * @brief Affiche l'écran des paramètres.
     */
    void showSettings();

private:
    AppController* m_controller = nullptr;
    SettingsScreen m_settingsScreen;

    // Conteneur de la vue Conversation
    lv_obj_t* m_contConv = nullptr;

    // Barre d'en-tête
    lv_obj_t* m_lblTitle = nullptr;
    lv_obj_t* m_lblProviderPill = nullptr;
    lv_obj_t* m_lblWifiStatus = nullptr;

    // Indicateur central d'état vocal
    lv_obj_t* m_cardState = nullptr;
    lv_obj_t* m_lblState = nullptr;
    lv_obj_t* m_lblVad = nullptr;

    // Cartes de dialogue
    lv_obj_t* m_cardUser = nullptr;
    lv_obj_t* m_lblUserText = nullptr;

    lv_obj_t* m_cardAssistant = nullptr;
    lv_obj_t* m_contAssistantScroll = nullptr;
    lv_obj_t* m_lblAssistantText = nullptr;

    // Boutons de contrôle vocal
    lv_obj_t* m_btnTalk = nullptr;
    lv_obj_t* m_lblBtnTalk = nullptr;
    lv_obj_t* m_btnStop = nullptr;

    // Barre d'onglets inférieure
    lv_obj_t* m_btnTabConv = nullptr;
    lv_obj_t* m_lblTabConv = nullptr;
    lv_obj_t* m_btnTabSettings = nullptr;
    lv_obj_t* m_lblTabSettings = nullptr;

    uint32_t m_lastHeaderRefreshMs = 0;

    void updateVoiceState(VoiceInteractionState state);
    void refreshHeader();

    static void onTalkButtonClicked(lv_event_t* e);
    static void onStopButtonClicked(lv_event_t* e);
    static void onTabConvClicked(lv_event_t* e);
    static void onTabSettingsClicked(lv_event_t* e);
};
