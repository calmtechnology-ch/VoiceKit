#pragma once
#include <Arduino.h>
#include <functional>
#include <cstdint>

// =============================================================================
// VoiceEvents.h : Types d'événements et callbacks pour VoxCore v0.2.0
// Architecture : Embedded Voice Application Framework
// =============================================================================

/**
 * Représentation générique d'un appel de fonction (Tool Call) émis par Gemini Live.
 * Préserve intégralement les identifiants et les arguments au format JSON brut.
 */
struct ToolCall {
    String callId;          // Identifiant unique de l'appel attribué par Gemini
    String functionName;    // Nom de la fonction déclarée
    String argumentsJson;   // Arguments passés par Gemini sous forme de chaîne JSON brute
    uint32_t generation = 0;// Contexte / génération d'exécution pour annulation STOP (v0.2.0.1)
};

// Signatures des callbacks d'application
using VoiceReadyCallback = std::function<void()>;
using VoiceListeningStartCallback = std::function<void()>;
using VoiceListeningEndCallback = std::function<void()>;
using VoiceTranscriptionCallback = std::function<void(const String& text, bool isFinal)>;
using VoiceAssistantTranscriptionCallback = std::function<void(const String& text)>;
using VoiceToolCallCallback = std::function<void(const ToolCall& toolCall)>;
using VoiceTurnFinishedCallback = std::function<void(uint64_t turnId, const String& userText, const String& assistantText)>;
using VoicePlaybackStateCallback = std::function<void(bool isPlaying)>;
using VoiceErrorCallback = std::function<void(const String& errorMessage)>;
using VoiceDisconnectedCallback = std::function<void()>;

/**
 * Événements internes pour la file d'attente bornée asynchrone.
 * Permet d'isoler les callbacks applicatifs des tâches FreeRTOS audio et WebSocket.
 */
enum class VoiceEventType {
    READY,
    LISTENING_START,
    LISTENING_END,
    USER_TRANSCRIPTION,
    ASSISTANT_TRANSCRIPTION,
    TOOL_CALL,
    TURN_FINISHED,
    PLAYBACK_STATE,
    ERROR_OCCURRED,
    DISCONNECTED
};

struct VoiceEvent {
    VoiceEventType type = VoiceEventType::READY;
    String text;
    bool isFinal = false;
    bool isPlaying = false;
    uint64_t turnId = 0;
    String assistantText;
    ToolCall toolCall;
    uint32_t generation = 0; // Contexte / génération d'exécution pour annulation STOP (v0.2.0.2)
};

#if defined(ESP32) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#else
#include <mutex>
#endif
#include <new>

/**
 * File d'attente d'événements vocaux bornée (capacité 16) adaptée à FreeRTOS (v0.2.0.2 - Correction C).
 * Garanties formelles :
 * - Aucune allocation mémoire dynamique (new/malloc) dans la section critique FreeRTOS.
 * - Aucune désallocation (delete/free) sous spinlock.
 * - Aucune copie de String sous spinlock (transport de pointeurs).
 * - Aucun callback utilisateur sous verrou.
 * - Priorité absolue aux événements critiques (TOOL_CALL, ERROR_OCCURRED, READY).
 * - Ordre FIFO strict pour les événements conservés.
 * - Zéro fuite mémoire lors des rejets ou de l'arrêt STOP.
 */
class BoundedVoiceEventQueue {
public:
    static constexpr size_t CAPACITY = 16;

    BoundedVoiceEventQueue() = default;

    ~BoundedVoiceEventQueue() {
        clear();
    }

    BoundedVoiceEventQueue(const BoundedVoiceEventQueue&) = delete;
    BoundedVoiceEventQueue& operator=(const BoundedVoiceEventQueue&) = delete;

    size_t size() const {
#if defined(ESP32) || defined(ARDUINO)
        portENTER_CRITICAL(&_spinlock);
        size_t s = _count;
        portEXIT_CRITICAL(&_spinlock);
        return s;
#else
        std::lock_guard<std::mutex> lock(_mutex);
        return _count;
#endif
    }

    bool empty() const {
        return size() == 0;
    }

    bool full() const {
        return size() == CAPACITY;
    }

    bool enqueue(const VoiceEvent& ev) {
        bool isCritical = (ev.type == VoiceEventType::TOOL_CALL ||
                           ev.type == VoiceEventType::ERROR_OCCURRED ||
                           ev.type == VoiceEventType::READY);

        // 1. Allocation dynamique effectuée STRICTEMENT HORS de la section critique FreeRTOS
        VoiceEvent* pEv = new (std::nothrow) VoiceEvent(ev);
        if (!pEv) {
            return false;
        }

        VoiceEvent* toDelete = nullptr;

        // 2. Section critique brève (manipulation exclusive de pointeurs, durée < 50 ns)
#if defined(ESP32) || defined(ARDUINO)
        portENTER_CRITICAL(&_spinlock);
#else
        std::lock_guard<std::mutex> lock(_mutex);
#endif

        if (_count >= CAPACITY) {
            if (isCritical) {
                // Trouver le plus ancien événement non-critique à évincer pour préserver l'événement critique
                int dropIdx = -1;
                for (size_t i = 0; i < _count; ++i) {
                    size_t idx = (_head + i) % CAPACITY;
                    if (_slots[idx] &&
                        _slots[idx]->type != VoiceEventType::TOOL_CALL &&
                        _slots[idx]->type != VoiceEventType::ERROR_OCCURRED &&
                        _slots[idx]->type != VoiceEventType::READY) {
                        dropIdx = (int)idx;
                        break;
                    }
                }

                if (dropIdx >= 0) {
                    toDelete = _slots[dropIdx];
                    // Décalage pour préserver l'ordre FIFO strict des événements restants
                    size_t cur = (size_t)dropIdx;
                    size_t lastIdx = (_head + _count - 1) % CAPACITY;
                    while (cur != lastIdx) {
                        size_t next = (cur + 1) % CAPACITY;
                        _slots[cur] = _slots[next];
                        cur = next;
                    }
                    _slots[lastIdx] = pEv;
                    _tail = (_head + _count) % CAPACITY;
                } else {
                    // Tous les événements sont déjà critiques : évincer le plus ancien (_head)
                    toDelete = _slots[_head];
                    _slots[_head] = nullptr;
                    size_t slotToUse = _head;
                    _head = (_head + 1) % CAPACITY;
                    _slots[slotToUse] = pEv;
                    _tail = (_head + _count) % CAPACITY;
                }
            } else {
                // Événement non critique : rejet immédiat pour préserver la mémoire et la stabilité
                toDelete = pEv;
            }
        } else {
            // File non saturée : insertion directe à la queue
            _slots[_tail] = pEv;
            _tail = (_tail + 1) % CAPACITY;
            _count++;
        }

#if defined(ESP32) || defined(ARDUINO)
        portEXIT_CRITICAL(&_spinlock);
#endif

        // 3. Libération mémoire effectuée STRICTEMENT HORS de la section critique FreeRTOS
        if (toDelete) {
            delete toDelete;
            if (toDelete == pEv) {
                return false;
            }
        }
        return true;
    }

    size_t popAll(VoiceEvent* outArray[], size_t maxCount) {
        size_t popped = 0;
#if defined(ESP32) || defined(ARDUINO)
        portENTER_CRITICAL(&_spinlock);
#else
        std::lock_guard<std::mutex> lock(_mutex);
#endif
        while (_count > 0 && popped < maxCount) {
            outArray[popped++] = _slots[_head];
            _slots[_head] = nullptr;
            _head = (_head + 1) % CAPACITY;
            _count--;
        }
        if (_count == 0) {
            _head = 0;
            _tail = 0;
        }
#if defined(ESP32) || defined(ARDUINO)
        portEXIT_CRITICAL(&_spinlock);
#endif
        return popped;
    }

    void purgeNonError() {
        VoiceEvent* toDelete[CAPACITY];
        size_t deleteCount = 0;

#if defined(ESP32) || defined(ARDUINO)
        portENTER_CRITICAL(&_spinlock);
#else
        std::lock_guard<std::mutex> lock(_mutex);
#endif
        VoiceEvent* remainingSlots[CAPACITY];
        size_t remainingCount = 0;

        for (size_t i = 0; i < _count; ++i) {
            size_t idx = (_head + i) % CAPACITY;
            VoiceEvent* pEv = _slots[idx];
            _slots[idx] = nullptr;
            if (pEv) {
                if (pEv->type != VoiceEventType::ERROR_OCCURRED) {
                    toDelete[deleteCount++] = pEv;
                } else {
                    remainingSlots[remainingCount++] = pEv;
                }
            }
        }

        for (size_t i = 0; i < remainingCount; ++i) {
            _slots[i] = remainingSlots[i];
        }
        for (size_t i = remainingCount; i < CAPACITY; ++i) {
            _slots[i] = nullptr;
        }
        _head = 0;
        _tail = remainingCount % CAPACITY;
        _count = remainingCount;

#if defined(ESP32) || defined(ARDUINO)
        portEXIT_CRITICAL(&_spinlock);
#endif

        // Libération hors de la section critique
        for (size_t i = 0; i < deleteCount; ++i) {
            delete toDelete[i];
        }
    }

    void clear() {
        VoiceEvent* toDelete[CAPACITY];
        size_t deleteCount = 0;

#if defined(ESP32) || defined(ARDUINO)
        portENTER_CRITICAL(&_spinlock);
#else
        std::lock_guard<std::mutex> lock(_mutex);
#endif
        while (_count > 0) {
            toDelete[deleteCount++] = _slots[_head];
            _slots[_head] = nullptr;
            _head = (_head + 1) % CAPACITY;
            _count--;
        }
        _head = 0;
        _tail = 0;
        _count = 0;
#if defined(ESP32) || defined(ARDUINO)
        portEXIT_CRITICAL(&_spinlock);
#endif

        for (size_t i = 0; i < deleteCount; ++i) {
            delete toDelete[i];
        }
    }

    // Inspecteurs d'invariants pour validation formelle et tests
    size_t getHead() const { return _head; }
    size_t getTail() const { return _tail; }
    size_t getCount() const { return _count; }

    bool checkInvariants() const {
#if defined(ESP32) || defined(ARDUINO)
        portENTER_CRITICAL(&_spinlock);
#else
        std::lock_guard<std::mutex> lock(_mutex);
#endif
        bool ok = true;
        if (_count > CAPACITY) ok = false;
        if (_head >= CAPACITY) ok = false;
        if (_tail >= CAPACITY) ok = false;
        if (_tail != ((_head + _count) % CAPACITY)) ok = false;
        for (size_t i = 0; i < _count; ++i) {
            size_t idx = (_head + i) % CAPACITY;
            if (_slots[idx] == nullptr) ok = false;
        }
#if defined(ESP32) || defined(ARDUINO)
        portEXIT_CRITICAL(&_spinlock);
#endif
        return ok;
    }

private:
    VoiceEvent* _slots[CAPACITY] = {};
    size_t _count = 0;
    size_t _head = 0;
    size_t _tail = 0;

#if defined(ESP32) || defined(ARDUINO)
    mutable portMUX_TYPE _spinlock = portMUX_INITIALIZER_UNLOCKED;
#else
    mutable std::mutex _mutex;
#endif
};
