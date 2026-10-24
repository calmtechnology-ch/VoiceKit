#pragma once
#include <Arduino.h>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>
#include <atomic>
#include "VoiceHubConfig.h"
#include "VoiceHubTypes.h"
#include "ILiveTransport.h"
#include "core/VoiceConfig.h"
#include "core/VoiceEvents.h"

// =============================================================================
// GeminiLiveClient : Client WebSocket TLS et protocole Gemini Multimodal Live API
// VoxCore v0.2.0 (Embedded Voice Application Framework)
// Point de terminaison :
// wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=...
// - Configuration dynamique du setup (instructions système personnalisées, outils configurables)
// - Transmission progressive des blocs audio PCM 16 kHz (realtimeInput / mediaChunks)
// - Clôture explicite de tour utilisateur (clientContent / turnComplete)
// - Décodage asynchrone des réponses audio PCM 24 kHz (serverContent / modelTurn / inlineData)
// - Support générique du Function Calling (ToolCall) avec préservation des IDs et arguments JSON
// - Sécurité TLS stricte avec GOOGLE_ROOTS et masquage absolu des clés API
// =============================================================================

class GeminiLiveClient {
public:
    using TextCallback = std::function<void(const String&)>;
    using AudioCallback = std::function<void(const int16_t* pcm, size_t samples)>;
    using TranscriptionCallback = std::function<void(const String& text, bool isFinal)>;
    using OutputTranscriptionCallback = std::function<void(const String& text)>;
    using VoidCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const String&)>;
    using GenericToolCallCallback = std::function<void(const ToolCall& toolCall)>;
    using ToolCallCallback = std::function<void(const String& callId, const String& funcName, const String& projectArg)>;

    GeminiLiveClient(ILiveTransport* customTransport = nullptr);
    ~GeminiLiveClient();

    // Initialisation des identifiants et modèles
    bool begin(const char* apiKey, const char* model = nullptr, const char* voice = nullptr);

    // Configuration applicative de la session (VoxCore v0.2.0 / VoiceKit v0.4.1)
    void setSystemInstruction(const String& instruction) { _systemInstruction = instruction; }
    const String& getSystemInstruction() const { return _systemInstruction; }
    const String& getModel() const { return _model; }
    void setModel(const String& model) { _model = model; }
    const String& getVoice() const { return _voice; }
    void setVoice(const String& voice) { _voice = voice; }
    const String& getLanguage() const { return _language; }
    void setLanguage(const String& language) { _language = language; }
    const String& getVoiceDirectives() const { return _voiceDirectives; }
    void setVoiceDirectives(const String& directives) { _voiceDirectives = directives; }
    static bool isModelSpeechLanguageCodeSupported(const String& model);
    void setTools(const std::vector<ToolDeclaration>& tools) { _tools = tools; }
    const std::vector<ToolDeclaration>& getTools() const { return _tools; }

    // Établissement de la connexion WebSocket TLS
    bool connect();

    // Déconnexion propre
    void disconnect();

    // Traitement périodique (dépilage événements, maintien)
    void tick();

    // Signalisation du début d'activité vocale utilisateur (appui PARLER)
    bool sendActivityStart();

    // Transmission d'un bloc PCM 16 kHz mono (512 échantillons = ~32 ms)
    bool sendAudioChunk(const int16_t* samples, size_t sampleCount);

    // Signalisation de la fin d'activité vocale utilisateur (relâchement PARLER)
    bool sendActivityEnd();

    // Signalisation explicite turnComplete
    bool sendTurnComplete();

    // Envoi d'une interruption explicite
    bool sendInterruption();

    // Envoi d'un message texte utilisateur (VoxCore TTSTest v0.1.0)
    bool sendTextMessage(const String& text);

    // État
    bool isConnected() const;
    bool isSetupComplete() const { return _setupComplete.load(std::memory_order_acquire); }
    const char* getLastError() const { return _lastError.c_str(); }

    // Enregistrement des callbacks
    void setOnConnected(VoidCallback cb) { _onConnected = std::move(cb); }
    void setOnSetupComplete(VoidCallback cb) { _onSetupComplete = std::move(cb); }
    void setOnDisconnected(VoidCallback cb) { _onDisconnected = std::move(cb); }
    void setOnAudioData(AudioCallback cb) { _onAudioData = std::move(cb); }
    void setOnText(TextCallback cb) { _onText = std::move(cb); }
    void setOnInputTranscription(TranscriptionCallback cb) { _onInputTranscription = std::move(cb); }
    void setOnOutputTranscription(OutputTranscriptionCallback cb) { _onOutputTranscription = std::move(cb); }
    void setOnTurnComplete(VoidCallback cb) { _onTurnComplete = std::move(cb); }
    void setOnInterrupted(VoidCallback cb) { _onInterrupted = std::move(cb); }
    void setOnError(ErrorCallback cb) { _onError = std::move(cb); }

    // Callback générique de Function Calling (VoxCore v0.2.0)
    void setOnToolCallGeneric(GenericToolCallCallback cb) { _onToolCallGeneric = std::move(cb); }

    // Callback de compatibilité
    void setOnToolCall(ToolCallCallback cb) { _onToolCallLegacy = std::move(cb); }

    // Métriques réseau d'émission (v0.2.1.1 - Correction A)
    uint32_t getLastSendDurationUs() const { return _lastSendDurationUs; }
    uint32_t getMaxSendDurationUs() const { return _maxSendDurationUs; }
    uint32_t getSendsOver32msCount() const { return _sendsOver32msCount; }
    uint32_t getSendsOver100msCount() const { return _sendsOver100msCount; }
    uint32_t getSendRefusedCount() const { return _sendRefusedCount; }
    uint32_t getAudioChunksSentCount() const { return _audioChunksSentCount; }

    // Instrumentation audio RX par tour (v0.2.1.3 - Correction A)
    void resetTurnRxMetrics();
    uint32_t getAudioWsMessagesCount() const { return _audioWsMessagesCount; }
    uint32_t getB64BlocksDecodedCount() const { return _b64BlocksDecodedCount; }
    uint32_t getB64DecodeFailedCount() const { return _b64DecodeFailedCount; }
    uint32_t getPcmBlocksInvalidCount() const { return _pcmBlocksInvalidCount; }
    uint32_t getPcmBytesDecodedTotal() const { return _pcmBytesDecodedTotal; }
    uint32_t getPcmSamplesDispatchedTotal() const { return _pcmSamplesDispatchedTotal; }
    uint32_t getLastPcmPacketTimeMs() const { return _lastPcmPacketTimeMs; }
    uint32_t getTurnCompleteTimeMs() const { return _turnCompleteTimeMs; }
    bool isTurnCompleteReceived() const { return _turnCompleteReceived; }
    bool isTurnCompleteBeforeLastPcm() const { return _turnCompleteBeforeLastPcm; }
    uint32_t getPostTurnCompletePcmSamples() const { return _postTurnCompletePcmSamples; }
    uint32_t getPostTurnCompletePcmBlocks() const { return _postTurnCompletePcmBlocks; }
    uint32_t getTurnAudioPartsCount() const { return _turnAudioPartsCount; }

    // Envoi de la réponse structurée à un toolCall Gemini Live
    bool sendToolResponse(const String& callId, const String& funcName, const String& resultJson);
    bool sendToolError(const String& callId, const String& funcName, const String& errorMessage);

    // Injection transport (pour tests)
    void setTransport(ILiveTransport* transport);

    // Utilitaires de conversion Base64 portables (testables)
    static String encodeBase64(const uint8_t* data, size_t length);
    static bool decodeBase64(const char* input, size_t inLen, std::vector<uint8_t>& output);

    // Traitement des trames reçues (public pour tests automatisés de messages)
    void handleIncomingMessage(const char* data, size_t len);

    // Échappement JSON robuste et conforme RFC 8259
    static String escapeJsonString(const String& s);

    // Déduplication bornée des appels récents (VoxCore v0.2.0.1)
    bool isDuplicateCall(const String& callId) const;
    void recordHandledCall(const String& callId);

private:
    void sendSetupMessage();
    void buildWebSocketUrl(char* buf, size_t bufSize) const;

    String _apiKey;
    String _model = GEMINI_LIVE_MODEL;
    String _voice = GEMINI_LIVE_VOICE;
    String _language = "fr-FR";
    String _lastError;

    // Configuration dynamique applicative
    String _systemInstruction;
    String _voiceDirectives;
    std::vector<ToolDeclaration> _tools;

    ILiveTransport* _transport = nullptr;
    std::unique_ptr<ILiveTransport> _defaultTransport;

    std::atomic<bool> _connected{false};
    std::atomic<bool> _setupComplete{false};
    uint32_t _connectStartTimeMs = 0;

    // Callbacks
    VoidCallback _onConnected;
    VoidCallback _onSetupComplete;
    VoidCallback _onDisconnected;
    AudioCallback _onAudioData;
    TextCallback _onText;
    TranscriptionCallback _onInputTranscription;
    OutputTranscriptionCallback _onOutputTranscription;
    VoidCallback _onTurnComplete;
    VoidCallback _onInterrupted;
    ErrorCallback _onError;
    GenericToolCallCallback _onToolCallGeneric;
    ToolCallCallback _onToolCallLegacy;

    // Déduplication bornée des appels récents (v0.2.0.1)
    static constexpr size_t RECENT_CALL_IDS_CAP = 16;
    std::vector<String> _recentHandledCallIds;

    // Métriques temporelles et saturations d'émission (v0.2.1.1 - Correction A)
    uint32_t _lastSendDurationUs = 0;
    uint32_t _maxSendDurationUs = 0;
    uint32_t _sendsOver32msCount = 0;
    uint32_t _sendsOver100msCount = 0;
    uint32_t _sendRefusedCount = 0;
    uint32_t _audioChunksSentCount = 0;

    // Métriques d'instrumentation audio RX (v0.2.1.3 - Correction A)
    uint32_t _audioWsMessagesCount = 0;
    uint32_t _b64BlocksDecodedCount = 0;
    uint32_t _b64DecodeFailedCount = 0;
    uint32_t _pcmBlocksInvalidCount = 0;
    uint32_t _pcmBytesDecodedTotal = 0;
    uint32_t _pcmSamplesDispatchedTotal = 0;
    uint32_t _lastPcmPacketTimeMs = 0;
    uint32_t _turnCompleteTimeMs = 0;
    bool _turnCompleteReceived = false;
    bool _turnCompleteBeforeLastPcm = false;
    uint32_t _postTurnCompletePcmSamples = 0;
    uint32_t _postTurnCompletePcmBlocks = 0;
    uint32_t _turnAudioPartsCount = 0;
};
