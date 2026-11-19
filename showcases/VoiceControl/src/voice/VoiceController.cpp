#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#ifdef WIFI_PASS
#define WIFI_PASSWORD WIFI_PASS
#else
#define WIFI_PASSWORD ""
#endif
#endif

#ifndef OPENAI_API_KEY
#ifdef OPENAI_KEY
#define OPENAI_API_KEY OPENAI_KEY
#else
#define OPENAI_API_KEY ""
#endif
#endif

#ifndef GEMINI_API_KEY
#ifdef GEMINI_KEY
#define GEMINI_API_KEY GEMINI_KEY
#else
#define GEMINI_API_KEY ""
#endif
#endif

#include "VoiceController.h"
#include <WiFi.h>
#include <cstring>

VoiceController::VoiceController()
    : m_interaction(m_voice, m_interactionConfig),
      m_lang(VoiceControlConfig::getSystemLanguage()) {
}

String VoiceController::sanitizeSecret(const char* raw) {
    if (!raw || raw[0] == '\0') return "";
    static const char* const kPlaceholders[] = {
        "VOTRE_WIFI_SSID", "VOTRE_WIFI_PASSWORD",
        "VOTRE_CLE_GEMINI", "sk-proj-VOTRE_CLE_OPENAI",
        "VOTRE_CLE_API_GEMINI", "VOTRE_CLE_API_OPENAI",
        "YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD", "YOUR_API_KEY"
    };
    for (const char* ph : kPlaceholders) {
        if (strcmp(raw, ph) == 0) return "";
    }
    String s(raw);
    s.trim();
    return s;
}

void VoiceController::pushStatus(const char* msg) {
    if (!msg) return;
    portENTER_CRITICAL(&m_mux);
    strncpy(m_pendingStatus, msg, sizeof(m_pendingStatus) - 1);
    m_pendingStatus[sizeof(m_pendingStatus) - 1] = '\0';
    m_hasPendingStatus = true;
    portEXIT_CRITICAL(&m_mux);
}

bool VoiceController::consumeStatusMessage(String& outMsg) {
    bool has = false;
    char buf[128] = "";
    portENTER_CRITICAL(&m_mux);
    if (m_hasPendingStatus) {
        strncpy(buf, m_pendingStatus, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        m_hasPendingStatus = false;
        has = true;
    }
    portEXIT_CRITICAL(&m_mux);
    if (has) {
        outMsg = String(buf);
    }
    return has;
}

bool VoiceController::begin() {
    m_lang = VoiceControlConfig::getSystemLanguage();

    // 1. Appliquer le fuseau horaire suisse dès le démarrage (non bloquant)
    SwissTime::applyTimezone();
#if VC_DIAG_ENABLED
    Serial.printf("[VC][TIME] Timezone: %s\n", VC_TIMEZONE_NAME);
#endif

    // 2. Initialiser le registre local des commandes horaires
    if (!m_commands.begin(m_lang)) {
#if VC_DIAG_ENABLED
        Serial.println("[VC][ERROR] TimeCommands registration incomplete");
#endif
    }

    // 3. Configurer VoiceInteractionController (PTT 2 clics + VAD local + STOP)
    m_interactionConfig.debounceMs              = VC_PTT_DEBOUNCE_MS;
    m_interactionConfig.micTimeoutMs            = VC_PTT_MIC_TIMEOUT_MS;
    m_interactionConfig.serverResponseTimeoutMs = VC_PTT_SERVER_TIMEOUT_MS;
    m_interactionConfig.minCaptureDurationMs    = VC_VAD_MIN_CAPTURE_DURATION_MS;
    m_interactionConfig.minSpeechDurationMs     = VC_VAD_MIN_SPEECH_DURATION_MS;
    m_interactionConfig.minConsecutiveSpeechMs  = VC_VAD_MIN_CONSECUTIVE_SPEECH_MS;
    m_interactionConfig.speechThresholdFloorRms = VC_VAD_SPEECH_FLOOR_RMS;
    m_interactionConfig.noiseMultiplier         = VC_VAD_NOISE_MULTIPLIER;
    m_interactionConfig.noiseOffsetRms          = VC_VAD_NOISE_OFFSET_RMS;
    m_interaction.setConfig(m_interactionConfig);
    m_interaction.begin();

    m_interaction.onStateChanged([this](VoiceInteractionState /*oldSt*/, VoiceInteractionState newSt) {
        if (newSt == VoiceInteractionState::READY) {
            logVoiceStateIfChanged(VoiceUiStatus::READY);
        } else if (newSt == VoiceInteractionState::LISTENING) {
            logVoiceStateIfChanged(VoiceUiStatus::LISTENING);
        } else if (newSt == VoiceInteractionState::PROCESSING ||
                   newSt == VoiceInteractionState::STOPPING_CAPTURE) {
            logVoiceStateIfChanged(VoiceUiStatus::PROCESSING);
        } else if (newSt == VoiceInteractionState::SPEAKING) {
            logVoiceStateIfChanged(VoiceUiStatus::SPEAKING);
        } else if (newSt == VoiceInteractionState::ERROR_STATE) {
            logVoiceStateIfChanged(VoiceUiStatus::ERROR_STATE);
        }
    });

    m_interaction.onSilenceDetected([this](const char* reason) {
        m_stats.silenceRejectCount++;
#if VC_DIAG_ENABLED
        Serial.printf("[VC][PTT] SILENCE (%s)\n", reason ? reason : "SILENCE");
#endif
        pushStatus(Localization::get().statusSilence);
    });

    m_interaction.onCaptureValidated([](const VoiceCaptureMetrics& metrics) {
        if (metrics.isValid) {
#if VC_DIAG_ENABLED
            Serial.println("[VC][PTT] VALIDATED");
#endif
        }
    });

    // 4. Configurer VoiceKit v0.6.7.1 exclusivement depuis VoiceControlConfig.h + secrets.h
    String wifiSsid  = sanitizeSecret(WIFI_SSID);
    String wifiPass  = sanitizeSecret(WIFI_PASSWORD);
    String openAiKey = sanitizeSecret(OPENAI_API_KEY);
    String geminiKey = sanitizeSecret(GEMINI_API_KEY);

    const VoiceControlConfig::ProviderId providerId = VoiceControlConfig::getConfiguredProvider();
    const VoiceProviderType vkProvider = (providerId == VoiceControlConfig::ProviderId::GEMINI)
                                             ? VoiceProviderType::GEMINI
                                             : VoiceProviderType::OPENAI;

    VoiceKitConfig vConfig;
    vConfig.wifiMode                  = WiFiMode::MANAGED_WIFI;
    vConfig.wifiSSID                  = wifiSsid;
    vConfig.wifiPassword              = wifiPass;
    vConfig.enableFastConnect         = true;
    vConfig.volume                    = VC_DEFAULT_VOLUME;
    vConfig.restoreSavedProfileOnBoot = false; // Toujours respecter VoiceControlConfig.h

    // Mode unitaire obligatoire : STT -> TimeCommands (local) -> TTS say()
    vConfig.enableConversation = false;
    vConfig.provider           = vkProvider;
    vConfig.model              = VoiceControlConfig::getConfiguredModel();
    vConfig.voice              = VoiceControlConfig::getConfiguredVoice();
    vConfig.language           = VoiceControlConfig::getTtsLanguageBcp47();
    vConfig.inputTranscriptionLanguage = VoiceControlConfig::getSttLanguageBcp47();

    vConfig.openAiApiKey = openAiKey;
    vConfig.geminiApiKey = geminiKey;
    vConfig.apiKey       = (vkProvider == VoiceProviderType::GEMINI) ? geminiKey : openAiKey;

    vConfig.systemInstruction = String(VoiceControlConfig::getBaseSystemPrompt()) +
                                VoiceControlConfig::getVerbatimRule();
    vConfig.promptPrefix      = VoiceControlConfig::getVerbatimPrefix();

    if (vConfig.apiKey.isEmpty()) {
#if VC_DIAG_ENABLED
        Serial.printf("[VC][ERROR] Missing API key for provider %s in include/secrets.h\n",
                      VoiceControlConfig::getConfiguredProviderUpper());
#endif
    }

    // 5. Callbacks VoxCoreVoice -> VoiceInteractionController & TimeCommands
    m_voice.onAudioChunk([this](const int16_t* samples, size_t count, uint32_t rms) {
        m_interaction.processAudioChunk(samples, count, rms);
    });

    m_voice.onReady([this]() {
        m_interaction.notifyReady();
        logVoiceStateIfChanged(VoiceUiStatus::READY);
        pushStatus(Localization::get().statusReady);
    });

    m_voice.onStateChanged([this](VoiceState st) {
        m_interaction.notifyVoiceStateChanged(st);
    });

    m_voice.onListeningStarted([this]() {
        m_turnInterrupted = false;
        m_interaction.notifyListeningStarted();
#if VC_DIAG_ENABLED
        Serial.println("[VC][PTT] START");
#endif
        pushStatus(Localization::get().statusListening);
    });

    m_voice.onListeningFinished([this]() {
        m_interaction.notifyListeningFinished();
        pushStatus(Localization::get().statusProcessing);
    });

    m_voice.onTranscriptionWithTurnId([this](const String& text, bool isFinal, uint32_t turnId) {
        if (m_turnInterrupted || m_interaction.isLastTurnCancelled()) {
            return; // Ignorer toute transcription tardive après STOP
        }
        if (isFinal) {
            handleFinalTranscription(text, turnId);
        }
    });

    m_voice.onSpeechStarted([this]() {
        if (m_turnInterrupted || m_interaction.isLastTurnCancelled()) {
            return;
        }
        m_interaction.notifySpeechStarted();
        pushStatus(Localization::get().statusSpeaking);
    });

    m_voice.onSpeechFinished([this]() {
        m_interaction.notifySpeechFinished();
        m_turnInterrupted = false;
        logVoiceStateIfChanged(VoiceUiStatus::READY);
        pushStatus(Localization::get().statusReady);
    });

    m_voice.onError([this](const String& err) {
        m_stats.voiceErrorCount++;
#if VC_DIAG_ENABLED
        Serial.printf("[VC][ERROR] Voice: %s\n", err.c_str());
#endif
        m_interaction.notifyError(err);
        logVoiceStateIfChanged(VoiceUiStatus::ERROR_STATE);
        pushStatus(Localization::get().statusError);
    });

    // 6. Démarrage non bloquant du moteur VoiceKit
    if (!m_voice.begin(vConfig)) {
        m_stats.voiceErrorCount++;
#if VC_DIAG_ENABLED
        Serial.printf("[VC][ERROR] voice.begin() failed: %s\n", m_voice.getLastError().c_str());
#endif
        pushStatus(Localization::get().statusError);
        return false;
    }

    pushStatus(Localization::get().statusConnecting);
    return true;
}

void VoiceController::handleFinalTranscription(const String& text, uint32_t turnId) {
    if (turnId != 0 && turnId == m_lastProcessedTurnId) {
        return; // Protection anti-double exécution
    }

    String trimmed = text;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        return;
    }
    m_lastProcessedTurnId = turnId;

#if VC_DIAG_ENABLED
    Serial.printf("[VC][STT] %s\n", trimmed.c_str());
#endif

    // Lecture directe de l'horloge UTC locale du CoreS3 (jamais de synchro NTP ici)
    time_t nowUtc = time(nullptr);
    TimeCommandDispatchResult res = m_commands.process(trimmed, nowUtc, m_ntpSynced);

    if (res.recognized) {
        m_stats.commandSuccessCount++;
    } else {
        m_stats.commandRejectedCount++;
    }

#if VC_DIAG_ENABLED
    Serial.printf("[VC][CMD] %s\n", TimeCommandLogic::commandToLogName(res.command));
    Serial.printf("[VC][TTS] %s\n", res.spokenResponse.c_str());
#endif

    if (!res.spokenResponse.isEmpty()) {
        if (!m_voice.say(res.spokenResponse)) {
            m_stats.voiceErrorCount++;
#if VC_DIAG_ENABLED
            Serial.printf("[VC][ERROR] TTS say() rejected: %s\n", m_voice.getLastError().c_str());
#endif
            pushStatus(Localization::get().statusError);
        }
    }
}

void VoiceController::updateNtpState(uint32_t nowMs) {
    const bool wifiNow = (WiFi.status() == WL_CONNECTED);

    if (wifiNow != m_wifiWasConnected) {
        m_wifiWasConnected = wifiNow;
        if (wifiNow) {
            m_wifiConnectedSinceMs = nowMs;
            m_ntpTimeoutLogged = false;
            if (!m_ntpSynced) {
                m_ntpStatus = NtpUiStatus::SYNCING;
            }
        } else {
            if (!m_ntpSynced) {
                m_ntpStatus = NtpUiStatus::WAITING_WIFI;
            }
            pushStatus(Localization::get().statusOffline);
        }
    }

    // Toujours préserver le fuseau suisse (WiFiManager appelle configTime(0, 0, ...) lors de l'init SNTP)
    SwissTime::applyTimezone();

    const time_t nowUtc = time(nullptr);
    const bool validNow = SwissTime::isValidEpoch(nowUtc) || m_voice.getWiFiManager().isTimeSynced();

    if (!m_ntpSynced && validNow && SwissTime::isValidEpoch(nowUtc)) {
        SwissTime::applyTimezone();
        m_ntpSynced = true;
        m_ntpStatus = NtpUiStatus::SYNCED;
        m_stats.ntpSyncDurationMs = (m_wifiConnectedSinceMs > 0) ? (nowMs - m_wifiConnectedSinceMs) : 0;

#if VC_DIAG_ENABLED
        struct tm localTm{};
        if (SwissTime::utcEpochToSwissLocal(nowUtc, localTm)) {
            char dtBuf[32];
            SwissTime::formatDateTimeLog(localTm, dtBuf, sizeof(dtBuf));
            Serial.println("[VC][NTP] Synchronization OK");
            Serial.printf("[VC][TIME] Local: %s\n", dtBuf);
        }
#endif
        return;
    }

    if (!m_ntpSynced && wifiNow) {
        if (nowMs - m_wifiConnectedSinceMs >= VC_NTP_TIMEOUT_MS) {
            m_ntpStatus = NtpUiStatus::UNAVAILABLE;
            if (!m_ntpTimeoutLogged) {
                m_ntpTimeoutLogged = true;
#if VC_DIAG_ENABLED
                Serial.println("[VC][NTP] Synchronization unavailable (timeout) — retrying in background");
#endif
            }
        } else {
            m_ntpStatus = NtpUiStatus::SYNCING;
        }
    }
}

void VoiceController::update(uint32_t nowMs) {
    m_voice.update();
    m_interaction.update(nowMs);
    m_commands.getRegistry().update();
    updateNtpState(nowMs);

#if VC_DIAG_ENABLED
    if (VC_DIAG_HEARTBEAT_MS > 0 && (nowMs - m_lastHeartbeatMs >= VC_DIAG_HEARTBEAT_MS)) {
        m_lastHeartbeatMs = nowMs;
        Serial.printf("[VC][DIAG] WiFi=%s NTP=%s Voice=%s Cmd=%lu Rej=%lu Sil=%lu Stop=%lu Err=%lu\n",
                      isWifiConnected() ? "OK" : "OFF",
                      m_ntpSynced ? "OK" : "WAIT",
                      Localization::getVoiceStateLabel(getVoiceUiStatus()),
                      (unsigned long)m_stats.commandSuccessCount,
                      (unsigned long)m_stats.commandRejectedCount,
                      (unsigned long)m_stats.silenceRejectCount,
                      (unsigned long)m_stats.stopInterruptCount,
                      (unsigned long)m_stats.voiceErrorCount);
    }
#endif
}

void VoiceController::onTalkClick() {
    VoiceInteractionState st = m_interaction.getState();
    if (st == VoiceInteractionState::READY) {
        if (!isSessionReady()) {
            pushStatus(Localization::get().statusConnecting);
            return;
        }
        m_turnInterrupted = false;
        m_interaction.onTalkClick();
    } else if (st == VoiceInteractionState::LISTENING || st == VoiceInteractionState::STARTING) {
        m_interaction.onTalkClick();
    } else if (st == VoiceInteractionState::ERROR_STATE) {
        m_interaction.recover();
    }
}

void VoiceController::triggerStop() {
    uint32_t now = millis();
    if (now - m_lastStopMs < 250) {
        return; // Anti-rebond idempotent
    }

    VoiceInteractionState st = m_interaction.getState();
    if (st == VoiceInteractionState::READY && !m_voice.isSpeaking() && !m_voice.isBusy()) {
        return;
    }

    m_lastStopMs = now;
    m_turnInterrupted = true;
    m_stats.stopInterruptCount++;

#if VC_DIAG_ENABLED
    Serial.println("[VC][PTT] STOP");
#endif

    // Interruption immédiate via VoiceInteractionController sans forcer de reconnexion IA
    m_interaction.triggerStop();
    pushStatus(Localization::get().statusStopped);
}

bool VoiceController::getCurrentSwissTime(struct tm& outLocal, bool* outIsDst) const {
    if (!m_ntpSynced) {
        return false;
    }
    time_t nowUtc = time(nullptr);
    if (!SwissTime::isValidEpoch(nowUtc)) {
        return false;
    }
    return SwissTime::utcEpochToSwissLocal(nowUtc, outLocal, outIsDst);
}

bool VoiceController::isWifiConnected() const {
    return (WiFi.status() == WL_CONNECTED);
}

bool VoiceController::isSessionReady() const {
    return isWifiConnected() &&
           !m_voice.isConnecting() &&
           !m_voice.isSessionDisconnecting() &&
           (m_voice.getSession().getState() == LiveSessionState::CONNECTED);
}

bool VoiceController::isListening() const {
    VoiceInteractionState st = m_interaction.getState();
    return (st == VoiceInteractionState::LISTENING || st == VoiceInteractionState::STARTING);
}

VoiceUiStatus VoiceController::getVoiceUiStatus() const {
    if (!isWifiConnected()) {
        return VoiceUiStatus::INIT;
    }
    switch (m_interaction.getState()) {
        case VoiceInteractionState::READY:
            return isSessionReady() ? VoiceUiStatus::READY : VoiceUiStatus::INIT;
        case VoiceInteractionState::STARTING:
        case VoiceInteractionState::LISTENING:
            return VoiceUiStatus::LISTENING;
        case VoiceInteractionState::STOPPING_CAPTURE:
        case VoiceInteractionState::PROCESSING:
            return VoiceUiStatus::PROCESSING;
        case VoiceInteractionState::SPEAKING:
            return VoiceUiStatus::SPEAKING;
        case VoiceInteractionState::ERROR_STATE:
            return VoiceUiStatus::ERROR_STATE;
        case VoiceInteractionState::INTERRUPTING:
        case VoiceInteractionState::RECOVERING:
        case VoiceInteractionState::DISABLED:
        default:
            return isSessionReady() ? VoiceUiStatus::READY : VoiceUiStatus::INIT;
    }
}

void VoiceController::logVoiceStateIfChanged(VoiceUiStatus newUiStatus) {
    if (newUiStatus == m_lastLoggedVoiceUiStatus) {
        return;
    }
    m_lastLoggedVoiceUiStatus = newUiStatus;
#if VC_DIAG_ENABLED
    Serial.printf("[VC][VOICE] %s\n", Localization::getVoiceStateLabel(newUiStatus));
#endif
}
