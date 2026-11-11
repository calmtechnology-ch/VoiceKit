#include "IntegratedVoicePipeline.h"

IntegratedVoicePipeline::IntegratedVoicePipeline()
    : _voice(nullptr), _tools(nullptr), _voiceUx(nullptr) {
}

VoiceUxController& IntegratedVoicePipeline::getVoiceUxController() {
    if (_voiceUx != nullptr) {
        return *_voiceUx;
    }
    if (!_internalVoiceUx) {
        _internalVoiceUx = std::make_unique<VoiceUxController>();
        _internalVoiceUx->begin();
    }
    _voiceUx = _internalVoiceUx.get();
    return *_voiceUx;
}

bool IntegratedVoicePipeline::begin(VoxCoreVoice& voice, ToolRegistry& tools, const VoicePipelineConfig& config) {
    if (!_internalVoiceUx) {
        _internalVoiceUx = std::make_unique<VoiceUxController>();
        _internalVoiceUx->begin();
    }
    _voiceUx = _internalVoiceUx.get();
    return begin(voice, tools, *_voiceUx, config);
}

bool IntegratedVoicePipeline::begin(VoxCoreVoice& voice, ToolRegistry& tools, VoiceUxController& voiceUx, const VoicePipelineConfig& config) {
    _voice = &voice;
    _tools = &tools;
    _voiceUx = &voiceUx;
    _config = config;

    _dispatcher.setRegistry(_tools);

    // Initialisation du sous-système Voice UX
    _voiceUx->setEnabled(_config.enableVoiceUx);

    // Synchronisation de la consigne d'invite IA et des instructions intégrées
    if (!_config.aiPromptPrefix.isEmpty()) {
        _voice->setAiPromptPrefix(_config.aiPromptPrefix);
    }
    if (_voice->getSystemInstruction() == VOICEKIT_DEFAULT_TTS_INSTRUCTION) {
        _voice->setSystemInstruction(VOICEKIT_DEFAULT_INTEGRATED_INSTRUCTION);
    }

    // Abonnement aux événements du moteur vocal VoxCoreVoice
    _voice->onListeningStartedWithTurnId([this](uint32_t turnId) {
        if (_state == PipelineState::LISTENING && _activeTurnId == 0) {
            _activeTurnId = turnId;
        }
    });

    _voice->onTranscriptionWithTurnId([this](const String& text, bool isFinal, uint32_t originTurnId) {
        // Validation stricte de corrélation temporelle (Section 9 & 12)
        if (_cancelledTurnId != 0 && originTurnId == _cancelledTurnId) {
            // Transcription issue d'un tour annulé : rejet immédiat
            return;
        }
        if (_activeTurnId != 0 && originTurnId != 0 && originTurnId != _activeTurnId) {
            // Transcription issue d'un tour périmé : rejet
            return;
        }
        if (_state != PipelineState::TRANSCRIBING && _state != PipelineState::LISTENING) {
            return;
        }

        _lastTranscription = text;
        if (_onTranscription) {
            _onTranscription(_activeTurnId, text, isFinal);
        }

        // Seule la première transcription finale déclenche la décision de routage
        if (isFinal) {
            if (_finalTranscriptionReceivedForTurn) {
                return; // Protection contre les doubles finales
            }
            _finalTranscriptionReceivedForTurn = true;
            routeText(text);
        }
    });

    _voice->onSpeechStarted([this]() {
        _speechWaitingStart = false;
        _speechStartTimeMs = millis();
        if (_state == PipelineState::WAITING_CONFIRMATION) {
            // Ne pas écraser l'état d'attente de confirmation lors de l'annonce vocale
            return;
        }
        if (_state != PipelineState::SPEAKING) {
            setState(PipelineState::SPEAKING);
        }
        if (_onSpeechStarted) {
            _onSpeechStarted(_activeTurnId, _voice->getActiveSpeechId());
        }
    });

    _voice->onSpeechFinishedWithId([this](uint32_t finishedSpeechId) {
        if (_cancelledTurnId != 0 && _activeTurnId == _cancelledTurnId) {
            // Tour annulé : fin résiduelle ignorée
            return;
        }

        if (_activeSpeechId != 0 && finishedSpeechId != 0 && finishedSpeechId != _activeSpeechId) {
            // Notification associée à une restitution antérieure : ignorée
            return;
        }

        if (_state == PipelineState::WAITING_CONFIRMATION) {
            // Annonce vocale de confirmation achevée : maintenir l'état WAITING_CONFIRMATION
            return;
        }

        _speechWaitingStart = false;
        _activeSpeechId = 0;

        if (_onSpeechFinished) {
            _onSpeechFinished(_activeTurnId, finishedSpeechId);
        }

        if (_state == PipelineState::SPEAKING) {
            if (_isAiSpeechActive) {
                // Pour une réponse IA, mémoriser dans ReplyMemory après restitution complète
                if (_lastAiResponse.isEmpty()) {
                    _lastAiResponse = _voice->getLastAssistantText();
                }
                if (_shouldRememberReply && _voiceUx != nullptr && !_lastAiResponse.isEmpty()) {
                    _voiceUx->rememberReply(_lastAiResponse);
                }
                if (_onAiResponse && !_lastAiResponse.isEmpty()) {
                    _onAiResponse(_activeTurnId, _lastAiResponse);
                }
                _isAiSpeechActive = false;
                _shouldRememberReply = false;
                finishTurn(PipelineResult::COMPLETED, _lastAiResponse);
            } else {
                if (_shouldRememberReply && _voiceUx != nullptr && !_lastSpokenText.isEmpty()) {
                    _voiceUx->rememberReply(_lastSpokenText);
                }
                _shouldRememberReply = false;
                finishTurn(_lastResult == PipelineResult::NONE ? PipelineResult::COMPLETED : _lastResult, _lastSpokenText);
            }
        }
    });

    _voice->onAssistantText([this](const String& text) {
        if (_cancelledTurnId != 0 && _activeTurnId == _cancelledTurnId) {
            return;
        }
        _lastAiResponse = text;
        if (_onAiResponse) {
            _onAiResponse(_activeTurnId, _lastAiResponse);
        }
    });

    _voice->onError([this](const String& err) {
        if (_cancelledTurnId != 0 && _activeTurnId == _cancelledTurnId) {
            return;
        }
        if (_state != PipelineState::READY) {
            _lastResult = PipelineResult::FAILED;
            _speechWaitingStart = false;
            _isAiSpeechActive = false;
            _shouldRememberReply = false;
            if (_onError) {
                _onError(_activeTurnId, err);
            }
            finishTurn(PipelineResult::FAILED, err);
            setState(PipelineState::ERROR);
        }
    });

    setState(PipelineState::READY);
    return true;
}

void IntegratedVoicePipeline::setState(PipelineState newState) {
    if (_state != newState) {
        PipelineState old = _state;
        _state = newState;
        _stateStartTimeMs = millis();
        if (_onStateChanged) {
            _onStateChanged(old, newState);
        }
    }
}

bool IntegratedVoicePipeline::startListening() {
    if (_state != PipelineState::READY && _state != PipelineState::WAITING_CONFIRMATION &&
        _state != PipelineState::ERROR && !_isPaused) {
        return false;
    }

    // Si une confirmation était en attente, l'annuler proprement
    if (_state == PipelineState::WAITING_CONFIRMATION) {
        rejectPendingAction();
    }

    _activeTurnId = ++_turnCounter;
    _cancelledTurnId = 0;
    _lastResult = PipelineResult::NONE;
    _lastDestination = PipelineDestination::NONE;
    _lastTranscription = "";
    _lastSpokenText = "";
    _lastAiResponse = "";
    _lastToolResult = ToolResult();
    _pendingConfirmationToken = "";
    _pendingToolId = "";
    _isAiSpeechActive = false;
    _speechWaitingStart = false;
    _shouldRememberReply = false;
    _finalTranscriptionReceivedForTurn = false;

    setState(PipelineState::LISTENING);

    if (_voice != nullptr) {
        if (!_voice->startListening()) {
            setState(PipelineState::READY);
            return false;
        }
        _activeTurnId = _voice->getActiveCaptureTurnId();
        if (_activeTurnId == 0) {
            _activeTurnId = _turnCounter;
        }
    }

    if (_onListeningStarted) {
        _onListeningStarted(_activeTurnId);
    }
    return true;
}

bool IntegratedVoicePipeline::stopListening() {
    if (_state != PipelineState::LISTENING) {
        return false;
    }

    setState(PipelineState::TRANSCRIBING);

    if (_onListeningFinished) {
        _onListeningFinished(_activeTurnId);
    }

    if (_voice != nullptr) {
        return _voice->stopListening();
    }
    return true;
}

void IntegratedVoicePipeline::cancel() {
    _cancelledTurnId = _activeTurnId;
    _isPaused = false;
    _speechWaitingStart = false;
    _shouldRememberReply = false;

    if (_tools != nullptr) {
        _tools->cancelPending();
    }

    if (_voice != nullptr) {
        _voice->stop();
    }

    _pendingConfirmationToken = "";
    _pendingToolId = "";
    _isAiSpeechActive = false;

    if (_onStopped) {
        _onStopped(_cancelledTurnId);
    }

    finishTurn(PipelineResult::CANCELLED, "Interruption STOP demandee");

    if (_voice != nullptr && _voice->isBusy()) {
        setState(PipelineState::STOPPING);
    } else {
        setState(PipelineState::READY);
    }
}

void IntegratedVoicePipeline::update() {
    if (_voice != nullptr) {
        _voice->update();
    }
    if (_tools != nullptr) {
        _tools->update();
    }
    if (_voiceUx != nullptr) {
        _voiceUx->update();
    }

    uint32_t now = millis();

    // Surveillance des timeouts d'états
    switch (_state) {
        case PipelineState::TRANSCRIBING:
            if (_config.sttTimeoutMs > 0 && (now - _stateStartTimeMs > _config.sttTimeoutMs)) {
                if (_voice != nullptr) {
                    _voice->cancelListening();
                }
                finishTurn(PipelineResult::TIMEOUT, "Delai de transcription STT depasse");
            }
            break;

        case PipelineState::AI_PROCESSING:
            if (_config.aiResponseTimeoutMs > 0 && (now - _stateStartTimeMs > _config.aiResponseTimeoutMs)) {
                if (_voice != nullptr) {
                    _voice->stop();
                }
                _isAiSpeechActive = false;
                finishTurn(PipelineResult::TIMEOUT, "Delai de reponse IA depasse");
            }
            break;

        case PipelineState::WAITING_CONFIRMATION:
            if (_config.confirmationTimeoutMs > 0 && (now - _confirmationStartTimeMs > _config.confirmationTimeoutMs)) {
                if (_tools != nullptr) {
                    _tools->cancelPending();
                }
                _pendingConfirmationToken = "";
                _pendingToolId = "";
                finishTurn(PipelineResult::TIMEOUT, "Delai de confirmation expire");
            }
            break;

        case PipelineState::SPEAKING:
            if (_speechWaitingStart && _config.ttsStartTimeoutMs > 0 && (now - _stateStartTimeMs > _config.ttsStartTimeoutMs)) {
                if (_voice != nullptr) {
                    _voice->stop();
                }
                _speechWaitingStart = false;
                finishTurn(PipelineResult::TIMEOUT, "Timeout demarrage restitution vocale");
            } else if (_config.ttsPlaybackTimeoutMs > 0 && _speechStartTimeMs > 0 && (now - _speechStartTimeMs > _config.ttsPlaybackTimeoutMs)) {
                if (_voice != nullptr) {
                    _voice->stop();
                }
                _speechWaitingStart = false;
                finishTurn(PipelineResult::TIMEOUT, "Timeout restitution vocale");
            }
            break;

        case PipelineState::STOPPING:
            if (_voice == nullptr || !_voice->isBusy()) {
                setState(PipelineState::READY);
            }
            break;

        default:
            break;
    }
}

void IntegratedVoicePipeline::routeText(const String& text) {
    setState(PipelineState::ROUTING);

    // 1. Filtrage TextGate
    if (_voiceUx != nullptr) {
        String normText;
        String reason;
        TextGateDecision gate = _voiceUx->getTextGate().evaluate(text, &normText, &reason);
        if (gate == TextGateDecision::IGNORE) {
            _lastDestination = PipelineDestination::REJECTED;
            if (_onRoutingDecision) {
                _onRoutingDecision(_activeTurnId, PipelineDestination::REJECTED, "Filtre TextGate: " + reason);
            }
            finishTurn(PipelineResult::REJECTED, "Texte vide ou hesitation ignoree");
            return;
        }
    }

    // 2. Intentions conversationnelles locales Voice UX (STOP, REPEAT, PREVIOUS, PAUSE, RESUME)
    if (_config.enableVoiceUx && _voiceUx != nullptr) {
        VoiceUxIntent intent = _voiceUx->getIntentRouter().route(text);
        if (intent != VoiceUxIntent::NONE) {
            switch (intent) {
                case VoiceUxIntent::STOP:
                    _lastDestination = PipelineDestination::LOCAL_UX;
                    if (_onRoutingDecision) {
                        _onRoutingDecision(_activeTurnId, PipelineDestination::LOCAL_UX, "STOP");
                    }
                    cancel();
                    return;

                case VoiceUxIntent::RESUME:
                    _isPaused = false;
                    _lastDestination = PipelineDestination::LOCAL_UX;
                    if (_onRoutingDecision) {
                        _onRoutingDecision(_activeTurnId, PipelineDestination::LOCAL_UX, "RESUME");
                    }
                    setState(PipelineState::LOCAL_PROCESSING);
                    speakOutput("Reprise.", false, false);
                    return;

                case VoiceUxIntent::PAUSE:
                    _isPaused = true;
                    _lastDestination = PipelineDestination::LOCAL_UX;
                    if (_onRoutingDecision) {
                        _onRoutingDecision(_activeTurnId, PipelineDestination::LOCAL_UX, "PAUSE");
                    }
                    setState(PipelineState::LOCAL_PROCESSING);
                    speakOutput("En pause.", false, false);
                    return;

                case VoiceUxIntent::REPEAT: {
                    if (_isPaused) {
                        _lastDestination = PipelineDestination::REJECTED;
                        finishTurn(PipelineResult::REJECTED, "Pipeline en pause");
                        return;
                    }
                    _lastDestination = PipelineDestination::LOCAL_UX;
                    if (_onRoutingDecision) {
                        _onRoutingDecision(_activeTurnId, PipelineDestination::LOCAL_UX, "REPEAT");
                    }
                    setState(PipelineState::LOCAL_PROCESSING);
                    String reply = _voiceUx->getReplyMemory().getLastReply();
                    if (reply.isEmpty()) {
                        speakOutput("Aucune reponse precedente a repeter.", false, false);
                    } else {
                        speakOutput(reply, false, false); // Une répétition n'est pas réinsérée en mémoire
                    }
                    return;
                }

                case VoiceUxIntent::PREVIOUS: {
                    if (_isPaused) {
                        _lastDestination = PipelineDestination::REJECTED;
                        finishTurn(PipelineResult::REJECTED, "Pipeline en pause");
                        return;
                    }
                    _lastDestination = PipelineDestination::LOCAL_UX;
                    if (_onRoutingDecision) {
                        _onRoutingDecision(_activeTurnId, PipelineDestination::LOCAL_UX, "PREVIOUS");
                    }
                    setState(PipelineState::LOCAL_PROCESSING);
                    String reply = _voiceUx->getReplyMemory().getPreviousReply();
                    if (reply.isEmpty()) {
                        speakOutput("Aucune reponse precedente disponible.", false, false);
                    } else {
                        speakOutput(reply, false, false); // Une répétition n'est pas réinsérée en mémoire
                    }
                    return;
                }

                default:
                    break;
            }
        }
    }

    // Si le pipeline est actuellement en pause, rejeter toute commande ordinaire
    if (_isPaused) {
        _lastDestination = PipelineDestination::REJECTED;
        if (_onRoutingDecision) {
            _onRoutingDecision(_activeTurnId, PipelineDestination::REJECTED, "Pipeline en pause");
        }
        _lastResult = PipelineResult::REJECTED;
        if (_config.speakRejection) {
            speakOutput("En pause.", false, false);
        } else {
            finishTurn(PipelineResult::REJECTED, "Pipeline en pause");
        }
        return;
    }

    // 3. Commandes applicatives locales enregistrées (ToolRegistry + Dispatcher)
    if (_config.enableLocalTools && _tools != nullptr) {
        VoiceCommandMatch match = _dispatcher.match(text);
        if (match.matched) {
            const ToolDefinition* def = _tools->findTool(match.toolId);
            if (def != nullptr) {
                _lastDestination = PipelineDestination::LOCAL_TOOL;
                if (_onCommandRecognized) {
                    _onCommandRecognized(_activeTurnId, match.toolId, match.args);
                }
                if (_onRoutingDecision) {
                    _onRoutingDecision(_activeTurnId, PipelineDestination::LOCAL_TOOL, match.toolId);
                }

                // Vérification politique SENSITIVE : la protection de ToolRegistry ne peut pas être contournée
                if (def->securityPolicy == ToolSecurityPolicy::SENSITIVE) {
                    if (_config.requireConfirmationForSensitive) {
                        // Demander exécution pour générer le jeton de confirmation sécurisé
                        ToolResult reqRes = _tools->execute(match.toolId, match.args);
                        if (reqRes.isConfirmationRequired()) {
                            _pendingConfirmationToken = reqRes.confirmationToken;
                            _pendingToolId = match.toolId;
                            _pendingToolArgs = match.args;
                            _confirmationStartTimeMs = millis();
                            setState(PipelineState::WAITING_CONFIRMATION);

                            if (_onConfirmationRequired) {
                                _onConfirmationRequired(_activeTurnId, match.toolId, _pendingConfirmationToken, _config.confirmationPrompt);
                            }
                            if (_config.speakConfirmationRequired && _voice != nullptr) {
                                _voice->say(_config.confirmationPrompt);
                            }
                            return;
                        }
                    } else {
                        // Si la politique de confirmation sensible est désactivée au niveau pipeline,
                        // ToolRegistry refusera l'exécution sans jeton. Rejet explicite.
                        _lastDestination = PipelineDestination::REJECTED;
                        _lastResult = PipelineResult::REJECTED;
                        finishTurn(PipelineResult::REJECTED, "Action sensible sans confirmation explicite interdite");
                        return;
                    }
                }

                // Exécution nominale directe (READ_ONLY ou WRITE)
                executeTool(match.toolId, match.args);
                return;
            }
        }
    }

    // 4. Choix par défaut lorsque aucune commande locale n'est reconnue
    if (_config.defaultDestination == PipelineDefaultDestination::AI && _config.enableAiProcessing && _voice != nullptr) {
        _lastDestination = PipelineDestination::AI_PROVIDER;
        if (_onRoutingDecision) {
            _onRoutingDecision(_activeTurnId, PipelineDestination::AI_PROVIDER, text);
        }
        transmitToAi(text);
        return;
    }

    // 5. Refus explicite si destination par défaut REJECT ou IA indisponible
    _lastDestination = PipelineDestination::REJECTED;
    if (_onRoutingDecision) {
        _onRoutingDecision(_activeTurnId, PipelineDestination::REJECTED, "Commande non reconnue");
    }
    _lastResult = PipelineResult::REJECTED;
    if (_config.speakRejection) {
        speakOutput(_config.unknownCommandResponse, false, false);
    } else {
        finishTurn(PipelineResult::REJECTED, "Demande refusee");
    }
}

void IntegratedVoicePipeline::executeTool(const String& toolId, const ToolArgs& args) {
    setState(PipelineState::TOOL_EXECUTING);
    if (_onToolExecutionStarted) {
        _onToolExecutionStarted(_activeTurnId, toolId);
    }

    ToolResult res = _tools->execute(toolId, args);
    _lastToolResult = res;

    if (_onToolResult) {
        _onToolResult(_activeTurnId, toolId, res);
    }

    if (res.isSuccess()) {
        _lastResult = PipelineResult::COMPLETED;
        String spoken = res.output;
        if (spoken.isEmpty()) {
            spoken = "Commande executee.";
        }
        if (_config.speakToolResult) {
            speakOutput(spoken, false, true); // Les réponses d'outils sont enregistrées dans ReplyMemory
        } else {
            finishTurn(PipelineResult::COMPLETED, res.output);
        }
    } else {
        _lastResult = PipelineResult::FAILED;
        String errSpoken = res.error.isEmpty() ? _config.toolExecutionFailedResponse : res.error;
        if (_config.speakToolResult) {
            speakOutput(errSpoken, false, false);
        } else {
            finishTurn(PipelineResult::FAILED, res.error);
        }
    }
}

void IntegratedVoicePipeline::transmitToAi(const String& text) {
    setState(PipelineState::AI_PROCESSING);
    _isAiSpeechActive = true;
    _lastAiResponse = "";
    _shouldRememberReply = true;

    if (_voice != nullptr) {
        if (!_voice->askAi(text)) {
            _lastResult = PipelineResult::FAILED;
            _isAiSpeechActive = false;
            _shouldRememberReply = false;
            finishTurn(PipelineResult::FAILED, "Echec transmission vers service IA");
        }
    } else {
        _lastResult = PipelineResult::FAILED;
        _isAiSpeechActive = false;
        _shouldRememberReply = false;
        finishTurn(PipelineResult::FAILED, "Moteur vocal indisponible");
    }
}

String IntegratedVoicePipeline::truncateUtf8Safe(const String& str, size_t maxBytes) {
    if (maxBytes == 0 || str.length() <= maxBytes) {
        return str;
    }
    size_t targetLen = (maxBytes > 3) ? (maxBytes - 3) : maxBytes;
    if (targetLen >= str.length()) {
        return str;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(str.c_str());
    // Reculer si l'octet cible est un octet de continuation UTF-8 (0x80..0xBF)
    while (targetLen > 0 && (bytes[targetLen] & 0xC0) == 0x80) {
        targetLen--;
    }

    // Tenter de couper au dernier espace pour ne pas tronquer un mot en deux
    size_t lastSpace = 0;
    for (size_t i = 0; i < targetLen; ++i) {
        if (bytes[i] == ' ') {
            lastSpace = i;
        }
    }
    if (lastSpace > targetLen / 2) {
        targetLen = lastSpace;
    }

    String truncated = str.substring(0, targetLen);
    truncated.trim();
    truncated += "...";
    return truncated;
}

void IntegratedVoicePipeline::speakOutput(const String& text, bool isAiResponse, bool rememberReply) {
    if (text.isEmpty()) {
        finishTurn(_lastResult == PipelineResult::NONE ? PipelineResult::COMPLETED : _lastResult, "");
        return;
    }

    String safeText = text;
    if (_config.maxSpeechLength > 0 && safeText.length() > _config.maxSpeechLength) {
        safeText = truncateUtf8Safe(safeText, _config.maxSpeechLength);
    }

    _lastSpokenText = safeText;
    _isAiSpeechActive = isAiResponse;
    _shouldRememberReply = rememberReply;

    if (_voice != nullptr) {
        bool sayOk = _voice->say(safeText);
        if (!sayOk) {
            _lastResult = PipelineResult::FAILED;
            _isAiSpeechActive = false;
            _shouldRememberReply = false;
            _speechWaitingStart = false;
            finishTurn(PipelineResult::FAILED, "Refus restitution vocale (say)");
            return;
        }
        _activeSpeechId = _voice->getActiveSpeechId();
        _speechWaitingStart = true;
        setState(PipelineState::SPEAKING);
    } else {
        // En environnement de test sans voix physique
        setState(PipelineState::SPEAKING);
        if (_shouldRememberReply && _voiceUx != nullptr && !safeText.isEmpty()) {
            _voiceUx->rememberReply(safeText);
        }
        _shouldRememberReply = false;
        finishTurn(_lastResult == PipelineResult::NONE ? PipelineResult::COMPLETED : _lastResult, safeText);
    }
}

bool IntegratedVoicePipeline::confirmPendingAction(const String& token) {
    if (_state != PipelineState::WAITING_CONFIRMATION || _tools == nullptr) {
        return false;
    }

    String tok = token.isEmpty() ? _pendingConfirmationToken : token;
    ToolResult res = _tools->confirm(tok);
    _lastToolResult = res;

    if (_onToolResult) {
        _onToolResult(_activeTurnId, _pendingToolId, res);
    }

    if (res.isSuccess()) {
        _pendingConfirmationToken = "";
        _pendingToolId = "";
        _lastResult = PipelineResult::COMPLETED;
        String spoken = res.output.isEmpty() ? "Action confirmee et executee." : res.output;
        if (_config.speakToolResult) {
            speakOutput(spoken, false, true);
        } else {
            finishTurn(PipelineResult::COMPLETED, res.output);
        }
        return true;
    } else {
        // En cas d'erreur sur un mauvais jeton, ne pas solder immédiatement le tour
        // afin de permettre une nouvelle tentative avant expiration du délai.
        if (res.error.indexOf("expire") >= 0 || res.error.indexOf("deja utilise") >= 0) {
            _pendingConfirmationToken = "";
            _pendingToolId = "";
            _lastResult = PipelineResult::FAILED;
            if (_config.speakToolResult) {
                speakOutput(res.error, false, false);
            } else {
                finishTurn(PipelineResult::FAILED, res.error);
            }
        }
        return false;
    }
}

bool IntegratedVoicePipeline::rejectPendingAction(const String& token) {
    if (_state != PipelineState::WAITING_CONFIRMATION) {
        return false;
    }

    if (_tools != nullptr) {
        _tools->cancelPending();
    }

    _pendingConfirmationToken = "";
    _pendingToolId = "";
    _lastResult = PipelineResult::CANCELLED;

    finishTurn(PipelineResult::CANCELLED, "Confirmation rejetee par utilisateur");
    return true;
}

bool IntegratedVoicePipeline::recoverFromError() {
    if (_state == PipelineState::ERROR) {
        _cancelledTurnId = 0;
        _isAiSpeechActive = false;
        _speechWaitingStart = false;
        _shouldRememberReply = false;
        _pendingConfirmationToken = "";
        setState(PipelineState::READY);
        return true;
    }
    return false;
}

void IntegratedVoicePipeline::finishTurn(PipelineResult result, const String& summary) {
    uint32_t reportingTurn = (_cancelledTurnId != 0) ? _cancelledTurnId : _activeTurnId;

    if (reportingTurn == 0 || reportingTurn == _lastFinishedTurnId) {
        // Idempotence stricte : un tour ne peut avoir qu'une seule terminaison
        return;
    }

    _lastFinishedTurnId = reportingTurn;
    _lastResult = result;
    _speechWaitingStart = false;

    if (_onTurnFinished && reportingTurn > 0) {
        _onTurnFinished(reportingTurn, result, summary);
    }

    setState(PipelineState::READY);
}

void IntegratedVoicePipeline::feedSimulatedTranscription(const String& text, bool isFinal) {
    if (_state != PipelineState::LISTENING && _state != PipelineState::TRANSCRIBING) {
        _activeTurnId = (_voice != nullptr && _voice->getActiveCaptureTurnId() != 0) 
                        ? _voice->getActiveCaptureTurnId() 
                        : ++_turnCounter;
        setState(PipelineState::TRANSCRIBING);
    }

    if (_voice != nullptr) {
        _voice->handleIncomingTranscription(text, isFinal);
    }

    // Si VoxCoreVoice a ignoré la transcription comme résidu d'un ancien tour annulé,
    // mais que le pipeline est activement en attente d'une transcription pour le tour courant :
    if (_state == PipelineState::TRANSCRIBING) {
        _lastTranscription = text;
        if (_onTranscription) {
            _onTranscription(_activeTurnId, text, isFinal);
        }

        if (isFinal) {
            if (_finalTranscriptionReceivedForTurn) {
                return; // Protection contre les doubles finales
            }
            _finalTranscriptionReceivedForTurn = true;
            routeText(text);
        }
    }
}
