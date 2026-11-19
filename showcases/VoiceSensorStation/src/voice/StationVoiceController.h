#pragma once

#include <Arduino.h>
#include <VoxCoreVoice.h>
#include <VoiceInteractionController.h>
#include <VoiceProfileManager.h>
#include <vector>
#include "AppConfig.h"
#include "sensors/SensorManager.h"
#include "voice/SensorCommands.h"

/**
 * @file StationVoiceController.h
 * @brief Contrôleur vocal applicatif de VoiceSensorStation v0.1.0.
 *
 * Orchestre VoiceKit v0.6.7.1 sans modifier la bibliothèque :
 * - VoxCoreVoice en mode unitaire (STT + TTS say() + IA askAi())
 * - VoiceInteractionController (PTT à 2 clics, VAD local, STOP prioritaire)
 * - SensorCommands (ToolRegistry + VoiceCommandDispatcher pour commandes locales)
 * - Injection des mesures réelles dans les requêtes IA OpenAI / Gemini
 * - Bascule contrôlée de fournisseur, gestion des erreurs et accès hors ligne aux réglages locaux
 */

struct VoiceDiagnostics {
    uint32_t localCommandCount = 0;
    uint32_t aiQueryCount = 0;
    uint32_t silenceRejectCount = 0;
    uint32_t stopInterruptCount = 0;
    uint32_t voiceErrorCount = 0;
    uint32_t connectionErrorCount = 0;
    uint32_t lastUpdateDurationUs = 0;
};

class StationVoiceController {
public:
    StationVoiceController();
    ~StationVoiceController() = default;

    bool begin(const SensorManager& sensorManager);
    void update(uint32_t nowMs);

    // Actions utilisateur PTT 2 clics & STOP prioritaire
    void onTalkClick();
    void triggerStop();

    // États et diagnostics
    VoiceInteractionState getInteractionState() const { return m_interaction.getState(); }
    VoiceState getVoiceState() const { return m_voice.getState(); }
    LiveSessionState getSessionState() const { return m_voice.getSession().getState(); }
    const char* getInteractionStateString() const;
    const char* getAssistantStatusShort() const;

    bool isWifiConnected() const;
    bool isSessionReady() const;
    bool isProfileChangeInProgress() const;
    bool hasProviderKey(VoiceProviderType provider) const;

    // Configuration vocale et fournisseurs IA
    VoiceProviderType getProvider() const { return m_activeProfile.provider; }
    const char* getProviderName() const;
    String getLanguage() const { return m_activeProfile.language; }
    String getVoiceId() const { return m_activeProfile.voiceId; }
    uint8_t getVolume() const { return m_voice.getVolume(); }

    bool switchProvider(VoiceProviderType newProvider);
    bool setLanguage(const String& bcp47);
    bool setVoice(const String& voiceId);
    void setVolume(uint8_t volumePercent);
    bool saveProfileToNvs();

    std::vector<VoiceDescriptor> getAvailableVoices() const;
    std::vector<LanguageDescriptor> getAvailableLanguages() const;

    // Consommation thread-safe vers LVGL (boucle principale uniquement)
    bool consumeStatusMessage(String& outMsg);
    bool consumeLastInteractionText(String& outText);
    bool consumeProfileResult(bool& outSuccess, String& outMsg);

    const VoiceDiagnostics& getDiagnostics() const { return m_diag; }
    String getLastError() const { return m_lastError; }

    VoxCoreVoice& getVoice() { return m_voice; }
    const VoxCoreVoice& getVoice() const { return m_voice; }
    VoiceInteractionController& getInteraction() { return m_interaction; }
    const VoiceInteractionController& getInteraction() const { return m_interaction; }

private:
    enum class ProfileOpState : uint8_t {
        IDLE = 0,
        PENDING_STOP,
        DISCONNECTING_SESSION,
        APPLYING
    };

    static String sanitizeSecret(const char* raw);
    bool isEngineIdleForTransition() const;
    bool beginProfileApply(uint32_t nowMs);
    void finishProfileFailure(const char* msg);
    bool requestProfileUpdate(const VoiceProfile& target, bool isProviderSwitch);
    void handleFinalTranscription(const String& text, uint32_t turnId);
    void pushThreadSafeText(char* dst, size_t dstSize, const char* src, bool& flag);

    VoxCoreVoice m_voice;
    VoiceInteractionConfig m_interactionConfig;
    VoiceInteractionController m_interaction;
    SensorCommands m_commands;
    VoiceProfile m_activeProfile;
    VoiceDiagnostics m_diag;

    String m_lastError;
    uint32_t m_lastProcessedTurnId = 0;

    // Gestion STOP prioritaire sans reconnexion forcée
    bool m_stopActive = false;
    uint32_t m_lastStopMs = 0;
    uint32_t m_stopStableSinceMs = 0;
    bool m_turnInterrupted = false;

    // Machine d'état non bloquante de changement de profil / fournisseur
    ProfileOpState m_profileOp = ProfileOpState::IDLE;
    VoiceProfile m_pendingProfile;
    bool m_pendingIsProviderSwitch = false;
    uint32_t m_profileOpStartMs = 0;
    uint32_t m_disconnectSettleMs = 0;

    bool m_profileResultReady = false;
    bool m_profileResultSuccess = false;
    char m_profileResultMsg[96] = "";

    // Surveillance réseau périodique
    uint32_t m_lastNetCheckMs = 0;
    bool m_wifiWasConnected = false;

    // Découplage thread-safe vers LVGL 9
    portMUX_TYPE m_mux = portMUX_INITIALIZER_UNLOCKED;
    char m_pendingStatus[128] = "";
    bool m_hasPendingStatus = false;
    char m_pendingInteraction[256] = "";
    bool m_hasPendingInteraction = false;
};
