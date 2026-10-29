#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>

// =============================================================================
// ILiveTransport : Interface d'abstraction du transport WebSocket pour Gemini Live
// Permet d'injecter soit l'implémentation matérielle esp_websocket_client sur CoreS3,
// soit un transport simulé pour les tests automatisés natifs.
// =============================================================================

class ILiveTransport {
public:
    using ConnectHandler = std::function<void()>;
    using DisconnectHandler = std::function<void()>;
    using DataHandler = std::function<void(const char* data, size_t len)>;
    using ErrorHandler = std::function<void(const char* err)>;

    virtual ~ILiveTransport() = default;

    virtual bool connect(const char* uri, const char* certPem = nullptr, const char* extraHeaders = nullptr) = 0;
    virtual void disconnect() = 0;
    // Déconnexion non bloquante en deux phases (v0.6.3.1 — élimine le blocage select() 1000 ms sur loopTask)
    virtual void requestDisconnect() { disconnect(); }
    virtual bool pollDisconnect() { return true; }
    virtual bool isDisconnectPending() const { return false; }
    virtual uint32_t getLastDisconnectDurationUs() const { return 0; }
    virtual uint32_t getMaxDisconnectDurationUs() const { return 0; }
    virtual bool isConnected() const = 0;
    virtual bool send(const char* data, size_t len) = 0;
    virtual void setHandlers(ConnectHandler onConnect, DisconnectHandler onDisconnect, 
                            DataHandler onData, ErrorHandler onError) = 0;
    virtual void tick() {}

    // Diagnostics optionnels FreeRTOS & mémoire TLS (v0.6.1 — Correction B)
    virtual int8_t getTaskCore() const { return -1; }
    virtual uint8_t getTaskPriority() const { return 0; }
    virtual uint8_t getHandshakeTaskPriority() const { return 1; }
    virtual uint8_t getSteadyTaskPriority() const { return 2; }
    virtual uint32_t getFreeInternalHeapAtConnect() const { return 0; }
    virtual uint32_t getMinFreeInternalHeapAtConnect() const { return 0; }
    virtual uint32_t getLargestInternalBlockAtConnect() const { return 0; }
};
