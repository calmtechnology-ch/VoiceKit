#include "CachedAudioOutput.h"

#include <LittleFS.h>
#include <esp_heap_caps.h>

namespace {
constexpr const char* CACHE_PATH = "/last_voice.pcm";
constexpr uint32_t CACHE_MAGIC = 0x564B4332; // "VKC2"
constexpr size_t CACHE_CAPACITY_BYTES = 1024 * 1024;

struct CacheHeader {
    uint32_t magic;
    uint32_t text_hash;
    uint32_t sample_rate;
    uint32_t sample_count;
    uint8_t stereo;
    uint8_t reserved[3];
    char text[192];
};
}

CachedAudioOutput::~CachedAudioOutput() {
    if (buffer_) heap_caps_free(buffer_);
}

bool CachedAudioOutput::ensureBuffer() {
    if (buffer_) return true;
    buffer_capacity_samples_ = CACHE_CAPACITY_BYTES / sizeof(int16_t);
    buffer_ = static_cast<int16_t*>(
        heap_caps_malloc(CACHE_CAPACITY_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer_) {
        buffer_capacity_samples_ = 0;
        Serial.println("[AudioCache] PSRAM insuffisante, cache desactive.");
        return false;
    }
    return true;
}

bool CachedAudioOutput::begin() {
    bool storage_ok = LittleFS.begin(true);
    if (!storage_ok) Serial.println("[AudioCache] LittleFS indisponible.");
    ensureBuffer();
    return output_.begin();
}

void CachedAudioOutput::end(AudioStopReason reason) { output_.end(reason); }
bool CachedAudioOutput::isRunning() const { return output_.isRunning(); }

bool CachedAudioOutput::playRaw(const int16_t* samples, size_t sample_count,
                                uint32_t sample_rate_hz, bool stereo, uint8_t channel) {
    bool accepted = output_.playRaw(samples, sample_count, sample_rate_hz, stereo, channel);
    if (!accepted || !capturing_ || !buffer_) return accepted;

    if (captured_samples_ == 0) {
        capture_sample_rate_ = sample_rate_hz;
        capture_stereo_ = stereo;
    }
    if (sample_rate_hz != capture_sample_rate_ || stereo != capture_stereo_ ||
        captured_samples_ + sample_count > buffer_capacity_samples_) {
        capture_overflow_ = true;
        capturing_ = false;
        return accepted;
    }
    memcpy(buffer_ + captured_samples_, samples, sample_count * sizeof(int16_t));
    captured_samples_ += sample_count;
    return accepted;
}

int CachedAudioOutput::isPlaying(uint8_t channel) const { return output_.isPlaying(channel); }
void CachedAudioOutput::stop(uint8_t channel, AudioStopReason reason) { output_.stop(channel, reason); }
void CachedAudioOutput::setVolume(unsigned value) { output_.setVolume(value); }
void CachedAudioOutput::setChannelVolume(uint8_t channel, uint8_t value) { output_.setChannelVolume(channel, value); }
unsigned CachedAudioOutput::getVolume() const { return output_.getVolume(); }
uint32_t CachedAudioOutput::getHardwareDrainMs() const { return output_.getHardwareDrainMs(); }

void CachedAudioOutput::setBufferReleaseCallback(void* args, BufferReleaseCallback cb) {
    release_cb_args_ = args;
    release_cb_ = cb;
    if (!replaying_) output_.setBufferReleaseCallback(args, cb);
}

void CachedAudioOutput::resetTurnMetrics() { output_.resetTurnMetrics(); }
bool CachedAudioOutput::wasStopCalledInTurn() const { return output_.wasStopCalledInTurn(); }
bool CachedAudioOutput::wasEndCalledInTurn() const { return output_.wasEndCalledInTurn(); }
uint32_t CachedAudioOutput::getLastStopTimeMs() const { return output_.getLastStopTimeMs(); }
uint32_t CachedAudioOutput::getLastEndTimeMs() const { return output_.getLastEndTimeMs(); }
AudioStopReason CachedAudioOutput::getLastStopReason() const { return output_.getLastStopReason(); }
AudioStopReason CachedAudioOutput::getLastEndReason() const { return output_.getLastEndReason(); }
uint32_t CachedAudioOutput::getLastPlayRawAcceptedTimeMs() const { return output_.getLastPlayRawAcceptedTimeMs(); }
uint32_t CachedAudioOutput::getStopCallCountInTurn() const { return output_.getStopCallCountInTurn(); }
uint32_t CachedAudioOutput::getEndCallCountInTurn() const { return output_.getEndCallCountInTurn(); }
uint32_t CachedAudioOutput::getBeginCallCountInTurn() const { return output_.getBeginCallCountInTurn(); }
uint32_t CachedAudioOutput::getPlayRawCallCountInTurn() const { return output_.getPlayRawCallCountInTurn(); }
size_t CachedAudioOutput::getSamplesAcceptedInTurn() const { return output_.getSamplesAcceptedInTurn(); }

uint32_t CachedAudioOutput::hashText(const String& text) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < text.length(); ++i) {
        hash ^= static_cast<uint8_t>(text[i]);
        hash *= 16777619u;
    }
    return hash;
}

void CachedAudioOutput::startCapture(const String& text) {
    if (!ensureBuffer() || replaying_) return;
    captured_samples_ = 0;
    capture_text_hash_ = hashText(text);
    capture_text_ = text;
    capture_overflow_ = false;
    capturing_ = true;
}

void CachedAudioOutput::cancelCapture() {
    capturing_ = false;
    captured_samples_ = 0;
    capture_overflow_ = false;
}

bool CachedAudioOutput::finishCapture() {
    capturing_ = false;
    if (!buffer_ || captured_samples_ == 0 || capture_overflow_) return false;

    File file = LittleFS.open(CACHE_PATH, "w");
    if (!file) return false;
    CacheHeader header{};
    header.magic = CACHE_MAGIC;
    header.text_hash = capture_text_hash_;
    header.sample_rate = capture_sample_rate_;
    header.sample_count = static_cast<uint32_t>(captured_samples_);
    header.stereo = static_cast<uint8_t>(capture_stereo_);
    strlcpy(header.text, capture_text_.c_str(), sizeof(header.text));
    bool ok = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
              file.write(reinterpret_cast<const uint8_t*>(buffer_),
                         captured_samples_ * sizeof(int16_t)) == captured_samples_ * sizeof(int16_t);
    file.close();
    if (ok) {
        cached_bytes_ = captured_samples_ * sizeof(int16_t);
        Serial.printf("[AudioCache] %u octets PCM sauvegardes.\n", (unsigned)cached_bytes_);
    }
    return ok;
}

bool CachedAudioOutput::readHeader(uint32_t& text_hash, uint32_t& sample_rate,
                                   uint32_t& sample_count, bool& stereo) const {
    File file = LittleFS.open(CACHE_PATH, "r");
    if (!file) return false;
    CacheHeader header{};
    bool ok = file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
              header.magic == CACHE_MAGIC && header.sample_count > 0 &&
              header.sample_count <= buffer_capacity_samples_;
    file.close();
    if (!ok) return false;
    text_hash = header.text_hash;
    sample_rate = header.sample_rate;
    sample_count = header.sample_count;
    stereo = header.stereo != 0;
    return true;
}

bool CachedAudioOutput::hasCached(const String& text) const {
    uint32_t hash = 0, rate = 0, count = 0;
    bool stereo = false;
    return readHeader(hash, rate, count, stereo) && hash == hashText(text);
}

String CachedAudioOutput::cachedText() const {
    File file = LittleFS.open(CACHE_PATH, "r");
    if (!file) return "";
    CacheHeader header{};
    bool ok = file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
              header.magic == CACHE_MAGIC && header.text[0] != '\0';
    file.close();
    header.text[sizeof(header.text) - 1] = '\0';
    return ok ? String(header.text) : String("");
}

bool CachedAudioOutput::playCached(const String& text) {
    if (replaying_ || output_.isPlaying() > 0 || !ensureBuffer()) return false;
    uint32_t hash = 0, rate = 0, count = 0;
    bool stereo = false;
    if (!readHeader(hash, rate, count, stereo) || hash != hashText(text)) return false;

    File file = LittleFS.open(CACHE_PATH, "r");
    if (!file || !file.seek(sizeof(CacheHeader))) return false;
    size_t bytes = static_cast<size_t>(count) * sizeof(int16_t);
    bool loaded = file.read(reinterpret_cast<uint8_t*>(buffer_), bytes) == bytes;
    file.close();
    if (!loaded) return false;

    // Le callback VoiceKit ne doit pas recevoir notre buffer de relecture autonome.
    output_.setBufferReleaseCallback(nullptr, nullptr);
    replaying_ = output_.playRaw(buffer_, count, rate, stereo, 0);
    if (!replaying_) output_.setBufferReleaseCallback(release_cb_args_, release_cb_);
    return replaying_;
}

void CachedAudioOutput::update() {
    if (replaying_ && output_.isPlaying() == 0) {
        replaying_ = false;
        output_.setBufferReleaseCallback(release_cb_args_, release_cb_);
        Serial.println("[AudioCache] Relecture hors ligne terminee.");
    }
}
