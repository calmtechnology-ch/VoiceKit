#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
#include <Arduino.h>

/**
 * @brief Réassembleur de trames et fragments WebSocket pour Google Gemini Live.
 * Gère la fragmentation des buffers réseau ESP-IDF (payload_offset / payload_len)
 * et les trames de continuation RFC 6455 (opcode 0x00).
 * Applique une limite stricte de mémoire pour éviter tout épuisement (OOM).
 */
class WebSocketReassembler {
public:
    static constexpr size_t DEFAULT_MAX_SIZE = 64 * 1024; // 64 Ko max par message JSON

    explicit WebSocketReassembler(size_t maxSize = DEFAULT_MAX_SIZE)
        : _maxSize(maxSize) {}

    /**
     * @brief Traite un fragment reçu du transport WebSocket.
     * @param opCode Opcode de la trame (0x01 texte, 0x00 continuation, 0x02 binaire, 0x08 close).
     * @param data Pointeur vers les données du fragment courant.
     * @param dataLen Longueur du fragment courant en octets.
     * @param payloadLen Longueur totale attendue du payload complet (si fournie par le transport, sinon <= 0).
     * @param payloadOffset Décalage du fragment courant dans le payload complet.
     * @param outLen Sortie : taille totale du message réassemblé en octets.
     * @return Pointeur vers le message complet réassemblé, ou nullptr si le message est incomplet.
     */
    const char* processChunk(uint8_t opCode, const char* data, int dataLen,
                             int payloadLen, int payloadOffset, size_t& outLen) {
        outLen = 0;
        if (!data || dataLen <= 0) return nullptr;

        // Cas 1 : Trame complète non fragmentée (Fast path sans allocation ni copie)
        if (payloadOffset == 0 && payloadLen == dataLen) {
            reset();
            outLen = static_cast<size_t>(dataLen);
            return data;
        }

        // Vérification de la limite de taille maximale
        if (payloadLen > 0 && static_cast<size_t>(payloadLen) > _maxSize) {
            Serial.printf("[VoiceKit:WS] Fragment rejete : taille prevue (%d) depasse la limite (%u octets)\n",
                          payloadLen, (unsigned)_maxSize);
            reset();
            return nullptr;
        }

        // Cas 2 : Début d'un nouveau message fragmenté
        if (payloadOffset == 0 || (opCode == 0x01 && _buffer.empty())) {
            _buffer.clear();
            if (payloadLen > 0) {
                _buffer.reserve(payloadLen + 1);
            }
            _expectedTotalLen = (payloadLen > 0) ? payloadLen : 0;
        }

        // Protection contre un dépassement en cours d'accumulation
        if (_buffer.size() + dataLen > _maxSize) {
            Serial.printf("[VoiceKit:WS] Fragment rejete : debordement memoire (> %u octets)\n", (unsigned)_maxSize);
            reset();
            return nullptr;
        }

        // Accumulation du fragment
        _buffer.insert(_buffer.end(), data, data + dataLen);

        // Vérification de la complétude du message
        bool complete = false;
        if (_expectedTotalLen > 0 && _buffer.size() >= static_cast<size_t>(_expectedTotalLen)) {
            complete = true;
        } else if (payloadLen > 0 && (payloadOffset + dataLen >= payloadLen)) {
            complete = true;
        }

        if (complete) {
            _buffer.push_back('\0'); // Terminaison sûre pour parseur JSON / C-string
            outLen = _buffer.size() - 1;
            return _buffer.data();
        }

        return nullptr; // En attente des fragments suivants
    }

    /**
     * @brief Réinitialise l'état et libère la mémoire temporaire.
     */
    void reset() {
        _buffer.clear();
        _expectedTotalLen = 0;
    }

    /**
     * @brief Libère la capacité réservée par le tampon en cas de déconnexion.
     */
    void shrink() {
        reset();
        std::vector<char>().swap(_buffer);
    }

    /**
     * @brief Indique si des données partielles sont en attente de réassemblage.
     */
    bool hasPartialData() const {
        return !_buffer.empty();
    }

    /**
     * @brief Retourne la taille accumulée courante.
     */
    size_t getCurrentSize() const {
        return _buffer.size();
    }

    /**
     * @brief Retourne la taille maximale configurée.
     */
    size_t getMaxSize() const {
        return _maxSize;
    }

private:
    size_t _maxSize;
    int _expectedTotalLen = 0;
    std::vector<char> _buffer;
};
