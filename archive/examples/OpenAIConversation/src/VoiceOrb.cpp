#include "VoiceOrb.h"
#include <cmath>

VoiceOrb::VoiceOrb() = default;

VoiceOrb::~VoiceOrb() {
    if (_canvas) {
        _canvas->deleteSprite();
        delete _canvas;
        _canvas = nullptr;
    }
}

bool VoiceOrb::begin(M5GFX* display) {
    _display = display;
    if (!_display) return false;

    // Creation du sprite M5Canvas pour double-buffering haute performance
    _canvas = new M5Canvas(_display);
    _canvas->setColorDepth(16);
    if (_canvas->createSprite(ORB_W, ORB_H) != nullptr) {
        _hasCanvas = true;
        _canvas->fillScreen(COLOR_BG);
    } else {
        _hasCanvas = false;
        delete _canvas;
        _canvas = nullptr;
    }

    _forceRedraw = true;
    _lastFrameMs = 0;
    return true;
}

void VoiceOrb::setState(OrbState state) {
    if (_state != state) {
        _prevState = _state;
        _state = state;
        _forceRedraw = true;
    }
}

void VoiceOrb::update(uint32_t nowMs, uint32_t audioLevel, bool throttle) {
    if (!_display) return;

    // En etat ERREUR, l'orbe est fixe : un seul rendu necessaire
    if (_state == OrbState::ERROR_STATE && !_forceRedraw) {
        return;
    }

    // Gestion de cadence : 18 fps (~55 ms) en nominal, auto-degradation a ~9 fps (~110 ms) si throttled
    uint32_t intervalMs = throttle ? 110 : 55;
    if (!_forceRedraw && (nowMs - _lastFrameMs < intervalMs)) {
        return;
    }

    _lastFrameMs = nowMs;
    _forceRedraw = false;

    renderFrame(nowMs, audioLevel);
}

void VoiceOrb::renderFrame(uint32_t nowMs, uint32_t audioLevel) {
    LovyanGFX* target = _hasCanvas ? static_cast<LovyanGFX*>(_canvas) : static_cast<LovyanGFX*>(_display);
    if (!target) return;

    if (_hasCanvas) {
        _canvas->fillScreen(COLOR_BG);
    } else {
        _display->fillRect(ORB_X, ORB_Y, ORB_W, ORB_H, COLOR_BG);
    }

    switch (_state) {
        case OrbState::READY:
            drawReadyOrb(nowMs);
            break;
        case OrbState::LISTENING:
            drawListeningOrb(nowMs, audioLevel);
            break;
        case OrbState::PROCESSING:
            drawProcessingOrb(nowMs);
            break;
        case OrbState::SPEAKING:
            drawSpeakingOrb(nowMs);
            break;
        case OrbState::ERROR_STATE:
            drawErrorOrb();
            break;
    }

    if (_hasCanvas) {
        _canvas->pushSprite(ORB_X, ORB_Y);
    }
}

void VoiceOrb::drawReadyOrb(uint32_t nowMs) {
    LovyanGFX* target = _hasCanvas ? static_cast<LovyanGFX*>(_canvas) : static_cast<LovyanGFX*>(_display);
    int cx = _hasCanvas ? ORB_CX : (ORB_X + ORB_CX);
    int cy = _hasCanvas ? ORB_CY : (ORB_Y + ORB_CY);

    // Respiration calme et lente (periode ~3.6 s)
    float phase = (float)(nowMs % 3600) / 3600.0f * 2.0f * 3.14159265f;
    float pulse = 0.5f + 0.5f * sinf(phase); // [0.0 .. 1.0]

    int rCore = 22 + (int)(pulse * 2.0f);
    int rMid  = rCore + 4;
    int rHalo = rMid + 4;

    // Halo discret externe (cyan tres sombre)
    target->drawCircle(cx, cy, rHalo, 0x0215);
    target->drawCircle(cx, cy, rHalo - 1, 0x0276);

    // Cercle median (cyan moyen)
    target->drawCircle(cx, cy, rMid, 0x041F);

    // Corps spherique principal (degrade cyan profond)
    target->fillCircle(cx, cy, rCore, 0x0359);
    target->fillCircle(cx, cy, rCore - 3, 0x047D);
    target->drawCircle(cx, cy, rCore, 0x067E);

    // Point speculaire 3D
    target->fillCircle(cx - 5, cy - 5, 4, 0x5FFF);
    target->fillCircle(cx - 6, cy - 6, 2, 0xFFFF);
}

void VoiceOrb::drawListeningOrb(uint32_t nowMs, uint32_t audioLevel) {
    LovyanGFX* target = _hasCanvas ? static_cast<LovyanGFX*>(_canvas) : static_cast<LovyanGFX*>(_display);
    int cx = _hasCanvas ? ORB_CX : (ORB_X + ORB_CX);
    int cy = _hasCanvas ? ORB_CY : (ORB_Y + ORB_CY);

    // Lissage exponentiel du niveau audio RMS reel issu de VoxCoreVoice::getAudioLevel()
    if (audioLevel > 0) {
        _smoothedRms = (_smoothedRms * 3 + audioLevel) / 4;
    } else {
        _smoothedRms = (_smoothedRms * 3) / 4;
    }

    // Modulation proportionnelle au son reel (sans aucune simulation inventee)
    float rmsBoost = 0.0f;
    if (_smoothedRms > 150) {
        rmsBoost = (float)(_smoothedRms - 150) / 900.0f;
        if (rmsBoost > 6.0f) rmsBoost = 6.0f;
    }

    // Pulsation vive corail/orange (periode ~850 ms)
    float phase = (float)(nowMs % 850) / 850.0f * 2.0f * 3.14159265f;
    float pulse = 0.5f + 0.5f * sinf(phase);

    int rCore = 22 + (int)(pulse * 3.0f + rmsBoost);
    if (rCore > 32) rCore = 32;
    int rHalo = rCore + 4 + (int)(rmsBoost * 0.5f);
    if (rHalo > 36) rHalo = 36;

    // Halo dynamique reactif au son (teinte corail douce)
    target->drawCircle(cx, cy, rHalo, 0x6280);
    target->drawCircle(cx, cy, rHalo - 1, 0x9340);

    // Anneau de transition (orange chaud)
    target->drawCircle(cx, cy, rCore + 2, 0xCA60);

    // Coeur lumineux corail/orange
    target->fillCircle(cx, cy, rCore, 0xFA60);
    target->fillCircle(cx, cy, rCore - 3, 0xFD20);
    target->drawCircle(cx, cy, rCore, 0xFE00);

    // Eclat central chaud
    target->fillCircle(cx - 5, cy - 5, 4, 0xFFE0);
    target->fillCircle(cx - 6, cy - 6, 2, 0xFFFF);
}

void VoiceOrb::drawProcessingOrb(uint32_t nowMs) {
    LovyanGFX* target = _hasCanvas ? static_cast<LovyanGFX*>(_canvas) : static_cast<LovyanGFX*>(_display);
    int cx = _hasCanvas ? ORB_CX : (ORB_X + ORB_CX);
    int cy = _hasCanvas ? ORB_CY : (ORB_Y + ORB_CY);

    // Pulsation douce violette (periode ~1.5 s)
    float pulsePhase = (float)(nowMs % 1500) / 1500.0f * 2.0f * 3.14159265f;
    float pulse = 0.5f + 0.5f * sinf(pulsePhase);
    int rCore = 22 + (int)(pulse * 2.0f);

    // Halo violet profond
    target->drawCircle(cx, cy, rCore + 4, 0x410C);
    target->drawCircle(cx, cy, rCore + 2, 0x6954);

    // Coeur violet/pourpre
    target->fillCircle(cx, cy, rCore, 0x6156);
    target->fillCircle(cx, cy, rCore - 3, 0x81FA);
    target->drawCircle(cx, cy, rCore, 0xB2DF);

    // Eclat lilas
    target->fillCircle(cx - 5, cy - 5, 4, 0xC3FF);
    target->fillCircle(cx - 6, cy - 6, 2, 0xFFFF);

    // 3 particules orbitales lumineuses en rotation continue (periode ~2.2 s)
    float rotAngle = (float)(nowMs % 2200) / 2200.0f * 2.0f * 3.14159265f;
    float orbitRadius = 31.0f;

    for (int k = 0; k < 3; ++k) {
        float a = rotAngle + (float)k * (2.0f * 3.14159265f / 3.0f);
        int dotX = cx + (int)(cosf(a) * orbitRadius);
        int dotY = cy + (int)(sinf(a) * orbitRadius);
        target->fillCircle(dotX, dotY, 2, 0xDA7F);
    }
}

void VoiceOrb::drawSpeakingOrb(uint32_t nowMs) {
    LovyanGFX* target = _hasCanvas ? static_cast<LovyanGFX*>(_canvas) : static_cast<LovyanGFX*>(_display);
    int cx = _hasCanvas ? ORB_CX : (ORB_X + ORB_CX);
    int cy = _hasCanvas ? ORB_CY : (ORB_Y + ORB_CY);

    int rCore = 21;

    // Ondes sonores concentriques en expansion circulaire (periode ~1.2 s)
    float cycle = (float)(nowMs % 1200) / 1200.0f;
    float r1 = 22.0f + cycle * 13.0f;
    float r2 = 22.0f + fmodf(cycle + 0.5f, 1.0f) * 13.0f;

    // Onde externe 1
    if (r1 <= 35.0f) {
        uint16_t waveColor1 = (r1 > 30.0f) ? 0x02D3 : 0x0516;
        target->drawCircle(cx, cy, (int)r1, waveColor1);
    }

    // Onde externe 2
    if (r2 <= 35.0f) {
        uint16_t waveColor2 = (r2 > 30.0f) ? 0x02D3 : 0x0516;
        target->drawCircle(cx, cy, (int)r2, waveColor2);
    }

    // Bordure active turquoise
    target->drawCircle(cx, cy, rCore + 1, 0x077A);

    // Coeur turquoise lumineux
    target->fillCircle(cx, cy, rCore, 0x0538);
    target->fillCircle(cx, cy, rCore - 3, 0x067B);
    target->drawCircle(cx, cy, rCore, 0x2EDB);

    // Eclat brillant
    target->fillCircle(cx - 5, cy - 5, 4, 0x7FFF);
    target->fillCircle(cx - 6, cy - 6, 2, 0xFFFF);
}

void VoiceOrb::drawErrorOrb() {
    LovyanGFX* target = _hasCanvas ? static_cast<LovyanGFX*>(_canvas) : static_cast<LovyanGFX*>(_display);
    int cx = _hasCanvas ? ORB_CX : (ORB_X + ORB_CX);
    int cy = _hasCanvas ? ORB_CY : (ORB_Y + ORB_CY);

    int rCore = 24;

    // Anneau d'avertissement rouge sombre
    target->drawCircle(cx, cy, rCore + 4, 0x5000);
    target->drawCircle(cx, cy, rCore + 2, 0x8800);

    // Corps rouge d'alerte
    target->fillCircle(cx, cy, rCore, 0xC800);
    target->drawCircle(cx, cy, rCore, 0xF9A6);

    // Symbole d'avertissement point d'exclamation '!' au centre en blanc pur
    target->fillRect(cx - 1, cy - 10, 3, 11, TFT_WHITE);
    target->fillCircle(cx, cy + 6, 2, TFT_WHITE);
}

