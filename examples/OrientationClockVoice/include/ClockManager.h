#pragma once

#include <Arduino.h>
#include <time.h>
#include <M5Unified.h>

#if __has_include("wifi_config.h")
    #include "wifi_config.h"
#endif
#ifndef TIMEZONE_POSIX
    #define TIMEZONE_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

/**
 * @class ClockManager
 * @brief Handles system time retrieval, validation, 24-hour formatting,
 *        minute-change detection, and hardware RTC persistence (UTC convention).
 */
class ClockManager {
public:
    ClockManager();

    /**
     * @brief Initialize ClockManager, configure timezone, and restore UTC from RTC.
     * @return true if valid time was restored from RTC.
     */
    bool init();

    /**
     * @brief Periodic non-blocking check.
     * @param out_str Output buffer for time string (e.g. "12:45").
     * @param max_len Size of buffer (at least 6 bytes).
     * @return true if the displayed time has changed (minute rollover or first valid sync).
     */
    bool update(char* out_str, size_t max_len);

    /**
     * @brief Retrieve current formatted time string regardless of whether it changed.
     * @param out_str Output buffer.
     * @param max_len Size of buffer.
     * @return true if a valid time is currently available.
     */
    bool getTimeString(char* out_str, size_t max_len);

    /**
     * @brief Retrieve current local hour and minute (0-23 and 0-59).
     * Used by VoiceKit to announce the exact displayed time.
     * @param out_hour Output hour variable.
     * @param out_minute Output minute variable.
     * @return true if valid local time is available, false otherwise.
     */
    bool getLocalTime(int& out_hour, int& out_minute);

    /**
     * @brief Check whether system time has been set and is valid (year >= 2024).
     */
    bool hasValidTime() const { return valid_time; }

    /**
     * @brief Save confirmed UTC time structure into the onboard BM8563 RTC.
     * @param utc_timeinfo Reference to tm structure with UTC time.
     */
    void saveToRtc(const tm& utc_timeinfo);

    /**
     * @brief Force an update on the next call to update().
     */
    void forceRefresh() { force_refresh = true; }

private:
    int last_minute;
    bool valid_time;
    bool force_refresh;
};

