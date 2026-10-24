#pragma once
#include "ILiveTransport.h"
#include <string>

class MockLiveTransport : public ILiveTransport {
public:
    MockLiveTransport() = default;
    ~MockLiveTransport() override = default;

    bool connect(const char* uri, const char* certPem = nullptr) override {
        _connected = true;
        _lastUri = uri ? uri : "";
        if (_onConnect) _onConnect();
        return true;
    }

    void disconnect() override {
        _connected = false;
        if (_onDisconnect) _onDisconnect();
    }

    bool isConnected() const override {
        return _connected;
    }

    bool send(const char* data, size_t len) override {
        _lastSent = std::string(data, len);
        _sendCount++;
        return true;
    }

    void setHandlers(ConnectHandler onConnect, DisconnectHandler onDisconnect, 
                     DataHandler onData, ErrorHandler onError) override {
        _onConnect = std::move(onConnect);
        _onDisconnect = std::move(onDisconnect);
        _onData = std::move(onData);
        _onError = std::move(onError);
    }

    void simulateIncoming(const char* data, size_t len) {
        if (_onData) _onData(data, len);
    }

    void simulateError(const char* err) {
        if (_onError) _onError(err);
    }

    const std::string& getLastSent() const { return _lastSent; }
    size_t getSendCount() const { return _sendCount; }

private:
    bool _connected = false;
    std::string _lastUri;
    std::string _lastSent;
    size_t _sendCount = 0;
    ConnectHandler _onConnect;
    DisconnectHandler _onDisconnect;
    DataHandler _onData;
    ErrorHandler _onError;
};
