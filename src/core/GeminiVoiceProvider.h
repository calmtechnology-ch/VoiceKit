#pragma once
#include "IVoiceProvider.h"
#include "GeminiLiveClient.h"

/**
 * @class GeminiVoiceProvider
 * @brief Adaptateur IVoiceProvider pour le client Gemini Live existant (VoiceKit v0.6.0).
 * 
 * Implémente le pattern Adaptateur pour intégrer GeminiLiveClient dans l'architecture
 * multi-fournisseur unifiée sans modifier son comportement ni risquer de régression.
 */
class GeminiVoiceProvider : public IVoiceProvider {
public:
    explicit GeminiVoiceProvider(GeminiLiveClient* client = nullptr);
    ~GeminiVoiceProvider() override = default;

    // Cycle de vie & Session
    bool begin(const VoiceKitConfig& config) override;
    bool connect() override;
    void disconnect() override;
    void requestDisconnect() override;
    bool pollDisconnect() override;
    bool isDisconnectPending() const override;
    void tick() override;
    uint32_t getLastWsProcessDurationUs() const override;
    uint32_t getMaxWsProcessDurationUs() const override;
    bool isConnected() const override;
    bool isSetupComplete() const override;
    const char* getLastError() const override;
    VoiceProviderType getProviderType() const override { return VoiceProviderType::GEMINI; }

    // Configuration dynamique
    void setSystemInstruction(const String& instruction) override;
    const String& getSystemInstruction() const override;
    void setModel(const String& model) override;
    const String& getModel() const override;
    void setVoice(const String& voice) override;
    const String& getVoice() const override;
    void setLanguage(const String& language) override;
    const String& getLanguage() const override;
    void setVoiceDirectives(const String& directives) override;
    void setConversationMode(bool enable) override { _conversationMode = enable; }
    bool isConversationMode() const override { return _conversationMode; }

    // Émission audio / texte / signalisation
    bool sendTextMessage(const String& text) override;
    bool sendActivityStart() override;
    bool sendAudioChunk(const int16_t* samples, size_t sampleCount) override;
    bool sendActivityEnd() override;
    bool sendTurnComplete() override;
    bool sendInterruption() override;

    // Callbacks
    void setOnConnected(VoidCallback cb) override;
    void setOnSetupComplete(VoidCallback cb) override;
    void setOnDisconnected(VoidCallback cb) override;
    void setOnAudioData(AudioCallback cb) override;
    void setOnText(TextCallback cb) override;
    void setOnInputTranscription(TranscriptionCallback cb) override;
    void setOnOutputTranscription(OutputTranscriptionCallback cb) override;
    void setOnTurnComplete(VoidCallback cb) override;
    void setOnInterrupted(VoidCallback cb) override;
    void setOnError(ErrorCallback cb) override;

    // Métriques & Instrumentation
    uint32_t getWsConnectMs() const override;
    uint32_t getSetupCompleteMs() const override;
    uint32_t getConnectAttempts() const override;
    uint32_t getActivityEndSentMs() const override;
    uint32_t getFirstResponseReceivedMs() const override;
    uint32_t getFirstAudioReceivedMs() const override;
    void resetTurnRxMetrics() override;
    uint32_t getTurnDecodedSamples() const override;
    uint32_t getPostTurnCompleteBlocks() const override;
    uint32_t getPostTurnCompleteSamples() const override;
    bool isTurnCompleteReceived() const override;
    int8_t getWsTaskCore() const override;
    uint8_t getWsTaskPriority() const override;
    uint8_t getWsHandshakeTaskPriority() const override;
    uint8_t getWsSteadyTaskPriority() const override;
    uint32_t getFreeInternalHeapDuringTls() const override;
    uint32_t getMinFreeInternalHeapDuringTls() const override;
    uint32_t getLargestInternalBlockDuringTls() const override;

    // Injection transport et trames de tests
    void setTransport(ILiveTransport* transport) override;
    void handleIncomingMessage(const char* data, size_t len) override;

    // Accès direct au client GeminiLiveClient sous-jacent
    GeminiLiveClient* getClient() { return _client; }
    const GeminiLiveClient* getClient() const { return _client; }
    void setClient(GeminiLiveClient* client) { _client = client; }

private:
    GeminiLiveClient* _client = nullptr;
    std::unique_ptr<GeminiLiveClient> _ownedClient;
    bool _conversationMode = false;
};
