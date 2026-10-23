#pragma once
#include "ILiveTransport.h"
#include "WebSocketReassembler.h"
#include <string>

/**
 * @brief Transport simulé pour tests unitaires natifs hors cible ESP32.
 */
class MockLiveTransport : public ILiveTransport {
public:
    bool connect(const char*, const char*) override {
        _intentionalDisconnect = false;
        _connected = true;
        _lastCloseCode = 0;
        _lastCloseReason.clear();
        _reassembler.reset();
        if (_onConnect) _onConnect();
        return true;
    }
    void disconnect() override {
        _intentionalDisconnect = true;
        _connected = false;
        _reassembler.reset();
        if (_onDisconnect) _onDisconnect();
    }
    bool isConnected() const override { return _connected; }
    bool send(const char* data, size_t) override {
        _lastSent = data;
        return true;
    }
    void setHandlers(ConnectHandler onConnect, DisconnectHandler onDisconnect, 
                            DataHandler onData, ErrorHandler onError) override {
        _onConnect = std::move(onConnect);
        _onDisconnect = std::move(onDisconnect);
        _onData = std::move(onData);
        _onError = std::move(onError);
    }
    void setCloseHandler(CloseHandler onClose) override {
        _onClose = std::move(onClose);
    }
    std::string getLastSent() const { return _lastSent; }

    // Helpers d'injection pour les tests
    void simulateReceive(const char* data, size_t len) {
        if (_onData) _onData(data, len);
    }
    void simulateChunk(uint8_t opCode, const char* data, int dataLen, int payloadLen, int payloadOffset) {
        size_t outLen = 0;
        const char* assembled = _reassembler.processChunk(opCode, data, dataLen, payloadLen, payloadOffset, outLen);
        if (assembled && outLen > 0 && _onData) {
            _onData(assembled, outLen);
            _reassembler.reset();
        }
    }
    void simulateClose(uint16_t code, const char* reason) {
        _intentionalDisconnect = false;
        _lastCloseCode = code;
        _lastCloseReason = reason ? reason : "";
        _connected = false;
        _reassembler.reset();
        if (_onClose) _onClose(code, reason);
        if (_onDisconnect) _onDisconnect();
    }
    void simulateError(const char* err) {
        if (_onError) _onError(err);
    }

private:
    bool _connected = false;
    std::string _lastSent;
    WebSocketReassembler _reassembler;
    ConnectHandler _onConnect;
    DisconnectHandler _onDisconnect;
    DataHandler _onData;
    ErrorHandler _onError;
};
