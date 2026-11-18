#pragma once
#include <Arduino.h>
#include <functional>
#include <memory>
#include "VoiceKitConfig.h"
#include "core/GeminiLiveClient.h"
#include "core/IVoiceProvider.h"
#include "core/LiveSessionController.h"
#include "core/LiveAudioStreamer.h"
#include "core/LiveAudioPlayer.h"

/**
 * @brief Contrôleur de conversation bidirectionnelle native (Mode B) pour VoiceKit v0.4.0 / v0.6.0.
 * 
 * Coordonne l'acquisition vocale microphone (STT 16 kHz), la transmission en streaming
 * vers le fournisseur vocal actif (Gemini Live ou OpenAI Realtime), la réception de la
 * transcription utilisateur, la réception de la réponse native (PCM 24 kHz) et sa restitution sur le haut-parleur CoreS3.
 */
class ConversationController {
public:
    using UserTextCallback = std::function<void(const String& text, bool isFinal)>;
    using AssistantTextCallback = std::function<void(const String& text)>;
    using TurnFinishedCallback = std::function<void(const ConversationMetrics& metrics)>;
    using SimpleCallback = std::function<void()>;
    using StateCallback = std::function<void(VoiceState state)>;
    using ErrorCallback = std::function<void(const String& error)>;

    ConversationController();
    ~ConversationController();

    // Initialisation avec IVoiceProvider (v0.6.0 multi-fournisseurs)
    bool begin(IVoiceProvider* provider, LiveSessionController* session,
               LiveAudioStreamer* streamer, LiveAudioPlayer* player,
               const VoiceKitConfig& config);

    // Surcharge de compatibilité ascendante avec GeminiLiveClient (v0.5.x)
    bool begin(GeminiLiveClient* client, LiveSessionController* session,
               LiveAudioStreamer* streamer, LiveAudioPlayer* player,
               const VoiceKitConfig& config);

    void setProviderAndConfig(IVoiceProvider* provider, const VoiceKitConfig& config) {
        if (provider) {
            _provider = provider;
        }
        _config = config;
    }

    // Démarrage d'un tour de parole utilisateur (appui PARLER)
    bool startTurn();

    // Fin de parole utilisateur (relâchement PARLER) -> bascule en attente réponse
    bool finishTurnAudio();

    // Interruption prioritaire (appui STOP)
    void requestStop();

    // Arrêt local d'un tour lors d'une clôture de session sans trame réseau superflue (v0.6.3.1)
    void abortTurnForSessionClose();

    // Traitement cyclique obligatoire dans loop()
    void tick();

    // Enregistrement des callbacks
    void setOnUserText(UserTextCallback cb) { _onUserText = std::move(cb); }
    void setOnAssistantText(AssistantTextCallback cb) { _onAssistantText = std::move(cb); }
    void setOnConversationFinished(SimpleCallback cb) { _onConversationFinished = std::move(cb); }
    void setOnTurnMetrics(TurnFinishedCallback cb) { _onTurnMetrics = std::move(cb); }
    void setOnStateChanged(StateCallback cb) { _onStateChanged = std::move(cb); }
    void setOnError(ErrorCallback cb) { _onError = std::move(cb); }

    // États & Diagnostics
    bool isTurnActive() const { return _isTurnActive; }
    VoiceState getState() const { return _state; }
    const ConversationMetrics& getMetrics() const { return _metrics; }
    const VoiceLatencyMetrics& getLatencyMetrics() const { return _metrics.latency; }
    void notifySpeechEndDetected(uint32_t ms) { _metrics.latency.speechEndDetectedMs = ms; }
    const String& getLastUserText() const { return _lastUserText; }
    const String& getLastAssistantText() const { return _lastAssistantText; }

    // Gestionnaires d'événements internes
    void handleUserTranscription(const String& text, bool isFinal);
    void handleAssistantTranscription(const String& text);
    void handleAssistantTranscription(const String& text, bool isFinal);
    void handleTurnFinished(uint64_t turnId);

private:
    void setState(VoiceState newState);

    IVoiceProvider* _provider = nullptr;
    GeminiLiveClient* _legacyClient = nullptr;
    std::unique_ptr<IVoiceProvider> _legacyAdapter;
    LiveSessionController* _session = nullptr;
    LiveAudioStreamer* _streamer = nullptr;
    LiveAudioPlayer* _player = nullptr;
    VoiceKitConfig _config;

    VoiceState _state = VoiceState::READY;
    bool _isTurnActive = false;
    uint32_t _turnStartTimeMs = 0;
    uint32_t _micStopMs = 0;
    uint32_t _speakingStartTimeMs = 0;

    String _lastUserText = "";
    String _lastAssistantText = "";
    ConversationMetrics _metrics;

    UserTextCallback _onUserText;
    AssistantTextCallback _onAssistantText;
    SimpleCallback _onConversationFinished;
    TurnFinishedCallback _onTurnMetrics;
    StateCallback _onStateChanged;
    ErrorCallback _onError;
};
