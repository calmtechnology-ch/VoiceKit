#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>
#include <atomic>
#include "ILiveTransport.h"
#include "VoiceKitConfig.h"

/**
 * @brief Client WebSocket TLS pour le protocole bidirectionnel Google Gemini Live API.
 * Version v0.1.0.2 : Diagnostic approfondi de fermeture, réassemblage des fragments,
 * extraction des erreurs serveur JSON et des codes de clôture WebSocket.
 */
class GeminiLiveClient {
public:
    using AudioCallback = std::function<void(const int16_t* pcm, size_t sampleCount)>;
    using OutputTranscriptionCallback = std::function<void(const String& text)>;
    using VoidCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const String& error)>;
    using CloseCallback = std::function<void(uint16_t code, const String& reason)>;

    GeminiLiveClient(ILiveTransport* customTransport = nullptr);
    ~GeminiLiveClient();

    // Initialisation
    bool begin(const char* apiKey, const char* model = nullptr, const char* voice = nullptr);

    // Configuration de session
    void setSystemInstruction(const String& instruction) { _systemInstruction = instruction; }
    const String& getSystemInstruction() const { return _systemInstruction; }

    // Connexion / Déconnexion
    bool connect();
    void disconnect();
    void tick();

    // Envoi de messages
    bool sendTextMessage(const String& text);
    bool sendInterruption();

    // État
    bool isConnected() const;
    bool isSetupComplete() const { return _setupComplete.load(std::memory_order_acquire); }
    const char* getLastError() const { return _lastError.c_str(); }
    uint16_t getLastCloseCode() const { return _lastCloseCode; }
    const char* getLastCloseReason() const { return _lastCloseReason.c_str(); }
    bool isIntentionalDisconnect() const;

    String getModel() const { return _model; }
    String getVoice() const { return _voice; }

    // Enregistrement des callbacks
    void setOnConnected(VoidCallback cb) { _onConnected = std::move(cb); }
    void setOnSetupComplete(VoidCallback cb) { _onSetupComplete = std::move(cb); }
    void setOnDisconnected(VoidCallback cb) { _onDisconnected = std::move(cb); }
    void setOnAudioData(AudioCallback cb) { _onAudioData = std::move(cb); }
    void setOnOutputTranscription(OutputTranscriptionCallback cb) { _onOutputTranscription = std::move(cb); }
    void setOnTurnComplete(VoidCallback cb) { _onTurnComplete = std::move(cb); }
    void setOnInterrupted(VoidCallback cb) { _onInterrupted = std::move(cb); }
    void setOnError(ErrorCallback cb) { _onError = std::move(cb); }
    void setOnClose(CloseCallback cb) { _onClose = std::move(cb); }

    // Instrumentation métriques audio RX
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

    // Injection transport (pour tests simulés hors cible)
    void setTransport(ILiveTransport* transport);

    // Utilitaires Base64 & JSON
    static String encodeBase64(const uint8_t* data, size_t length);
    static bool decodeBase64(const char* input, size_t inLen, std::vector<uint8_t>& output);
    static String escapeJsonString(const String& s);
    static String normalizeModelName(const String& model);

    // Traitement des trames reçues (public pour tests automatisés)
    void handleIncomingMessage(const char* data, size_t len);

private:
    void sendSetupMessage();
    void buildWebSocketUrl(char* buf, size_t bufSize) const;

    String _apiKey;
    String _model = VOICEKIT_DEFAULT_MODEL;
    String _voice = VOICEKIT_DEFAULT_VOICE;
    String _lastError;
    String _lastCloseReason;
    uint16_t _lastCloseCode = 0;
    String _systemInstruction;

    ILiveTransport* _transport = nullptr;
    std::unique_ptr<ILiveTransport> _defaultTransport;

    std::atomic<bool> _connected{false};
    std::atomic<bool> _setupComplete{false};
    std::atomic<bool> _intentionalDisconnect{false};
    uint32_t _connectStartTimeMs = 0;

    // Callbacks
    VoidCallback _onConnected;
    VoidCallback _onSetupComplete;
    VoidCallback _onDisconnected;
    AudioCallback _onAudioData;
    OutputTranscriptionCallback _onOutputTranscription;
    VoidCallback _onTurnComplete;
    VoidCallback _onInterrupted;
    ErrorCallback _onError;
    CloseCallback _onClose;

    // Métriques RX
    uint32_t _audioWsMessagesCount = 0;
    uint32_t _b64BlocksDecodedCount = 0;
    uint32_t _b64DecodeFailedCount = 0;
    uint32_t _pcmBlocksInvalidCount = 0;
    uint32_t _pcmBytesDecodedTotal = 0;
    uint32_t _pcmSamplesDispatchedTotal = 0;
    uint32_t _lastPcmPacketTimeMs = 0;
    uint32_t _turnCompleteTimeMs = 0;
    bool _turnCompleteReceived = false;
};
