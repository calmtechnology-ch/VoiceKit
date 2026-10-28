#include "VoiceKit.h"

VoiceKit::VoiceKit() {
    _continuousController.begin(&_voice);
}

VoiceKit::~VoiceKit() {
    stop();
}

bool VoiceKit::begin(const VoiceKitConfig& config) {
    _continuousController.begin(&_voice);

    // Configuration des canaux de délégation des callbacks
    _voice.onReady([this]() {
        if (_onReady) {
            _onReady();
        }
    });

    _voice.onError([this](const String& err) {
        if (_onError) {
            _onError(err);
        }
    });

    _voice.onSpeechStarted([this]() {
        if (_onSpeechStarted) {
            _onSpeechStarted();
        }
    });

    _voice.onSpeechFinished([this]() {
        if (_onSpeechFinished) {
            _onSpeechFinished();
        }
    });

    _voice.onTranscription([this](const String& text, bool isFinal) {
        if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING) {
            _continuousController.handleTranscription(text, isFinal);
        }
        if (_onTranscription) {
            _onTranscription(text, isFinal);
        }
    });

    _voice.onConversationFinished([this]() {
        if (_onConversationFinished) {
            _onConversationFinished();
        }
    });

    _voice.onUserText([this](const String& text, bool isFinal) {
        if (_onUserText) {
            _onUserText(text, isFinal);
        }
        // En mode conversation, relayer également vers onTranscription pour uniformité
        if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONVERSATION && _onTranscription) {
            _onTranscription(text, isFinal);
        }
    });

    _voice.onAssistantText([this](const String& text) {
        if (_onAssistantText) {
            _onAssistantText(text);
        }
    });

    _voice.onListeningStarted([this]() {
        if (_onListeningStarted) {
            _onListeningStarted();
        }
    });

    _voice.onListeningFinished([this]() {
        if (_onListeningFinished) {
            _onListeningFinished();
        }
    });

    _voice.onStateChanged([this](VoiceState state) {
        if (_onStateChanged) {
            _onStateChanged(state);
        }
    });

    _voice.onConversationTurnMetrics([this](const ConversationMetrics& m) {
        if (_onConversationTurnMetrics) {
            _onConversationTurnMetrics(m);
        }
    });

    // Branchement audio non bloquant dans la tâche FreeRTOS audio
    _voice.onAudioChunk([this](const int16_t* samples, size_t count, uint32_t rms) {
        if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING) {
            _continuousController.processAudioChunk(samples, count, rms);
        }
        if (_userAudioChunk) {
            _userAudioChunk(samples, count, rms);
        }
    });

    if (config.vadSilenceDurationMs > 0) {
        _continuousController.setSilenceDurationMs(config.vadSilenceDurationMs);
    }

    bool ok = _voice.begin(config);

    // Surcharge du callback de vérification d'occupation pour inclure ContinuousSTT
    _voice.getWakeWordManager().setVoiceBusyCheckCallback([this]() {
        return this->isBusy() || this->isSpeaking() ||
               (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING &&
                !_continuousController.isPaused());
    });

    if (config.enableConversation) {
        _mode.store(VoiceKitMode::CONVERSATION, std::memory_order_release);
    } else {
        _mode.store(VoiceKitMode::IDLE, std::memory_order_release);
    }

    return ok;
}

void VoiceKit::update() {
    // Si la dictée continue est active (et non en pause), maintenir la suspension Wake Word
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING &&
        !_continuousController.isPaused()) {
        _voice.getWakeWordManager().notifyVoiceActivity(true, isSpeaking());
    }

    _voice.update();

    // Synchronisation du mode opérationnel exposé avec VoxCoreVoice une fois la transition achevée
    if (_mode.load(std::memory_order_relaxed) != VoiceKitMode::CONTINUOUS_LISTENING) {
        if (!_voice.isModeSwitchPending()) {
            VoiceKitMode confirmedMode = _voice.isConversationEnabled() ? VoiceKitMode::CONVERSATION : VoiceKitMode::IDLE;
            _mode.store(confirmedMode, std::memory_order_release);
        }
    }

    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING) {
        _continuousController.update();
    }
}

void VoiceKit::stop() {
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING) {
        _continuousController.stop();
        _mode.store(VoiceKitMode::IDLE, std::memory_order_release);
    }
    _voice.stop();
}

bool VoiceKit::isReady() const {
    return _voice.isReady();
}

bool VoiceKit::isBusy() const {
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING) {
        return (_continuousController.isBusy() || _voice.isBusy());
    }
    return _voice.isBusy();
}

VoiceState VoiceKit::getState() const {
    return _voice.getState();
}

String VoiceKit::getLastError() const {
    return _voice.getLastError();
}

void VoiceKit::setVolume(uint8_t percent) {
    _voice.setVolume(percent);
}

uint8_t VoiceKit::getVolume() const {
    return _voice.getVolume();
}

bool VoiceKit::say(const String& text) {
    // Refus strict si un autre mode incompatible est en cours ou en transition
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING ||
        _mode.load(std::memory_order_relaxed) == VoiceKitMode::CONVERSATION ||
        _voice.isConversationEnabled() ||
        _voice.isModeSwitchPending() ||
        _voice.isConnecting()) {
        return false;
    }
    if (_voice.isBusy()) {
        return false;
    }
    return _voice.say(text);
}

bool VoiceKit::isSpeaking() const {
    return _voice.isSpeaking();
}

bool VoiceKit::listen() {
    // Refus strict si mode continu ou conversationnel actif ou en transition
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING ||
        _mode.load(std::memory_order_relaxed) == VoiceKitMode::CONVERSATION ||
        _voice.isConversationEnabled() ||
        _voice.isModeSwitchPending() ||
        _voice.isConnecting()) {
        return false;
    }
    if (!_voice.isReady() || _voice.isBusy()) {
        return false;
    }
    return _voice.startListening();
}

bool VoiceKit::stopListening() {
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING) {
        return false;
    }
    if (!_voice.isListening()) {
        return false;
    }
    return _voice.stopListening();
}

void VoiceKit::cancelListening() {
    _voice.cancelListening();
}

bool VoiceKit::isListening() const {
    return _voice.isListening();
}

bool VoiceKit::isTranscribing() const {
    return _voice.isTranscribing();
}

bool VoiceKit::startContinuousListening() {
    // Refus strict si mode conversationnel actif ou moteur occupé en parole/écoute
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONVERSATION || _voice.isConversationEnabled()) {
        return false;
    }
    if (_voice.isBusy()) {
        return false;
    }
    if (_voice.isModeSwitchPending()) {
        return false;
    }
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING) {
        return true;
    }

    bool started = _continuousController.start();
    if (started) {
        _mode.store(VoiceKitMode::CONTINUOUS_LISTENING, std::memory_order_release);
        _voice.getWakeWordManager().notifyVoiceActivity(true, isSpeaking());
        return true;
    }
    return false;
}

bool VoiceKit::stopContinuousListening() {
    if (_mode.load(std::memory_order_relaxed) != VoiceKitMode::CONTINUOUS_LISTENING) {
        return false;
    }
    bool stopped = _continuousController.stop();
    _mode.store(VoiceKitMode::IDLE, std::memory_order_release);
    _voice.getWakeWordManager().notifyVoiceActivity(isBusy(), isSpeaking());
    return stopped;
}

bool VoiceKit::pauseContinuousListening() {
    if (_mode.load(std::memory_order_relaxed) != VoiceKitMode::CONTINUOUS_LISTENING) {
        return false;
    }
    _continuousController.pause();
    _voice.getWakeWordManager().notifyVoiceActivity(isBusy(), isSpeaking());
    return true;
}

bool VoiceKit::resumeContinuousListening() {
    if (_mode.load(std::memory_order_relaxed) != VoiceKitMode::CONTINUOUS_LISTENING) {
        return false;
    }
    bool resumed = _continuousController.resume();
    if (resumed) {
        _voice.getWakeWordManager().notifyVoiceActivity(true, isSpeaking());
    }
    return resumed;
}

bool VoiceKit::startConversation() {
    // Refus strict si reconnaissance continue active ou moteur occupé
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING) {
        return false;
    }
    if (_voice.isBusy()) {
        return false;
    }
    // Refus strict si connexion ou transition déjà en cours (évite double reconnexion)
    if (_voice.isConnecting() || _voice.isModeSwitchPending()) {
        return false;
    }
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONVERSATION && _voice.isConversationEnabled()) {
        return true;
    }

    bool ok = _voice.enableConversation(true);
    if (!ok) {
        return false;
    }

    // Si hors ligne, la transition est confirmée immédiatement
    if (!_voice.isModeSwitchPending()) {
        _mode.store(VoiceKitMode::CONVERSATION, std::memory_order_release);
    }
    // Si en ligne, demande acceptée, le mode sera confirmé à réception de setupComplete
    return true;
}

bool VoiceKit::stopConversation() {
    if (_mode.load(std::memory_order_relaxed) != VoiceKitMode::CONVERSATION && !_voice.isConversationEnabled()) {
        return false;
    }
    // Refus strict si le moteur est occupé par un tour ou en cours de connexion/transition
    if (_voice.isBusy() || _voice.isConnecting() || _voice.isModeSwitchPending()) {
        return false;
    }

    _voice.stop();
    bool ok = _voice.enableConversation(false);
    if (!ok) {
        // En cas d'échec de la désactivation, ne jamais forcer IDLE : conserver le dernier mode valide
        return false;
    }

    // Si hors ligne, la transition est confirmée immédiatement
    if (!_voice.isModeSwitchPending()) {
        _mode.store(VoiceKitMode::IDLE, std::memory_order_release);
    }
    // Si en ligne, demande acceptée, le mode sera confirmé à réception de setupComplete
    return true;
}

bool VoiceKit::enableWakeWord(bool enable) {
    if (enable) {
        bool voiceActive = isBusy() || isSpeaking() ||
                           (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING &&
                            !_continuousController.isPaused());
        _voice.getWakeWordManager().notifyVoiceActivity(voiceActive, isSpeaking());
    }
    return _voice.enableWakeWord(enable);
}

void VoiceKit::disableWakeWord() {
    _voice.disableWakeWord();
}

bool VoiceKit::suspendWakeWord() {
    return _voice.suspendWakeWord();
}

bool VoiceKit::resumeWakeWord() {
    bool voiceActive = isBusy() || isSpeaking() ||
                       (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING &&
                        !_continuousController.isPaused());
    _voice.getWakeWordManager().notifyVoiceActivity(voiceActive, isSpeaking());
    return _voice.resumeWakeWord();
}

bool VoiceKit::setVoiceProfile(const String& voice, const String& language,
                               VoiceStyle style, VoiceTone tone, const String& accent) {
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING && _continuousController.isBusy()) {
        return false;
    }
    return _voice.setVoiceProfile(voice, language, style, tone, accent);
}

bool VoiceKit::setVoice(const String& voice) {
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING && _continuousController.isBusy()) {
        return false;
    }
    return _voice.setVoice(voice);
}

bool VoiceKit::setLanguage(const String& language) {
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING && _continuousController.isBusy()) {
        return false;
    }
    return _voice.setLanguage(language);
}

bool VoiceKit::setStyle(VoiceStyle style) {
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING && _continuousController.isBusy()) {
        return false;
    }
    return _voice.setStyle(style);
}

bool VoiceKit::setTone(VoiceTone tone) {
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING && _continuousController.isBusy()) {
        return false;
    }
    return _voice.setTone(tone);
}

bool VoiceKit::setAccent(const String& accent) {
    if (_mode.load(std::memory_order_relaxed) == VoiceKitMode::CONTINUOUS_LISTENING && _continuousController.isBusy()) {
        return false;
    }
    return _voice.setAccent(accent);
}

String VoiceKit::getVoice() const {
    return _voice.getVoice();
}

String VoiceKit::getLanguage() const {
    return _voice.getLanguage();
}

VoiceStyle VoiceKit::getStyle() const {
    return _voice.getStyle();
}

VoiceTone VoiceKit::getTone() const {
    return _voice.getTone();
}

String VoiceKit::getAccent() const {
    return _voice.getAccent();
}

void VoiceKit::setVadSilenceDurationMs(uint32_t ms) {
    if (ms > 0) {
        _continuousController.setSilenceDurationMs(ms);
    }
}

uint32_t VoiceKit::getVadSilenceDurationMs() const {
    return _continuousController.getVadConfig().silenceDurationMs;
}
