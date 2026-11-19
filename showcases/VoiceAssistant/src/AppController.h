#pragma once

#include <Arduino.h>
#include <VoxCoreVoice.h>
#include <VoiceInteractionController.h>
#include <VoiceProfileManager.h>
#include <vector>
#include "app_config.h"

/**
 * @struct HistoryItem
 * @brief Élément d'historique conversationnel local conservé en RAM.
 */
struct HistoryItem {
    String userText;
    String assistantText;
    uint32_t timestampMs = 0;
};

/**
 * @enum SettingsBlockReason
 * @brief Diagnostic explicite de disponibilité des paramètres et du changement de fournisseur.
 */
enum class SettingsBlockReason : uint8_t {
    ALLOWED = 0,
    WIFI_DISCONNECTED,
    SESSION_CONNECTING,
    SESSION_RECONNECTING,
    PROFILE_CHANGE_PENDING,
    STOP_IN_PROGRESS,
    VOICE_BUSY,
    SESSION_ERROR
};

/**
 * @class AppController
 * @brief Contrôleur applicatif central de VoiceAssistant v0.1.0.
 *        Coordonne VoxCoreVoice, VoiceInteractionController, les transitions
 *        d'états, le découplage thread-safe vers LVGL 9 et l'historique en RAM.
 */
class AppController {
public:
    AppController();
    ~AppController() = default;

    /**
     * @brief Initialise les sous-systèmes VoiceKit et VoiceInteractionController.
     * @return true si l'initialisation a réussi.
     */
    bool begin();

    /**
     * @brief Cycle périodique obligatoire à appeler dans loop().
     * @param nowMs Horodatage millis() actuel.
     */
    void update(uint32_t nowMs);

    // =========================================================================
    // Commandes vocales (actions utilisateur)
    // =========================================================================

    /**
     * @brief Déclenche l'action du bouton PARLER / TERMINER (PTT à 2 clics).
     */
    void onTalkClick();

    /**
     * @brief Déclenche l'arrêt prioritaire STOP.
     */
    void triggerStop();

    // =========================================================================
    // États et accesseurs
    // =========================================================================

    VoiceInteractionState getState() const { return m_interaction.getState(); }
    const char* getStateString() const;
    const char* getStateFrenchLabel() const;

    bool isActionAllowed() const { return m_interaction.isActionAllowed(); }
    bool isWifiConnected() const;
    bool isSessionConnected() const;
    bool isSessionReady() const;
    SettingsBlockReason getSettingsBlockReason() const;
    SettingsBlockReason getProviderSwitchBlockReason() const;
    bool canModifySettings() const;
    bool canSwitchProvider() const;
    static const char* settingsBlockReasonToString(SettingsBlockReason reason);
    static const char* settingsBlockReasonToFrenchMessage(SettingsBlockReason reason);
    void logStateDiagnostics(const char* tag) const;
    bool isStopInProgress() const { return m_stopHandlingActive; }

    VoiceProviderType getProvider() const { return m_activeProfile.provider; }
    const char* getProviderName() const;
    String getLanguage() const { return m_activeProfile.language; }
    String getVoiceId() const { return m_activeProfile.voiceId; }
    uint8_t getVolume() const { return m_voice.getVolume(); }

    // =========================================================================
    // Configuration & Fournisseurs IA
    // =========================================================================

    /**
     * @brief Change le fournisseur IA actif (Gemini Live <-> OpenAI Realtime).
     * @param newProvider Nouveau fournisseur cible.
     * @return true si la demande est acceptée.
     */
    bool switchProvider(VoiceProviderType newProvider);

    /**
     * @brief Modifie la langue vocale de conversation (BCP-47).
     */
    bool setLanguage(const String& bcp47);

    /**
     * @brief Modifie la voix active pour le fournisseur courant.
     */
    bool setVoice(const String& voiceId);

    /**
     * @brief Règle le volume audio (0 à 100 %).
     */
    void setVolume(uint8_t volumePercent);

    /**
     * @brief Sauvegarde le profil vocal actuel en NVS (sans clés API).
     */
    bool saveProfileToNvs();

    /**
     * @brief Liste des voix compatibles pour le fournisseur actif.
     */
    std::vector<VoiceDescriptor> getAvailableVoices() const;

    /**
     * @brief Liste des langues prédéfinies.
     */
    std::vector<LanguageDescriptor> getAvailableLanguages() const;

    // =========================================================================
    // Historique conversationnel local (RAM uniquement)
    // =========================================================================

    const std::vector<HistoryItem>& getHistory() const { return m_history; }
    const String& getLastUserText() const { return m_lastUserText; }
    const String& getLastAssistantText() const { return m_lastAssistantText; }
    void clearHistory();
    bool consumeHistoryCleared();

    // =========================================================================
    // Suivi du basculement / modification de profil asynchrone
    // =========================================================================

    bool isProfileChangeInProgress() const;
    bool consumeProfileChangeResult(bool& success, String& message);

    // =========================================================================
    // Consommation des messages différés vers l'interface LVGL (thread-safe)
    // =========================================================================

    bool consumeNewUserText(String& outText);
    bool consumeNewAssistantText(String& outText);
    bool consumeNewVadText(String& outVad);
    bool consumeNewStatusMessage(String& outMsg);
    bool hasStateChanged(VoiceInteractionState& outState);

    // Diagnostics / Accès interne
    VoxCoreVoice& getVoice() { return m_voice; }
    const VoxCoreVoice& getVoice() const { return m_voice; }
    VoiceInteractionController& getInteraction() { return m_interaction; }
    const VoiceInteractionController& getInteraction() const { return m_interaction; }

private:
    VoxCoreVoice m_voice;
    VoiceInteractionConfig m_interactionConfig;
    VoiceInteractionController m_interaction;

    VoiceProfile m_activeProfile;

    // Historique local en RAM (max 4 échanges)
    std::vector<HistoryItem> m_history;
    String m_lastUserText;
    String m_lastAssistantText;
    static constexpr size_t MAX_HISTORY = 4;

    // Instrumentation STOP et synchronisation de tour
    bool m_stopHandlingActive = false;
    uint32_t m_lastStopTriggerMs = 0;
    uint32_t m_stopStableSinceMs = 0;
    bool m_turnInterruptedByStop = false;
    uint32_t m_lastAssistantUiPushMs = 0;
    LiveSessionState m_lastObservedLiveState = LiveSessionState::DISCONNECTED;
    SettingsBlockReason m_lastObservedSettingsReason = SettingsBlockReason::ALLOWED;

    // Buffers de découplage thread-safe (FreeRTOS -> boucle principale)
    portMUX_TYPE m_mux = portMUX_INITIALIZER_UNLOCKED;
    char m_pendingUserText[512] = "";
    bool m_hasPendingUser = false;
    char m_pendingAssistantText[512] = "";
    bool m_hasPendingAssistant = false;
    char m_pendingVadText[64] = "";
    bool m_hasPendingVad = false;
    char m_pendingStatusMsg[128] = "";
    bool m_hasPendingStatus = false;

    VoiceInteractionState m_lastReportedState = VoiceInteractionState::DISABLED;
    bool m_stateChanged = false;

    uint32_t m_lastConnCheckMs = 0;
    bool m_wifiWasConnected = false;
    bool m_sessionWasConnected = false;

    enum class ProfileOperationState {
        IDLE,
        PENDING_STOP,
        DISCONNECTING_SESSION,
        APPLYING
    };

    ProfileOperationState m_profileOpState = ProfileOperationState::IDLE;
    VoiceProfile m_pendingTargetProfile;
    bool m_pendingIsProviderSwitch = false;
    uint32_t m_profileOpStartMs = 0;
    uint32_t m_disconnectSettleStartMs = 0;

    bool m_profileResultReady = false;
    bool m_profileResultSuccess = false;
    char m_profileResultMessage[96] = "";

    bool m_historyCleared = false;
    String m_currentTurnUserText;
    String m_currentTurnAssistantText;

    bool isEngineStoppedForTransition() const;
    void finishProfileOpWithFailure(const char* message);
    bool beginProfileApplication(uint32_t nowMs);
    bool requestAcousticProfileChange(const VoiceProfile& targetProfile);
    void recordTurnInHistory(const String& user, const String& assistant);
    void setThreadSafeText(char* dst, size_t dstSize, const char* src, bool& flag);
};
