#pragma once
#include <Arduino.h>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>
#include <atomic>
#include "IVoiceProvider.h"
#include "ILiveTransport.h"
#include "VoiceKitConfig.h"

// Modèle et voix par défaut pour OpenAI Realtime (VoiceKit v0.6.0)
#ifndef OPENAI_REALTIME_DEFAULT_MODEL
#define OPENAI_REALTIME_DEFAULT_MODEL "gpt-realtime-2.1-mini"
#endif

#ifndef OPENAI_REALTIME_DEFAULT_VOICE
#define OPENAI_REALTIME_DEFAULT_VOICE "alloy" // alloy, echo, shimmer, ash, ballad, coral, sage, verse
#endif

/**
 * @class OpenAIRealtimeClient
 * @brief Client WebSocket TLS direct pour l'API OpenAI Realtime (VoiceKit v0.6.0).
 * 
 * Implémente l'interface IVoiceProvider pour M5Stack CoreS3 / ESP32-S3 :
 * - Point de terminaison officiel : wss://api.openai.com/v1/realtime?model=...
 * - Authentification via en-tête standard Authorization Bearer
 * - Masquage absolu des clés secrètes dans tous les journaux série
 * - Négociation de session via l'événement client session.update
 * - Streaming audio bidirectionnel PCM16 mono :
 *     - Émission : rééchantillonnage 16 kHz micro vers 24 kHz avec input_audio_buffer.append
 *     - Réception : décodage Base64 des paquets 24 kHz response.audio.delta
 * - Gestion des tours de parole :
 *     - Mode manuel / VAD local : input_audio_buffer.commit puis response.create
 *     - Interruption prioritaire STOP via response.cancel et vidage buffer
 * - Transcriptions synchronisées :
 *     - Entrée utilisateur via conversation.item.input_audio_transcription.completed (whisper-1)
 *     - Sortie assistant via response.audio_transcript.delta
 * - Suivi précis des métriques temporelles de latence et temps de connexion
 */
class OpenAIRealtimeClient : public IVoiceProvider {
public:
    explicit OpenAIRealtimeClient(ILiveTransport* customTransport = nullptr);
    ~OpenAIRealtimeClient() override;

    // Initialisation
    bool begin(const VoiceKitConfig& config) override;

    // Cycle de vie réseau
    bool connect() override;
    void disconnect() override;
    void requestDisconnect() override;
    bool pollDisconnect() override;
    bool isDisconnectPending() const override { return _transport ? _transport->isDisconnectPending() : false; }
    void tick() override;
    uint32_t getLastWsProcessDurationUs() const override { return _lastWsProcessDurationUs; }
    uint32_t getMaxWsProcessDurationUs() const override { return _maxWsProcessDurationUs; }

    // États
    bool isConnected() const override;
    bool isSetupComplete() const override { return _setupComplete.load(std::memory_order_acquire); }
    const char* getLastError() const override { return _lastError.c_str(); }
    VoiceProviderType getProviderType() const override { return VoiceProviderType::OPENAI; }

    // Configuration dynamique
    void setSystemInstruction(const String& instruction) override { _systemInstruction = instruction; }
    const String& getSystemInstruction() const override { return _systemInstruction; }
    void setModel(const String& model) override { _model = model; }
    const String& getModel() const override { return _model; }
    void setVoice(const String& voice) override { _voice = voice; }
    const String& getVoice() const override { return _voice; }
    void setLanguage(const String& language) override { _language = language; }
    const String& getLanguage() const override { return _language; }
    void setInputTranscriptionLanguage(const String& lang) { _inputTranscriptionLanguage = lang; }
    const String& getInputTranscriptionLanguage() const { return _inputTranscriptionLanguage; }
    void setVoiceDirectives(const String& directives) override { _voiceDirectives = directives; }
    void setConversationMode(bool enable) override { _conversationMode = enable; }
    bool isConversationMode() const override { return _conversationMode; }

    // Contraintes de session OpenAI Realtime pour changement dynamique de profil (v0.6.6)
    bool hasAudioEmittedInSession() const { return _audioEmittedInSession.load(std::memory_order_acquire); }
    const String& getConfirmedSessionVoice() const { return _confirmedSessionVoice; }
    bool canUpdateVoiceInCurrentSession(const String& targetVoice) const {
        if (!isConnected()) return false;
        if (!_audioEmittedInSession.load(std::memory_order_acquire)) return true;
        return !_confirmedSessionVoice.isEmpty() && targetVoice == _confirmedSessionVoice;
    }
    bool requestSessionUpdate();

    // Émission audio et textuelle
    bool sendTextMessage(const String& text) override;
    bool sendActivityStart() override;
    bool sendAudioChunk(const int16_t* samples, size_t sampleCount) override;
    bool sendActivityEnd() override;
    bool sendTurnComplete() override;
    bool sendInterruption() override;

    // Callbacks
    void setOnConnected(VoidCallback cb) override { _onConnected = std::move(cb); }
    void setOnSetupComplete(VoidCallback cb) override { _onSetupComplete = std::move(cb); }
    void setOnDisconnected(VoidCallback cb) override { _onDisconnected = std::move(cb); }
    void setOnAudioData(AudioCallback cb) override { _onAudioData = std::move(cb); }
    void setOnText(TextCallback cb) override { _onText = std::move(cb); }
    void setOnInputTranscription(TranscriptionCallback cb) override { _onInputTranscription = std::move(cb); }
    void setOnOutputTranscription(OutputTranscriptionCallback cb) override { _onOutputTranscription = std::move(cb); }
    void setOnOutputTranscriptionWithFinal(OutputTranscriptionWithFinalCallback cb) override { _onOutputTranscriptionWithFinal = std::move(cb); }
    void setOnTurnComplete(VoidCallback cb) override { _onTurnComplete = std::move(cb); }
    void setOnInterrupted(VoidCallback cb) override { _onInterrupted = std::move(cb); }
    void setOnError(ErrorCallback cb) override { _onError = std::move(cb); }

    // Métriques temporelles et audio RX par tour (v0.6.1 — Correction A)
    uint32_t getWsConnectMs() const override { return _wsConnectDurationMs; }
    uint32_t getSetupCompleteMs() const override { return _setupCompleteDurationMs; }
    uint32_t getConnectAttempts() const override { return _connectAttempts; }
    uint32_t getActivityEndSentMs() const override { return _lastActivityEndSentMs.load(std::memory_order_relaxed); }
    uint32_t getFirstResponseReceivedMs() const override { return _firstResponseReceivedMs.load(std::memory_order_relaxed); }
    uint32_t getFirstAudioReceivedMs() const override { return _firstAudioReceivedMs.load(std::memory_order_relaxed); }
    void resetTurnRxMetrics() override;
    uint32_t getTurnDecodedSamples() const override { return _pcmSamplesDispatchedTotal.load(std::memory_order_relaxed); }
    uint32_t getPcmSamplesDispatchedTotal() const { return _pcmSamplesDispatchedTotal.load(std::memory_order_relaxed); }
    uint32_t getPostTurnCompleteBlocks() const override { return _postTurnCompletePcmBlocks.load(std::memory_order_relaxed); }
    uint32_t getPostTurnCompleteSamples() const override { return _postTurnCompletePcmSamples.load(std::memory_order_relaxed); }
    bool isTurnCompleteReceived() const override { return _turnCompleteReceived.load(std::memory_order_acquire); }
    bool isWaitingForResponse() const override { return _waitingForResponse.load(std::memory_order_acquire); }
    bool isTurnCompleteBeforeLastPcm() const { return _turnCompleteBeforeLastPcm.load(std::memory_order_acquire); }

    // Diagnostics FreeRTOS & mémoire TLS (v0.6.1 — Correction B)
    // Diagnostics JSON & parsing (v0.6.3.2.1 - Correction A)
    uint32_t getJsonErrorCount() const { return _jsonErrorCount.load(std::memory_order_relaxed); }
    const String& getLastParseErrorCategory() const { return _lastParseErrorCategory; }
    void resetJsonErrorCount() { _jsonErrorCount.store(0, std::memory_order_relaxed); _lastParseErrorCategory = ""; }

    int8_t getWsTaskCore() const override { return _transport ? _transport->getTaskCore() : -1; }
    uint8_t getWsTaskPriority() const override { return _transport ? _transport->getTaskPriority() : 0; }
    uint8_t getWsHandshakeTaskPriority() const override { return _transport ? _transport->getHandshakeTaskPriority() : 1; }
    uint8_t getWsSteadyTaskPriority() const override { return _transport ? _transport->getSteadyTaskPriority() : 2; }
    uint32_t getFreeInternalHeapDuringTls() const override { return _transport ? _transport->getFreeInternalHeapAtConnect() : 0; }
    uint32_t getMinFreeInternalHeapDuringTls() const override { return _transport ? _transport->getMinFreeInternalHeapAtConnect() : 0; }
    uint32_t getLargestInternalBlockDuringTls() const override { return _transport ? _transport->getLargestInternalBlockAtConnect() : 0; }

    // Injection transport et simulation de trames pour tests unitaires
    void setTransport(ILiveTransport* transport) override;
    void handleIncomingMessage(const char* data, size_t len) override;

    // Utilitaires de conversion et formatage
    static String escapeJsonString(const String& s);
    static size_t encodeBase64Direct(const uint8_t* data, size_t length, char* outBuf);
    static bool decodeBase64(const char* input, size_t inLen, std::vector<uint8_t>& output);

private:
    void sendSessionUpdate();
    void buildWebSocketUrl(char* buf, size_t bufSize) const;

    String _apiKey;
    String _model = OPENAI_REALTIME_DEFAULT_MODEL;
    String _voice = OPENAI_REALTIME_DEFAULT_VOICE;
    String _confirmedSessionVoice = "";
    String _language = "fr-FR";
    String _inputTranscriptionLanguage = "";
    String _systemInstruction;
    String _voiceDirectives;
    String _lastError;
    std::atomic<bool> _audioEmittedInSession{false};
    std::atomic<uint32_t> _jsonErrorCount{0};
    String _lastParseErrorCategory;

    String _currentResponseId;
    String _cancelledResponseId;
    std::vector<String> _cancelledResponseIds;
    bool isResponseCancelled(const char* respId) const;
    void addCancelledResponseId(const String& respId);

    bool _conversationMode = false;
    std::atomic<bool> _connected{false};
    std::atomic<bool> _setupComplete{false};

    ILiveTransport* _transport = nullptr;
    std::unique_ptr<ILiveTransport> _defaultTransport;

    // Callbacks
    VoidCallback _onConnected;
    VoidCallback _onSetupComplete;
    VoidCallback _onDisconnected;
    AudioCallback _onAudioData;
    TextCallback _onText;
    TranscriptionCallback _onInputTranscription;
    OutputTranscriptionCallback _onOutputTranscription;
    OutputTranscriptionWithFinalCallback _onOutputTranscriptionWithFinal;
    String _lastDoneTranscript;
    VoidCallback _onTurnComplete;
    VoidCallback _onInterrupted;
    ErrorCallback _onError;

    // Métriques (thread-safe entre websocket_task et loopTask)
    uint32_t _wsConnectStartTimeMs = 0;
    uint32_t _wsConnectDurationMs = 0;
    uint32_t _lastWsProcessDurationUs = 0;
    uint32_t _maxWsProcessDurationUs = 0;
    void recordWsProcessDurationUs(uint32_t us) {
        _lastWsProcessDurationUs = us;
        if (us > _maxWsProcessDurationUs) _maxWsProcessDurationUs = us;
    }
    uint32_t _setupStartTimeMs = 0;
    uint32_t _setupCompleteDurationMs = 0;
    uint32_t _connectAttempts = 0;
    std::atomic<uint32_t> _lastActivityEndSentMs{0};
    std::atomic<uint32_t> _firstResponseReceivedMs{0};
    std::atomic<uint32_t> _firstAudioReceivedMs{0};
    std::atomic<bool> _waitingForResponse{false};

    // Métriques RX par tour (v0.6.1 — Correction A)
    std::atomic<uint32_t> _pcmSamplesDispatchedTotal{0};
    std::atomic<bool> _turnCompleteReceived{false};
    std::atomic<bool> _turnCompleteBeforeLastPcm{false};
    std::atomic<uint32_t> _postTurnCompletePcmSamples{0};
    std::atomic<uint32_t> _postTurnCompletePcmBlocks{0};

    // Buffer rééchantillonné temporaire pour sendAudioChunk (768 échantillons = 1536 octets)
    std::vector<int16_t> _resampleBuffer;
};
