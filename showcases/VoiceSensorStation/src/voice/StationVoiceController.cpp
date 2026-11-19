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

#ifndef GEMINI_API_KEY
#ifdef GEMINI_KEY
#define GEMINI_API_KEY GEMINI_KEY
#else
#define GEMINI_API_KEY ""
#endif
#endif

#ifndef OPENAI_API_KEY
#ifdef OPENAI_KEY
#define OPENAI_API_KEY OPENAI_KEY
#else
#define OPENAI_API_KEY ""
#endif
#endif

#include "StationVoiceController.h"
#include <WiFi.h>
#include <cstring>

StationVoiceController::StationVoiceController()
    : m_interaction(m_voice, m_interactionConfig) {
#if defined(USE_OPENAI) && !defined(USE_GEMINI)
    m_activeProfile.provider = VoiceProviderType::OPENAI;
    m_activeProfile.voiceId = AppConfig::OPENAI_VOICE;
#else
    m_activeProfile.provider = VoiceProviderType::GEMINI;
    m_activeProfile.voiceId = AppConfig::GEMINI_VOICE;
#endif
    m_activeProfile.language = AppConfig::DEFAULT_LANGUAGE;
    m_activeProfile.preferredGender = VoiceGender::ANY;
    m_activeProfile.style = VoiceStyle::NATURAL;
    m_activeProfile.tone = VoiceTone::NEUTRAL;
}

String StationVoiceController::sanitizeSecret(const char* raw) {
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

void StationVoiceController::pushThreadSafeText(char* dst, size_t dstSize,
                                                const char* src, bool& flag) {
    if (!dst || dstSize == 0 || !src) return;
    portENTER_CRITICAL(&m_mux);
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
    flag = true;
    portEXIT_CRITICAL(&m_mux);
}

StationSwitch::Provider StationVoiceController::toSwitchProvider(VoiceProviderType p) {
    return (p == VoiceProviderType::OPENAI) ? StationSwitch::Provider::OPENAI
                                            : StationSwitch::Provider::GEMINI;
}

StationSwitch::SessionState StationVoiceController::toSwitchSessionState(LiveSessionState st) {
    switch (st) {
        case LiveSessionState::DISCONNECTED:     return StationSwitch::SessionState::DISCONNECTED;
        case LiveSessionState::CONNECTING:       return StationSwitch::SessionState::CONNECTING;
        case LiveSessionState::CONNECTED:        return StationSwitch::SessionState::CONNECTED;
        case LiveSessionState::WAITING_RESPONSE: return StationSwitch::SessionState::WAITING_RESPONSE;
        case LiveSessionState::PLAYING:          return StationSwitch::SessionState::PLAYING;
        case LiveSessionState::INTERRUPTING:     return StationSwitch::SessionState::INTERRUPTING;
        case LiveSessionState::RECONNECTING:     return StationSwitch::SessionState::RECONNECTING;
        case LiveSessionState::ERROR:
        default:                                 return StationSwitch::SessionState::ERROR;
    }
}

const char* StationVoiceController::providerLabel(VoiceProviderType p) {
    return (p == VoiceProviderType::OPENAI) ? "OpenAI" : "Gemini";
}

void StationVoiceController::logSwitchState(const char* eventMsg, const char* detail) const {
    const char* activeName = providerLabel(m_activeProfile.provider);
    const char* targetName = (m_profileOp != ProfileOpState::IDLE)
                                 ? providerLabel(m_pendingProfile.provider)
                                 : activeName;
    if (detail && detail[0] != '\0') {
        Serial.printf("[%lu][SWITCH] %s (%s) | Active=%s Target=%s Voice=%s Session=%s Op=%s\n",
                      (unsigned long)millis(),
                      eventMsg ? eventMsg : "",
                      detail,
                      activeName,
                      targetName,
                      voiceStateToString(m_voice.getState()),
                      liveSessionStateToString(m_voice.getSession().getState()),
                      StationSwitch::opStateToString(m_profileOp));
    } else {
        Serial.printf("[%lu][SWITCH] %s | Active=%s Target=%s Voice=%s Session=%s Op=%s\n",
                      (unsigned long)millis(),
                      eventMsg ? eventMsg : "",
                      activeName,
                      targetName,
                      voiceStateToString(m_voice.getState()),
                      liveSessionStateToString(m_voice.getSession().getState()),
                      StationSwitch::opStateToString(m_profileOp));
    }
}

bool StationVoiceController::begin(const SensorManager& sensorManager) {
    Serial.println("[VOICE] Initialisation de StationVoiceController...");

    // 1. Enregistrer les commandes environnementales locales (ToolRegistry + VoiceCommandDispatcher)
    if (!m_commands.begin(sensorManager)) {
        Serial.println("[VOICE][WARN] Enregistrement incomplet des commandes locales.");
    }

    // 2. Configurer VoiceInteractionController (PTT 2 clics + VAD local + STOP)
    m_interactionConfig.debounceMs = AppConfig::DEBOUNCE_MS;
    m_interactionConfig.micTimeoutMs = AppConfig::MIC_TIMEOUT_MS;
    m_interactionConfig.serverResponseTimeoutMs = AppConfig::SERVER_TIMEOUT_MS;
    m_interactionConfig.minCaptureDurationMs = AppConfig::MIN_CAPTURE_DURATION_MS;
    m_interactionConfig.minSpeechDurationMs = AppConfig::MIN_SPEECH_DURATION_MS;
    m_interactionConfig.minConsecutiveSpeechMs = AppConfig::MIN_CONSECUTIVE_SPEECH_MS;
    m_interactionConfig.speechThresholdFloorRms = AppConfig::SPEECH_THRESHOLD_FLOOR_RMS;
    m_interactionConfig.noiseMultiplier = AppConfig::NOISE_MULTIPLIER;
    m_interactionConfig.noiseOffsetRms = AppConfig::NOISE_OFFSET_RMS;
    m_interaction.setConfig(m_interactionConfig);
    m_interaction.begin();

    m_interaction.onStateChanged([](VoiceInteractionState oldSt, VoiceInteractionState newSt) {
        Serial.printf("[%lu][PTT] %s -> %s\n",
                      (unsigned long)millis(),
                      voiceInteractionStateToString(oldSt),
                      voiceInteractionStateToString(newSt));
    });

    m_interaction.onSilenceDetected([this](const char* reason) {
        m_diag.silenceRejectCount++;
        Serial.printf("[VAD][REJECT] Silence ou bruit bref ignoré (%s)\n", reason ? reason : "SILENCE");
        pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus),
                           "Aucune parole détectée", m_hasPendingStatus);
    });

    m_interaction.onCaptureValidated([](const VoiceCaptureMetrics& m) {
        if (m.isValid) {
            Serial.printf("[VAD][VALID] Parole confirmée (%u ms, peak %u RMS)\n",
                          (unsigned)m.cumulativeSpeechMs, (unsigned)m.peakRms);
        }
    });

    // 3. Restauration optionnelle du profil vocal sauvegardé en NVS (sans clé API)
    VoiceProfile savedProf;
    bool restored = false;
    m_voice.getProfileManager().loadActiveProfile(savedProf, m_activeProfile.provider, &restored);
    if (restored) {
        m_activeProfile = savedProf;
#if defined(USE_OPENAI) && !defined(USE_GEMINI)
        m_activeProfile.provider = VoiceProviderType::OPENAI;
#elif defined(USE_GEMINI) && !defined(USE_OPENAI)
        m_activeProfile.provider = VoiceProviderType::GEMINI;
#endif
    }

    if (m_activeProfile.voiceId.isEmpty() ||
        !m_voice.getProfileManager().isValidVoiceForProvider(m_activeProfile.provider, m_activeProfile.voiceId)) {
        m_activeProfile.voiceId = (m_activeProfile.provider == VoiceProviderType::OPENAI)
                                      ? AppConfig::OPENAI_VOICE
                                      : AppConfig::GEMINI_VOICE;
    }

    // 4. Configuration VoiceKit (mode unitaire : STT -> routage local ou askAi -> TTS)
    String wifiSsid = sanitizeSecret(WIFI_SSID);
    String wifiPass = sanitizeSecret(WIFI_PASSWORD);
    String geminiKey = sanitizeSecret(GEMINI_API_KEY);
    String openAiKey = sanitizeSecret(OPENAI_API_KEY);

    // Basculer automatiquement si le fournisseur choisi n'a pas de clé mais l'autre oui
    if (m_activeProfile.provider == VoiceProviderType::GEMINI && geminiKey.isEmpty() && !openAiKey.isEmpty()) {
        m_activeProfile.provider = VoiceProviderType::OPENAI;
        m_activeProfile.voiceId = AppConfig::OPENAI_VOICE;
    } else if (m_activeProfile.provider == VoiceProviderType::OPENAI && openAiKey.isEmpty() && !geminiKey.isEmpty()) {
        m_activeProfile.provider = VoiceProviderType::GEMINI;
        m_activeProfile.voiceId = AppConfig::GEMINI_VOICE;
    }

    VoiceKitConfig vConfig;
    vConfig.wifiMode = WiFiMode::MANAGED_WIFI;
    vConfig.wifiSSID = wifiSsid;
    vConfig.wifiPassword = wifiPass;
    vConfig.enableFastConnect = true;
    vConfig.volume = AppConfig::DEFAULT_VOLUME;

    // Mode unitaire obligatoire pour intercepter la transcription avant TTS / askAi
    vConfig.enableConversation = false;
    vConfig.systemInstruction = AppConfig::SYSTEM_INSTRUCTION;
    vConfig.aiPromptPrefix = ""; // Le contexte structuré est injecté directement par SensorCommands

    vConfig.geminiApiKey = geminiKey;
    vConfig.openAiApiKey = openAiKey;
    vConfig.provider = m_activeProfile.provider;
    vConfig.language = m_activeProfile.language;
    vConfig.voice = m_activeProfile.voiceId;

    if (m_activeProfile.provider == VoiceProviderType::OPENAI) {
        vConfig.apiKey = openAiKey;
        vConfig.model = AppConfig::OPENAI_MODEL;
    } else {
        vConfig.apiKey = geminiKey;
        vConfig.model = AppConfig::GEMINI_MODEL;
    }

    // 5. Callbacks VoxCoreVoice -> VoiceInteractionController & routage
    m_voice.onAudioChunk([this](const int16_t* samples, size_t count, uint32_t rms) {
        m_interaction.processAudioChunk(samples, count, rms);
    });

    m_voice.onReady([this]() {
        Serial.println("[VOICE] Session vocale prête.");
        m_interaction.notifyReady();
        portENTER_CRITICAL(&m_mux);
        m_evtVoiceReady = true;
        portEXIT_CRITICAL(&m_mux);
        pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus),
                           "Assistant prêt", m_hasPendingStatus);
    });

    m_voice.onStateChanged([this](VoiceState st) {
        m_interaction.notifyVoiceStateChanged(st);
    });

    m_voice.onListeningStarted([this]() {
        m_turnInterrupted = false;
        m_interaction.notifyListeningStarted();
    });

    m_voice.onListeningFinished([this]() {
        m_interaction.notifyListeningFinished();
    });

    m_voice.onTranscriptionWithTurnId([this](const String& text, bool isFinal, uint32_t turnId) {
        if (m_turnInterrupted || m_interaction.isLastTurnCancelled()) {
            return; // Rejeter toute transcription tardive après STOP
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
    });

    m_voice.onSpeechFinished([this]() {
        m_interaction.notifySpeechFinished();
        m_turnInterrupted = false;
    });

    m_voice.onAssistantText([this](const String& text) {
        if (m_turnInterrupted || m_interaction.isLastTurnCancelled() || text.isEmpty()) {
            return;
        }
        pushThreadSafeText(m_pendingInteraction, sizeof(m_pendingInteraction),
                           text.c_str(), m_hasPendingInteraction);
    });

    m_voice.onError([this](const String& err) {
        m_diag.voiceErrorCount++;
        if (!isWifiConnected() || m_voice.getSession().getState() != LiveSessionState::CONNECTED) {
            m_diag.connectionErrorCount++;
        }
        Serial.printf("[VOICE][ERROR] %s\n", err.c_str());
        m_interaction.notifyError(err);
        const char* safeErr = err.isEmpty() ? "Erreur vocale" : err.c_str();
        portENTER_CRITICAL(&m_mux);
        strncpy(m_evtVoiceErrorMsg, safeErr, sizeof(m_evtVoiceErrorMsg) - 1);
        m_evtVoiceErrorMsg[sizeof(m_evtVoiceErrorMsg) - 1] = '\0';
        m_evtVoiceError = true;
        portEXIT_CRITICAL(&m_mux);
        pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus),
                           safeErr, m_hasPendingStatus);
    });

    m_voice.onProfileApplied([this](const VoiceProfile& prof) {
        Serial.printf("[%lu][VOICE] Profil confirmé : %s | %s | %s\n",
                      (unsigned long)millis(),
                      providerLabel(prof.provider),
                      prof.voiceId.c_str(),
                      prof.language.c_str());
        portENTER_CRITICAL(&m_mux);
        m_evtAppliedSnapshot.provider = prof.provider;
        m_evtAppliedSnapshot.preferredGender = prof.preferredGender;
        m_evtAppliedSnapshot.style = prof.style;
        m_evtAppliedSnapshot.tone = prof.tone;
        strncpy(m_evtAppliedSnapshot.language, prof.language.c_str(), sizeof(m_evtAppliedSnapshot.language) - 1);
        m_evtAppliedSnapshot.language[sizeof(m_evtAppliedSnapshot.language) - 1] = '\0';
        strncpy(m_evtAppliedSnapshot.voiceId, prof.voiceId.c_str(), sizeof(m_evtAppliedSnapshot.voiceId) - 1);
        m_evtAppliedSnapshot.voiceId[sizeof(m_evtAppliedSnapshot.voiceId) - 1] = '\0';
        strncpy(m_evtAppliedSnapshot.accent, prof.accent.c_str(), sizeof(m_evtAppliedSnapshot.accent) - 1);
        m_evtAppliedSnapshot.accent[sizeof(m_evtAppliedSnapshot.accent) - 1] = '\0';
        m_evtProfileApplied = true;
        portEXIT_CRITICAL(&m_mux);
    });

    m_voice.onProfileError([this](const String& err) {
        m_diag.voiceErrorCount++;
        const char* safeErr = err.isEmpty() ? "Erreur profil" : err.c_str();
        Serial.printf("[%lu][VOICE][PROFILE_ERR] %s\n", (unsigned long)millis(), safeErr);
        portENTER_CRITICAL(&m_mux);
        strncpy(m_evtProfileErrorMsg, safeErr, sizeof(m_evtProfileErrorMsg) - 1);
        m_evtProfileErrorMsg[sizeof(m_evtProfileErrorMsg) - 1] = '\0';
        m_evtProfileError = true;
        portEXIT_CRITICAL(&m_mux);
    });

    // 6. Démarrage du moteur VoiceKit (non bloquant ; si hors ligne, la station capteurs reste active)
    if (!m_voice.begin(vConfig)) {
        m_diag.voiceErrorCount++;
        m_lastError = m_voice.getLastError();
        Serial.printf("[VOICE][WARN] voice.begin() incomplet : %s (mode capteur local actif)\n",
                      m_lastError.c_str());
        pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus),
                           "Mode local (sans IA)", m_hasPendingStatus);
        return false;
    }

    return true;
}

void StationVoiceController::handleFinalTranscription(const String& text, uint32_t turnId) {
    if (turnId != 0 && turnId == m_lastProcessedTurnId) {
        return; // Protection anti-double exécution
    }
    String trimmed = text;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        return;
    }
    m_lastProcessedTurnId = turnId;
    Serial.printf("[USER] %s\n", trimmed.c_str());

    uint32_t nowMs = millis();

    // 1. Tenter l'exécution locale déterministe via ToolRegistry
    SensorCommandDispatchResult localRes = m_commands.tryExecuteLocal(trimmed, nowMs);
    if (localRes.handledLocally && !localRes.spokenResponse.isEmpty()) {
        m_diag.localCommandCount++;
        Serial.printf("[LOCAL_CMD][%s] %s\n", localRes.toolId.c_str(), localRes.spokenResponse.c_str());
        pushThreadSafeText(m_pendingInteraction, sizeof(m_pendingInteraction),
                           localRes.spokenResponse.c_str(), m_hasPendingInteraction);
        if (!m_voice.say(localRes.spokenResponse)) {
            m_diag.voiceErrorCount++;
            m_lastError = m_voice.getLastError();
            Serial.printf("[VOICE][ERR] say() refusé : %s\n", m_lastError.c_str());
        }
        return;
    }

    // 2. Sinon, question évoluée -> injection des mesures réelles + askAi()
    m_diag.aiQueryCount++;
    String enrichedPrompt = m_commands.buildAiPrompt(trimmed, nowMs);
    Serial.println("[AI_QUERY] Envoi avec contexte capteur réel.");
    if (!m_voice.askAi(enrichedPrompt)) {
        m_diag.voiceErrorCount++;
        m_lastError = m_voice.getLastError();
        Serial.printf("[VOICE][ERR] askAi() refusé : %s\n", m_lastError.c_str());
    }
}

void StationVoiceController::update(uint32_t nowMs) {
    uint32_t t0 = micros();
    m_voice.update();
    m_interaction.update(nowMs);
    m_commands.getRegistry().update();

    // 0. Traitement déterministe sur loopTask des callbacks asynchrones de VoiceKit (zéro race FreeRTOS)
    processDeferredSwitchEvents(nowMs);

    // 1. Stabilisation de STOP (sans reconnexion forcée)
    if (m_stopActive) {
        constexpr uint32_t STOP_MIN_HOLD_MS = 120;
        constexpr uint32_t STOP_STABLE_MS = 120;
        constexpr uint32_t STOP_MAX_MS = 2500;

        if (isEngineIdleForTransition() && (nowMs - m_lastStopMs >= STOP_MIN_HOLD_MS)) {
            if (m_stopStableSinceMs == 0) m_stopStableSinceMs = nowMs;
            if (nowMs - m_stopStableSinceMs >= STOP_STABLE_MS) {
                m_stopActive = false;
                m_stopStableSinceMs = 0;
            }
        } else {
            m_stopStableSinceMs = 0;
        }
        if (m_stopActive && (nowMs - m_lastStopMs >= STOP_MAX_MS)) {
            m_stopActive = false;
            m_stopStableSinceMs = 0;
        }
    }

    // 2. Suivi des transitions réelles de session/moteur durant une bascule
    if (m_profileOp != ProfileOpState::IDLE) {
        LiveSessionState curSess = m_voice.getSession().getState();
        VoiceState curVoice = m_voice.getState();
        if (curSess != m_lastLoggedSwitchSessionState || curVoice != m_lastLoggedSwitchVoiceState) {
            m_lastLoggedSwitchSessionState = curSess;
            m_lastLoggedSwitchVoiceState = curVoice;
            logSwitchState("State transition");
        }
    }

    // 3. Machine d'état non bloquante de bascule de fournisseur / profil (délégation native à VoiceKit)
    if (m_profileOp == ProfileOpState::PENDING_STOP) {
        if (isEngineIdleForTransition() && !m_stopActive) {
            if (m_voice.isConnecting() || m_voice.isSessionDisconnecting() ||
                m_voice.getSession().getState() == LiveSessionState::CONNECTING ||
                m_voice.getSession().getState() == LiveSessionState::RECONNECTING) {
                logSwitchState("Disconnect requested after stop");
                m_voice.requestDisconnectSession();
                m_profileOp = ProfileOpState::DISCONNECTING_SESSION;
                m_profileOpStartMs = nowMs;
                m_disconnectSettleMs = 0;
            } else {
                beginProfileApply(nowMs);
            }
        } else if (nowMs - m_profileOpStartMs > 4000) {
            logSwitchState("Result: FAILED", "Timeout arrêt vocal");
            finishProfileFailure("Timeout arrêt vocal");
        }
    } else if (m_profileOp == ProfileOpState::DISCONNECTING_SESSION) {
        bool disconnected = (!m_voice.isSessionDisconnecting() &&
                             !m_voice.isConnecting() &&
                             !m_voice.isBusy() &&
                             (m_voice.getSession().getState() == LiveSessionState::DISCONNECTED ||
                              m_voice.getSession().getState() == LiveSessionState::ERROR));
        if (disconnected) {
            if (m_disconnectSettleMs == 0) m_disconnectSettleMs = nowMs;
            if (nowMs - m_disconnectSettleMs >= 50) {
                m_disconnectSettleMs = 0;
                logSwitchState("Session disconnected -> applying profile");
                beginProfileApply(nowMs);
            }
        } else {
            m_disconnectSettleMs = 0;
            if (nowMs - m_profileOpStartMs > 3000) {
                logSwitchState("Result: FAILED", "Timeout fermeture session");
                m_voice.stop();
                m_voice.resumeAutoReconnect();
                m_activeProfile = m_voice.getVoiceProfile();
                finishProfileFailure("Timeout fermeture session");
            }
        }
    } else if (m_profileOp == ProfileOpState::APPLYING) {
        // Watchdog de sécurité supérieur aux timeouts natifs VoiceKit (Gemini ~12 s, OpenAI 15 s)
        // afin de laisser le rollback natif de VoiceKit agir en priorité.
        if (nowMs - m_profileOpStartMs > 18000) {
            logSwitchState("Watchdog timeout in APPLYING");
            if (m_voice.isProfileChangePending() || m_voice.isConnecting()) {
                m_voice.stop(); // Force le rollback propre dans VoiceKit
            }
            m_voice.resumeAutoReconnect();
            m_activeProfile = m_voice.getVoiceProfile();
            logSwitchState("Result: FAILED", "Délai connexion dépassé");
            finishProfileFailure("Délai connexion dépassé");
        }
    }

    // 4. Surveillance réseau légère (toutes les 500 ms)
    if (nowMs - m_lastNetCheckMs >= 500) {
        m_lastNetCheckMs = nowMs;
        bool wifiNow = (WiFi.status() == WL_CONNECTED);
        if (wifiNow != m_wifiWasConnected) {
            m_wifiWasConnected = wifiNow;
            pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus),
                               wifiNow ? "Wi-Fi connecté" : "Wi-Fi déconnecté",
                               m_hasPendingStatus);
        }
    }

    m_diag.lastUpdateDurationUs = static_cast<uint32_t>(micros() - t0);
}

void StationVoiceController::onTalkClick() {
    VoiceInteractionState st = m_interaction.getState();
    if (st == VoiceInteractionState::READY) {
        if (!isSessionReady()) {
            pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus),
                               "Service vocal non prêt", m_hasPendingStatus);
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

void StationVoiceController::triggerStop() {
    uint32_t now = millis();
    if (m_stopActive || (now - m_lastStopMs < 400)) {
        return; // Idempotence
    }
    const bool switchInProgress = (m_profileOp != ProfileOpState::IDLE || m_voice.isProfileChangePending());
    VoiceInteractionState st = m_interaction.getState();
    if (!switchInProgress &&
        st == VoiceInteractionState::READY && !m_voice.isSpeaking() && !m_voice.isBusy()) {
        return;
    }

    m_diag.stopInterruptCount++;
    m_lastStopMs = now;
    m_stopActive = true;
    m_stopStableSinceMs = 0;
    m_turnInterrupted = true;

    if (switchInProgress) {
        logSwitchState("STOP requested during switch");
        // Interruption propre de la bascule sans reconnexion incontrôlée
        m_voice.stop();
        m_activeProfile = m_voice.getVoiceProfile();
        finishProfileFailure("Bascule annulée (STOP)");
        logSwitchState("Result: CANCELLED", "STOP");
    }

    m_interaction.triggerStop();
    pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus),
                       "Interrompu (STOP)", m_hasPendingStatus);
}

bool StationVoiceController::isWifiConnected() const {
    return (WiFi.status() == WL_CONNECTED);
}

bool StationVoiceController::isNetworkReadyForOnlineApply() const {
    return isWifiConnected() &&
           (m_voice.getWiFiManager().isTimeSynced() || VoxCoreVoice::isSystemTimeValid());
}

bool StationVoiceController::isSessionReady() const {
    return isWifiConnected() &&
           !m_voice.isConnecting() &&
           !m_voice.isSessionDisconnecting() &&
           (m_voice.getSession().getState() == LiveSessionState::CONNECTED) &&
           !isProfileChangeInProgress() &&
           !m_stopActive;
}

bool StationVoiceController::isProfileChangeInProgress() const {
    return (m_profileOp != ProfileOpState::IDLE || m_voice.isProfileChangePending());
}

bool StationVoiceController::hasProviderKey(VoiceProviderType provider) const {
    return m_voice.hasProviderApiKey(provider);
}

const char* StationVoiceController::getProviderName() const {
    return (m_activeProfile.provider == VoiceProviderType::OPENAI) ? "OpenAI" : "Gemini";
}

const char* StationVoiceController::getInteractionStateString() const {
    return voiceInteractionStateToString(m_interaction.getState());
}

const char* StationVoiceController::getAssistantStatusShort() const {
    if (!isWifiConnected()) return "Hors ligne";
    if (isProfileChangeInProgress()) return "Bascule IA...";
    LiveSessionState liveSt = m_voice.getSession().getState();
    if (liveSt == LiveSessionState::CONNECTING || liveSt == LiveSessionState::RECONNECTING) {
        return "Connexion IA...";
    }
    switch (m_interaction.getState()) {
        case VoiceInteractionState::READY:            return "Assistant READY";
        case VoiceInteractionState::STARTING:         return "Micro...";
        case VoiceInteractionState::LISTENING:        return "Écoute...";
        case VoiceInteractionState::STOPPING_CAPTURE: return "Validation...";
        case VoiceInteractionState::PROCESSING:       return "Traitement...";
        case VoiceInteractionState::SPEAKING:         return "Parole...";
        case VoiceInteractionState::INTERRUPTING:     return "Arrêt STOP";
        case VoiceInteractionState::RECOVERING:       return "Reprise...";
        case VoiceInteractionState::ERROR_STATE:      return "Erreur IA";
        case VoiceInteractionState::DISABLED:
        default:                                      return "IA indisponible";
    }
}

bool StationVoiceController::isEngineIdleForTransition() const {
    if (m_voice.isBusy() || m_voice.isSpeaking() || m_voice.isListening() ||
        m_voice.isTranscribing() || m_voice.getStreamer().isStreaming() ||
        m_voice.getPlayer().isPlaying() > 0) {
        return false;
    }
    VoiceInteractionState st = m_interaction.getState();
    if (st == VoiceInteractionState::INTERRUPTING ||
        st == VoiceInteractionState::LISTENING ||
        st == VoiceInteractionState::SPEAKING ||
        st == VoiceInteractionState::STARTING ||
        st == VoiceInteractionState::STOPPING_CAPTURE) {
        return false;
    }
    return true;
}

void StationVoiceController::finishProfileSuccess(const VoiceProfile& confirmed, const char* sourceTag) {
    bool wasSwitch = m_pendingIsProviderSwitch;
    m_activeProfile = confirmed;
    m_profileOp = ProfileOpState::IDLE;
    m_pendingIsProviderSwitch = false;
    m_disconnectSettleMs = 0;
    m_profileResultReady = true;
    m_profileResultSuccess = true;

    const char* msg = wasSwitch
                          ? (confirmed.provider == VoiceProviderType::GEMINI ? "Gemini actif" : "OpenAI actif")
                          : "Réglage appliqué";
    strncpy(m_profileResultMsg, msg, sizeof(m_profileResultMsg) - 1);
    m_profileResultMsg[sizeof(m_profileResultMsg) - 1] = '\0';

    if (m_voice.isReady() && m_interaction.getState() != VoiceInteractionState::READY) {
        m_interaction.notifyReady();
    }

    logSwitchState("Profile confirmed", sourceTag);
    logSwitchState("Result: SUCCESS", msg);
    pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus),
                       "Assistant prêt", m_hasPendingStatus);
}

void StationVoiceController::finishProfileFailure(const char* msg) {
    m_profileOp = ProfileOpState::IDLE;
    m_pendingIsProviderSwitch = false;
    m_disconnectSettleMs = 0;
    m_profileResultReady = true;
    m_profileResultSuccess = false;
    const char* safeMsg = (msg && msg[0] != '\0') ? msg : "Échec modification";
    strncpy(m_profileResultMsg, safeMsg, sizeof(m_profileResultMsg) - 1);
    m_profileResultMsg[sizeof(m_profileResultMsg) - 1] = '\0';
    pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus), safeMsg, m_hasPendingStatus);
}

void StationVoiceController::processDeferredSwitchEvents(uint32_t /*nowMs*/) {
    bool hasApplied = false;
    PendingAppliedSnapshot appliedSnap;
    bool hasProfileErr = false;
    char profErrBuf[96] = "";
    bool hasVoiceErr = false;
    char voiceErrBuf[96] = "";
    bool hasReady = false;

    portENTER_CRITICAL(&m_mux);
    if (m_evtProfileApplied) {
        hasApplied = true;
        appliedSnap = m_evtAppliedSnapshot;
        m_evtProfileApplied = false;
    }
    if (m_evtProfileError) {
        hasProfileErr = true;
        strncpy(profErrBuf, m_evtProfileErrorMsg, sizeof(profErrBuf) - 1);
        profErrBuf[sizeof(profErrBuf) - 1] = '\0';
        m_evtProfileError = false;
    }
    if (m_evtVoiceError) {
        hasVoiceErr = true;
        strncpy(voiceErrBuf, m_evtVoiceErrorMsg, sizeof(voiceErrBuf) - 1);
        voiceErrBuf[sizeof(voiceErrBuf) - 1] = '\0';
        m_evtVoiceError = false;
    }
    if (m_evtVoiceReady) {
        hasReady = true;
        m_evtVoiceReady = false;
    }
    portEXIT_CRITICAL(&m_mux);

    // 1. Confirmation explicite par onProfileApplied()
    if (hasApplied) {
        VoiceProfile confirmed;
        confirmed.provider = appliedSnap.provider;
        confirmed.preferredGender = appliedSnap.preferredGender;
        confirmed.style = appliedSnap.style;
        confirmed.tone = appliedSnap.tone;
        confirmed.language = String(appliedSnap.language);
        confirmed.voiceId = String(appliedSnap.voiceId);
        confirmed.accent = String(appliedSnap.accent);

        if (m_profileOp == ProfileOpState::APPLYING) {
            if (StationSwitch::doesProfileAppliedConfirm(
                    m_profileOp, m_pendingIsProviderSwitch,
                    toSwitchProvider(m_pendingProfile.provider),
                    toSwitchProvider(confirmed.provider))) {
                finishProfileSuccess(confirmed, "onProfileApplied");
            } else {
                // Callback tardif d'un autre fournisseur : synchroniser sans annoncer la réussite
                m_activeProfile = m_voice.getVoiceProfile();
                logSwitchState("Ignored stale onProfileApplied", providerLabel(confirmed.provider));
            }
        } else {
            // Hors bascule active : simple synchronisation de l'état actif
            m_activeProfile = confirmed;
        }
    }

    // 2. Erreur profil dédiée (onProfileError) -> rollback déjà effectué dans VoiceKit
    if (hasProfileErr) {
        m_lastError = String(profErrBuf);
        m_activeProfile = m_voice.getVoiceProfile();
        if (m_profileOp != ProfileOpState::IDLE) {
            m_voice.resumeAutoReconnect();
            logSwitchState("Result: FAILED", profErrBuf);
            finishProfileFailure(profErrBuf);
        }
    }

    // 3. Erreur vocale générale (onError)
    if (hasVoiceErr) {
        m_lastError = String(voiceErrBuf);
        if (m_profileOp == ProfileOpState::APPLYING && !m_voice.isProfileChangePending()) {
            m_activeProfile = m_voice.getVoiceProfile();
            m_voice.resumeAutoReconnect();
            logSwitchState("Result: FAILED", voiceErrBuf);
            finishProfileFailure(voiceErrBuf);
        }
    }

    // 4. Réception onReady() : réconciliation uniquement si VoiceKit confirme le fournisseur cible
    if (hasReady) {
        if (m_profileOp == ProfileOpState::APPLYING) {
            StationSwitch::ReadyDecision dec = StationSwitch::evaluateOnReadyDuringSwitch(
                m_profileOp,
                m_voice.isProfileChangePending(),
                toSwitchSessionState(m_voice.getSession().getState()),
                toSwitchProvider(m_voice.getProviderType()),
                toSwitchProvider(m_pendingProfile.provider));
            if (dec == StationSwitch::ReadyDecision::CONFIRM_SWITCH) {
                finishProfileSuccess(m_voice.getVoiceProfile(), "onReady");
            } else {
                logSwitchState("Ignored stale onReady during APPLYING");
            }
        } else if (m_profileOp == ProfileOpState::IDLE &&
                   m_voice.getSession().getState() == LiveSessionState::CONNECTED) {
            // Garder m_activeProfile fidèle au fournisseur réellement connecté
            m_activeProfile = m_voice.getVoiceProfile();
        }
    }
}

bool StationVoiceController::beginProfileApply(uint32_t nowMs) {
    if (m_voice.isConnecting() || m_voice.isSessionDisconnecting() || m_voice.isBusy()) {
        logSwitchState("Result: FAILED", "Session en transition");
        finishProfileFailure("Session en transition");
        return false;
    }

    bool connectNow = isNetworkReadyForOnlineApply() &&
                      m_voice.hasProviderApiKey(m_pendingProfile.provider);

    Serial.printf("[%lu][SWITCH] Applying %s profile (voice=%s, lang=%s, connectNow=%s)\n",
                  (unsigned long)millis(),
                  providerLabel(m_pendingProfile.provider),
                  m_pendingProfile.voiceId.c_str(),
                  m_pendingProfile.language.c_str(),
                  connectNow ? "YES" : "NO");

    // IMPORTANT (v0.1.0.1) : armer APPLYING *AVANT* d'appeler applyVoiceProfile()
    // afin qu'un callback synchrone (ex. hasSameAcousticConfig) ou très rapide sur websocket_task
    // ne soit jamais manqué ni écrasé par un retour ultérieur de beginProfileApply().
    if (connectNow) {
        m_profileOp = ProfileOpState::APPLYING;
        m_profileOpStartMs = nowMs;
        m_lastLoggedSwitchSessionState = m_voice.getSession().getState();
        m_lastLoggedSwitchVoiceState = m_voice.getState();
    }

    if (!m_voice.applyVoiceProfile(m_pendingProfile, connectNow)) {
        String err = m_voice.getLastError();
        m_voice.resumeAutoReconnect();
        m_activeProfile = m_voice.getVoiceProfile();
        const char* errText = err.isEmpty() ? "Échec application" : err.c_str();
        // Traiter les événements synchrones éventuels avant de finaliser
        processDeferredSwitchEvents(nowMs);
        if (m_profileOp != ProfileOpState::IDLE || !m_profileResultReady) {
            logSwitchState("Result: FAILED", errText);
            finishProfileFailure(errText);
        }
        return false;
    }

    if (!connectNow) {
        // Hors ligne : profil mémorisé localement immédiatement
        m_activeProfile = m_voice.getVoiceProfile();
        m_profileOp = ProfileOpState::IDLE;
        m_pendingIsProviderSwitch = false;
        m_profileResultReady = true;
        m_profileResultSuccess = true;
        strncpy(m_profileResultMsg, "Profil mis à jour (hors ligne)", sizeof(m_profileResultMsg) - 1);
        m_profileResultMsg[sizeof(m_profileResultMsg) - 1] = '\0';
        logSwitchState("Result: SUCCESS", "Offline profile updated");
        return true;
    }

    Serial.printf("[%lu][SWITCH] %s connection started (Voice=%s, Session=%s, Op=%s)\n",
                  (unsigned long)millis(),
                  providerLabel(m_pendingProfile.provider),
                  voiceStateToString(m_voice.getState()),
                  liveSessionStateToString(m_voice.getSession().getState()),
                  StationSwitch::opStateToString(m_profileOp));

    // Si VoiceKit a déjà émis onProfileApplied de manière synchrone (ex. profil acoustique identique),
    // le consommer immédiatement sans attendre le prochain cycle.
    processDeferredSwitchEvents(nowMs);
    return true;
}

bool StationVoiceController::requestProfileUpdate(const VoiceProfile& target, bool isProviderSwitch) {
    const uint32_t nowMs = millis();
    const bool hasKey = m_voice.hasProviderApiKey(target.provider);
    const bool engineIdle = isEngineIdleForTransition();

    Serial.printf("[%lu][SWITCH] Request: %s -> %s (isSwitch=%s, Voice=%s, Session=%s, Op=%s)\n",
                  (unsigned long)nowMs,
                  providerLabel(m_activeProfile.provider),
                  providerLabel(target.provider),
                  isProviderSwitch ? "YES" : "NO",
                  voiceStateToString(m_voice.getState()),
                  liveSessionStateToString(m_voice.getSession().getState()),
                  StationSwitch::opStateToString(m_profileOp));
    Serial.printf("[%lu][SWITCH] Target API key: %s | Stop required: %s\n",
                  (unsigned long)nowMs,
                  hasKey ? "PRESENT" : "ABSENT",
                  engineIdle ? "NO" : "YES");

    StationSwitch::RequestAction action = StationSwitch::decideSwitchAction(
        m_profileOp,
        m_voice.isProfileChangePending(),
        m_stopActive,
        isProviderSwitch,
        toSwitchProvider(m_activeProfile.provider),
        toSwitchProvider(target.provider),
        hasKey,
        engineIdle,
        toSwitchSessionState(m_voice.getSession().getState()),
        m_voice.isConnecting(),
        m_voice.isSessionDisconnecting());

    if (action == StationSwitch::RequestAction::REJECT_BUSY) {
        logSwitchState("Result: REJECTED", "Opération déjà en cours");
        finishProfileFailure("Opération déjà en cours");
        return false;
    }

    if (action == StationSwitch::RequestAction::ALREADY_ACTIVE) {
        const char* msg = (target.provider == VoiceProviderType::GEMINI) ? "Gemini déjà actif" : "OpenAI déjà actif";
        m_profileResultReady = true;
        m_profileResultSuccess = true;
        strncpy(m_profileResultMsg, msg, sizeof(m_profileResultMsg) - 1);
        m_profileResultMsg[sizeof(m_profileResultMsg) - 1] = '\0';
        logSwitchState("Result: NOOP", msg);
        return true;
    }

    if (action == StationSwitch::RequestAction::REJECT_NO_KEY) {
        const char* err = (target.provider == VoiceProviderType::OPENAI)
                              ? "Clé OpenAI absente"
                              : "Clé Gemini absente";
        logSwitchState("Result: FAILED", err);
        finishProfileFailure(err);
        return false;
    }

    // Purger d'éventuels anciens événements résiduels avant d'engager la nouvelle opération
    portENTER_CRITICAL(&m_mux);
    m_evtProfileApplied = false;
    m_evtProfileError = false;
    m_evtVoiceError = false;
    m_evtVoiceReady = false;
    portEXIT_CRITICAL(&m_mux);

    m_pendingProfile = target;
    m_pendingIsProviderSwitch = isProviderSwitch;
    m_profileResultReady = false;
    m_disconnectSettleMs = 0;

    if (action == StationSwitch::RequestAction::NEED_STOP) {
        m_interaction.triggerStop();
        m_profileOp = ProfileOpState::PENDING_STOP;
        m_profileOpStartMs = nowMs;
        logSwitchState("Waiting for engine idle (PENDING_STOP)");
        return true;
    }

    if (action == StationSwitch::RequestAction::NEED_DISCONNECT) {
        logSwitchState("Disconnect requested (session was connecting/reconnecting)");
        m_voice.requestDisconnectSession();
        m_profileOp = ProfileOpState::DISCONNECTING_SESSION;
        m_profileOpStartMs = nowMs;
        return true;
    }

    // Cas nominal (CONNECTED / DISCONNECTED / ERROR) : délégation directe à VoxCoreVoice::applyVoiceProfile()
    return beginProfileApply(nowMs);
}

bool StationVoiceController::switchProvider(VoiceProviderType newProvider) {
    VoiceProfile target = m_activeProfile;
    target.provider = newProvider;
    if (!m_voice.getProfileManager().isValidVoiceForProvider(newProvider, target.voiceId)) {
        target.voiceId = m_voice.getProfileManager().getDefaultVoiceForProvider(newProvider, VoiceGender::ANY);
    }
    return requestProfileUpdate(target, true);
}

bool StationVoiceController::setLanguage(const String& bcp47) {
    VoiceProfile target = m_activeProfile;
    target.language = bcp47;
    return requestProfileUpdate(target, false);
}

bool StationVoiceController::setVoice(const String& voiceId) {
    VoiceProfile target = m_activeProfile;
    target.voiceId = voiceId;
    return requestProfileUpdate(target, false);
}

void StationVoiceController::setVolume(uint8_t volumePercent) {
    m_voice.setVolume(volumePercent);
}

bool StationVoiceController::saveProfileToNvs() {
    if (isProfileChangeInProgress()) return false;
    return m_voice.getProfileManager().saveActiveProfile(m_activeProfile);
}

std::vector<VoiceDescriptor> StationVoiceController::getAvailableVoices() const {
    return m_voice.getProfileManager().getVoicesForProvider(m_activeProfile.provider, VoiceGender::ANY);
}

std::vector<LanguageDescriptor> StationVoiceController::getAvailableLanguages() const {
    return m_voice.getProfileManager().getSupportedLanguages();
}

bool StationVoiceController::consumeStatusMessage(String& outMsg) {
    if (!m_hasPendingStatus) return false;
    char buf[128];
    portENTER_CRITICAL(&m_mux);
    strncpy(buf, m_pendingStatus, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    m_hasPendingStatus = false;
    portEXIT_CRITICAL(&m_mux);
    outMsg = String(buf);
    return true;
}

bool StationVoiceController::consumeLastInteractionText(String& outText) {
    if (!m_hasPendingInteraction) return false;
    char buf[256];
    portENTER_CRITICAL(&m_mux);
    strncpy(buf, m_pendingInteraction, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    m_hasPendingInteraction = false;
    portEXIT_CRITICAL(&m_mux);
    outText = String(buf);
    return true;
}

bool StationVoiceController::consumeProfileResult(bool& outSuccess, String& outMsg) {
    if (!m_profileResultReady) return false;
    m_profileResultReady = false;
    outSuccess = m_profileResultSuccess;
    outMsg = String(m_profileResultMsg);
    return true;
}
