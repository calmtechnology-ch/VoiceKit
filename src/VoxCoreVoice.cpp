#include "VoxCoreVoice.h"
#include <WiFi.h>
#include <time.h>

VoxCoreVoice::VoxCoreVoice() = default;

VoxCoreVoice::~VoxCoreVoice() {
    stop();
}

bool VoxCoreVoice::isSystemTimeValid() {
    time_t now = time(nullptr);
    // Vérification : timestamp valide supérieur à novembre 2023 (~1700000000)
    return (now >= 1700000000);
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

    Serial.println("\n[VoiceKit] Initialisation de VoxCore VoiceKit v0.1.0.1...");

    // 1. Initialisation de la couche audio matérielle (Composant invité)
    if (_config.audioOutput) {
        _audioOutput = _config.audioOutput;
    } else {
        _internalAudioOutput = std::unique_ptr<M5AudioOutput>(new M5AudioOutput());
        _audioOutput = _internalAudioOutput.get();
    }

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

    // Callbacks Audio Player
    _player.setOnFirstAudio([this]() {
        handleFirstAudio();
    });

    _player.setOnPlaybackFinished([this]() {
        handlePlaybackFinished();
    });

    // 2. Initialisation du client Gemini Live
    if (!_client.begin(_config.apiKey.c_str(), _config.model.c_str(), _config.voice.c_str())) {
        _lastError = _client.getLastError();
        Serial.printf("[VoiceKit] ERREUR : %s\n", _lastError.c_str());
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }

    _client.setSystemInstruction(_config.systemInstruction);

    // Callbacks Gemini Live Client
    _client.setOnConnected([this]() {
        handleClientConnected();
    });

    _client.setOnSetupComplete([this]() {
        handleClientSetupComplete();
    });

    _client.setOnDisconnected([this]() {
        handleClientDisconnected();
    });

    _client.setOnAudioData([this](const int16_t* pcm, size_t samples) {
        handleAudioData(pcm, samples);
    });

    _client.setOnTurnComplete([this]() {
        handleTurnComplete();
    });

    _client.setOnError([this](const String& err) {
        handleClientError(err);
    });

    // 3. Gestion du réseau Wi-Fi
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        if (!_config.wifiSSID.isEmpty()) {
            Serial.printf("[VoiceKit] Demarrage Wi-Fi gere (SSID: %s)...\n", _config.wifiSSID.c_str());
            WiFi.mode(WIFI_STA);
            WiFi.begin(_config.wifiSSID.c_str(), _config.wifiPassword.c_str());
            setState(VoiceState::CONNECTING_WIFI);
        } else {
            Serial.println("[VoiceKit] AVERTISSEMENT : Mode MANAGED_WIFI sans SSID specifie");
            setState(VoiceState::IDLE);
        }
    } else {
        // En mode EXTERNAL_WIFI : Respect strict de l'application hôte
        Serial.println("[VoiceKit] Mode EXTERNAL_WIFI actif. Attente de la connexion par l'application hote.");
        setState(VoiceState::IDLE);
    }

    _lastActivityTimeMs = millis();
    Serial.println("[VoiceKit] Composant vocal pret.");
    return true;
}

void VoxCoreVoice::update() {
    uint32_t now = millis();

    // 1. Surveillance du Wi-Fi en mode MANAGED_WIFI
    if (_config.wifiMode == WiFiMode::MANAGED_WIFI) {
        if (WiFi.status() == WL_CONNECTED) {
            if (_state == VoiceState::CONNECTING_WIFI) {
                Serial.printf("[VoiceKit] Wi-Fi connecte ! IP: %s\n", WiFi.localIP().toString().c_str());
                setState(VoiceState::IDLE);
            }
        } else {
            if (_state != VoiceState::CONNECTING_WIFI && (now - _lastWiFiRetryMs > 30000)) {
                _lastWiFiRetryMs = now;
                Serial.println("[VoiceKit] Reconnexion Wi-Fi gere...");
                WiFi.reconnect();
                setState(VoiceState::CONNECTING_WIFI);
            }
        }
    }

    // 1b. Surveillance de coupure Wi-Fi en cours de requete active (Correction E)
    if (WiFi.status() != WL_CONNECTED) {
        if (_state == VoiceState::CONNECTING_SERVICE || _state == VoiceState::GENERATING || 
            _state == VoiceState::SPEAKING || !_pendingTextOnConnect.isEmpty()) {
            Serial.println("[VoiceKit] ERREUR : Connexion Wi-Fi perdue pendant la requete vocale");
            _lastError = "Connexion Wi-Fi interrompue";
            _client.disconnect();
            _player.stop(AudioStopReason::HARDWARE_ERROR);
            _pendingTextOnConnect = "";
            while (!_textQueue.empty()) _textQueue.pop();
            _connectingStartTimeMs = 0;
            _generatingStartTimeMs = 0;
            _isSpeakingActive = false;
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
        }
    }

    // 2. Traitement non-bloquant des flux sous-jacents
    _client.tick();
    _player.tick();

    // 2b. Surveillance des timeouts asynchrones (Correction E)
    if (_state == VoiceState::CONNECTING_SERVICE) {
        if (_connectingStartTimeMs > 0 && (now - _connectingStartTimeMs > 12000)) {
            Serial.println("[VoiceKit] ERREUR : Timeout de connexion au service Gemini Live");
            _lastError = "Timeout de connexion au service Gemini Live";
            _client.disconnect();
            _pendingTextOnConnect = "";
            _connectingStartTimeMs = 0;
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
        }
    } else if (_state == VoiceState::GENERATING) {
        if (_generatingStartTimeMs > 0 && (now - _generatingStartTimeMs > 15000)) {
            Serial.println("[VoiceKit] ERREUR : Timeout de generation vocale Gemini Live");
            _lastError = "Timeout de generation vocale Gemini Live";
            _player.stop(AudioStopReason::HARDWARE_ERROR);
            _generatingStartTimeMs = 0;
            setState(VoiceState::ERROR_STATE);
            if (_onError) _onError(_lastError);
        }
    }

    // 3. Connexion initiale automatique si Wi-Fi disponible et non à la demande
    if (!_config.connectOnDemand && (_state == VoiceState::IDLE)) {
        if (WiFi.status() == WL_CONNECTED && isSystemTimeValid()) {
            if (!_client.isConnected()) {
                Serial.println("[VoiceKit] Wi-Fi & Heure valides. Connexion prealable a Gemini Live...");
                _connectingStartTimeMs = now;
                _client.connect();
                setState(VoiceState::CONNECTING_SERVICE);
            }
        }
    }

    // 4. Depilage asynchrone de la file d'attente
    if (!_textQueue.empty() && !isBusy()) {
        if (_client.isConnected() && _client.isSetupComplete()) {
            String nextText = _textQueue.front();
            _textQueue.pop();
            dispatchSpeechRequest(nextText);
        } else if (_state == VoiceState::IDLE || _state == VoiceState::READY) {
            String nextText = _textQueue.front();
            _textQueue.pop();
            say(nextText);
        }
    }

    // 5. Gestion de la déconnexion automatique en mode économie d'énergie (connectOnDemand)
    if (_config.connectOnDemand && (_config.autoDisconnectIdleMs > 0)) {
        if (!isBusy() && _client.isConnected() && (now - _lastActivityTimeMs >= _config.autoDisconnectIdleMs)) {
            Serial.printf("[VoiceKit] Inactivite detectee (> %u ms). Deconnexion session Gemini pour economie d'energie.\n",
                          (unsigned)_config.autoDisconnectIdleMs);
            _client.disconnect();
            setState(VoiceState::IDLE);
        }
    }
}

bool VoxCoreVoice::say(const String& text) {
    if (text.isEmpty()) {
        _lastError = "Texte vide";
        return false;
    }

    _lastActivityTimeMs = millis();
    _lastError = "";

    // Cas 1 : Le moteur est actuellement occupe (generation, restitution ou file active)
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

    // Cas 2 : Le moteur est libre, verification prealable des prerequis reseau
    if (WiFi.status() != WL_CONNECTED) {
        _lastError = "Wi-Fi non connecte";
        Serial.printf("[VoiceKit] say() impossible : %s\n", _lastError.c_str());
        if (_onError) _onError(_lastError);
        return false;
    }

    if (!isSystemTimeValid()) {
        _lastError = "Heure systeme non synchronisee (requise pour la validation TLS)";
        Serial.printf("[VoiceKit] say() impossible : %s\n", _lastError.c_str());
        if (_onError) _onError(_lastError);
        return false;
    }

    // Cas 3 : Session deja connectee et prete
    if (_client.isConnected() && _client.isSetupComplete()) {
        return dispatchSpeechRequest(text);
    }

    // Cas 3b : Connexion deja en cours (attente de setup ou appel say() consecutif)
    if (_state == VoiceState::CONNECTING_SERVICE || _client.isConnected()) {
        Serial.println("[VoiceKit] Connexion deja en cours -> Mise en attente de la requete vocale");
        _pendingTextOnConnect = text;
        setState(VoiceState::CONNECTING_SERVICE);
        return true;
    }

    // Cas 4 : Session non connectee (demarrage a la demande connectOnDemand)
    _pendingTextOnConnect = text;
    _connectingStartTimeMs = millis();
    Serial.println("[VoiceKit] Connexion a la demande a Gemini Live...");
    setState(VoiceState::CONNECTING_SERVICE);
    if (!_client.connect()) {
        _pendingTextOnConnect = "";
        _connectingStartTimeMs = 0;
        _lastError = (strlen(_client.getLastError()) == 0) ? "Echec connexion Gemini Live" : _client.getLastError();
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }
    return true;
}

bool VoxCoreVoice::dispatchSpeechRequest(const String& text) {
    _player.reset();
    _client.resetTurnRxMetrics();
    _generatingStartTimeMs = millis();

    String prompt = _config.promptPrefix + text;
    bool sent = _client.sendTextMessage(prompt);

    if (sent) {
        setState(VoiceState::GENERATING);
        return true;
    } else {
        _generatingStartTimeMs = 0;
        _lastError = "Echec envoi requete a Gemini Live";
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
        return false;
    }
}

void VoxCoreVoice::stop() {
    Serial.println("[VoiceKit] Arret demande (stop)");
    _connectingStartTimeMs = 0;
    _generatingStartTimeMs = 0;
    _pendingTextOnConnect = "";
    _isSpeakingActive = false;

    while (!_textQueue.empty()) {
        _textQueue.pop();
    }

    if (_client.isConnected()) {
        _client.sendInterruption();
    }
    _player.stop(AudioStopReason::USER_STOP);

    setState(_client.isSetupComplete() ? VoiceState::READY : VoiceState::IDLE);
}

bool VoxCoreVoice::isSpeaking() const {
    return _player.isPlaying();
}

bool VoxCoreVoice::isReady() const {
    if (_config.connectOnDemand) {
        return (WiFi.status() == WL_CONNECTED && isSystemTimeValid() && !isBusy());
    }
    return (_client.isConnected() && _client.isSetupComplete() && !isBusy());
}

bool VoxCoreVoice::isBusy() const {
    return (_state == VoiceState::GENERATING || 
            _state == VoiceState::SPEAKING || 
            _player.isPlaying() ||
            !_pendingTextOnConnect.isEmpty() ||
            !_textQueue.empty());
}

void VoxCoreVoice::setVolume(uint8_t percent) {
    _config.volume = percent;
    _player.setVolume(percent);
}

uint8_t VoxCoreVoice::getVolume() const {
    return _player.getVolume();
}

void VoxCoreVoice::handleClientConnected() {
    Serial.println("[VoiceKit] WebSocket Gemini Live connecte");
}

void VoxCoreVoice::handleClientSetupComplete() {
    Serial.println("[VoiceKit] Setup Gemini Live confirme");
    _connectingStartTimeMs = 0;
    setState(VoiceState::READY);

    if (_onReady) {
        _onReady();
    }

    // Traitement d'un texte mis en attente pendant la connexion
    if (!_pendingTextOnConnect.isEmpty()) {
        String textToSay = _pendingTextOnConnect;
        _pendingTextOnConnect = "";
        dispatchSpeechRequest(textToSay);
    }
}

void VoxCoreVoice::handleClientDisconnected() {
    Serial.println("[VoiceKit] Session Gemini Live deconnectee");
    _connectingStartTimeMs = 0;

    if (_state == VoiceState::GENERATING) {
        Serial.println("[VoiceKit] Deconnexion pendant la generation vocale");
        _generatingStartTimeMs = 0;
        _player.stop(AudioStopReason::GEMINI_INTERRUPT);
        _lastError = "Deconnexion inattendue de Gemini Live pendant la generation";
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
    } else if (_state == VoiceState::CONNECTING_SERVICE) {
        Serial.println("[VoiceKit] Deconnexion pendant l'etablissement de session");
        _pendingTextOnConnect = "";
        _lastError = "Echec connexion Gemini Live (session fermee)";
        setState(VoiceState::ERROR_STATE);
        if (_onError) _onError(_lastError);
    } else if (_state == VoiceState::SPEAKING || _isSpeakingActive) {
        // En cours de parole : Gemini a clos le flux, notifier la fin de tour au lecteur
        _player.notifyTurnComplete();
    } else {
        setState(VoiceState::IDLE);
    }
}

void VoxCoreVoice::handleAudioData(const int16_t* pcm, size_t samples) {
    _generatingStartTimeMs = 0;
    if (_state == VoiceState::GENERATING) {
        setState(VoiceState::SPEAKING);
    }
    _player.feedPcm(pcm, samples);
}

void VoxCoreVoice::handleFirstAudio() {
    _generatingStartTimeMs = 0;
    _isSpeakingActive = true;
    setState(VoiceState::SPEAKING);
    Serial.println("[VoiceKit] Debut de restitution vocale (onSpeechStarted)");
    if (_onSpeechStarted) {
        _onSpeechStarted();
    }
}

void VoxCoreVoice::handleTurnComplete() {
    Serial.println("[VoiceKit] Fin de generation Gemini reçue (turnComplete)");
    _generatingStartTimeMs = 0;
    _player.notifyTurnComplete();
}

void VoxCoreVoice::handlePlaybackFinished() {
    Serial.println("[VoiceKit] Fin effective de restitution sonore (onSpeechFinished)");
    _lastActivityTimeMs = millis();
    _isSpeakingActive = false;

    setState(_client.isSetupComplete() ? VoiceState::READY : VoiceState::IDLE);

    if (_onSpeechFinished) {
        _onSpeechFinished();
    }

    // Depiler le message suivant s'il y en a un
    if (!_textQueue.empty()) {
        String nextText = _textQueue.front();
        _textQueue.pop();
        if (_client.isConnected() && _client.isSetupComplete()) {
            dispatchSpeechRequest(nextText);
        } else {
            say(nextText);
        }
    }
}

void VoxCoreVoice::handleClientError(const String& err) {
    _lastError = err;
    _connectingStartTimeMs = 0;
    _generatingStartTimeMs = 0;
    _pendingTextOnConnect = "";
    _isSpeakingActive = false;
    _player.stop(AudioStopReason::HARDWARE_ERROR);
    while (!_textQueue.empty()) {
        _textQueue.pop();
    }
    Serial.printf("[VoiceKit] Erreur Gemini Live: %s\n", err.c_str());
    setState(VoiceState::ERROR_STATE);
    if (_onError) {
        _onError(err);
    }
}
