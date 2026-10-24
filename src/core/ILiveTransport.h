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

    virtual bool connect(const char* uri, const char* certPem = nullptr) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual bool send(const char* data, size_t len) = 0;
    virtual void setHandlers(ConnectHandler onConnect, DisconnectHandler onDisconnect, 
                            DataHandler onData, ErrorHandler onError) = 0;
    virtual void tick() {}
};
