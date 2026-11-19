#include "OrientationManager.h"
#include <math.h>

OrientationManager::OrientationManager()
    : last_sample_ms(0),
      candidate_start_ms(0),
      current_orientation(DeviceOrientation::LANDSCAPE_NORMAL),
      candidate_orientation(DeviceOrientation::LANDSCAPE_NORMAL),
      detected_orientation(DeviceOrientation::LANDSCAPE_NORMAL),
      filt_x(0.0f),
      filt_y(1.0f),
      filt_z(0.0f),
      last_angle_deg(90.0f),
      initialized(false),
      is_flat(false) {
}

float OrientationManager::angleDiff(float a, float b) {
    float d = fmodf(fabsf(a - b), 360.0f);
    return (d > 180.0f) ? 360.0f - d : d;
}

DeviceOrientation OrientationManager::classifySectorNearest(float angle_deg) {
    DeviceOrientation best_match = ORIENTATION_TABLE[0].orientation;
    float min_diff = 360.0f;

    for (const auto& entry : ORIENTATION_TABLE) {
        float diff = angleDiff(angle_deg, entry.center_angle_deg);
        if (diff < min_diff) {
            min_diff = diff;
            best_match = entry.orientation;
        }
    }

    return best_match;
}

DeviceOrientation OrientationManager::classifyPhysicalOrientation(float angle_deg) const {
    // 1. Angular hysteresis deadband around current confirmed orientation
    float current_center = getSectorCenterAngle(current_orientation);
    if (angleDiff(angle_deg, current_center) <= (45.0f + HYSTERESIS_DEG)) {
        return current_orientation;
    }

    // 2. Beyond deadband, classify to nearest sector center
    return classifySectorNearest(angle_deg);
}

bool OrientationManager::readRawAccel(float& rx, float& ry, float& rz) {
    M5.Imu.update();
    return M5.Imu.getAccel(&rx, &ry, &rz);
}

void OrientationManager::applyEmaFilter(float rx, float ry, float rz) {
    filt_x = filt_x * (1.0f - FILTER_ALPHA) + rx * FILTER_ALPHA;
    filt_y = filt_y * (1.0f - FILTER_ALPHA) + ry * FILTER_ALPHA;
    filt_z = filt_z * (1.0f - FILTER_ALPHA) + rz * FILTER_ALPHA;
}

bool OrientationManager::isDisturbance(float norm) const {
    return (norm < DISTURBANCE_MIN_G || norm > DISTURBANCE_MAX_G);
}

bool OrientationManager::checkFlat(float in_plane, float az) const {
    return (fabsf(az) >= FLAT_MIN_Z_G || in_plane <= FLAT_MAX_IN_PLANE_G);
}

float OrientationManager::calculateAngle(float ax, float ay) const {
    return atan2f(ay, ax) * (180.0f / (float)M_PI);
}

bool OrientationManager::init() {
    if (!M5.Imu.isEnabled()) {
        M5.Imu.begin();
    }

    initialized = M5.Imu.isEnabled();
    if (!initialized) {
        Serial.println("[OrientationManager] WARN: BMI270 IMU not detected on internal I2C bus");
        return false;
    }

    // Read initial acceleration with brief retry (5 x 10ms = max 50ms)
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    bool got_sample = false;
    for (int retry = 0; retry < 5; ++retry) {
        delay(10);
        if (readRawAccel(rx, ry, rz)) {
            got_sample = true;
            break;
        }
    }

    if (got_sample) {
        filt_x = rx;
        filt_y = ry;
        filt_z = rz;

        float norm = sqrtf(filt_x * filt_x + filt_y * filt_y + filt_z * filt_z);
        float in_plane = sqrtf(filt_x * filt_x + filt_y * filt_y);

        // Check if flat at startup
        if (checkFlat(in_plane, filt_z)) {
            is_flat = true;
            current_orientation = DeviceOrientation::LANDSCAPE_NORMAL;
            candidate_orientation = DeviceOrientation::LANDSCAPE_NORMAL;
            detected_orientation = DeviceOrientation::LANDSCAPE_NORMAL;
            last_angle_deg = 90.0f;
            Serial.printf("[OrientationManager] Device is flat at boot (Z=%.2f in_plane=%.2f). Defaulting to LANDSCAPE_NORMAL.\n",
                          filt_z, in_plane);
        } else if (!isDisturbance(norm)) {
            // Reliable initial acceleration -> determine orientation immediately
            is_flat = false;
            last_angle_deg = calculateAngle(filt_x, filt_y);
            DeviceOrientation detected = classifySectorNearest(last_angle_deg);
            current_orientation = detected;
            candidate_orientation = detected;
            detected_orientation = detected;
            Serial.printf("[OrientationManager] Initial orientation detected: %s (ax=%.2f ay=%.2f az=%.2f angle=%.1f deg)\n",
                          orientationToString(current_orientation), filt_x, filt_y, filt_z, last_angle_deg);
        } else {
            // Dynamic motion at boot -> keep default
            is_flat = false;
            current_orientation = DeviceOrientation::LANDSCAPE_NORMAL;
            candidate_orientation = DeviceOrientation::LANDSCAPE_NORMAL;
            detected_orientation = DeviceOrientation::LANDSCAPE_NORMAL;
            last_angle_deg = 90.0f;
            Serial.printf("[OrientationManager] Dynamic motion detected at boot (|a|=%.2fg). Defaulting to LANDSCAPE_NORMAL.\n", norm);
        }
    } else {
        filt_x = 0.0f;
        filt_y = 1.0f;
        filt_z = 0.0f;
        last_angle_deg = 90.0f;
        current_orientation = DeviceOrientation::LANDSCAPE_NORMAL;
        candidate_orientation = DeviceOrientation::LANDSCAPE_NORMAL;
        detected_orientation = DeviceOrientation::LANDSCAPE_NORMAL;
    }

    uint32_t now = millis();
    last_sample_ms = now;
    candidate_start_ms = now;

    return true;
}

bool OrientationManager::update(DeviceOrientation* out_orientation) {
    if (!initialized) {
        return false;
    }

    uint32_t now = millis();
    if (now - last_sample_ms < SAMPLE_INTERVAL_MS) {
        return false;
    }
    last_sample_ms = now;

    // 1. Read raw acceleration from BMI270
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    if (!readRawAccel(rx, ry, rz)) {
        return false;
    }

    // 2. Exponential Moving Average (EMA) low-pass filtering
    applyEmaFilter(rx, ry, rz);

    // 3. Motion disturbance rejection
    // In normal static/quasi-static conditions, vector magnitude |a| ~ 1.0g.
    float norm = sqrtf(filt_x * filt_x + filt_y * filt_y + filt_z * filt_z);
    if (isDisturbance(norm)) {
        // Device is under significant dynamic linear acceleration or free-fall.
        // Discard measurement and reset candidate timer to avoid spurious switching.
        candidate_start_ms = now;
        return false;
    }

    // 4. Flat surface detection ("posé à plat")
    // When lying flat on a table, gravity is almost exclusively along Z.
    float in_plane = sqrtf(filt_x * filt_x + filt_y * filt_y);
    if (checkFlat(in_plane, filt_z)) {
        is_flat = true;
        // Retain last stable orientation without changes
        candidate_start_ms = now;
        candidate_orientation = current_orientation;
        return false;
    }
    is_flat = false;

    // 5. In-plane angle calculation [-180, +180] deg
    last_angle_deg = calculateAngle(filt_x, filt_y);

    // 6. Sector classification with hysteresis
    detected_orientation = classifyPhysicalOrientation(last_angle_deg);

    // 7. Debounce / minimum stability duration (350 ms)
    if (detected_orientation != current_orientation) {
        if (detected_orientation == candidate_orientation) {
            if (now - candidate_start_ms >= STABILITY_DELAY_MS) {
                // Orientation confirmed after 350 ms of continuous stability
                current_orientation = candidate_orientation;
                if (out_orientation) {
                    *out_orientation = current_orientation;
                }
                return true;
            }
        } else {
            // New candidate detected, start stability timer
            candidate_orientation = detected_orientation;
            candidate_start_ms = now;
        }
    } else {
        // Back to current orientation, reset candidate
        candidate_orientation = current_orientation;
        candidate_start_ms = now;
    }

    return false;
}


