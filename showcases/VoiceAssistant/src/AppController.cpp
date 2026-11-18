/**
 * @file AppController.cpp
 * @brief Implémentation du contrôleur applicatif VoiceAssistant v0.1.0.
 */

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #include "secrets.example.h"
#endif

#include "AppController.h"
#include <WiFi.h>

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

AppController::AppController()
    : m_interaction(m_voice, m_interactionConfig)
{
#if defined(USE_OPENAI) && !defined(USE_GEMINI)
    // Sélection par défaut OpenAI Realtime si demandé dans secrets.h
    m_activeProfile.provider = VoiceProviderType::OPENAI;
    m_activeProfile.language = AppConfig::DEFAULT_LANGUAGE;
    m_activeProfile.voiceId = AppConfig::OPENAI_VOICE;
#else
    // Profil par défaut initialisé sur Gemini Live
    m_activeProfile.provider = VoiceProviderType::GEMINI;
    m_activeProfile.language = AppConfig::DEFAULT_LANGUAGE;
    m_activeProfile.voiceId = AppConfig::GEMINI_VOICE;
#endif
    m_activeProfile.preferredGender = VoiceGender::ANY;
    m_activeProfile.style = VoiceStyle::NATURAL;
    m_activeProfile.tone = VoiceTone::NEUTRAL;
}

void AppController::setThreadSafeText(char* dst, size_t dstSize, const char* src, bool& flag) {
    if (!dst || dstSize == 0 || !src) return;
    portENTER_CRITICAL(&m_mux);
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
    flag = true;
    portEXIT_CRITICAL(&m_mux);
}

bool AppController::begin() {
    Serial.println("[APP] Initialisation de VoiceAssistant v0.1.0...");

    // 1. Configuration de VoiceInteractionController
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

    // 2. Callbacks de VoiceInteractionController
    m_interaction.onStateChanged([this](VoiceInteractionState oldSt, VoiceInteractionState newSt) {
        Serial.printf("[PTT] Transition d'état : %s -> %s\n",
                      voiceInteractionStateToString(oldSt), voiceInteractionStateToString(newSt));
        m_stateChanged = true;
    });

    m_interaction.onSilenceDetected([this](const char* reason) {
        Serial.printf("[VAD][REJECT] Silence ou bruit bref rejeté [cause: %s]\n",
                      reason ? reason : "INCONNU");
        char vadBuf[64];
        snprintf(vadBuf, sizeof(vadBuf), "VAD: REJET (%s)", reason ? reason : "SILENCE");
        setThreadSafeText(m_pendingVadText, sizeof(m_pendingVadText), vadBuf, m_hasPendingVad);

        char userBuf[128];
        snprintf(userBuf, sizeof(userBuf), "(Aucune parole détectée : %s)", reason ? reason : "");
        setThreadSafeText(m_pendingUserText, sizeof(m_pendingUserText), userBuf, m_hasPendingUser);
    });

    m_interaction.onCaptureValidated([this](const VoiceCaptureMetrics& m) {
        if (m.isValid) {
            Serial.printf("[VAD][VALID] Parole validée [durée: %u ms, parole: %u ms, peak: %u RMS]\n",
                          (unsigned)m.captureDurationMs, (unsigned)m.cumulativeSpeechMs, (unsigned)m.peakRms);
            char vadBuf[64];
            snprintf(vadBuf, sizeof(vadBuf), "VAD: OK (%ums)", (unsigned)m.cumulativeSpeechMs);
            setThreadSafeText(m_pendingVadText, sizeof(m_pendingVadText), vadBuf, m_hasPendingVad);
        }
    });

    // 3. Restauration du profil vocal depuis NVS si disponible
    VoiceProfile savedProf;
    if (m_voice.getProfileManager().loadActiveProfile(savedProf)) {
        Serial.println("[APP] Profil vocal restauré depuis NVS avec succès.");
        m_activeProfile = savedProf;
    }

    // 4. Configuration VoiceKit
    VoiceKitConfig vConfig;
    vConfig.wifiMode = WiFiMode::MANAGED_WIFI;
    vConfig.wifiSSID = WIFI_SSID;
    vConfig.wifiPassword = WIFI_PASSWORD;
    vConfig.enableFastConnect = true;
    vConfig.volume = AppConfig::DEFAULT_VOLUME;
    vConfig.enableConversation = true;
    vConfig.conversationInstruction = AppConfig::SYSTEM_INSTRUCTION;

    // Enregistrement des deux clés API
    vConfig.geminiApiKey = GEMINI_API_KEY;
    vConfig.openAiApiKey = OPENAI_API_KEY;

    if (m_activeProfile.provider == VoiceProviderType::OPENAI) {
        vConfig.provider = VoiceProviderType::OPENAI;
        vConfig.apiKey = OPENAI_API_KEY;
        vConfig.model = AppConfig::OPENAI_MODEL;
        vConfig.voice = m_activeProfile.voiceId.isEmpty() ? AppConfig::OPENAI_VOICE : m_activeProfile.voiceId;
        vConfig.language = m_activeProfile.language;
    } else {
        vConfig.provider = VoiceProviderType::GEMINI;
        vConfig.apiKey = GEMINI_API_KEY;
        vConfig.model = AppConfig::GEMINI_MODEL;
        vConfig.voice = m_activeProfile.voiceId.isEmpty() ? AppConfig::GEMINI_VOICE : m_activeProfile.voiceId;
        vConfig.language = m_activeProfile.language;
    }

    // 5. Relais explicite des callbacks VoxCoreVoice vers VoiceInteractionController
    m_voice.onAudioChunk([this](const int16_t* samples, size_t count, uint32_t rms) {
        m_interaction.processAudioChunk(samples, count, rms);
    });

    m_voice.onReady([this]() {
        Serial.println("[VOICE] Session conversationnelle prête et connectée.");
        m_interaction.notifyReady();
        setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), "Assistant prêt", m_hasPendingStatus);
    });

    m_voice.onStateChanged([this](VoiceState st) {
        m_interaction.notifyVoiceStateChanged(st);
    });

    m_voice.onListeningStarted([this]() {
        Serial.println("[PTT] Début écoute micro ES7210");
        m_interaction.notifyListeningStarted();
    });

    m_voice.onListeningFinished([this]() {
        Serial.println("[PTT] Fin écoute micro ES7210");
        m_interaction.notifyListeningFinished();
    });

    m_voice.onSpeechStarted([this]() {
        Serial.println("[VOICE] Début restitution vocale haut-parleur AW88298");
        m_interaction.notifySpeechStarted();
    });

    m_voice.onSpeechFinished([this]() {
        Serial.println("[VOICE] Fin restitution vocale");
        m_interaction.notifySpeechFinished();
    });

    m_voice.onError([this](const String& err) {
        Serial.printf("[ERROR] VoiceKit : %s\n", err.c_str());
        m_interaction.notifyError(err);
        setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), err.c_str(), m_hasPendingStatus);
    });

    m_voice.onProfileApplied([this](const VoiceProfile& prof) {
        Serial.printf("[VOICE] Nouveau profil confirmé par le serveur : [%s] %s (%s)\n",
                      prof.provider == VoiceProviderType::GEMINI ? "Gemini" : "OpenAI",
                      prof.voiceId.c_str(), prof.language.c_str());
        m_activeProfile = prof;
        setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), "Profil actif appliqué", m_hasPendingStatus);
    });

    // Callbacks de texte conversationnel
    m_voice.onUserText([this](const String& text, bool isFinal) {
        if (m_interaction.isLastTurnCancelled()) {
            return; // Discard late callbacks following STOP
        }
        if (isFinal) {
            Serial.printf("[USER] %s\n", text.c_str());
            m_lastUserText = text;
        }
        setThreadSafeText(m_pendingUserText, sizeof(m_pendingUserText), text.c_str(), m_hasPendingUser);
    });

    m_voice.onAssistantText([this](const String& text) {
        if (m_interaction.isLastTurnCancelled()) {
            return; // Discard late callbacks following STOP
        }
        Serial.printf("[VOICE][AI] %s\n", text.c_str());
        m_lastAssistantText = text;
        recordTurnInHistory(m_lastUserText, text);
        setThreadSafeText(m_pendingAssistantText, sizeof(m_pendingAssistantText), text.c_str(), m_hasPendingAssistant);
    });

    // 6. Démarrage effectif du moteur vocal
    if (!m_voice.begin(vConfig)) {
        Serial.printf("[ERROR] Échec voice.begin() : %s\n", m_voice.getLastError().c_str());
        m_interaction.notifyError(m_voice.getLastError());
        return false;
    }

    Serial.println("[APP] Initialisation achevée. Connexion Wi-Fi et IA en cours...");
    return true;
}

void AppController::update(uint32_t nowMs) {
    m_voice.update();
    m_interaction.update(nowMs);

    // Surveillance périodique de l'état réseau et session toutes les 500 ms
    if (nowMs - m_lastConnCheckMs >= 500) {
        m_lastConnCheckMs = nowMs;

        bool wifiCur = (WiFi.status() == WL_CONNECTED);
        bool sessionCur = (m_voice.getSession().getState() == LiveSessionState::CONNECTED);

        if (wifiCur != m_wifiWasConnected) {
            m_wifiWasConnected = wifiCur;
            if (!wifiCur) {
                Serial.println("[NET] Perte de connexion Wi-Fi !");
                setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), "Wi-Fi déconnecté", m_hasPendingStatus);
            } else {
                Serial.println("[NET] Wi-Fi connecté.");
                setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), "Wi-Fi connecté", m_hasPendingStatus);
            }
        }

        if (sessionCur != m_sessionWasConnected) {
            m_sessionWasConnected = sessionCur;
            if (sessionCur) {
                Serial.println("[NET] Session IA établie et prête.");
            }
        }
    }
}

void AppController::onTalkClick() {
    Serial.println("[PTT] Bouton PARLER / TERMINER actionné");
    m_interaction.onTalkClick();
}

void AppController::triggerStop() {
    Serial.println("[PTT] Bouton STOP prioritaire actionné");
    m_interaction.triggerStop();
    setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), "Arrêt demandé", m_hasPendingStatus);
}

const char* AppController::getStateString() const {
    return voiceInteractionStateToString(m_interaction.getState());
}

const char* AppController::getStateFrenchLabel() const {
    switch (m_interaction.getState()) {
        case VoiceInteractionState::DISABLED:
            return "Non disponible";
        case VoiceInteractionState::READY:
            return "Assistant prêt";
        case VoiceInteractionState::STARTING:
            return "Ouverture du micro...";
        case VoiceInteractionState::LISTENING:
            return "Écoute en cours...";
        case VoiceInteractionState::STOPPING_CAPTURE:
            return "Validation parole...";
        case VoiceInteractionState::PROCESSING:
            return "Réflexion en cours...";
        case VoiceInteractionState::SPEAKING:
            return "Réponse vocale...";
        case VoiceInteractionState::INTERRUPTING:
            return "Arrêt en cours...";
        case VoiceInteractionState::RECOVERING:
            return "Reconnexion / reprise...";
        case VoiceInteractionState::ERROR_STATE:
            return "Erreur";
        default:
            return "En attente";
    }
}

bool AppController::isWifiConnected() const {
    return (WiFi.status() == WL_CONNECTED);
}

bool AppController::isSessionConnected() const {
    return (m_voice.getSession().getState() == LiveSessionState::CONNECTED);
}

const char* AppController::getProviderName() const {
    return (m_activeProfile.provider == VoiceProviderType::GEMINI) ? "Gemini Live" : "OpenAI Realtime";
}

bool AppController::switchProvider(VoiceProviderType newProvider) {
    if (m_activeProfile.provider == newProvider) {
        return true;
    }

    Serial.printf("[APP] Demande de basculement vers fournisseur : %s\n",
                  newProvider == VoiceProviderType::GEMINI ? "Gemini Live" : "OpenAI Realtime");

    // Arrêt prioritaire si une interaction est active
    m_interaction.triggerStop();

    // Vérifier la présence d'une clé API valide
    if (newProvider == VoiceProviderType::OPENAI && !m_voice.hasProviderApiKey(VoiceProviderType::OPENAI)) {
        Serial.println("[ERROR] Clé API OpenAI absente.");
        setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), "Clé OpenAI manquante", m_hasPendingStatus);
        return false;
    }
    if (newProvider == VoiceProviderType::GEMINI && !m_voice.hasProviderApiKey(VoiceProviderType::GEMINI)) {
        Serial.println("[ERROR] Clé API Gemini absente.");
        setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), "Clé Gemini manquante", m_hasPendingStatus);
        return false;
    }

    VoiceProfile targetProfile = m_activeProfile;
    targetProfile.provider = newProvider;

    // Sélection de la première voix compatible si la voix courante n'appartient pas au nouveau fournisseur
    if (!m_voice.getProfileManager().isValidVoiceForProvider(newProvider, targetProfile.voiceId)) {
        targetProfile.voiceId = m_voice.getProfileManager().getDefaultVoiceForProvider(newProvider, VoiceGender::ANY);
    }

    setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), "Connexion fournisseur...", m_hasPendingStatus);

    if (!m_voice.applyVoiceProfile(targetProfile, true)) {
        Serial.printf("[ERROR] Impossible de changer de fournisseur : %s\n", m_voice.getLastError().c_str());
        setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), m_voice.getLastError().c_str(), m_hasPendingStatus);
        return false;
    }

    m_activeProfile = targetProfile;
    return true;
}

bool AppController::setLanguage(const String& bcp47) {
    if (m_activeProfile.language.equalsIgnoreCase(bcp47)) {
        return true;
    }
    Serial.printf("[APP] Changement de langue vocale vers : %s\n", bcp47.c_str());

    VoiceProfile targetProfile = m_activeProfile;
    targetProfile.language = bcp47;

    if (!m_voice.applyVoiceProfile(targetProfile, true)) {
        Serial.printf("[ERROR] Impossible d'appliquer la langue : %s\n", m_voice.getLastError().c_str());
        return false;
    }
    m_activeProfile.language = bcp47;
    return true;
}

bool AppController::setVoice(const String& voiceId) {
    if (m_activeProfile.voiceId.equalsIgnoreCase(voiceId)) {
        return true;
    }
    Serial.printf("[APP] Changement de voix vers : %s\n", voiceId.c_str());

    VoiceProfile targetProfile = m_activeProfile;
    targetProfile.voiceId = voiceId;

    if (!m_voice.applyVoiceProfile(targetProfile, true)) {
        Serial.printf("[ERROR] Impossible d'appliquer la voix : %s\n", m_voice.getLastError().c_str());
        return false;
    }
    m_activeProfile.voiceId = voiceId;
    return true;
}

void AppController::setVolume(uint8_t volumePercent) {
    m_voice.setVolume(volumePercent);
    Serial.printf("[APP] Volume réglé à %u %%\n", (unsigned)m_voice.getVolume());
}

bool AppController::saveProfileToNvs() {
    bool ok = m_voice.getProfileManager().saveActiveProfile(m_activeProfile);
    if (ok) {
        Serial.println("[APP] Profil vocal enregistré avec succès en NVS (sans clé API).");
        setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), "Profil sauvé en NVS", m_hasPendingStatus);
    } else {
        Serial.println("[ERROR] Échec de sauvegarde du profil vocal en NVS.");
        setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), "Erreur sauvegarde NVS", m_hasPendingStatus);
    }
    return ok;
}

std::vector<VoiceDescriptor> AppController::getAvailableVoices() const {
    return m_voice.getProfileManager().getVoicesForProvider(m_activeProfile.provider, VoiceGender::ANY);
}

std::vector<LanguageDescriptor> AppController::getAvailableLanguages() const {
    return m_voice.getProfileManager().getSupportedLanguages();
}

void AppController::clearHistory() {
    m_history.clear();
    m_lastUserText = "";
    m_lastAssistantText = "";
    Serial.println("[APP] Historique conversationnel local effacé.");
    setThreadSafeText(m_pendingStatusMsg, sizeof(m_pendingStatusMsg), "Historique effacé", m_hasPendingStatus);
}

void AppController::recordTurnInHistory(const String& user, const String& assistant) {
    HistoryItem item;
    item.userText = user;
    item.assistantText = assistant;
    item.timestampMs = millis();

    if (m_history.size() >= MAX_HISTORY) {
        m_history.erase(m_history.begin());
    }
    m_history.push_back(item);
}

bool AppController::consumeNewUserText(String& outText) {
    if (!m_hasPendingUser) return false;
    char buf[512];
    portENTER_CRITICAL(&m_mux);
    strncpy(buf, m_pendingUserText, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    m_hasPendingUser = false;
    portEXIT_CRITICAL(&m_mux);
    outText = String(buf);
    return true;
}

bool AppController::consumeNewAssistantText(String& outText) {
    if (!m_hasPendingAssistant) return false;
    char buf[512];
    portENTER_CRITICAL(&m_mux);
    strncpy(buf, m_pendingAssistantText, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    m_hasPendingAssistant = false;
    portEXIT_CRITICAL(&m_mux);
    outText = String(buf);
    return true;
}

bool AppController::consumeNewVadText(String& outVad) {
    if (!m_hasPendingVad) return false;
    char buf[64];
    portENTER_CRITICAL(&m_mux);
    strncpy(buf, m_pendingVadText, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    m_hasPendingVad = false;
    portEXIT_CRITICAL(&m_mux);
    outVad = String(buf);
    return true;
}

bool AppController::consumeNewStatusMessage(String& outMsg) {
    if (!m_hasPendingStatus) return false;
    char buf[128];
    portENTER_CRITICAL(&m_mux);
    strncpy(buf, m_pendingStatusMsg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    m_hasPendingStatus = false;
    portEXIT_CRITICAL(&m_mux);
    outMsg = String(buf);
    return true;
}

bool AppController::hasStateChanged(VoiceInteractionState& outState) {
    VoiceInteractionState cur = m_interaction.getState();
    if (cur != m_lastReportedState || m_stateChanged) {
        m_lastReportedState = cur;
        m_stateChanged = false;
        outState = cur;
        return true;
    }
    return false;
}
