#include "VoxCoreVoice.h"
#include <WiFi.h>
#include <time.h>

VoxCoreVoice::VoxCoreVoice() = default;

VoxCoreVoice::~VoxCoreVoice() {
    stop();
}

bool VoxCoreVoice::isSystemTimeValid() {
    time_t now = time(nullptr);
    return (now >= NTP_MIN_VALID_TIMESTAMP);
}

void VoxCoreVoice::setState(VoiceState newState) {
    if (_state != newState) {
        _state = newState;
        if (_onStateChanged) {
            _onStateChanged(_state);
        }
    }
}

bool VoxCoreVoice::begin(const VoiceKitConfig& config) {
    _config = config;
    _lastError = "";

    Serial.println("\n========================================================");
    Serial.println("  VoxCore VoiceKit v0.2.1 — Moteur Audio & TLS VoxCore  ");
    Serial.printf("  Mode Audio : %s\n",
                  (LiveAudioPlayer::getCompiledAudioMode() == 1) ? "MODE 1 (REACTIVE_CALLBACK)" : "MODE 2 (DEDICATED_AUDIO_TASK)");
    Serial.println("========================================================");

    // 1. Initialisation HAL audio matérielle (M5Unified Speaker)
    if (_config.audioOutput) {
        _audioOutput = _config.audioOutput;
    } else {
        _internalAudioOutput = std::unique_ptr<M5AudioOutput>(new M5AudioOutput());
        _audioOutput = _internalAudioOutput.get();
    }

    if (!_audioOutput->begin()) {
        _lastError = "Echec initialisation materielle AudioOutput";
        Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    // 2. Initialisation du lecteur LiveAudioPlayer (tampon PSRAM, tâche audio FreeRTOS)
    _player.setAudioOutput(_audioOutput);
    _player.setVolume(_config.volume);
    _player.setPlaybackAllowed(true);

    if (!_player.begin()) {
        _lastError = "Echec initialisation memoire LiveAudioPlayer";
        Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    // 3. Initialisation du client Gemini Live (protocole BidiGenerateContent)
    if (!_client.begin(_config.apiKey.c_str(), _config.model.c_str(), _config.voice.c_str())) {
        _lastError = _client.getLastError();
        Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    _client.setSystemInstruction(_config.systemInstruction);

    // 4. Initialisation du contrôleur de session LiveSessionController
    if (!_session.begin(&_client, &_player)) {
        _lastError = "Echec initialisation LiveSessionController";
        Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    // Callbacks du contrôleur de session
    _session.setOnStateChanged([this](LiveSessionState s) {
        switch (s) {
            case LiveSessionState::CONNECTED:
                setState(VoiceState::READY);
                if (_onReady) {
                    _onReady();
                }
                break;
            case LiveSessionState::WAITING_RESPONSE:
                setState(VoiceState::WAITING_RESPONSE);
                break;
            case LiveSessionState::PLAYING:
                setState(VoiceState::SPEAKING);
                break;
            case LiveSessionState::ERROR:
                _lastError = _session.getLastError();
                setState(VoiceState::ERROR_STATE);
                if (_onError) {
                    _onError(_lastError);
                }
                break;
            default:
                break;
        }
    });

    _session.setOnFirstAudio([this]() {
        setState(VoiceState::SPEAKING);
        if (_onSpeechStarted) {
            _onSpeechStarted();
        }
    });

    _session.setOnPlaybackFinished([this]() {
        setState(VoiceState::READY);
        if (_onSpeechFinished) {
            _onSpeechFinished();
        }

        // Dépilement de la requête suivante en file FIFO
        if (!_textQueue.empty()) {
            String nextText = _textQueue.front();
            _textQueue.pop();
            dispatchSpeech(nextText);
        }
    });

    _session.setOnError([this](const String& err) {
        _lastError = err;
        setState(VoiceState::ERROR_STATE);
        if (_onError) {
            _onError(err);
        }
    });

    // 5. Connectivité Wi-Fi
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        if (!_config.wifiSSID.isEmpty()) {
            Serial.printf("[VoiceKit] Demarrage WiFiManager gere (SSID: %s)...\n", _config.wifiSSID.c_str());
            _wifi.begin(_config.wifiSSID.c_str(), _config.wifiPassword.c_str());
            setState(VoiceState::CONNECTING_WIFI);
        } else {
            Serial.println("[VoiceKit] AVERTISSEMENT : Mode MANAGED_WIFI sans SSID");
            setState(VoiceState::IDLE);
        }
    } else {
        Serial.println("[VoiceKit] Mode EXTERNAL_WIFI : Wi-Fi gere par l'application hote.");
        setState(VoiceState::IDLE);
    }

    Serial.println("[VoiceKit] Initialisation terminee avec succes.");
    return true;
}

void VoxCoreVoice::update() {
    // 1. Mise à jour réseau Wi-Fi / SNTP si en mode MANAGED_WIFI
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        _wifi.tick();
        if (_wifi.isConnected() && _wifi.isTimeSynced()) {
            if (_state == VoiceState::CONNECTING_WIFI || _state == VoiceState::IDLE) {
                if (!_client.isConnected() && _session.getState() == LiveSessionState::DISCONNECTED) {
                    Serial.println("[VoiceKit] Wi-Fi et NTP valides -> Connexion a Gemini Live...");
                    setState(VoiceState::CONNECTING_SERVICE);
                    _connectingStartTimeMs = millis();
                    _session.activate();
                }
            }
        }
    } else {
        // En mode EXTERNAL_WIFI : connexion automatique dès que l'application hôte a établi le réseau
        if (_state == VoiceState::IDLE || _state == VoiceState::CONNECTING_WIFI) {
            if (WiFi.status() == WL_CONNECTED && isSystemTimeValid()) {
                if (!_client.isConnected() && _session.getState() == LiveSessionState::DISCONNECTED) {
                    Serial.println("[VoiceKit] Reseau hote & NTP valides -> Connexion a Gemini Live...");
                    setState(VoiceState::CONNECTING_SERVICE);
                    _connectingStartTimeMs = millis();
                    _session.activate();
                }
            }
        }
    }

    // 2. Traitement régulier des moteurs VoxCore
    _client.tick();
    _player.tick();
    _session.tick();

    // 3. Traitement des requêtes en attente
    if (!_pendingText.isEmpty()) {
        if (_session.getState() == LiveSessionState::CONNECTED) {
            String textToSay = _pendingText;
            _pendingText = "";
            dispatchSpeech(textToSay);
        }
    }

    // 4. Surveillance des timeouts
    const uint32_t now = millis();
    if (_state == VoiceState::CONNECTING_SERVICE) {
        if (_connectingStartTimeMs > 0 && (now - _connectingStartTimeMs > GEMINI_LIVE_CONNECT_TIMEOUT_MS + 2000)) {
            Serial.println("[VoiceKit] ERREUR : Delai de connexion depasse vers Gemini Live");
            _lastError = "Timeout connexion Gemini Live";
            _connectingStartTimeMs = 0;
            _pendingText = "";
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
        }
    }
}

bool VoxCoreVoice::say(const String& text) {
    if (text.isEmpty()) {
        _lastError = "Texte vide";
        return false;
    }

    _lastError = "";

    // Si le moteur est occupé
    if (isBusy()) {
        switch (_config.queuePolicy) {
            case QueuePolicy::REJECT_IF_BUSY:
                _lastError = "Moteur vocal occupe";
                Serial.println("[VoiceKit] say() refuse : moteur actuellement occupe");
                return false;

            case QueuePolicy::INTERRUPT_CURRENT:
                Serial.println("[VoiceKit] say() interruption demandee de la parole en cours");
                stop();
                break;

            case QueuePolicy::QUEUE_FIFO:
                if (_textQueue.size() >= _config.maxQueueSize) {
                    _lastError = "File d'attente vocale saturee";
                    Serial.println("[VoiceKit] say() refuse : file d'attente saturee");
                    return false;
                }
                _textQueue.push(text);
                Serial.printf("[VoiceKit] say() mis en file d'attente (position %u)\n", (unsigned)_textQueue.size());
                return true;
        }
    }

    // Vérification des prérequis réseau
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        if (!_wifi.isConnected()) {
            _lastError = "Wi-Fi non connecte";
            if (_onError) _onError(_lastError);
            return false;
        }
        if (!_wifi.isTimeSynced()) {
            _lastError = "Horloge NTP non synchronisee (requise pour TLS)";
            if (_onError) _onError(_lastError);
            return false;
        }
    } else {
        if (WiFi.status() != WL_CONNECTED) {
            _lastError = "Wi-Fi hote non connecte";
            if (_onError) _onError(_lastError);
            return false;
        }
        if (!isSystemTimeValid()) {
            _lastError = "Heure systeme non synchronisee (requise pour TLS)";
            if (_onError) _onError(_lastError);
            return false;
        }
    }

    // Si la session est prête, envoi direct
    if (_session.getState() == LiveSessionState::CONNECTED) {
        return dispatchSpeech(text);
    }

    // Si la session est en cours de connexion
    if (_session.getState() == LiveSessionState::CONNECTING) {
        Serial.println("[VoiceKit] Connexion en cours -> requete mise en attente");
        _pendingText = text;
        return true;
    }

    // Session déconnectée ou en erreur : tentative d'activation
    Serial.println("[VoiceKit] Session deconnectee -> Activation de session...");
    _pendingText = text;
    _connectingStartTimeMs = millis();
    setState(VoiceState::CONNECTING_SERVICE);
    return _session.activate();
}

bool VoxCoreVoice::dispatchSpeech(const String& text) {
    String prompt = _config.promptPrefix + text;
    Serial.printf("[VoiceKit] Prononciation : \"%s\"\n", text.c_str());
    bool ok = _session.sendTextMessage(prompt);
    if (!ok) {
        _lastError = "Echec envoi message texte vers Gemini Live";
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }
    return true;
}

void VoxCoreVoice::stop() {
    Serial.println("[VoiceKit] Arret demande (stop)");
    _pendingText = "";
    while (!_textQueue.empty()) {
        _textQueue.pop();
    }
    _session.requestStop();
    setState(VoiceState::READY);
}

bool VoxCoreVoice::isReady() const {
    return (_session.getState() == LiveSessionState::CONNECTED && !isBusy());
}

bool VoxCoreVoice::isSpeaking() const {
    return (_player.isPlaying() > 0 || _session.getState() == LiveSessionState::PLAYING);
}

bool VoxCoreVoice::isBusy() const {
    return (isSpeaking() || 
            _session.getState() == LiveSessionState::WAITING_RESPONSE || 
            !_pendingText.isEmpty() || 
            !_textQueue.empty());
}

void VoxCoreVoice::setVolume(uint8_t percent) {
    _config.volume = percent;
    _player.setVolume(percent);
}

uint8_t VoxCoreVoice::getVolume() const {
    return _player.getVolume();
}
