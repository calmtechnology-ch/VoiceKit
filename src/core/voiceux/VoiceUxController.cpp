#include "VoiceUxController.h"

VoiceUxController::VoiceUxController()
    : _enabled(false),
      _callback(nullptr) {
}

bool VoiceUxController::begin() {
    _enabled = false; // Désactivé par défaut conformément aux exigences
    resetStats();
    _lastResult = VoiceUxResult();
    return true;
}

void VoiceUxController::setEnabled(bool enabled) {
    _enabled = enabled;
}

bool VoiceUxController::isEnabled() const {
    return _enabled;
}

VoiceUxResult VoiceUxController::processFinalText(const String& rawText) {
    uint32_t startUs = micros();
    VoiceUxResult res;
    res.rawText = rawText;
    res.timestampMs = millis();

    // Si le module est désactivé, laisser passer le texte sans traitement
    if (!_enabled) {
        res.gateDecision = TextGateDecision::ACCEPT;
        res.intent = VoiceUxIntent::NONE;
        res.reason = "Module Voice UX desactive";
        res.handledLocally = false;
        res.processingTimeUs = micros() - startUs;
        _lastResult = res;
        return res;
    }

    _stats.processedCount++;

    // 1. Évaluation par TextGate (filtrage déterministe)
    String normalized;
    String gateReason;
    res.gateDecision = _gate.evaluate(rawText, &normalized, &gateReason);
    res.normalizedText = normalized;
    res.reason = gateReason;

    // Si TextGate rejette (vide ou hésitation isolée)
    if (res.gateDecision == TextGateDecision::IGNORE) {
        _stats.ignoredCount++;
        res.intent = VoiceUxIntent::NONE;
        res.handledLocally = true; // Filtré localement : pas d'action utile
        res.processingTimeUs = micros() - startUs;
        _lastResult = res;
        if (_callback) {
            _callback(res);
        }
        return res;
    }

    // Transcription acceptée (ACCEPT ou INCOMPLETE informatif)
    _stats.acceptedCount++;

    // 2. Routage d'intention conversationnelle
    res.intent = _router.route(normalized);

    if (res.intent != VoiceUxIntent::NONE) {
        _stats.recognizedIntentCount++;
        res.handledLocally = true;
        res.reason = String("Intention locale ") + voiceUxIntentToString(res.intent) + " reconnue";

        // Résolution de mémoire pour les intentions de rappel
        if (res.intent == VoiceUxIntent::REPEAT) {
            res.recallText = _memory.getLastReply();
        } else if (res.intent == VoiceUxIntent::PREVIOUS) {
            res.recallText = _memory.getPreviousReply();
        }
    } else {
        // Phrase ordinaire ou question utilisateur
        res.handledLocally = false;
        if (res.gateDecision == TextGateDecision::INCOMPLETE) {
            res.reason = "Texte potentiellement incomplet (transmis)";
        } else {
            res.reason = "Texte valide sans commande locale";
        }
    }

    res.processingTimeUs = micros() - startUs;
    _lastResult = res;

    if (_callback) {
        _callback(res);
    }

    return res;
}

bool VoiceUxController::rememberReply(const String& reply) {
    if (!_enabled) {
        return false;
    }
    return _memory.addReply(reply);
}

void VoiceUxController::update() {
    // Non-bloquant : aucun calcul lourd ni attente active
}

void VoiceUxController::onResult(ResultCallback cb) {
    _callback = cb;
}

const VoiceUxResult& VoiceUxController::getLastResult() const {
    return _lastResult;
}

TextGate& VoiceUxController::getTextGate() {
    return _gate;
}

const TextGate& VoiceUxController::getTextGate() const {
    return _gate;
}

VoiceIntentRouter& VoiceUxController::getIntentRouter() {
    return _router;
}

const VoiceIntentRouter& VoiceUxController::getIntentRouter() const {
    return _router;
}

ReplyMemory& VoiceUxController::getReplyMemory() {
    return _memory;
}

const ReplyMemory& VoiceUxController::getReplyMemory() const {
    return _memory;
}

const VoiceUxStats& VoiceUxController::getStats() const {
    return _stats;
}

void VoiceUxController::resetStats() {
    _stats = VoiceUxStats();
}

