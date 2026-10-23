#include "ClockManager.h"

ClockManager::ClockManager()
    : last_minute(-1),
      valid_time(false),
      force_refresh(true) {
}

bool ClockManager::init() {
    // Configure local timezone (Europe/Zurich with automatic DST)
    setenv("TZ", TIMEZONE_POSIX, 1);
    tzset();

    valid_time = false;

    // Check if onboard BM8563 RTC is enabled and contains valid time
    if (M5.Rtc.isEnabled()) {
        m5::rtc_datetime_t dt;
        bool volt_low = M5.Rtc.getVoltLow();
        bool has_dt = M5.Rtc.getDateTime(&dt);

        if (!volt_low && has_dt) {
            // Plausibility check: valid date/time ranges
            if (dt.date.year >= 2024 && dt.date.year <= 2099 &&
                dt.date.month >= 1 && dt.date.month <= 12 &&
                dt.date.date >= 1 && dt.date.date <= 31 &&
                dt.time.hours < 24 && dt.time.minutes < 60 && dt.time.seconds < 60) {
                
                // RTC contains plausible UTC time -> restore to system clock as UTC
                M5.Rtc.setSystemTimeFromRtc();

                time_t now = time(nullptr);
                struct tm timeinfo;
                localtime_r(&now, &timeinfo);

                if (timeinfo.tm_year >= 124) {
                    valid_time = true;
                    Serial.printf("[ClockManager] Valid UTC time restored from RTC -> Local time: %02d:%02d:%02d (%04d-%02d-%02d)\n",
                                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
                }
            } else {
                Serial.printf("[ClockManager] RTC date/time is invalid or uninitialized (year: %d). Skipping restore.\n", dt.date.year);
            }
        } else {
            Serial.printf("[ClockManager] RTC battery low or uninitialized (volt_low: %d). Skipping restore.\n", volt_low);
        }
    }

    if (!valid_time) {
        Serial.println("[ClockManager] No valid time in RTC. Clock will display '--:--' until NTP sync.");
    }

    return valid_time;
}

bool ClockManager::getTimeString(char* out_str, size_t max_len) {
    if (!out_str || max_len < 6) {
        return false;
    }

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year >= 124) {
        valid_time = true;
        snprintf(out_str, max_len, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        return true;
    } else {
        valid_time = false;
        snprintf(out_str, max_len, "--:--");
        return false;
    }
}

bool ClockManager::getLocalTime(int& out_hour, int& out_minute) {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year >= 124) {
        valid_time = true;
        out_hour = timeinfo.tm_hour;
        out_minute = timeinfo.tm_min;
        return true;
    } else {
        valid_time = false;
        out_hour = 0;
        out_minute = 0;
        return false;
    }
}

bool ClockManager::update(char* out_str, size_t max_len) {
    if (!out_str || max_len < 6) {
        return false;
    }

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    bool is_valid = (timeinfo.tm_year >= 124);
    int current_minute = is_valid ? timeinfo.tm_min : -1;

    if (force_refresh || current_minute != last_minute || is_valid != valid_time) {
        last_minute = current_minute;
        valid_time = is_valid;
        force_refresh = false;

        if (valid_time) {
            snprintf(out_str, max_len, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        } else {
            snprintf(out_str, max_len, "--:--");
        }
        return true;
    }

    return false;
}

void ClockManager::saveToRtc(const tm& utc_timeinfo) {
    // Strictly validate time before writing to hardware RTC
    if (utc_timeinfo.tm_year < 124 || utc_timeinfo.tm_year > 199 ||
        utc_timeinfo.tm_mon < 0 || utc_timeinfo.tm_mon > 11 ||
        utc_timeinfo.tm_mday < 1 || utc_timeinfo.tm_mday > 31 ||
        utc_timeinfo.tm_hour < 0 || utc_timeinfo.tm_hour > 23 ||
        utc_timeinfo.tm_min < 0 || utc_timeinfo.tm_min > 59 ||
        utc_timeinfo.tm_sec < 0 || utc_timeinfo.tm_sec > 59) {
        Serial.println("[ClockManager] WARN: Refusing to save unconfirmed or invalid time to RTC.");
        return;
    }

    if (M5.Rtc.isEnabled()) {
        M5.Rtc.setDateTime(&utc_timeinfo);
        valid_time = true;
        force_refresh = true;
        Serial.printf("[ClockManager] Saved confirmed UTC time to RTC: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                      utc_timeinfo.tm_year + 1900, utc_timeinfo.tm_mon + 1, utc_timeinfo.tm_mday,
                      utc_timeinfo.tm_hour, utc_timeinfo.tm_min, utc_timeinfo.tm_sec);
    }
}

