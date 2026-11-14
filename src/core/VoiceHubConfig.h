#pragma once
#include <Arduino.h>

// Inclusion conditionnelle de secrets pour exemples locaux (sans bloquer la bibliothèque)
#if __has_include("Secrets.h")
#include "Secrets.h"
#elif __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef GEMINI_API_KEY
#define GEMINI_API_KEY ""
#endif
#ifndef GEMINI_LIVE_MODEL_CONFIG
#define GEMINI_LIVE_MODEL_CONFIG "models/gemini-3.8-live"
#endif
#ifndef GEMINI_LIVE_VOICE_CONFIG
#define GEMINI_LIVE_VOICE_CONFIG "Aoede"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

// =============================================================================
// Configuration Materielle & Dialogue VoxCore v0.2.2 / v0.3.0 pour VoiceKit v0.2.0
// Architecture : Gemini Live Only
// =============================================================================
static constexpr const char* VOXCORE_VERSION = "v0.2.1";
static constexpr const char* VOICEHUB_VERSION = VOXCORE_VERSION;
static constexpr const char* VOICEKIT_VERSION = "v0.6.6.2";

// Niveaux de diagnostics centralisés
#ifndef VOXCORE_DIAGNOSTICS_LEVEL
#define VOXCORE_DIAGNOSTICS_LEVEL 1
#endif

// Modes audio
#define VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK 1
#define VOXCORE_AUDIO_MODE_DEDICATED_TASK    2

#ifndef VOXCORE_AUDIO_MODE
#if defined(ESP32) || defined(ARDUINO)
#define VOXCORE_AUDIO_MODE VOXCORE_AUDIO_MODE_DEDICATED_TASK
#else
#define VOXCORE_AUDIO_MODE VOXCORE_AUDIO_MODE_REACTIVE_CALLBACK
#endif
#endif

static constexpr uint32_t AUDIO_RELEASE_TIMEOUT_MS = 2500;

// =============================================================================
// Configuration Gemini Live WebSocket API
// =============================================================================
static constexpr const char *GEMINI_LIVE_MODEL = "models/gemini-3.8-live";
static constexpr const char *GEMINI_LIVE_VOICE = "Aoede"; // Aoede, Charon, Fenrir, Kore, Puck
static constexpr const char *GEMINI_LIVE_HOST = "generativelanguage.googleapis.com";
static constexpr const char *GEMINI_LIVE_PATH = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent";
static constexpr int GEMINI_LIVE_PORT = 443;
static constexpr uint32_t GEMINI_LIVE_CONNECT_TIMEOUT_MS = 10000;
static constexpr uint32_t GEMINI_LIVE_INACTIVITY_TIMEOUT_MS = 0; // Session maintenue en continu via Ping/Pong
static constexpr uint32_t LIVE_RECONNECT_INTERVAL_MS = 2000;
static constexpr size_t LIVE_MAX_RECONNECT_ATTEMPTS = 3;
static constexpr size_t LIVE_AUDIO_TX_QUEUE_MAX = 8;
static constexpr size_t LIVE_AUDIO_PLAY_THRESHOLD = 2;
static constexpr size_t LIVE_AUDIO_RX_BUFFER_SAMPLES = 120000; // 5 secondes de tampon PCM 24 kHz en PSRAM
static constexpr uint32_t LIVE_AUDIO_SAMPLE_RATE_RX = 24000;   // Gemini Live génère du 24 kHz mono 16 bits
static constexpr uint32_t LIVE_MIN_FREE_INTERNAL_HEAP = 30000; // 30 Ko min RAM interne pour TLS WebSocket

// Configuration Audio Matérielle
static constexpr uint32_t AUDIO_SAMPLE_RATE_HZ = 16000;
static constexpr size_t AUDIO_CHUNK_SAMPLES = 512;
static constexpr size_t AUDIO_MIN_SAMPLES = 1600;

// Configuration Restitution Audio
static constexpr uint8_t VOXCORE_AUDIO_CHANNEL_DEFAULT = 0;
static constexpr uint8_t M5SPK_VIRTUAL_CHANNEL = VOXCORE_AUDIO_CHANNEL_DEFAULT;
static constexpr unsigned DEFAULT_SPEAKER_VOLUME = 65; // %
#ifndef AUDIO_STOP_TIMEOUT_MS
#define AUDIO_STOP_TIMEOUT_MS 2000
#endif

// Configuration Reseau & Horloge TLS
static constexpr const char *NTP_SERVER1 = "time.google.com";
static constexpr const char *NTP_SERVER2 = "pool.ntp.org";
static constexpr time_t NTP_MIN_VALID_TIMESTAMP = 1700000000;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 30000;
static constexpr uint32_t NTP_TIMEOUT_MS = 15000;
static constexpr uint32_t NTP_RETRY_INTERVAL_MS = 30000;
