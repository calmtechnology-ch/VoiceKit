#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <functional>

/**
 * @brief Interface d'abstraction du transport WebSocket pour Gemini Live.
 * Permet d'injecter soit l'implémentation matérielle esp_websocket_client sur ESP32,
 * soit un transport simulé pour les tests automatisés natifs.
 */
class ILiveTransport {
public:
    using ConnectHandler = std::function<void()>;
    using DisconnectHandler = std::function<void()>;
    using DataHandler = std::function<void(const char* data, size_t len)>;
    using ErrorHandler = std::function<void(const char* err)>;
    using CloseHandler = std::function<void(uint16_t code, const char* reason)>;

    virtual ~ILiveTransport() = default;

    virtual bool connect(const char* uri, const char* certPem = nullptr) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual bool send(const char* data, size_t len) = 0;
    virtual void setHandlers(ConnectHandler onConnect, DisconnectHandler onDisconnect, 
                            DataHandler onData, ErrorHandler onError) = 0;
    virtual void setCloseHandler(CloseHandler onClose) { _onClose = std::move(onClose); }
    virtual void tick() {}

    virtual uint16_t getLastCloseCode() const { return _lastCloseCode; }
    virtual const char* getLastCloseReason() const { return _lastCloseReason.c_str(); }
    virtual bool isIntentionalDisconnect() const { return _intentionalDisconnect; }

protected:
    CloseHandler _onClose;
    uint16_t _lastCloseCode = 0;
    std::string _lastCloseReason;
    bool _intentionalDisconnect = false;
};
