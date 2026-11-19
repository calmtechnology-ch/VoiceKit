#include "AlarmManager.h"

#include <Preferences.h>
#include <time.h>

static constexpr const char* ALARM_NAMESPACE = "orientclock";

bool AlarmManager::init() {
    Preferences prefs;
    if (!prefs.begin(ALARM_NAMESPACE, true)) {
        Serial.println("[Alarm] NVS indisponible, valeurs par defaut utilisees.");
        return false;
    }
    enabled_ = prefs.getBool("enabled", false);
    hour_ = constrain(prefs.getInt("hour", 7), 0, 23);
    minute_ = constrain(prefs.getInt("minute", 0), 0, 59);
    prefs.end();
    return true;
}

void AlarmManager::persist() {
    Preferences prefs;
    if (!prefs.begin(ALARM_NAMESPACE, false)) return;
    prefs.putBool("enabled", enabled_);
    prefs.putInt("hour", hour_);
    prefs.putInt("minute", minute_);
    prefs.end();
}

void AlarmManager::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    persist();
}

void AlarmManager::toggle() {
    setEnabled(!enabled_);
}

void AlarmManager::shiftMinutes(int delta_minutes) {
    int total = hour_ * 60 + minute_ + delta_minutes;
    total %= 24 * 60;
    if (total < 0) total += 24 * 60;
    hour_ = total / 60;
    minute_ = total % 60;
    persist();
}

void AlarmManager::setTime(int hour, int minute) {
    int total = hour * 60 + minute;
    total %= 24 * 60;
    if (total < 0) total += 24 * 60;
    hour_ = total / 60;
    minute_ = total % 60;
    persist();
}

void AlarmManager::snooze(uint8_t minutes) {
    time_t now = time(nullptr);
    if (now > 0) snooze_until_ = now + static_cast<time_t>(minutes) * 60;
}

void AlarmManager::cancelSnooze() {
    snooze_until_ = 0;
}

bool AlarmManager::update() {
    time_t now = time(nullptr);
    struct tm local_time;
    localtime_r(&now, &local_time);
    if (local_time.tm_year < 124) return false;

    if (snooze_until_ > 0 && now >= snooze_until_) {
        snooze_until_ = 0;
        return true;
    }
    if (!enabled_) return false;

    if (local_time.tm_hour == hour_ && local_time.tm_min == minute_ &&
        local_time.tm_yday != last_trigger_year_day_) {
        last_trigger_year_day_ = local_time.tm_yday;
        return true;
    }
    return false;
}

String AlarmManager::displayText() const {
    char text[24];
    snprintf(text, sizeof(text), "%s %02d:%02d", enabled_ ? "Reveil" : "Reveil off", hour_, minute_);
    return String(text);
}
