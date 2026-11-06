#include "ReplyMemory.h"
#include "core/Utf8Utils.h"

ReplyMemory::ReplyMemory(size_t capacity, size_t maxReplyBytes)
    : _capacity(capacity > 0 ? capacity : DEFAULT_CAPACITY),
      _maxReplyBytes(maxReplyBytes > 0 ? maxReplyBytes : DEFAULT_MAX_REPLY_BYTES) {
    _entries.reserve(_capacity);
}

String ReplyMemory::truncateUtf8(const String& input, size_t maxBytes) {
    if (input.length() <= maxBytes) {
        return input;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(input.c_str());
    size_t cut = maxBytes;

    // Si cut tombe sur un octet de continuation (0x80..0xBF), remonter jusqu'à l'octet de début
    while (cut > 0 && (bytes[cut] & 0xC0) == 0x80) {
        cut--;
    }

    // Si bytes[cut] est un octet de tête multioctet (0xC0..0xFF)
    if (cut < input.length() && (bytes[cut] & 0x80) != 0) {
        size_t seqLen = 1;
        uint8_t b0 = bytes[cut];
        if ((b0 & 0xE0) == 0xC0) seqLen = 2;
        else if ((b0 & 0xF0) == 0xE0) seqLen = 3;
        else if ((b0 & 0xF8) == 0xF0) seqLen = 4;

        if (cut + seqLen > maxBytes) {
            // La séquence ne peut pas être complète dans maxBytes -> couper avant
        } else {
            cut += seqLen;
        }
    }

    String truncated = input.substring(0, cut);
    // Assainissement final de précaution
    return Utf8Utils::sanitizeUtf8(truncated);
}

bool ReplyMemory::addReply(const String& reply) {
    String trimmed = reply;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        return false;
    }

    // Troncature sécurisée respectant les limites de taille et l'encodage UTF-8
    String bounded = truncateUtf8(trimmed, _maxReplyBytes);

    ReplyEntry entry;
    entry.text = bounded;
    entry.timestampMs = millis();

    // Insérer en tête (index 0 = la plus récente)
    _entries.insert(_entries.begin(), entry);

    // Éviction FIFO de l'entrée la plus ancienne si capacité dépassée
    while (_entries.size() > _capacity) {
        _entries.pop_back();
    }

    return true;
}

String ReplyMemory::getLastReply() const {
    if (_entries.empty()) {
        return "";
    }
    return _entries[0].text;
}

bool ReplyMemory::getLastReply(String& out) const {
    if (_entries.empty()) {
        out = "";
        return false;
    }
    out = _entries[0].text;
    return true;
}

String ReplyMemory::getPreviousReply() const {
    if (_entries.size() < 2) {
        return "";
    }
    return _entries[1].text;
}

bool ReplyMemory::getPreviousReply(String& out) const {
    if (_entries.size() < 2) {
        out = "";
        return false;
    }
    out = _entries[1].text;
    return true;
}

String ReplyMemory::getReplyAt(size_t indexFromLatest) const {
    if (indexFromLatest >= _entries.size()) {
        return "";
    }
    return _entries[indexFromLatest].text;
}

bool ReplyMemory::getReplyAt(size_t indexFromLatest, String& out) const {
    if (indexFromLatest >= _entries.size()) {
        out = "";
        return false;
    }
    out = _entries[indexFromLatest].text;
    return true;
}

void ReplyMemory::clear() {
    _entries.clear();
}

size_t ReplyMemory::getCount() const {
    return _entries.size();
}

size_t ReplyMemory::getCapacity() const {
    return _capacity;
}

void ReplyMemory::setCapacity(size_t capacity) {
    _capacity = (capacity > 0 ? capacity : 1);
    while (_entries.size() > _capacity) {
        _entries.pop_back();
    }
}

size_t ReplyMemory::getMaxReplyBytes() const {
    return _maxReplyBytes;
}

bool ReplyMemory::isEmpty() const {
    return _entries.empty();
}

bool ReplyMemory::isFull() const {
    return _entries.size() >= _capacity;
}

