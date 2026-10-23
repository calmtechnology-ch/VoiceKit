#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include "DeviceOrientation.h"

/**
 * @class OrientationManager
 * @brief Handles IMU reading (BMI270), digital filtering, motion disturbance rejection,
 *        flat surface detection, angle calculation with hysteresis, and stability debouncing.
 * 
 * Pipeline architecture:
 * 1. Raw IMU Acquisition (~20 Hz)
 * 2. Low-Pass Exponential Moving Average (EMA) Filtering
 * 3. Motion Disturbance & Free-fall Rejection
 * 4. Flat Surface (Tabletop) Detection & Angle Hold
 * 5. In-plane Angle Calculation (atan2)
 * 6. Sector Classification with Angular Hysteresis (12°)
 * 7. Temporal Debounce / Stability Confirmation (350 ms)
 */
class OrientationManager {
public:
    OrientationManager();

    /**
     * @brief Initialize IMU, detect immediate orientation at boot, and seed filter.
     * @return true if IMU was successfully initialized.
     */
    bool init();

    /**
     * @brief Non-blocking periodic update (~20 Hz).
     * @param out_orientation Pointer to store the new orientation if changed (nullable).
     * @return true if the stable orientation has changed, false otherwise.
     */
    bool update(DeviceOrientation* out_orientation = nullptr);

    /**
     * @brief Get the current confirmed stable orientation.
     */
    DeviceOrientation getOrientation() const { return current_orientation; }

    /**
     * @brief Get the instantaneously detected / candidate orientation.
     */
    DeviceOrientation getDetectedOrientation() const { return detected_orientation; }

    /**
     * @brief Get the latest calculated angle in degrees [-180, +180].
     */
    float getLastAngle() const { return last_angle_deg; }

    /**
     * @brief Check whether device is currently lying flat.
     */
    bool isFlat() const { return is_flat; }

    /**
     * @brief Check whether IMU initialized successfully.
     */
    bool isInitialized() const { return initialized; }

    /**
     * @brief Get last filtered accelerometer values (in g).
     */
    void getFilteredAccel(float& ax, float& ay, float& az) const {
        ax = filt_x;
        ay = filt_y;
        az = filt_z;
    }

    // Static classification helpers (exposed for deterministic testing)
    static float angleDiff(float a, float b);
    static DeviceOrientation classifySectorNearest(float angle_deg);
    DeviceOrientation classifyPhysicalOrientation(float angle_deg) const;

private:
    // Pipeline constants
    static constexpr uint32_t SAMPLE_INTERVAL_MS = 50;    // ~20 Hz
    static constexpr uint32_t STABILITY_DELAY_MS = 350;   // 350 ms debounce stability
    static constexpr float FILTER_ALPHA          = 0.25f; // EMA filter coefficient
    static constexpr float HYSTERESIS_DEG        = 12.0f; // Angular hysteresis deadband
    static constexpr float DISTURBANCE_MIN_G     = 0.65f; // Reject free-fall / violent motion
    static constexpr float DISTURBANCE_MAX_G     = 1.35f; // Reject violent shocks
    static constexpr float FLAT_MAX_IN_PLANE_G   = 0.35f; // Max horizontal accel when flat
    static constexpr float FLAT_MIN_Z_G          = 0.85f; // Min vertical Z accel when flat

    // Explicit pipeline stage methods
    bool readRawAccel(float& rx, float& ry, float& rz);
    void applyEmaFilter(float rx, float ry, float rz);
    bool isDisturbance(float norm) const;
    bool checkFlat(float in_plane, float az) const;
    float calculateAngle(float ax, float ay) const;

    uint32_t last_sample_ms;
    uint32_t candidate_start_ms;

    DeviceOrientation current_orientation;
    DeviceOrientation candidate_orientation;
    DeviceOrientation detected_orientation;

    float filt_x;
    float filt_y;
    float filt_z;
    float last_angle_deg;

    bool initialized;
    bool is_flat;
};


