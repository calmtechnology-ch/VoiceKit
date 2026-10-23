#pragma once
#include <Arduino.h>
#include <vector>
#include "VoiceHubConfig.h"
#include "VoiceHubTypes.h"

enum class BoardProfile {
    M5STACK_CORES3,
    ATOMS3R_GENERIC,
    CUSTOM_ESP32S3
};

struct ToolDeclaration {
    String name;
    String description;
    String parametersSchemaJson;
};

struct VoiceEngineConfig {
    String appName = "VoiceKit";
    String systemInstruction = "";
    std::vector<ToolDeclaration> tools;
    const char* apiKey = nullptr;
    const char* model = GEMINI_LIVE_MODEL;
    const char* voice = GEMINI_LIVE_VOICE;
    const char* wifiSsid = nullptr;
    const char* wifiPassword = nullptr;
    BoardProfile board = BoardProfile::M5STACK_CORES3;
};
