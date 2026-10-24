#pragma once
#include <time.h>

inline void sntp_set_time_sync_notification_cb(void (*cb)(struct timeval*)) {
    if (cb) {
        struct timeval tv;
        tv.tv_sec = time(nullptr);
        tv.tv_usec = 0;
        cb(&tv);
    }
}
