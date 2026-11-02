#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <functional>
#include "VoiceKitConfig.h"
#include "ILiveTransport.h"

/**
 * @class IVoiceProvider
 * @brief Interface d'abstraction multi-fournisseurs vocaux pour VoiceKit v0.6.0.
 * 
 * Définit le contrat unifié pour les fournisseurs d'IA vocale en temps réel (Gemini Live, OpenAI Realtime, etc.) :
 * - Gestion du cycle de vie de connexion (connect, disconnect, tick, status)
 * - Authentification et configuration dynamique de session (modèle, voix, langue, directives)
 * - Envoi audio en streaming (16 kHz / 24 kHz) et envoi de messages textuels
 * - Réception audio en streaming et décodage PCM natif
 * - Transcriptions STT utilisateur et assistant
 * - Contrôle de flux conversationnel, signalisation de tour et interruption prioritaire STOP
 * - Métriques instrumentées d'étapes de latence et temps de démarrage
 */
class IVoiceProvider {
public:
    using VoidCallback = std::function<void()>;
    using TextCallback = std::function<void(const String&)>;
    using AudioCallback = std::function<void(const int16_t* pcm, size_t samples)>;
    using TranscriptionCallback = std::function<void(const String& text, bool isFinal)>;
    using OutputTranscriptionCallback = std::function<void(const String& text)>;
    using ErrorCallback = std::function<void(const String&)>;

    virtual ~IVoiceProvider() = default;

    virtual VoiceProviderType getProviderType() const = 0;

    // =========================================================================
    // Initialisation, Cycle de Vie et Connexion
    // =========================================================================

    /**
     * @brief Initialise le fournisseur avec la configuration VoiceKit.
     * @param config Configuration contenant clé API, modèle, voix, langue, etc.
     * @return true si l'initialisation locale a réussi.
     */
    virtual bool begin(const VoiceKitConfig& config) = 0;

    /**
     * @brief Ouvre la connexion réseau WebSocket TLS vers le service distant.
     * @return true si la connexion a été initiée avec succès.
     */
    virtual bool connect() = 0;

    /**
     * @brief Ferme proprement la session et la connexion réseau.
     */
    virtual void disconnect() = 0;

    /**
     * @brief Déconnexion non bloquante en deux phases (v0.6.3.1).
     */
    virtual void requestDisconnect() { disconnect(); }
    virtual bool pollDisconnect() { return true; }
    virtual bool isDisconnectPending() const { return false; }

    /**
     * @brief Traitement périodique à appeler dans loop() / update().
     */
    virtual void tick() = 0;

    /**
     * @brief Instrumentation des traitements WebSocket (v0.6.3.1).
     */
    virtual uint32_t getLastWsProcessDurationUs() const { return 0; }
    virtual uint32_t getMaxWsProcessDurationUs() const { return 0; }

    /**
     * @brief Indique si la connexion réseau est actuellement établie.
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Indique si la session est initialisée et confirmée prête par le serveur distant.
     */
    virtual bool isSetupComplete() const = 0;

    /**
     * @brief Retourne la description de la dernière erreur survenue.
     */
    virtual const char* getLastError() const = 0;

    // =========================================================================
    // Configuration Dynamique de Session
    // =========================================================================

    virtual void setSystemInstruction(const String& instruction) = 0;
    virtual const String& getSystemInstruction() const = 0;

    virtual void setModel(const String& model) = 0;
    virtual const String& getModel() const = 0;

    virtual void setVoice(const String& voice) = 0;
    virtual const String& getVoice() const = 0;

    virtual void setLanguage(const String& language) = 0;
    virtual const String& getLanguage() const = 0;

    virtual void setVoiceDirectives(const String& directives) = 0;

    virtual void setConversationMode(bool enable) = 0;
    virtual bool isConversationMode() const = 0;

    // =========================================================================
    // Émission Audio, Texte et Contrôle de Tour
    // =========================================================================

    /**
     * @brief Envoie un message texte utilisateur (pour synthèse vocale ou consigne).
     */
    virtual bool sendTextMessage(const String& text) = 0;

    /**
     * @brief Signale le début d'une prise de parole utilisateur (appui PARLER).
     */
    virtual bool sendActivityStart() = 0;

    /**
     * @brief Transmet un bloc audio microphone PCM (512 échantillons 16 kHz).
     *        Le fournisseur effectue le rééchantillonnage nécessaire si requis.
     */
    virtual bool sendAudioChunk(const int16_t* samples, size_t sampleCount) = 0;

    /**
     * @brief Signale la fin de prise de parole utilisateur (relâchement PARLER / VAD).
     */
    virtual bool sendActivityEnd() = 0;

    /**
     * @brief Clôture formelle du tour utilisateur (déclenche la réponse si applicable).
     */
    virtual bool sendTurnComplete() = 0;

    /**
     * @brief Interrompt immédiatement toute réponse ou génération en cours (STOP prioritaire).
     */
    virtual bool sendInterruption() = 0;

    // =========================================================================
    // Enregistrement des Callbacks d'Événements
    // =========================================================================

    virtual void setOnConnected(VoidCallback cb) = 0;
    virtual void setOnSetupComplete(VoidCallback cb) = 0;
    virtual void setOnDisconnected(VoidCallback cb) = 0;
    virtual void setOnAudioData(AudioCallback cb) = 0;
    virtual void setOnText(TextCallback cb) = 0;
    virtual void setOnInputTranscription(TranscriptionCallback cb) = 0;
    virtual void setOnOutputTranscription(OutputTranscriptionCallback cb) = 0;
    virtual void setOnTurnComplete(VoidCallback cb) = 0;
    virtual void setOnInterrupted(VoidCallback cb) = 0;
    virtual void setOnError(ErrorCallback cb) = 0;

    // =========================================================================
    // Métriques et Instrumentation
    // =========================================================================

    virtual uint32_t getWsConnectMs() const = 0;
    virtual uint32_t getSetupCompleteMs() const = 0;
    virtual uint32_t getConnectAttempts() const = 0;
    virtual uint32_t getActivityEndSentMs() const = 0;
    virtual uint32_t getFirstResponseReceivedMs() const = 0;
    virtual uint32_t getFirstAudioReceivedMs() const = 0;

    // Métriques de réception audio par tour (v0.6.1 — Correction A)
    // Implémentations par défaut fournies pour préserver la compatibilité des mocks et implémentations existantes
    virtual void resetTurnRxMetrics() {}
    virtual uint32_t getTurnDecodedSamples() const { return 0; }
    virtual uint32_t getPostTurnCompleteBlocks() const { return 0; }
    virtual uint32_t getPostTurnCompleteSamples() const { return 0; }
    virtual bool isTurnCompleteReceived() const { return false; }
    virtual bool isWaitingForResponse() const { return false; }

    // Diagnostics optionnels FreeRTOS & mémoire TLS (v0.6.1 — Correction B)
    virtual int8_t getWsTaskCore() const { return -1; }
    virtual uint8_t getWsTaskPriority() const { return 0; }
    virtual uint8_t getWsHandshakeTaskPriority() const { return 1; }
    virtual uint8_t getWsSteadyTaskPriority() const { return 2; }
    virtual uint32_t getFreeInternalHeapDuringTls() const { return 0; }
    virtual uint32_t getMinFreeInternalHeapDuringTls() const { return 0; }
    virtual uint32_t getLargestInternalBlockDuringTls() const { return 0; }

    // =========================================================================
    // Injection Transport et Traitement de Tests
    // =========================================================================

    virtual void setTransport(ILiveTransport* transport) = 0;
    virtual void handleIncomingMessage(const char* data, size_t len) = 0;
};
