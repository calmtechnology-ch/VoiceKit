#pragma once

#include <Arduino.h>
#include <VoxCoreVoice.h>
#include <VoiceInteractionController.h>
#include <time.h>
#include "config/VoiceControlConfig.h"
#include "commands/TimeCommands.h"
#include "ui/Localization.h"

/**
 * @file VoiceController.h
 * @brief Contrôleur vocal et horaire de VoiceControl v0.1.0 pour M5Stack CoreS3.
 *
 * Réutilise directement VoiceKit v0.6.7.1 sans modifier la bibliothèque :
 * - VoxCoreVoice (Wi-Fi/SNTP non bloquant, STT, TTS say(), connexion OpenAI/Gemini)
 * - VoiceInteractionController (PTT à deux clics, VAD local anti-silence, STOP immédiat)
 * - TimeCommands (horloge locale suisse Europe/Zurich, commandes horaires et rejet hors périmètre)
 */

struct VoiceControlStats {
    uint32_t commandSuccessCount = 0;
    uint32_t commandRejectedCount = 0;
    uint32_t silenceRejectCount = 0;
    uint32_t stopInterruptCount = 0;
    uint32_t voiceErrorCount = 0;
    uint32_t ntpSyncDurationMs = 0;
};

class VoiceController {
public:
    VoiceController();
    ~VoiceController() = default;

    bool begin();
    void update(uint32_t nowMs);

    // Actions utilisateur (boutons LVGL PARLER et STOP)
    void onTalkClick();
    void triggerStop();

    // État horloge suisse / NTP
    bool isTimeSynced() const { return m_ntpSynced; }
    NtpUiStatus getNtpStatus() const { return m_ntpStatus; }
    bool getCurrentSwissTime(struct tm& outLocal, bool* outIsDst = nullptr) const;

    // État vocal et réseau
    bool isWifiConnected() const;
    bool isSessionReady() const;
    bool isListening() const;
    VoiceUiStatus getVoiceUiStatus() const;
    VoiceInteractionState getInteractionState() const { return m_interaction.getState(); }
    const char* getProviderDisplayName() const { return VoiceControlConfig::getConfiguredProviderDisplay(); }
    const VoiceControlStats& getStats() const { return m_stats; }

    // Consommation thread-safe des messages vers LVGL (loopTask uniquement)
    bool consumeStatusMessage(String& outMsg);

private:
    static String sanitizeSecret(const char* raw);
    void handleFinalTranscription(const String& text, uint32_t turnId);
    void updateNtpState(uint32_t nowMs);
    void pushStatus(const char* msg);
    void logVoiceStateIfChanged(VoiceUiStatus newUiStatus);

    VoxCoreVoice m_voice;
    VoiceInteractionConfig m_interactionConfig;
    VoiceInteractionController m_interaction;
    TimeCommands m_commands;
    VoiceControlStats m_stats;

    VoiceControlConfig::LanguageCode m_lang = VoiceControlConfig::LanguageCode::FR;
    uint32_t m_lastProcessedTurnId = 0;

    // Suivi STOP prioritaire (sans reconnexion forcée)
    bool m_turnInterrupted = false;
    uint32_t m_lastStopMs = 0;

    // Suivi NTP non bloquant
    bool m_ntpSynced = false;
    bool m_ntpTimeoutLogged = false;
    NtpUiStatus m_ntpStatus = NtpUiStatus::WAITING_WIFI;
    uint32_t m_wifiConnectedSinceMs = 0;
    bool m_wifiWasConnected = false;

    // Diagnostics d'état vocal
    VoiceUiStatus m_lastLoggedVoiceUiStatus = VoiceUiStatus::INIT;
    uint32_t m_lastHeartbeatMs = 0;

    // Découplage thread-safe (callbacks WebSocket -> loopTask LVGL)
    portMUX_TYPE m_mux = portMUX_INITIALIZER_UNLOCKED;
    char m_pendingStatus[128] = "";
    bool m_hasPendingStatus = false;
};
