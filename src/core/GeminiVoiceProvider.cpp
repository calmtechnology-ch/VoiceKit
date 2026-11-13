#include "GeminiVoiceProvider.h"

GeminiVoiceProvider::GeminiVoiceProvider(GeminiLiveClient* client)
    : _client(client) {
    if (!_client) {
        _ownedClient = std::unique_ptr<GeminiLiveClient>(new GeminiLiveClient());
        _client = _ownedClient.get();
    }
}

bool GeminiVoiceProvider::begin(const VoiceKitConfig& config) {
    if (!_client) return false;

    _conversationMode = config.enableConversation;

    if (!_client->begin(config.apiKey.c_str(), config.model.c_str(), config.voice.c_str())) {
        return false;
    }

    _client->setLanguage(config.language);
    _client->setVoiceDirectives(config.buildVoiceDirectives());
    _client->setSystemInstruction(config.enableConversation ? config.conversationInstruction : config.systemInstruction);

    return true;
}

bool GeminiVoiceProvider::connect() {
    return _client ? _client->connect() : false;
}

void GeminiVoiceProvider::disconnect() {
    if (_client) _client->disconnect();
}

void GeminiVoiceProvider::requestDisconnect() {
    if (_client) _client->requestDisconnect();
}

bool GeminiVoiceProvider::pollDisconnect() {
    return _client ? _client->pollDisconnect() : true;
}

bool GeminiVoiceProvider::isDisconnectPending() const {
    return _client ? _client->isDisconnectPending() : false;
}

void GeminiVoiceProvider::tick() {
    if (_client) _client->tick();
}

uint32_t GeminiVoiceProvider::getLastWsProcessDurationUs() const {
    return _client ? _client->getLastWsProcessDurationUs() : 0;
}

uint32_t GeminiVoiceProvider::getMaxWsProcessDurationUs() const {
    return _client ? _client->getMaxWsProcessDurationUs() : 0;
}

bool GeminiVoiceProvider::isConnected() const {
    return _client ? _client->isConnected() : false;
}

bool GeminiVoiceProvider::isSetupComplete() const {
    return _client ? _client->isSetupComplete() : false;
}

const char* GeminiVoiceProvider::getLastError() const {
    return _client ? _client->getLastError() : "Client non initialise";
}

void GeminiVoiceProvider::setSystemInstruction(const String& instruction) {
    if (_client) _client->setSystemInstruction(instruction);
}

const String& GeminiVoiceProvider::getSystemInstruction() const {
    static const String empty = "";
    return _client ? _client->getSystemInstruction() : empty;
}

void GeminiVoiceProvider::setModel(const String& model) {
    if (_client) _client->setModel(model);
}

const String& GeminiVoiceProvider::getModel() const {
    static const String empty = "";
    return _client ? _client->getModel() : empty;
}

void GeminiVoiceProvider::setVoice(const String& voice) {
    if (_client) _client->setVoice(voice);
}

const String& GeminiVoiceProvider::getVoice() const {
    static const String empty = "";
    return _client ? _client->getVoice() : empty;
}

void GeminiVoiceProvider::setLanguage(const String& language) {
    if (_client) _client->setLanguage(language);
}

const String& GeminiVoiceProvider::getLanguage() const {
    static const String empty = "";
    return _client ? _client->getLanguage() : empty;
}

void GeminiVoiceProvider::setVoiceDirectives(const String& directives) {
    if (_client) _client->setVoiceDirectives(directives);
}

bool GeminiVoiceProvider::sendTextMessage(const String& text) {
    return _client ? _client->sendTextMessage(text) : false;
}

bool GeminiVoiceProvider::sendActivityStart() {
    return _client ? _client->sendActivityStart() : false;
}

bool GeminiVoiceProvider::sendAudioChunk(const int16_t* samples, size_t sampleCount) {
    return _client ? _client->sendAudioChunk(samples, sampleCount) : false;
}

bool GeminiVoiceProvider::sendActivityEnd() {
    return _client ? _client->sendActivityEnd() : false;
}

bool GeminiVoiceProvider::sendTurnComplete() {
    return _client ? _client->sendTurnComplete() : false;
}

bool GeminiVoiceProvider::sendInterruption() {
    return _client ? _client->sendInterruption() : false;
}

void GeminiVoiceProvider::setOnConnected(VoidCallback cb) {
    if (_client) _client->setOnConnected(std::move(cb));
}

void GeminiVoiceProvider::setOnSetupComplete(VoidCallback cb) {
    if (_client) _client->setOnSetupComplete(std::move(cb));
}

void GeminiVoiceProvider::setOnDisconnected(VoidCallback cb) {
    if (_client) _client->setOnDisconnected(std::move(cb));
}

void GeminiVoiceProvider::setOnAudioData(AudioCallback cb) {
    if (_client) _client->setOnAudioData(std::move(cb));
}

void GeminiVoiceProvider::setOnText(TextCallback cb) {
    if (_client) _client->setOnText(std::move(cb));
}

void GeminiVoiceProvider::setOnInputTranscription(TranscriptionCallback cb) {
    if (_client) _client->setOnInputTranscription(std::move(cb));
}

void GeminiVoiceProvider::setOnOutputTranscription(OutputTranscriptionCallback cb) {
    _onOutputTranscription = std::move(cb);
    if (!_onOutputTranscription && !_onOutputTranscriptionWithFinal) {
        if (_client) _client->setOnOutputTranscription(nullptr);
        return;
    }
    if (_client) {
        _client->setOnOutputTranscription([this](const String& text) {
            if (_onOutputTranscriptionWithFinal) {
                _onOutputTranscriptionWithFinal(text, false);
            } else if (_onOutputTranscription) {
                _onOutputTranscription(text);
            }
        });
    }
}

void GeminiVoiceProvider::setOnOutputTranscriptionWithFinal(OutputTranscriptionWithFinalCallback cb) {
    _onOutputTranscriptionWithFinal = std::move(cb);
    if (!_onOutputTranscription && !_onOutputTranscriptionWithFinal) {
        if (_client) _client->setOnOutputTranscription(nullptr);
        return;
    }
    if (_client) {
        _client->setOnOutputTranscription([this](const String& text) {
            if (_onOutputTranscriptionWithFinal) {
                _onOutputTranscriptionWithFinal(text, false);
            } else if (_onOutputTranscription) {
                _onOutputTranscription(text);
            }
        });
    }
}

void GeminiVoiceProvider::setOnTurnComplete(VoidCallback cb) {
    if (_client) _client->setOnTurnComplete(std::move(cb));
}

void GeminiVoiceProvider::setOnInterrupted(VoidCallback cb) {
    if (_client) _client->setOnInterrupted(std::move(cb));
}

void GeminiVoiceProvider::setOnError(ErrorCallback cb) {
    if (_client) _client->setOnError(std::move(cb));
}

uint32_t GeminiVoiceProvider::getWsConnectMs() const {
    return _client ? _client->getWsConnectMs() : 0;
}

uint32_t GeminiVoiceProvider::getSetupCompleteMs() const {
    return _client ? _client->getSetupCompleteMs() : 0;
}

uint32_t GeminiVoiceProvider::getConnectAttempts() const {
    return _client ? _client->getConnectAttempts() : 0;
}

uint32_t GeminiVoiceProvider::getActivityEndSentMs() const {
    return _client ? _client->getActivityEndSentMs() : 0;
}

uint32_t GeminiVoiceProvider::getFirstResponseReceivedMs() const {
    return _client ? _client->getFirstResponseReceivedMs() : 0;
}

uint32_t GeminiVoiceProvider::getFirstAudioReceivedMs() const {
    return _client ? _client->getFirstAudioReceivedMs() : 0;
}

void GeminiVoiceProvider::resetTurnRxMetrics() {
    if (_client) _client->resetTurnRxMetrics();
}

uint32_t GeminiVoiceProvider::getTurnDecodedSamples() const {
    return _client ? _client->getTurnDecodedSamples() : 0;
}

uint32_t GeminiVoiceProvider::getPostTurnCompleteBlocks() const {
    return _client ? _client->getPostTurnCompleteBlocks() : 0;
}

uint32_t GeminiVoiceProvider::getPostTurnCompleteSamples() const {
    return _client ? _client->getPostTurnCompletePcmSamples() : 0;
}

bool GeminiVoiceProvider::isTurnCompleteReceived() const {
    return _client ? _client->isTurnCompleteReceived() : false;
}

int8_t GeminiVoiceProvider::getWsTaskCore() const {
    return _client ? _client->getWsTaskCore() : -1;
}

uint8_t GeminiVoiceProvider::getWsTaskPriority() const {
    return _client ? _client->getWsTaskPriority() : 0;
}

uint8_t GeminiVoiceProvider::getWsHandshakeTaskPriority() const {
    return _client ? _client->getWsHandshakeTaskPriority() : 1;
}

uint8_t GeminiVoiceProvider::getWsSteadyTaskPriority() const {
    return _client ? _client->getWsSteadyTaskPriority() : 2;
}

uint32_t GeminiVoiceProvider::getFreeInternalHeapDuringTls() const {
    return _client ? _client->getFreeInternalHeapDuringTls() : 0;
}

uint32_t GeminiVoiceProvider::getMinFreeInternalHeapDuringTls() const {
    return _client ? _client->getMinFreeInternalHeapDuringTls() : 0;
}

uint32_t GeminiVoiceProvider::getLargestInternalBlockDuringTls() const {
    return _client ? _client->getLargestInternalBlockDuringTls() : 0;
}

void GeminiVoiceProvider::setTransport(ILiveTransport* transport) {
    if (_client) _client->setTransport(transport);
}

void GeminiVoiceProvider::handleIncomingMessage(const char* data, size_t len) {
    if (_client) _client->handleIncomingMessage(data, len);
}
