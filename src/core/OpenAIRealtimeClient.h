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
    void tick() override;

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
    void setVoiceDirectives(const String& directives) override { _voiceDirectives = directives; }
    void setConversationMode(bool enable) override { _conversationMode = enable; }
    bool isConversationMode() const override { return _conversationMode; }

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
    void setOnTurnComplete(VoidCallback cb) override { _onTurnComplete = std::move(cb); }
    void setOnInterrupted(VoidCallback cb) override { _onInterrupted = std::move(cb); }
    void setOnError(ErrorCallback cb) override { _onError = std::move(cb); }

    // Métriques temporelles
    uint32_t getWsConnectMs() const override { return _wsConnectDurationMs; }
    uint32_t getSetupCompleteMs() const override { return _setupCompleteDurationMs; }
    uint32_t getConnectAttempts() const override { return _connectAttempts; }
    uint32_t getActivityEndSentMs() const override { return _lastActivityEndSentMs; }
    uint32_t getFirstResponseReceivedMs() const override { return _firstResponseReceivedMs; }
    uint32_t getFirstAudioReceivedMs() const override { return _firstAudioReceivedMs; }

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
    String _language = "fr-FR";
    String _systemInstruction;
    String _voiceDirectives;
    String _lastError;

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
    VoidCallback _onTurnComplete;
    VoidCallback _onInterrupted;
    ErrorCallback _onError;

    // Métriques
    uint32_t _wsConnectStartTimeMs = 0;
    uint32_t _wsConnectDurationMs = 0;
    uint32_t _setupStartTimeMs = 0;
    uint32_t _setupCompleteDurationMs = 0;
    uint32_t _connectAttempts = 0;
    uint32_t _lastActivityEndSentMs = 0;
    uint32_t _firstResponseReceivedMs = 0;
    uint32_t _firstAudioReceivedMs = 0;
    bool _waitingForResponse = false;

    // Buffer rééchantillonné temporaire pour sendAudioChunk (768 échantillons = 1536 octets)
    std::vector<int16_t> _resampleBuffer;
};
