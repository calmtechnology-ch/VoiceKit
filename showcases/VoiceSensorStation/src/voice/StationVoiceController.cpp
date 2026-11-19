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
        m_lastError = err;
        Serial.printf("[VOICE][ERROR] %s\n", err.c_str());
        m_interaction.notifyError(err);
        if (m_profileOp == ProfileOpState::APPLYING) {
            finishProfileFailure(err.isEmpty() ? "Échec changement profil" : err.c_str());
        }
        pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus),
                           err.isEmpty() ? "Erreur vocale" : err.c_str(), m_hasPendingStatus);
    });

    m_voice.onProfileApplied([this](const VoiceProfile& prof) {
        m_activeProfile = prof;
        Serial.printf("[VOICE] Profil confirmé : %s | %s | %s\n",
                      getProviderName(), prof.voiceId.c_str(), prof.language.c_str());
        if (m_profileOp == ProfileOpState::APPLYING) {
            bool wasSwitch = m_pendingIsProviderSwitch;
            m_profileOp = ProfileOpState::IDLE;
            m_pendingIsProviderSwitch = false;
            m_profileResultReady = true;
            m_profileResultSuccess = true;
            const char* msg = wasSwitch
                                  ? (prof.provider == VoiceProviderType::GEMINI ? "Gemini actif" : "OpenAI actif")
                                  : "Réglage appliqué";
            strncpy(m_profileResultMsg, msg, sizeof(m_profileResultMsg) - 1);
            m_profileResultMsg[sizeof(m_profileResultMsg) - 1] = '\0';
        }
        pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus),
                           "Assistant prêt", m_hasPendingStatus);
    });

    m_voice.onProfileError([this](const String& err) {
        m_diag.voiceErrorCount++;
        m_lastError = err;
        if (m_profileOp != ProfileOpState::IDLE) {
            finishProfileFailure(err.isEmpty() ? "Erreur profil" : err.c_str());
        }
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

    // 2. Machine d'état non bloquante de bascule de fournisseur / profil (avec timeouts explicites)
    if (m_profileOp == ProfileOpState::PENDING_STOP) {
        if (isEngineIdleForTransition() && !m_stopActive) {
            if (m_pendingIsProviderSwitch ||
                m_voice.isConnecting() ||
                m_voice.getSession().getState() != LiveSessionState::CONNECTED) {
                m_voice.requestDisconnectSession();
                m_profileOp = ProfileOpState::DISCONNECTING_SESSION;
                m_profileOpStartMs = nowMs;
            } else {
                beginProfileApply(nowMs);
            }
        } else if (nowMs - m_profileOpStartMs > 4000) {
            finishProfileFailure("Timeout arrêt vocal");
        }
    } else if (m_profileOp == ProfileOpState::DISCONNECTING_SESSION) {
        bool disconnected = (!m_voice.isSessionDisconnecting() &&
                             !m_voice.isConnecting() &&
                             !m_voice.isBusy() &&
                             m_voice.getSession().getState() == LiveSessionState::DISCONNECTED);
        if (disconnected) {
            if (m_disconnectSettleMs == 0) m_disconnectSettleMs = nowMs;
            if (nowMs - m_disconnectSettleMs >= 180) {
                m_disconnectSettleMs = 0;
                beginProfileApply(nowMs);
            }
        } else {
            m_disconnectSettleMs = 0;
            if (nowMs - m_profileOpStartMs > 5000) {
                m_voice.resumeAutoReconnect();
                finishProfileFailure("Timeout fermeture session");
            }
        }
    } else if (m_profileOp == ProfileOpState::APPLYING) {
        if (nowMs - m_profileOpStartMs > 15000) {
            m_voice.resumeAutoReconnect();
            finishProfileFailure("Délai connexion dépassé");
        }
    }

    // 3. Surveillance réseau légère (toutes les 500 ms)
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
    VoiceInteractionState st = m_interaction.getState();
    if (st == VoiceInteractionState::READY && !m_voice.isSpeaking() && !m_voice.isBusy()) {
        return;
    }

    m_diag.stopInterruptCount++;
    m_lastStopMs = now;
    m_stopActive = true;
    m_stopStableSinceMs = 0;
    m_turnInterrupted = true;

    m_interaction.triggerStop();
    pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus),
                       "Interrompu (STOP)", m_hasPendingStatus);
}

bool StationVoiceController::isWifiConnected() const {
    return (WiFi.status() == WL_CONNECTED);
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

void StationVoiceController::finishProfileFailure(const char* msg) {
    m_profileOp = ProfileOpState::IDLE;
    m_pendingIsProviderSwitch = false;
    m_profileResultReady = true;
    m_profileResultSuccess = false;
    const char* safeMsg = (msg && msg[0] != '\0') ? msg : "Échec modification";
    strncpy(m_profileResultMsg, safeMsg, sizeof(m_profileResultMsg) - 1);
    m_profileResultMsg[sizeof(m_profileResultMsg) - 1] = '\0';
    pushThreadSafeText(m_pendingStatus, sizeof(m_pendingStatus), safeMsg, m_hasPendingStatus);
}

bool StationVoiceController::beginProfileApply(uint32_t nowMs) {
    if (m_voice.isConnecting() || m_voice.isSessionDisconnecting() || m_voice.isBusy()) {
        finishProfileFailure("Session en transition");
        return false;
    }
    bool connectNow = isWifiConnected() && m_voice.hasProviderApiKey(m_pendingProfile.provider);
    if (!m_voice.applyVoiceProfile(m_pendingProfile, connectNow)) {
        String err = m_voice.getLastError();
        m_voice.resumeAutoReconnect();
        finishProfileFailure(err.isEmpty() ? "Échec application" : err.c_str());
        return false;
    }
    if (!connectNow) {
        // Hors ligne : profil mémorisé localement immédiatement
        m_activeProfile = m_pendingProfile;
        m_profileOp = ProfileOpState::IDLE;
        m_pendingIsProviderSwitch = false;
        m_profileResultReady = true;
        m_profileResultSuccess = true;
        strncpy(m_profileResultMsg, "Profil mis à jour (hors ligne)", sizeof(m_profileResultMsg) - 1);
        m_profileResultMsg[sizeof(m_profileResultMsg) - 1] = '\0';
        return true;
    }
    m_profileOp = ProfileOpState::APPLYING;
    m_profileOpStartMs = nowMs;
    return true;
}

bool StationVoiceController::requestProfileUpdate(const VoiceProfile& target, bool isProviderSwitch) {
    if (isProfileChangeInProgress() || m_stopActive) {
        finishProfileFailure("Opération déjà en cours");
        return false;
    }

    if (isProviderSwitch && !m_voice.hasProviderApiKey(target.provider)) {
        finishProfileFailure(target.provider == VoiceProviderType::OPENAI
                                 ? "Clé OpenAI absente"
                                 : "Clé Gemini absente");
        return false;
    }

    m_pendingProfile = target;
    m_pendingIsProviderSwitch = isProviderSwitch;
    m_profileResultReady = false;
    uint32_t nowMs = millis();

    if (!isEngineIdleForTransition()) {
        m_interaction.triggerStop();
        m_profileOp = ProfileOpState::PENDING_STOP;
        m_profileOpStartMs = nowMs;
        return true;
    }

    bool needDisconnect = isProviderSwitch &&
                          (m_voice.getSession().getState() != LiveSessionState::DISCONNECTED ||
                           m_voice.isConnecting() ||
                           m_voice.getProvider().isConnected());
    if (needDisconnect) {
        m_voice.requestDisconnectSession();
        m_profileOp = ProfileOpState::DISCONNECTING_SESSION;
        m_profileOpStartMs = nowMs;
        return true;
    }

    return beginProfileApply(nowMs);
}

bool StationVoiceController::switchProvider(VoiceProviderType newProvider) {
    if (m_activeProfile.provider == newProvider &&
        m_profileOp == ProfileOpState::IDLE &&
        m_voice.getSession().getState() == LiveSessionState::CONNECTED) {
        return true;
    }

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
