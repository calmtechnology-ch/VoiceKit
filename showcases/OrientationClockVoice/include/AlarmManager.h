#pragma once

#include <Arduino.h>

/**
 * Reveil quotidien minimal persiste dans la NVS de l'ESP32.
 * update() ne declenche qu'une fois par jour, durant la minute programmee.
 */
class AlarmManager {
public:
    bool init();
    void setEnabled(bool enabled);
    void toggle();
    void shiftMinutes(int delta_minutes);
    void setTime(int hour, int minute);
    void snooze(uint8_t minutes = 5);
    void cancelSnooze();
    bool update();

    bool isEnabled() const { return enabled_; }
    int hour() const { return hour_; }
    int minute() const { return minute_; }
    String displayText() const;
    bool isSnoozed() const { return snooze_until_ > 0; }

private:
    void persist();

    int hour_ = 7;
    int minute_ = 0;
    int last_trigger_year_day_ = -1;
    time_t snooze_until_ = 0;
    bool enabled_ = false;
};
