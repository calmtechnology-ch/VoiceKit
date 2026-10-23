#pragma once

#include <stdint.h>
#include <math.h>

/**
 * @brief 4 main physical orientations for CoreS3
 */
enum class DeviceOrientation : uint8_t {
    LANDSCAPE_NORMAL   = 0,  ///< 0°   - Paysage normal (upright, camera at top)
    PORTRAIT_RIGHT     = 1,  ///< 90°  - Portrait à droite (rotated 90° CW, camera at right)
    PORTRAIT           = 1,  ///< Backward-compatible alias for PORTRAIT_RIGHT
    LANDSCAPE_INVERTED = 2,  ///< 180° - Paysage inversé (upside down, camera at bottom)
    PORTRAIT_LEFT      = 3,  ///< 270° - Portrait à gauche (rotated 270° CW, camera at left)
    PORTRAIT_INVERTED  = 3   ///< Backward-compatible alias for PORTRAIT_LEFT
};

/**
 * @struct OrientationMapping
 * @brief Centralized correspondence entry mapping IMU acceleration angle,
 *        physical orientation, and M5GFX hardware display rotation.
 * 
 * Standard IMU coordinate frame on CoreS3 under Earth gravity:
 * - Upright (Landscape Normal): reaction force pushes upward towards camera (+Y ~ +1.0g). Angle = +90°. M5GFX rot 1 (320x240).
 * - Rotated 90° CW (Portrait Right): camera is right, reaction force pushes toward left side (-X ~ -1.0g). Angle = 180°. M5GFX rot 0 (240x320).
 * - Upside down (Landscape Inverted): camera is bottom, reaction force pushes toward base (-Y ~ -1.0g). Angle = -90°. M5GFX rot 3 (320x240).
 * - Rotated 270° CW (Portrait Left): camera is left, reaction force pushes toward right side (+X ~ +1.0g). Angle = 0°. M5GFX rot 2 (240x320).
 */
struct OrientationMapping {
    DeviceOrientation orientation;
    const char* name;
    float center_angle_deg;  ///< Expected angle atan2(ay, ax) in degrees
    uint8_t m5_rotation;     ///< M5GFX hardware rotation (0..3)
};

/**
 * @brief Unique, centralized correspondence table.
 *        Easily configurable if physical IMU mounting or preferences differ.
 */
constexpr OrientationMapping ORIENTATION_TABLE[4] = {
    { DeviceOrientation::LANDSCAPE_NORMAL,   "PAYSAGE_NORMAL",    90.0f, 1 },
    { DeviceOrientation::PORTRAIT_RIGHT,     "PORTRAIT_DROITE",  180.0f, 0 },
    { DeviceOrientation::LANDSCAPE_INVERTED, "PAYSAGE_INVERSE",  -90.0f, 3 },
    { DeviceOrientation::PORTRAIT_LEFT,      "PORTRAIT_GAUCHE",    0.0f, 2 }
};

/**
 * @brief Retrieve configuration entry for a given orientation.
 */
inline const OrientationMapping& getOrientationMapping(DeviceOrientation orient) {
    for (const auto& entry : ORIENTATION_TABLE) {
        if (entry.orientation == orient) {
            return entry;
        }
    }
    return ORIENTATION_TABLE[0];
}

/**
 * @brief Helper to get human-readable name of orientation
 */
inline const char* orientationToString(DeviceOrientation orientation) {
    return getOrientationMapping(orientation).name;
}

/**
 * @brief Get corresponding M5GFX hardware rotation index (0..3)
 */
inline uint8_t getM5Rotation(DeviceOrientation orientation) {
    return getOrientationMapping(orientation).m5_rotation;
}

/**
 * @brief Get sector center angle in degrees for gravity vector atan2(ay, ax)
 */
inline float getSectorCenterAngle(DeviceOrientation orientation) {
    return getOrientationMapping(orientation).center_angle_deg;
}

