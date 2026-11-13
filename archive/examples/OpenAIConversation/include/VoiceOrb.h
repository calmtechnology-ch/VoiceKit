#pragma once

#include <Arduino.h>
#include <M5GFX.h>

/**
 * @file VoiceOrb.h
 * @brief Orbe vocal lumineux anime M5GFX pour VoiceKit v0.6.3.2 sur M5Stack CoreS3.
 *
 * Caracteristiques :
 * - Diametre cible ~65 px (Rayon ~32 px) au centre de l'ecran (160, 71).
 * - Bounding box optimisee 74x74 px (Y = 35..108).
 * - Rendu par double-buffering M5Canvas (zero scintillement, pushSprite ~1 ms).
 * - 5 comportements distincts :
 *   1. PRET (Cyan, respiration douce et lente)
 *   2. ECOUTE (Corail/Orange, pulsation vive + reaction audio RMS authentique)
 *   3. ANALYSE (Violet/Pourpre, rotation orbitale)
 *   4. REPONSE (Turquoise, ondes concentriques en expansion)
 *   5. ERREUR (Rouge, orbe fixe avec symbole d'alerte sans surcout CPU)
 * - Cadence cible 18 fps (intervalle ~55 ms), zero delay(), planification non-bloquante.
 * - Degadation automatique en cas de charge sur voice.update().
 */
enum class OrbState : uint8_t {
    READY,
    LISTENING,
    PROCESSING,
    SPEAKING,
    ERROR_STATE
};

class VoiceOrb {
public:
    VoiceOrb();
    ~VoiceOrb();

    bool begin(M5GFX* display);
    void setState(OrbState state);
    OrbState getState() const { return _state; }

    void forceRedraw() { _forceRedraw = true; }
    void update(uint32_t nowMs, uint32_t audioLevel = 0, bool throttle = false);

    // Coordonnées de l'orbe sur l'écran CoreS3 (320x240)
    static constexpr int ORB_X = 123;
    static constexpr int ORB_Y = 35;
    static constexpr int ORB_W = 74;
    static constexpr int ORB_H = 74;
    static constexpr int ORB_CX = 37;
    static constexpr int ORB_CY = 37;

    // Constante de couleur de fond (Dark mode premium)
    static constexpr uint16_t COLOR_BG = 0x0842; // Deep midnight navy

private:
    M5GFX* _display = nullptr;
    M5Canvas* _canvas = nullptr;
    OrbState _state = OrbState::READY;
    OrbState _prevState = OrbState::READY;

    uint32_t _lastFrameMs = 0;
    uint32_t _smoothedRms = 0;
    bool _forceRedraw = true;
    bool _hasCanvas = false;

    void renderFrame(uint32_t nowMs, uint32_t audioLevel);
    void drawReadyOrb(uint32_t nowMs);
    void drawListeningOrb(uint32_t nowMs, uint32_t audioLevel);
    void drawProcessingOrb(uint32_t nowMs);
    void drawSpeakingOrb(uint32_t nowMs);
    void drawErrorOrb();
};

