#pragma once
#include <Arduino.h>
#include <cstdint>
#include <functional>
#include <atomic>
#include "VoiceHubConfig.h"
#include "VoiceHubTypes.h"
#include "core/GeminiLiveClient.h"
#include "LiveAudioPlayer.h"
#include "core/AudioTraceLog.h"

// =============================================================================
// LiveSessionController : Cycle de vie, etats et session Gemini Live
// VoxCore v0.2.2 / v0.3.0 pour VoiceKit v0.2.0
// Machine a etats dediee :
// DISCONNECTED -> CONNECTING -> CONNECTED -> WAITING_RESPONSE -> PLAYING -> CONNECTED
// Etats annexes : INTERRUPTING (STOP immediat), RECONNECTING, ERROR
// =============================================================================

enum class AudioPacketRejectReason : uint8_t {
    NONE = 0,
    TURN_INACTIVE,
    TURN_CANCELLED,
    STATE_CONNECTED,
    STATE_DISCONNECTED,
    STATE_ERROR,
    STATE_INTERRUPTING,
    STATE_UNEXPECTED,
    BUFFER_SATURATED
};

const char* audioPacketRejectReasonToString(AudioPacketRejectReason reason);

class LiveSessionController {
public:
    LiveSessionController();
    ~LiveSessionController();

    // Initialisation : streamer est optionnel pour la synthese pure TTS
    bool begin(GeminiLiveClient* client, void* streamer, LiveAudioPlayer* player);
    bool begin(GeminiLiveClient* client, LiveAudioPlayer* player) {
        return begin(client, nullptr, player);
    }

    // Activation et desactivation du mode LIVE
    bool activate();
    void deactivate();

    // Boucle de traitement principale
    void tick();

    // Envoi d'un message texte (mode TTS)
    bool sendTextMessage(const String& text);

    // Gestion des tours audio en mode conversationnel (v0.4.0)
    bool beginAudioTurn();
    void finishAudioCapture();

    // Interruption prioritaire STOP
    void requestStop();

    // Volume
    void setVolume(unsigned volumePercent);

    // Etats & Metriques
    LiveSessionState getState() const { return _state; }
    bool isActive() const { return _state != LiveSessionState::DISCONNECTED; }
    uint32_t getChunksReceived() const;
    uint32_t getFirstAudioLatencyMs() const { return _firstAudioLatencyMs; }


    uint32_t getDisconnectCount() const { return _disconnectCount; }
    const char* getLastError() const { return _lastError; }
    const String& getLastAssistantText() const { return _lastAssistantText; }
    uint64_t getCurrentTurnId() const { return _currentTurnId; }
    bool isTurnActive() const { return _turnActive.load(std::memory_order_acquire); }
    bool isTurnCancelled() const { return _turnCancelled.load(std::memory_order_acquire); }
    bool hasBufferOverflowAnomaly() const { return _hasBufferOverflowAnomaly; }
    bool hasPlaybackHardwareError() const { return _hasPlaybackHardwareError; }
    uint32_t getDroppedSamplesCount() const { return _player ? _player->getOverflowDroppedSamples() : 0; }
    uint8_t getReconnectAttempts() const { return _reconnectAttempts; }

    // Enregistrement des callbacks
    using StateCallback = std::function<void(LiveSessionState state)>;
    using TurnCallback = std::function<void(uint64_t turnId)>;
    using VoidCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const String& err)>;
    using TranscriptionCallback = std::function<void(const String& text)>;

    void setOnStateChanged(StateCallback cb) { _onStateChanged = std::move(cb); }
    void setOnTurnFinished(TurnCallback cb) { _onTurnFinished = std::move(cb); }
    void setOnFirstAudio(VoidCallback cb) { _onFirstAudio = std::move(cb); }
    void setOnPlaybackFinished(VoidCallback cb) { _onPlaybackFinished = std::move(cb); }
    void setOnError(ErrorCallback cb) { _onError = std::move(cb); }
    void setOnOutputTranscription(TranscriptionCallback cb) { _onOutputTranscription = std::move(cb); }

    void transitionTo(LiveSessionState newState, const char* reason = nullptr);
    void finishTurn();
    void printAudioEndCheck(const char* label);

private:
    void appendTranscriptionFragment(String& target, const String& fragment, size_t maxLength = 512);

    GeminiLiveClient* _client = nullptr;
    LiveAudioPlayer* _player = nullptr;

    LiveSessionState _state = LiveSessionState::DISCONNECTED;
    uint32_t _stateEnterTimeMs = 0;
    uint32_t _lastActivityMs = 0;
    uint32_t _waitingStartTimeMs = 0;
    uint32_t _firstAudioLatencyMs = 0;
    uint32_t _disconnectCount = 0;
    uint8_t _reconnectAttempts = 0;
    uint32_t _lastReconnectAttemptMs = 0;

    std::atomic<bool> _turnActive{false};
    std::atomic<bool> _turnCancelled{false};
    uint64_t _currentTurnId = 0;
    bool _turnCompleteReceived = false;
    uint32_t _turnCompleteTimeMs = 0;
    bool _turnCompletedLogged = false;
    bool _audioStartedForTurn = false;
    bool _hasAudioResponse = false;
    bool _hasOutputTranscription = false;
    bool _hasModelTurnText = false;
    bool _hasBufferOverflowAnomaly = false;
    bool _hasPlaybackHardwareError = false;
    bool _playbackErrorReported = false;

    char _lastError[128] = {};
    String _lastAssistantText;

    // Metriques de flux audio RX
    uint32_t _audioPacketsReceivedCount = 0;
    uint32_t _audioPacketsPresentedToPlayerCount = 0;
    uint32_t _audioSamplesPresentedToPlayerTotal = 0;
    uint32_t _audioPacketsDispatchedCount = 0;
    uint32_t _audioPacketsWithSaturationCount = 0;
    uint32_t _audioPacketsRejectedCount = 0;
    uint32_t _audioSamplesRejectedTotal = 0;
    uint32_t _audioPacketsRejectedBeforePlayerCount = 0;
    uint32_t _audioSamplesRejectedBeforePlayerTotal = 0;
    uint32_t _audioSamplesRejectedTurnCancelled = 0;
    uint32_t _audioSamplesRejectedTurnInactive = 0;
    uint32_t _audioSamplesRejectedStateUnexpected = 0;
    uint32_t _lastAudioPacketTimeMs = 0;
    AudioPacketRejectReason _lastRejectReason = AudioPacketRejectReason::NONE;

    // Callbacks
    StateCallback _onStateChanged;
    TurnCallback _onTurnFinished;
    VoidCallback _onFirstAudio;
    VoidCallback _onPlaybackFinished;
    ErrorCallback _onError;
    TranscriptionCallback _onOutputTranscription;
};
