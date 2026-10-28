#pragma once
#include "ILiveTransport.h"
#include <string>

class MockLiveTransport : public ILiveTransport {
public:
    MockLiveTransport() = default;
    ~MockLiveTransport() override = default;

    bool connect(const char* uri, const char* certPem = nullptr, const char* extraHeaders = nullptr) override {
        _connected = true;
        _taskCore = 0;
        _taskPriority = _steadyTaskPrio;
        _lastUri = uri ? uri : "";
        _lastHeaders = extraHeaders ? extraHeaders : "";
        if (_onConnect) _onConnect();
        return true;
    }

    void disconnect() override {
        _connected = false;
        _taskPriority = _handshakeTaskPrio;
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

    void triggerConnected() {
        _connected = true;
        if (_onConnect) _onConnect();
    }

    const std::string& getLastSent() const { return _lastSent; }
    const std::string& getLastMessage() const { return _lastSent; }
    const std::string& getLastHeaders() const { return _lastHeaders; }
    const std::string& getLastUri() const { return _lastUri; }
    size_t getSendCount() const { return _sendCount; }

    int8_t getTaskCore() const override { return _taskCore; }
    uint8_t getTaskPriority() const override { return _taskPriority; }
    uint8_t getHandshakeTaskPriority() const override { return _handshakeTaskPrio; }
    uint8_t getSteadyTaskPriority() const override { return _steadyTaskPrio; }
    uint32_t getFreeInternalHeapAtConnect() const override { return _freeInternalHeap; }
    uint32_t getMinFreeInternalHeapAtConnect() const override { return _minFreeInternalHeap; }
    uint32_t getLargestInternalBlockAtConnect() const override { return _largestInternalBlock; }

    void setSimulatedTlsDiagnostics(int8_t core, uint8_t prio, uint32_t freeHeap, uint32_t minFreeHeap, uint32_t largestBlock) {
        _taskCore = core;
        _taskPriority = prio;
        _freeInternalHeap = freeHeap;
        _minFreeInternalHeap = minFreeHeap;
        _largestInternalBlock = largestBlock;
    }

private:
    bool _connected = false;
    int8_t _taskCore = 0;
    uint8_t _handshakeTaskPrio = 1;
    uint8_t _steadyTaskPrio = 2;
    uint8_t _taskPriority = 1;
    uint32_t _freeInternalHeap = 185000;
    uint32_t _minFreeInternalHeap = 142000;
    uint32_t _largestInternalBlock = 110592;
    std::string _lastUri;
    std::string _lastHeaders;
    std::string _lastSent;
    size_t _sendCount = 0;
    ConnectHandler _onConnect;
    DisconnectHandler _onDisconnect;
    DataHandler _onData;
    ErrorHandler _onError;
};
