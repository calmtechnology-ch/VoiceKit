#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>
#include <iostream>

// Minimal Arduino environment mock for native host unit tests
class String : public std::string {
public:
    String() : std::string() {}
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(std::string&& s) : std::string(std::move(s)) {}
    String(int val) : std::string(std::to_string(val)) {}
    String(unsigned int val) : std::string(std::to_string(val)) {}
    String(long val) : std::string(std::to_string(val)) {}
    String(unsigned long val) : std::string(std::to_string(val)) {}
    String(unsigned long long val) : std::string(std::to_string(val)) {}

    bool isEmpty() const { return empty(); }
    int indexOf(char c, size_t fromIndex = 0) const { auto pos = find(c, fromIndex); return pos == std::string::npos ? -1 : (int)pos; }
    int indexOf(const char* s, size_t fromIndex = 0) const { auto pos = find(s, fromIndex); return pos == std::string::npos ? -1 : (int)pos; }
    int indexOf(const String& s, size_t fromIndex = 0) const { auto pos = find(s, fromIndex); return pos == std::string::npos ? -1 : (int)pos; }
    int lastIndexOf(const char* s) const { auto pos = rfind(s); return pos == std::string::npos ? -1 : (int)pos; }
    bool startsWith(const char* prefix) const { return rfind(prefix, 0) == 0; }
    bool startsWith(const String& prefix) const { return startsWith(prefix.c_str()); }
    bool endsWith(const char* suffix) const {
        if (!suffix) return false;
        size_t sLen = strlen(suffix);
        if (length() < sLen) return false;
        return compare(length() - sLen, sLen, suffix) == 0;
    }
    bool endsWith(const String& suffix) const { return endsWith(suffix.c_str()); }
    String substring(size_t start, size_t end = std::string::npos) const {
        if (start >= length()) return String("");
        if (end == std::string::npos || end > length()) end = length();
        return String(substr(start, end - start));
    }
    void trim() {
        while (!empty() && (front() == ' ' || front() == '\t' || front() == '\r' || front() == '\n')) erase(begin());
        while (!empty() && (back() == ' ' || back() == '\t' || back() == '\r' || back() == '\n')) pop_back();
    }
    void toLowerCase() {
        for (char& c : *this) c = tolower(c);
    }
    bool equalsIgnoreCase(const char* s) const {
        if (!s) return false;
        if (length() != strlen(s)) return false;
        for (size_t i = 0; i < length(); ++i) {
            if (tolower((*this)[i]) != tolower(s[i])) return false;
        }
        return true;
    }
    bool equalsIgnoreCase(const String& s) const {
        return equalsIgnoreCase(s.c_str());
    }
    bool reserve(size_t n) { std::string::reserve(n); return true; }
};

inline String operator+(const String& a, const String& b) { return String((const std::string&)a + (const std::string&)b); }
inline String operator+(const String& a, const char* b) { return String((const std::string&)a + b); }
inline String operator+(const char* a, const String& b) { return String(a + (const std::string&)b); }

inline uint32_t& getMockMillisOffset() {
    static uint32_t s_offset = 0;
    return s_offset;
}
inline void setMockMillisOffset(uint32_t offset) { getMockMillisOffset() = offset; }
inline void advanceMockMillis(uint32_t ms) { getMockMillisOffset() += ms; }

inline uint32_t millis() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() + getMockMillisOffset();
}

inline uint32_t micros() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() + getMockMillisOffset() * 1000;
}

inline void delay(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline void configTime(long, int, const char*, const char* = nullptr, const char* = nullptr) {}

#ifndef PROGMEM
#define PROGMEM
#endif

class MockSerial {
public:
    void print(const char* s) {}
    void print(int n) {}
    void println(const char* s = "") {}
    void println(const String& s) {}
    void println(int n) {}
    template<typename... Args>
    void printf(const char* fmt, Args... args) {}
};
static MockSerial Serial;

inline uint32_t esp_get_free_heap_size() { return 150000; }

#if defined(_WIN32)
#ifndef localtime_r
inline struct tm* localtime_r(const time_t* t, struct tm* tm_out) {
#if defined(__MINGW32__) || defined(_MSC_VER)
    if (localtime_s(tm_out, t) == 0) return tm_out;
    return nullptr;
#else
    struct tm* p = localtime(t);
    if (p) { *tm_out = *p; return tm_out; }
    return nullptr;
#endif
}
#endif

#ifndef gmtime_r
inline struct tm* gmtime_r(const time_t* t, struct tm* tm_out) {
#if defined(__MINGW32__) || defined(_MSC_VER)
    if (gmtime_s(tm_out, t) == 0) return tm_out;
    return nullptr;
#else
    struct tm* p = gmtime(t);
    if (p) { *tm_out = *p; return tm_out; }
    return nullptr;
#endif
}
#endif
#endif

class MockESP {
public:
    uint32_t getPsramSize() const { return 8 * 1024 * 1024; }
};
static MockESP ESP;
inline bool psramInit() { return true; }
inline void* ps_malloc(size_t size) { return malloc(size); }
