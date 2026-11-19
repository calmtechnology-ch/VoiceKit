#include "core/LiveSessionController.h"
#include "DisplayTextNormalizer.h"
#include "core/AudioTraceLog.h"
#include "core/GeminiVoiceProvider.h"
#include "core/Utf8Utils.h"
#include <cctype>
#include <cstring>

const char *audioPacketRejectReasonToString(AudioPacketRejectReason reason) {
  switch (reason) {
  case AudioPacketRejectReason::NONE:
    return "AUCUN";
  case AudioPacketRejectReason::TURN_INACTIVE:
    return "TOUR_INACTIF";
  case AudioPacketRejectReason::TURN_CANCELLED:
    return "TOUR_ANNULE";
  case AudioPacketRejectReason::STATE_CONNECTED:
    return "SESSION_CONNECTED";
  case AudioPacketRejectReason::STATE_DISCONNECTED:
    return "SESSION_DISCONNECTED";
  case AudioPacketRejectReason::STATE_ERROR:
    return "SESSION_ERROR";
  case AudioPacketRejectReason::STATE_INTERRUPTING:
    return "SESSION_INTERRUPTING";
  case AudioPacketRejectReason::STATE_UNEXPECTED:
    return "ETAT_INATTENDU";
  case AudioPacketRejectReason::BUFFER_SATURATED:
    return "TAMPON_SATURE";
  default:
    return "INCONNU";
  }
}

LiveSessionController::LiveSessionController() { _lastError[0] = '\0'; }

LiveSessionController::~LiveSessionController() {
  if (_provider) {
    _provider->setOnConnected(nullptr);
    _provider->setOnDisconnected(nullptr);
    _provider->setOnError(nullptr);
    _provider->setOnAudioData(nullptr);
    _provider->setOnOutputTranscription(nullptr);
    _provider->setOnOutputTranscriptionWithFinal(nullptr);
    _provider->setOnText(nullptr);
    _provider->setOnInputTranscription(nullptr);
    _provider->setOnTurnComplete(nullptr);
    _provider->setOnInterrupted(nullptr);
    _provider->setOnSetupComplete(nullptr);
  }
}

void LiveSessionController::appendTranscriptionFragment(String &target,
                                                        const String &fragment,
                                                        size_t maxLength) {
  if (fragment.isEmpty())
    return;

  if (target.isEmpty()) {
    size_t start = 0;
    while (start < fragment.length() && fragment[start] == ' ')
      start++;
    if (start < fragment.length()) {
      target = fragment.substring(start);
      if (target.length() > maxLength) {
        target = Utf8Utils::truncateUtf8(target, maxLength);
      }
    }
    return;
  }

  auto isAllDigits = [](const String &s) -> bool {
    if (s.isEmpty()) return false;
    for (size_t i = 0; i < s.length(); ++i) {
      if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
  };

  // 1. Fragment identique à la cible (hors chiffres consécutifs légitimes comme "11" + "11")
  if (target == fragment && (!isAllDigits(target) || !isAllDigits(fragment)))
    return;

  // 2. Gestion des deltas constitués uniquement d'espaces / blancs
  String fTrim = fragment;
  fTrim.trim();
  if (fTrim.isEmpty()) {
    char lastChar = target[target.length() - 1];
    if (lastChar != ' ' && lastChar != '\t' && lastChar != '\n' && lastChar != '\r') {
      if (target.length() < maxLength) {
        target += ' ';
      }
    }
    return;
  }

  String tTrim = target;
  tTrim.trim();
  if (tTrim.isEmpty()) {
    target = fTrim;
    if (target.length() > maxLength) {
      target = Utf8Utils::truncateUtf8(target, maxLength);
    }
    return;
  }

  if (tTrim == fTrim && (!isAllDigits(tTrim) || !isAllDigits(fTrim)))
    return;

  auto stripPunct = [](const String &s) -> String {
    size_t len = s.length();
    while (len > 0 &&
           (s[len - 1] == '.' || s[len - 1] == '!' || s[len - 1] == '?' ||
            s[len - 1] == ',' || s[len - 1] == ';' || s[len - 1] == ' ')) {
      len--;
    }
    return s.substring(0, len);
  };

  String tBase = stripPunct(tTrim);
  String fBase = stripPunct(fTrim);

  // Retransmission de phrase complète identique (hors chiffres consécutifs comme "11" + "11")
  if (!tBase.isEmpty() && tBase == fBase && (!isAllDigits(tBase) || !isAllDigits(fBase))) {
    // Si le fragment apporte une ponctuation finale que la cible n'avait pas
    if (fTrim.length() > tTrim.length() && fTrim.startsWith(tBase)) {
      target = fragment;
      if (target.length() > maxLength) {
        target = Utf8Utils::truncateUtf8(target, maxLength);
      }
    }
    return;
  }

  // 3. Transcription cumulative (le fragment commence par l'intégralité de la cible et apporte du contenu)
  if ((fragment.startsWith(target) && fragment.length() > target.length()) ||
      (!tTrim.isEmpty() && fTrim.startsWith(tTrim) && fTrim.length() > tTrim.length())) {
    target = fragment;
    if (target.length() > maxLength) {
      target = Utf8Utils::truncateUtf8(target, maxLength);
    }
    return;
  }

  // 4. Ponctuation isolée : élimination des doublons de fin
  if (fTrim.length() == 1 && (fTrim[0] == '.' || fTrim[0] == '!' || fTrim[0] == '?' || fTrim[0] == ',' || fTrim[0] == ';')) {
    if (target.endsWith(fTrim)) {
      return;
    }
  }

  // Éviter l'accumulation anormale d'espaces à la frontière de deux deltas
  size_t fragStart = 0;
  char lastT = target[target.length() - 1];
  if (lastT == ' ' || lastT == '\t' || lastT == '\n' || lastT == '\r') {
    while (fragStart < fragment.length() && (fragment[fragStart] == ' ' || fragment[fragStart] == '\t')) {
      fragStart++;
    }
    if (fragStart >= fragment.length()) {
      return;
    }
  }
  String effectiveFrag = (fragStart > 0) ? fragment.substring(fragStart) : fragment;

  // 5. Recherche du chevauchement partiel sécurisé
  // RÈGLES STRICTES ANTI-FUSION :
  // - Longueur de chevauchement minimale >= 3 caractères
  // - Aucun chiffre ASCII dans le chevauchement (pour préserver "20" + "06" -> "2006", "11" + "11" -> "1111")
  // - Frontière de mot obligatoire dans target (précédé d'un espace/séparateur ou début de chaîne)
  size_t targetLen = target.length();
  size_t fragLen = effectiveFrag.length();
  size_t maxOverlap = (targetLen < fragLen) ? targetLen : (fragLen - 1);
  size_t bestOverlap = 0;

  for (size_t k = 3; k <= maxOverlap; ++k) {
    String suffix = target.substring(targetLen - k);
    String prefix = effectiveFrag.substring(0, k);
    if (suffix == prefix) {
      bool hasDigit = false;
      for (size_t d = 0; d < k; ++d) {
        if (suffix[d] >= '0' && suffix[d] <= '9') {
          hasDigit = true;
          break;
        }
      }
      if (hasDigit) {
        continue;
      }

      bool atWordBoundary = (targetLen == k || target[targetLen - k - 1] == ' ' ||
                             target[targetLen - k - 1] == '\t' || target[targetLen - k] == ' ');
      if (atWordBoundary) {
        bestOverlap = k;
      }
    }
  }

  if (bestOverlap > 0) {
    String toAppend = effectiveFrag.substring(bestOverlap);
    target += toAppend;
  } else {
    target += effectiveFrag;
  }

  if (target.length() > maxLength) {
    target = Utf8Utils::truncateUtf8(target, maxLength);
  }
}

void LiveSessionController::handleIncomingAssistantTranscription(const String &text, bool isFinal) {
  if (!_turnActive.load(std::memory_order_acquire) ||
      _turnCancelled.load(std::memory_order_acquire))
    return;
  _lastActivityMs = millis();
  _hasOutputTranscription = true;

  if (isFinal) {
    _lastAssistantText = text;
    if (_lastAssistantText.length() > 512) {
      _lastAssistantText = Utf8Utils::truncateUtf8(_lastAssistantText, 512);
    }
  } else {
    appendTranscriptionFragment(_lastAssistantText, text);
  }

  if (_onOutputTranscriptionWithFinal) {
    _onOutputTranscriptionWithFinal(_lastAssistantText, isFinal);
  }
  if (_onOutputTranscription) {
    _onOutputTranscription(_lastAssistantText);
  }
}

void LiveSessionController::finishTurn() {
      if (!_turnCompletedLogged &&
          _turnActive.load(std::memory_order_acquire) &&
          !_turnCancelled.load(std::memory_order_acquire)) {
        _turnCompletedLogged = true;

        if (!_lastAssistantText.isEmpty()) {
          const char *providerTag =
              (_provider &&
               _provider->getProviderType() == VoiceProviderType::OPENAI)
                  ? "[OPENAI]"
                  : "[GEMINI]";
          Serial.printf("%s %s\n", providerTag, _lastAssistantText.c_str());
        }

        uint32_t pcmDecoded = getTurnDecodedSamples();
        uint32_t pcmRejectedBefore = _audioSamplesRejectedBeforePlayerTotal;
        uint32_t pcmPresented = _audioSamplesPresentedToPlayerTotal;
        uint32_t pcmAccepted =
            _player ? _player->getSamplesAcceptedBuffer() : 0;
        uint32_t pcmPlayed = _player ? _player->getSamplesPlayedTotal() : 0;
        uint32_t pcmDroppedOverflow =
            _player ? _player->getOverflowDroppedSamples() : 0;
        uint32_t pcmLost = pcmRejectedBefore + pcmDroppedOverflow;

        Serial.println("-------------------------------------------------------"
                       "-------------------------");
        Serial.printf("[STATUS] Tour #%llu termine : %u ech decodes, %u ech "
                      "joues (%u rejet, %u perte)\n",
                      (unsigned long long)_currentTurnId, (unsigned)pcmDecoded,
                      (unsigned)pcmPlayed, (unsigned)pcmRejectedBefore,
                      (unsigned)pcmLost);
        Serial.println("-------------------------------------------------------"
                       "-------------------------");

        _turnActive.store(false, std::memory_order_release);

        if (_onTurnFinished) {
          _onTurnFinished(_currentTurnId);
        }
      }
    }

    void LiveSessionController::printAudioEndCheck(const char *label) {
      uint32_t pcmDecoded = getTurnDecodedSamples();
      uint32_t pcmPlayed = _player ? _player->getSamplesPlayedTotal() : 0;
      Serial.printf("[AUDIO END CHECK][%s] Decodes: %u | Joues: %u\n",
                    label ? label : "INFO", (unsigned)pcmDecoded,
                    (unsigned)pcmPlayed);
    }

    uint32_t LiveSessionController::getTurnDecodedSamples() const {
      if (_provider) {
        return _provider->getTurnDecodedSamples();
      }
      if (_legacyClient) {
        return _legacyClient->getPcmSamplesDispatchedTotal();
      }
      return 0;
    }

    uint32_t LiveSessionController::getPostTurnCompleteBlocks() const {
      if (_provider) {
        return _provider->getPostTurnCompleteBlocks();
      }
      if (_legacyClient) {
        return _legacyClient->getPostTurnCompleteBlocks();
      }
      return 0;
    }

    uint32_t LiveSessionController::getPostTurnCompleteSamples() const {
      if (_provider) {
        return _provider->getPostTurnCompleteSamples();
      }
      if (_legacyClient) {
        return _legacyClient->getPostTurnCompletePcmSamples();
      }
      return 0;
    }

    void LiveSessionController::resetTurnSessionMetrics() {
      _audioPacketsReceivedCount = 0;
      _audioPacketsPresentedToPlayerCount = 0;
      _audioSamplesPresentedToPlayerTotal = 0;
      _audioPacketsDispatchedCount = 0;
      _audioPacketsWithSaturationCount = 0;
      _audioPacketsRejectedBeforePlayerCount = 0;
      _audioSamplesRejectedBeforePlayerTotal = 0;
      _lastRejectReason = AudioPacketRejectReason::NONE;
    }

    bool LiveSessionController::begin(GeminiLiveClient * client, void *streamer,
                                      LiveAudioPlayer *player) {
      if (!client || !player)
        return false;
      _legacyClient = client;
      _legacyAdapter =
          std::unique_ptr<IVoiceProvider>(new GeminiVoiceProvider(client));
      return begin(_legacyAdapter.get(), streamer, player);
    }

    bool LiveSessionController::begin(IVoiceProvider * provider, void *,
                                      LiveAudioPlayer *player) {
      if (!provider || !player)
        return false;
      if (_provider && _provider != provider) {
        _provider->setOnConnected(nullptr);
        _provider->setOnDisconnected(nullptr);
        _provider->setOnError(nullptr);
        _provider->setOnAudioData(nullptr);
        _provider->setOnOutputTranscription(nullptr);
        _provider->setOnOutputTranscriptionWithFinal(nullptr);
        _provider->setOnText(nullptr);
        _provider->setOnTurnComplete(nullptr);
        _provider->setOnInterrupted(nullptr);
        _provider->setOnSetupComplete(nullptr);
      }
      _provider = provider;
      _player = player;

      _provider->setOnSetupComplete([this]() {
        _lastActivityMs = millis();
        if (_state == LiveSessionState::RECONNECTING ||
            _reconnectAttempts > 0) {
          _reconnectCount++;
        }
        _reconnectAttempts = 0;
        Serial.println("[LiveSession] Setup termine : session Live prete");
        transitionTo(LiveSessionState::CONNECTED, "Setup confirme");
      });

      _provider->setOnAudioData([this](const int16_t *pcm, size_t samples) {
        _audioPacketsReceivedCount++;
        _lastAudioPacketTimeMs = millis();
        _lastActivityMs = _lastAudioPacketTimeMs;

        if (_turnCancelled.load(std::memory_order_acquire)) {
          _audioPacketsRejectedBeforePlayerCount++;
          _audioSamplesRejectedBeforePlayerTotal += samples;
          _audioSamplesRejectedTurnCancelled += samples;
          _audioPacketsRejectedCount++;
          _audioSamplesRejectedTotal += samples;
          _lastRejectReason = AudioPacketRejectReason::TURN_CANCELLED;
          AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED,
                                (uint32_t)samples, (uint32_t)_lastRejectReason);
          return;
        }

        if (!_turnActive.load(std::memory_order_acquire)) {
          _audioPacketsRejectedBeforePlayerCount++;
          _audioSamplesRejectedBeforePlayerTotal += samples;
          _audioSamplesRejectedTurnInactive += samples;
          _audioPacketsRejectedCount++;
          _audioSamplesRejectedTotal += samples;
          _lastRejectReason = AudioPacketRejectReason::TURN_INACTIVE;
          AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED,
                                (uint32_t)samples, (uint32_t)_lastRejectReason);
          return;
        }

        if (_state == LiveSessionState::INTERRUPTING) {
          _audioPacketsRejectedBeforePlayerCount++;
          _audioSamplesRejectedBeforePlayerTotal += samples;
          _audioSamplesRejectedTurnCancelled += samples;
          _audioPacketsRejectedCount++;
          _audioSamplesRejectedTotal += samples;
          _lastRejectReason = AudioPacketRejectReason::STATE_INTERRUPTING;
          AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED,
                                (uint32_t)samples, (uint32_t)_lastRejectReason);
          return;
        }

        if (_state == LiveSessionState::DISCONNECTED ||
            _state == LiveSessionState::ERROR) {
          _audioPacketsRejectedBeforePlayerCount++;
          _audioSamplesRejectedBeforePlayerTotal += samples;
          _audioSamplesRejectedStateUnexpected += samples;
          _audioPacketsRejectedCount++;
          _audioSamplesRejectedTotal += samples;
          _lastRejectReason = (_state == LiveSessionState::DISCONNECTED)
                                  ? AudioPacketRejectReason::STATE_DISCONNECTED
                                  : AudioPacketRejectReason::STATE_ERROR;
          AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED,
                                (uint32_t)samples, (uint32_t)_lastRejectReason);
          return;
        }

        _hasAudioResponse = true;

        if (!_audioStartedForTurn) {
          _audioStartedForTurn = true;
          Serial.println("[AUDIO] Lecture PCM 24 kHz");
          if (_onFirstAudio) {
            _onFirstAudio();
          }
        }

        if (_state == LiveSessionState::WAITING_RESPONSE) {
          _firstAudioLatencyMs = millis() - _waitingStartTimeMs;
          Serial.printf("[LiveSession] Premier bloc audio recu en %u ms !\n",
                        _firstAudioLatencyMs);
          transitionTo(LiveSessionState::PLAYING, "Premier son recu");
        }

        if (_state == LiveSessionState::PLAYING) {
          _audioPacketsPresentedToPlayerCount++;
          _audioSamplesPresentedToPlayerTotal += samples;

          bool accepted = _player->feedPcm(pcm, samples);
          if (accepted) {
            _audioPacketsDispatchedCount++;
          } else {
            _audioPacketsWithSaturationCount++;
            _lastRejectReason = AudioPacketRejectReason::BUFFER_SATURATED;
            _hasBufferOverflowAnomaly = true;
          }
          if (_player->hasHardwareError()) {
            _hasPlaybackHardwareError = true;
          }
        } else {
          _audioPacketsRejectedBeforePlayerCount++;
          _audioSamplesRejectedBeforePlayerTotal += samples;
          _audioSamplesRejectedStateUnexpected += samples;
          _audioPacketsRejectedCount++;
          _audioSamplesRejectedTotal += samples;
          _lastRejectReason = AudioPacketRejectReason::STATE_UNEXPECTED;
          AudioTraceLog::record(AudioTraceEventType::PCM_REJECTED,
                                (uint32_t)samples, (uint32_t)_lastRejectReason);
        }
      });

      _provider->setOnOutputTranscription([this](const String &text) {
        handleIncomingAssistantTranscription(text, false);
      });

      _provider->setOnOutputTranscriptionWithFinal([this](const String &text, bool isFinal) {
        handleIncomingAssistantTranscription(text, isFinal);
      });

      _provider->setOnText([this](const String &text) {
        if (!_turnActive.load(std::memory_order_acquire) ||
            _turnCancelled.load(std::memory_order_acquire))
          return;
        _lastActivityMs = millis();
        _hasModelTurnText = true;
        if (!_hasOutputTranscription) {
          appendTranscriptionFragment(_lastAssistantText, text);
          if (_onOutputTranscriptionWithFinal) {
            _onOutputTranscriptionWithFinal(_lastAssistantText, false);
          }
          if (_onOutputTranscription) {
            _onOutputTranscription(_lastAssistantText);
          }
        }
      });

      _provider->setOnTurnComplete([this]() {
        if (!_turnActive.load(std::memory_order_acquire) ||
            _turnCancelled.load(std::memory_order_acquire))
          return;
        _lastActivityMs = millis();
        _turnCompleteReceived = true;
        _turnCompleteTimeMs = millis();
        if (_state == LiveSessionState::PLAYING ||
            _state == LiveSessionState::WAITING_RESPONSE) {
          _player->notifyTurnComplete();
        }
      });

      _provider->setOnInterrupted([this]() {
        Serial.println("[LiveSession] Interruption confirmee par le serveur");
        // Si un nouveau tour a deja demarre apres la demande d'interruption, ne
        // pas annuler le nouveau tour
        if (_turnActive.load(std::memory_order_acquire) &&
            !_turnCancelled.load(std::memory_order_acquire) &&
            _currentTurnId > _interruptedTurnId) {
          Serial.printf("[LiveSession] Interruption recue pour tour precedent "
                        "(#%llu vs actif #%llu) - ignoree pour tour en cours\n",
                        (unsigned long long)_interruptedTurnId,
                        (unsigned long long)_currentTurnId);
          return;
        }
        _turnCancelled.store(true, std::memory_order_release);
        _turnActive.store(false, std::memory_order_release);
        if (_state == LiveSessionState::PLAYING ||
            _state == LiveSessionState::WAITING_RESPONSE) {
          _player->stop(AudioStopReason::GEMINI_INTERRUPT);
          printAudioEndCheck("INTERROMPU");
          transitionTo(LiveSessionState::CONNECTED, "Interrompu par serveur");
        }
      });

      _provider->setOnDisconnected([this]() {
        _disconnectCount++;
        Serial.printf(
            "[LiveSession] Deconnexion WebSocket session (deconnexion #%u)\n",
            (unsigned)_disconnectCount);
        if (_deactivating) {
          Serial.println("[LiveSession] Deconnexion attendue pendant requestDeactivate()");
          return;
        }
        if (_state != LiveSessionState::DISCONNECTED &&
            _state != LiveSessionState::ERROR) {
          if (_turnActive.load(std::memory_order_acquire)) {
            _turnCancelled.store(true, std::memory_order_release);
            _turnActive.store(false, std::memory_order_release);
            if (_player)
              _player->stop(AudioStopReason::HARDWARE_ERROR);
            printAudioEndCheck("DECONNECTE");
          }
          _reconnectAttempts = 0;
          _lastReconnectAttemptMs = millis();
          transitionTo(LiveSessionState::RECONNECTING, "Deconnexion reseau");
        }
      });

      _provider->setOnError([this](const String &err) {
        strncpy(_lastError, err.c_str(), sizeof(_lastError) - 1);
        _lastError[sizeof(_lastError) - 1] = '\0';
        Serial.printf("[LiveSession] Erreur client : %s\n", _lastError);
        if (_deactivating) {
          return;
        }
        if (_turnActive.load(std::memory_order_acquire)) {
          abortTurnLocally();
        }
        if (_state == LiveSessionState::RECONNECTING ||
            _state == LiveSessionState::CONNECTING) {
          if (_reconnectAttempts > 0 &&
              _reconnectAttempts < LIVE_MAX_RECONNECT_ATTEMPTS) {
            _lastReconnectAttemptMs = millis();
            transitionTo(LiveSessionState::RECONNECTING,
                         "Erreur tentative - retry");
            if (_onError)
              _onError(err);
            return;
          }
        }
        transitionTo(LiveSessionState::ERROR, "Erreur client");
        if (_onError) {
          _onError(err);
        }
      });

      _player->setOnPlaybackFinishedWithId([this](uint32_t playbackId) {
        _lastActivityMs = millis();
        AudioTraceLog::record(AudioTraceEventType::PLAYBACK_FINISHED);
        finishTurn();
        if (_state == LiveSessionState::PLAYING ||
            _state == LiveSessionState::WAITING_RESPONSE) {
          AudioTraceLog::record(AudioTraceEventType::SESSION_CONNECTED);
          transitionTo(LiveSessionState::CONNECTED, "Fin lecture audio");
        }
        if (!_turnCancelled.load(std::memory_order_acquire)) {
          if (_onPlaybackFinishedWithId) {
            _onPlaybackFinishedWithId(playbackId);
          }
          if (_onPlaybackFinished) {
            _onPlaybackFinished();
          }
        }
      });

      return true;
    }

    void LiveSessionController::transitionTo(LiveSessionState newState,
                                             const char *reason) {
      if (_state == newState)
        return;

      Serial.printf("[LiveSession] Transition : %s -> %s%s%s%s\n",
                    liveSessionStateToString(_state),
                    liveSessionStateToString(newState), reason ? " (" : "",
                    reason ? reason : "", reason ? ")" : "");

      _state = newState;
      _stateEnterTimeMs = millis();

      if (_onStateChanged) {
        _onStateChanged(_state);
      }
    }

    bool LiveSessionController::activate() {
      if (!_provider)
        return false;
      _deactivating = false;

      if (_provider->isConnected() && _provider->isSetupComplete()) {
        transitionTo(LiveSessionState::CONNECTED, "Session deja active");
        return true;
      }

      _lastError[0] = '\0';
      _lastAssistantText = "";
      _disconnectCount = 0;
      _firstAudioLatencyMs = 0;
      _turnActive.store(false, std::memory_order_release);
      _turnCancelled.store(false, std::memory_order_release);
      _hasBufferOverflowAnomaly = false;
      _hasPlaybackHardwareError = false;
      _playbackErrorReported = false;
      _reconnectAttempts = 0;
      _lastReconnectAttemptMs = 0;

      transitionTo(LiveSessionState::CONNECTING, "Activation session LIVE");
      return _provider->connect();
    }

    void LiveSessionController::deactivate() {
      uint32_t t0Us = micros();
      _deactivating = false;
      if (_provider) {
        _provider->disconnect();
      }
      if (_player &&
          (_player->isPlaying() > 0 || _player->getBufferedSamples() > 0)) {
        _player->stop(AudioStopReason::USER_STOP);
      }
      _turnActive.store(false, std::memory_order_release);
      _turnCancelled.store(false, std::memory_order_release);
      transitionTo(LiveSessionState::DISCONNECTED, "Desactivation LIVE");
      recordSessionStopDurationUs(micros() - t0Us);
    }

    void LiveSessionController::requestDeactivate() {
      uint32_t t0Us = micros();
      _deactivating = true;
      if (_provider) {
        _provider->requestDisconnect();
      }
      if (_player &&
          (_player->isPlaying() > 0 || _player->getBufferedSamples() > 0)) {
        _player->stop(AudioStopReason::USER_STOP);
      }
      _turnActive.store(false, std::memory_order_release);
      _turnCancelled.store(false, std::memory_order_release);
      recordSessionStopDurationUs(micros() - t0Us);
    }

    bool LiveSessionController::pollDeactivate() {
      uint32_t t0Us = micros();
      if (_provider && !_provider->pollDisconnect()) {
        recordSessionStopDurationUs(micros() - t0Us);
        return false;
      }
      _deactivating = false;
      transitionTo(LiveSessionState::DISCONNECTED, "Desactivation LIVE");
      recordSessionStopDurationUs(micros() - t0Us);
      return true;
    }

    void LiveSessionController::abortTurnLocally() {
      _turnCancelled.store(true, std::memory_order_release);
      _turnActive.store(false, std::memory_order_release);
      _turnCompletedLogged = true;
      if (_player &&
          (_player->isPlaying() > 0 || _player->getBufferedSamples() > 0)) {
        _player->stop(AudioStopReason::USER_STOP);
      }
      if (_state != LiveSessionState::DISCONNECTED &&
          _state != LiveSessionState::ERROR) {
        transitionTo(LiveSessionState::CONNECTED,
                     "Cloture locale avant fin session");
      }
    }

    void LiveSessionController::markPendingSessionUpdate() {
      _deactivating = false;
      _lastError[0] = '\0';
      _turnActive.store(false, std::memory_order_release);
      _turnCancelled.store(false, std::memory_order_release);
      _reconnectAttempts = 0;
      transitionTo(LiveSessionState::CONNECTING,
                   "Negociation session.update en cours");
    }

    void LiveSessionController::tick() {
      uint32_t t0Us = micros();
      const uint32_t now = millis();

      if (_deactivating) {
        recordTickDurationUs(micros() - t0Us);
        return;
      }

      if (_state == LiveSessionState::RECONNECTING) {
        // Optimisation v0.5.2 : Ne lancer une reconnexion que si le réseau est
        // opérationnel
        if (!isNetworkReady()) {
          _lastReconnectAttemptMs = now;
          recordTickDurationUs(micros() - t0Us);
          return;
        }

        uint32_t reconnectInterval =
            (_reconnectAttempts == 0)
                ? 1000
                : ((_reconnectAttempts == 1) ? 2000 : 4000);
        if (now - _lastReconnectAttemptMs >= reconnectInterval) {
          _lastReconnectAttemptMs = now;
          if (_reconnectAttempts < LIVE_MAX_RECONNECT_ATTEMPTS) {
            _reconnectAttempts++;
            Serial.printf(
                "[LiveSession] Tentative de reconnexion auto (%u/%u)...\n",
                _reconnectAttempts, (unsigned)LIVE_MAX_RECONNECT_ATTEMPTS);
            if (_provider && _provider->connect()) {
              transitionTo(LiveSessionState::CONNECTING, "Reconnexion initiee");
            }
          } else {
            Serial.println(
                "[LiveSession] Echec de toutes les tentatives de reconnexion.");
            transitionTo(LiveSessionState::ERROR, "Reconnexion impossible");
          }
        }
      }

      // Detection non bloquante de fin de tour sans audio
      if (_turnCompleteReceived &&
          _state == LiveSessionState::WAITING_RESPONSE && !_hasAudioResponse) {
        if (!_player || !_player->isPlaying()) {
          finishTurn();
          transitionTo(LiveSessionState::CONNECTED,
                       "Reponse terminee sans audio");
        }
      }

      // Timeout de securite attente reponse serveur (15 s)
      if (_state == LiveSessionState::WAITING_RESPONSE &&
          _waitingStartTimeMs > 0) {
        if (now - _waitingStartTimeMs >= 15000) {
          Serial.println("[LiveSession] Timeout attente premiere reponse (15 "
                         "s) -> retour CONNECTED");
          _turnCancelled.store(true, std::memory_order_release);
          _turnActive.store(false, std::memory_order_release);
          transitionTo(LiveSessionState::CONNECTED,
                       "Timeout attente reponse serveur");
        }
      }
      recordTickDurationUs(micros() - t0Us);
    }

    bool LiveSessionController::sendTextMessage(const String &text,
                                                uint32_t speechId) {
      if (!_provider || !_provider->isConnected() ||
          !_provider->isSetupComplete()) {
        return false;
      }

      _turnActive.store(true, std::memory_order_release);
      _turnCancelled.store(false, std::memory_order_release);
      if (speechId != 0) {
        _currentTurnId = speechId;
      } else {
        _currentTurnId++;
      }
      _turnCompleteReceived = false;
      _turnCompletedLogged = false;
      _audioStartedForTurn = false;
      _hasAudioResponse = false;
      _hasOutputTranscription = false;
      _hasModelTurnText = false;
      _lastAssistantText = "";
      _waitingStartTimeMs = millis();
      _lastActivityMs = _waitingStartTimeMs;

      resetTurnSessionMetrics();

      if (_player) {
        _player->reset();
        _player->setPlaybackId(speechId != 0 ? speechId
                                             : (uint32_t)_currentTurnId);
        if (_player->getAudioOutput()) {
          _player->getAudioOutput()->resetTurnMetrics();
        }
      }
      if (_provider) {
        _provider->resetTurnRxMetrics();
      } else if (_legacyClient) {
        _legacyClient->resetTurnRxMetrics();
      }

      transitionTo(LiveSessionState::WAITING_RESPONSE,
                   "Envoi message texte TTS");
      return _provider->sendTextMessage(text);
    }

    bool LiveSessionController::beginAudioTurn() {
      if (!_provider || !_provider->isConnected() ||
          !_provider->isSetupComplete()) {
        return false;
      }

      _turnActive.store(true, std::memory_order_release);
      _turnCancelled.store(false, std::memory_order_release);
      _currentTurnId++;
      _turnCompleteReceived = false;
      _turnCompletedLogged = false;
      _audioStartedForTurn = false;
      _hasAudioResponse = false;
      _hasOutputTranscription = false;
      _hasModelTurnText = false;
      _lastAssistantText = "";
      _waitingStartTimeMs = millis();
      _lastActivityMs = _waitingStartTimeMs;

      resetTurnSessionMetrics();

      if (_player) {
        _player->reset();
        if (_player->getAudioOutput()) {
          _player->getAudioOutput()->resetTurnMetrics();
        }
      }
      if (_provider) {
        _provider->resetTurnRxMetrics();
      } else if (_legacyClient) {
        _legacyClient->resetTurnRxMetrics();
      }

      transitionTo(LiveSessionState::LISTENING,
                   "Debut tour vocal audio conversation");
      return true;
    }

    void LiveSessionController::finishAudioCapture() {
      _waitingStartTimeMs = millis();
      _lastActivityMs = _waitingStartTimeMs;
      transitionTo(LiveSessionState::WAITING_RESPONSE,
                   "Attente reponse serveur (fin parole)");
    }

    void LiveSessionController::requestStop() {
      // 1. Coupure locale immediate du haut-parleur en priorite absolue (0 latence, independant du reseau)
      if (_player) {
        _player->stop(AudioStopReason::USER_STOP);
      }

      // 2. Idempotence stricte pour un meme tour : si deja interrompu/annule pour ce turnId,
      // ne pas reemettre d'annulation reseau redundante
      if (_interruptedTurnId == _currentTurnId &&
          _turnCancelled.load(std::memory_order_acquire) &&
          !_turnActive.load(std::memory_order_acquire)) {
        Serial.println("[LiveSession] STOP ignore : tour deja annule");
        return;
      }

      Serial.println(
          "\n[LiveSession] >>> DEMANDE INTERRUPTION STOP PRIORITAIRE <<<");
      _interruptedTurnId = _currentTurnId;
      _turnCancelled.store(true, std::memory_order_release);
      _turnActive.store(false, std::memory_order_release);
      _turnCompletedLogged = true;

      if (_provider && _provider->isConnected()) {
        _provider->sendInterruption();
      }
      printAudioEndCheck("STOP");
      if (_state != LiveSessionState::DISCONNECTED) {
        if (_state == LiveSessionState::ERROR) {
          if (_provider && _provider->isConnected() &&
              _provider->isSetupComplete()) {
            transitionTo(LiveSessionState::CONNECTED,
                         "Interruption STOP et reprise apres erreur");
          }
        } else if (_state == LiveSessionState::CONNECTING || _state == LiveSessionState::RECONNECTING) {
          Serial.println("[LiveSession] STOP pendant connexion : tour annule, maintien de la phase de connexion");
        } else {
          transitionTo(LiveSessionState::CONNECTED,
                       "Interruption STOP demandee");
        }
      }
    }

    void LiveSessionController::setVolume(unsigned volumePercent) {
      if (_player) {
        _player->setVolume(volumePercent);
      }
    }

    uint32_t LiveSessionController::getChunksReceived() const {
      return _player ? _player->getChunksReceived() : 0;
    }
