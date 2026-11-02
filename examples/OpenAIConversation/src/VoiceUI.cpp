#include "VoiceUI.h"
#include "FrenchUI.h"

const char* uiStateToString(ConvUIState st) {
    switch (st) {
        case ConvUIState::INITIALIZING: return "INITIALIZING";
        case ConvUIState::READY:        return "READY";
        case ConvUIState::LISTENING:    return "LISTENING";
        case ConvUIState::PROCESSING:   return "PROCESSING";
        case ConvUIState::SPEAKING:     return "SPEAKING";
        case ConvUIState::STOPPED:      return "STOPPED";
        case ConvUIState::ERROR_STATE:  return "ERROR_STATE";
        default:                        return "UNKNOWN";
    }
}

VoiceUI::VoiceUI() = default;
VoiceUI::~VoiceUI() = default;

bool VoiceUI::begin(M5GFX* display) {
    _display = display;
    if (!_display) return false;

    // Rendu statique initial exécuté à froid pendant le setup()
    drawStaticBackground();
    _dirtyFull = false;

    _orb.begin(_display);
    forceFullRedraw();
    _dirtyFull = false; // Le fond statique vient d'être tracé
    return true;
}

void VoiceUI::setState(ConvUIState state) {
    if (_state != state) {
        _state = state;
        _dirtyHeader = true;
        _dirtyStateText = true;
        _dirtyBtn = true;

        // Lors d'une transition d'état, afficher immédiatement les textes en attente sans attendre le délai de cadence
        if (_pendingAsstUpdate) {
            _assistantText = _pendingAssistantText;
            _dirtyAsstCard = true;
            _pendingAsstUpdate = false;
            _lastAsstDrawMs = millis();
        }
        if (_pendingUserUpdate) {
            _userText = _pendingUserText;
            _dirtyUserCard = true;
            _pendingUserUpdate = false;
            _lastUserDrawMs = millis();
        }

        // Mise à jour synchrone de l'orbe
        switch (_state) {
            case ConvUIState::READY:
            case ConvUIState::STOPPED:
                _orb.setState(OrbState::READY);
                break;
            case ConvUIState::LISTENING:
                _orb.setState(OrbState::LISTENING);
                break;
            case ConvUIState::PROCESSING:
                _orb.setState(OrbState::PROCESSING);
                break;
            case ConvUIState::SPEAKING:
                _orb.setState(OrbState::SPEAKING);
                break;
            case ConvUIState::ERROR_STATE:
                _orb.setState(OrbState::ERROR_STATE);
                break;
            case ConvUIState::INITIALIZING:
            default:
                _orb.setState(OrbState::READY);
                break;
        }
    }
}

void VoiceUI::setUserText(const String& text) {
    if (_pendingUserText != text) {
        _pendingUserText = text;
        _pendingUserUpdate = true;
    }
}

void VoiceUI::setAssistantText(const String& text) {
    if (_pendingAssistantText != text) {
        _pendingAssistantText = text;
        _pendingAsstUpdate = true;
    }
}

void VoiceUI::setErrorMessage(const String& error) {
    if (_errorMessage != error) {
        _errorMessage = error;
        _dirtyAsstCard = true;
    }
}

void VoiceUI::setWifiConnected(bool connected) {
    if (_wifiConnected != connected) {
        _wifiConnected = connected;
        _dirtyHeader = true;
    }
}

void VoiceUI::forceFullRedraw() {
    _dirtyFull = true;
    _dirtyHeader = true;
    _dirtyStateText = true;
    _dirtyUserCard = true;
    _dirtyAsstCard = true;
    _dirtyBtn = true;
    _orb.forceRedraw();
}

bool VoiceUI::isPointInButton(int32_t x, int32_t y) const {
    return (x >= BTN_X && x <= (BTN_X + BTN_W) &&
            y >= BTN_Y && y <= (BTN_Y + BTN_H));
}

bool VoiceUI::isPointNearbyButton(int32_t x, int32_t y, int margin) const {
    return (x >= (BTN_X - margin) && x <= (BTN_X + BTN_W + margin) &&
            y >= (BTN_Y - margin) && y <= (BTN_Y + BTN_H + margin));
}

void VoiceUI::update(uint32_t nowMs, uint32_t audioLevel, bool throttle) {
    if (!_display) return;

    // Régulation de cadence des cartes textuelles pendant le streaming (~10 Hz max)
    // Garantit l'affichage immédiat dès que l'assistant a terminé (hors état SPEAKING)
    if (_pendingAsstUpdate && (nowMs - _lastAsstDrawMs >= TEXT_UPDATE_INTERVAL_MS || _state != ConvUIState::SPEAKING)) {
        _assistantText = _pendingAssistantText;
        _dirtyAsstCard = true;
        _pendingAsstUpdate = false;
        _lastAsstDrawMs = nowMs;
    }
    if (_pendingUserUpdate && (nowMs - _lastUserDrawMs >= TEXT_UPDATE_INTERVAL_MS || _state != ConvUIState::LISTENING)) {
        _userText = _pendingUserText;
        _dirtyUserCard = true;
        _pendingUserUpdate = false;
        _lastUserDrawMs = nowMs;
    }

    if (_dirtyFull) {
        _dirtyFull = false;
        drawStaticBackground();
    }

    if (_dirtyHeader) {
        _dirtyHeader = false;
        uint32_t t0 = micros();
        drawHeader();
        uint32_t dur = micros() - t0;
        if (dur > _maxHeaderUs) _maxHeaderUs = dur;
    }

    if (_dirtyStateText) {
        _dirtyStateText = false;
        uint32_t t0 = micros();
        drawStateText();
        uint32_t dur = micros() - t0;
        if (dur > _maxStateTextUs) _maxStateTextUs = dur;
    }

    if (_dirtyUserCard) {
        _dirtyUserCard = false;
        uint32_t t0 = micros();
        drawUserCard();
        uint32_t dur = micros() - t0;
        if (dur > _maxUserCardUs) _maxUserCardUs = dur;
    }

    if (_dirtyAsstCard) {
        _dirtyAsstCard = false;
        uint32_t t0 = micros();
        drawAssistantCard();
        uint32_t dur = micros() - t0;
        if (dur > _maxAsstCardUs) _maxAsstCardUs = dur;
    }

    if (_dirtyBtn) {
        _dirtyBtn = false;
        uint32_t t0 = micros();
        drawMainButton();
        uint32_t dur = micros() - t0;
        if (dur > _maxMainBtnUs) _maxMainBtnUs = dur;
    }

    // Animation non-bloquante de l'orbe vocal (Y = 35..108)
    uint32_t tOrb = micros();
    _orb.update(nowMs, audioLevel, throttle);
    uint32_t durOrb = micros() - tOrb;
    if (durOrb > _maxOrbUs) _maxOrbUs = durOrb;
}

void VoiceUI::drawStaticBackground() {
    _display->fillScreen(COLOR_BG);
    _display->drawFastHLine(10, HEADER_H, SCREEN_W - 20, 0x1166);
}

void VoiceUI::drawHeader() {
    // Effacement de la zone d'en-tête
    _display->fillRect(0, 0, SCREEN_W, HEADER_H, COLOR_BG);
    _display->drawFastHLine(10, HEADER_H, SCREEN_W - 20, 0x1166);

    // Titre à gauche
    _display->setTextSize(1);
    _display->setTextColor(COLOR_TEXT_PRI, COLOR_BG);
    _display->setTextDatum(TL_DATUM);
    _display->drawString("OpenAI Conversation", 12, 10);

    // Indicateur Wi-Fi à droite (X = 230, Y = 10)
    int wifiX = 230;
    int wifiY = 19;
    if (_wifiConnected) {
        // Point et arcs Wi-Fi verts
        _display->fillCircle(wifiX, wifiY, 1, 0x07E0);
        _display->drawCircle(wifiX, wifiY, 4, 0x07E0);
        _display->drawCircle(wifiX, wifiY, 8, 0x07E0);
    } else {
        // Point rouge et barre de déconnexion
        _display->fillCircle(wifiX, wifiY, 2, 0xF800);
        _display->drawLine(wifiX - 4, wifiY - 6, wifiX + 4, wifiY + 2, 0xF800);
    }

    // Badge d'état court à droite (X = 248, Y = 6, W = 62, H = 18)
    int badgeX = 248;
    int badgeY = 6;
    int badgeW = 62;
    int badgeH = 18;

    const char* badgeText = "ATTENTE";
    uint16_t badgeBg = 0x2124;
    uint16_t badgeBorder = 0x52AA;
    uint16_t badgeFg = 0xAD55;

    switch (_state) {
        case ConvUIState::READY:
            badgeText = "PRÊT";
            badgeBg = 0x0276;
            badgeBorder = 0x05BF;
            badgeFg = 0x07FF;
            break;
        case ConvUIState::LISTENING:
            badgeText = "ÉCOUTE";
            badgeBg = 0x6180;
            badgeBorder = 0xFA60;
            badgeFg = 0xFD20;
            break;
        case ConvUIState::PROCESSING:
            badgeText = "ANALYSE";
            badgeBg = 0x38B0;
            badgeBorder = 0x91DC;
            badgeFg = 0xDA7F;
            break;
        case ConvUIState::SPEAKING:
            badgeText = "RÉPONSE";
            badgeBg = 0x0312;
            badgeBorder = 0x077A;
            badgeFg = 0x5FFF;
            break;
        case ConvUIState::STOPPED:
            badgeText = "ARRÊT";
            badgeBg = 0x4200;
            badgeBorder = 0xFD20;
            badgeFg = 0xFFE0;
            break;
        case ConvUIState::ERROR_STATE:
            badgeText = "ERREUR";
            badgeBg = 0x6000;
            badgeBorder = 0xF800;
            badgeFg = 0xF9A6;
            break;
        case ConvUIState::INITIALIZING:
        default:
            badgeText = "ATTENTE";
            badgeBg = 0x2124;
            badgeBorder = 0x52AA;
            badgeFg = 0xAD55;
            break;
    }

    _display->fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, badgeBg);
    _display->drawRoundRect(badgeX, badgeY, badgeW, badgeH, 4, badgeBorder);

    FrenchUI::drawCenteredFrenchLine(*_display, badgeX + (badgeW / 2), badgeY + 5, badgeText, badgeFg, badgeBg);
}

void VoiceUI::drawStateText() {
    _display->fillRect(10, 110, SCREEN_W - 20, 24, COLOR_BG);

    const char* stateMsg = "Connexion en cours…";
    switch (_state) {
        case ConvUIState::INITIALIZING:
            stateMsg = "Connexion en cours…";
            break;
        case ConvUIState::READY:
            stateMsg = "Appuyez pour parler";
            break;
        case ConvUIState::LISTENING:
            stateMsg = "Je vous écoute…";
            break;
        case ConvUIState::PROCESSING:
            stateMsg = "Je réfléchis…";
            break;
        case ConvUIState::SPEAKING:
            stateMsg = "Je vous réponds";
            break;
        case ConvUIState::STOPPED:
            stateMsg = "Conversation interrompue";
            break;
        case ConvUIState::ERROR_STATE:
            stateMsg = "Connexion impossible";
            break;
    }

    FrenchUI::drawCenteredFrenchLine(*_display, SCREEN_W / 2, STATE_TEXT_Y, stateMsg, COLOR_TEXT_SEC, COLOR_BG);
}

void VoiceUI::drawUserCard() {
    _display->fillRoundRect(USER_CARD_X, USER_CARD_Y, USER_CARD_W, USER_CARD_H, 4, COLOR_CARD_BG);
    _display->drawRoundRect(USER_CARD_X, USER_CARD_Y, USER_CARD_W, USER_CARD_H, 4, COLOR_CARD_BORDER);

    // Pill label "Vous"
    int pillX = USER_CARD_X + 4;
    int pillY = USER_CARD_Y + 5;
    int pillW = 38;
    int pillH = 18;
    _display->fillRoundRect(pillX, pillY, pillW, pillH, 3, 0x1149);
    _display->drawRoundRect(pillX, pillY, pillW, pillH, 3, 0x228E);
    FrenchUI::drawCenteredFrenchLine(*_display, pillX + (pillW / 2), pillY + 5, "Vous", 0x7DFB, 0x1149);

    // Texte utilisateur sur la droite
    int textX = pillX + pillW + 8;
    int maxTextW = (USER_CARD_X + USER_CARD_W) - textX - 6;

    if (_userText.isEmpty()) {
        FrenchUI::drawFrenchLine(*_display, textX, USER_CARD_Y + 10, "(Votre question...)", COLOR_TEXT_MUTED, COLOR_CARD_BG);
    } else {
        std::vector<String> lines = FrenchUI::wrapUtf8WordsByPixels(*_display, _userText, maxTextW, 2);
        if (lines.size() == 1) {
            FrenchUI::drawFrenchLine(*_display, textX, USER_CARD_Y + 10, lines[0].c_str(), COLOR_TEXT_PRI, COLOR_CARD_BG);
        } else if (lines.size() >= 2) {
            FrenchUI::drawFrenchLine(*_display, textX, USER_CARD_Y + 5, lines[0].c_str(), COLOR_TEXT_PRI, COLOR_CARD_BG);
            FrenchUI::drawFrenchLine(*_display, textX, USER_CARD_Y + 15, lines[1].c_str(), COLOR_TEXT_PRI, COLOR_CARD_BG);
        }
    }
}

void VoiceUI::drawAssistantCard() {
    _display->fillRoundRect(ASST_CARD_X, ASST_CARD_Y, ASST_CARD_W, ASST_CARD_H, 4, COLOR_CARD_BG);
    _display->drawRoundRect(ASST_CARD_X, ASST_CARD_Y, ASST_CARD_W, ASST_CARD_H, 4, COLOR_CARD_BORDER);

    // Pill label "Assistant"
    int pillX = ASST_CARD_X + 4;
    int pillY = ASST_CARD_Y + 5;
    int pillW = 58;
    int pillH = 18;
    _display->fillRoundRect(pillX, pillY, pillW, pillH, 3, 0x094A);
    _display->drawRoundRect(pillX, pillY, pillW, pillH, 3, 0x1334);
    FrenchUI::drawCenteredFrenchLine(*_display, pillX + (pillW / 2), pillY + 5, "Assistant", 0x2DD4, 0x094A);

    // Texte assistant sur la droite
    int textX = pillX + pillW + 8;
    int maxTextW = (ASST_CARD_X + ASST_CARD_W) - textX - 6;

    String content = _assistantText;
    uint16_t textColor = 0x5FFF; // Turquoise doux lisible

    if (_state == ConvUIState::ERROR_STATE && !_errorMessage.isEmpty()) {
        content = "Erreur : " + _errorMessage;
        textColor = 0xFAEA; // Rouge doux
    }

    if (content.isEmpty()) {
        FrenchUI::drawFrenchLine(*_display, textX, ASST_CARD_Y + 10, "(En attente de dialogue...)", COLOR_TEXT_MUTED, COLOR_CARD_BG);
    } else {
        std::vector<String> lines = FrenchUI::wrapUtf8WordsByPixels(*_display, content, maxTextW, 2);
        if (lines.size() == 1) {
            FrenchUI::drawFrenchLine(*_display, textX, ASST_CARD_Y + 10, lines[0].c_str(), textColor, COLOR_CARD_BG);
        } else if (lines.size() >= 2) {
            FrenchUI::drawFrenchLine(*_display, textX, ASST_CARD_Y + 5, lines[0].c_str(), textColor, COLOR_CARD_BG);
            FrenchUI::drawFrenchLine(*_display, textX, ASST_CARD_Y + 15, lines[1].c_str(), textColor, COLOR_CARD_BG);
        }
    }
}

void VoiceUI::drawMainButton() {
    uint16_t btnBg = 0x1B3F;
    uint16_t btnBorder = 0x451F;
    uint16_t textFg = TFT_WHITE;
    const char* btnTitle = "PARLER";
    bool drawMicIcon = false;
    bool drawStopIcon = false;

    switch (_state) {
        case ConvUIState::READY:
            btnBg = 0x1B3F;      // Bleu royal lumineux
            btnBorder = 0x5DFB;
            btnTitle = "PARLER";
            drawMicIcon = true;
            break;
        case ConvUIState::LISTENING:
            btnBg = 0xFA60;      // Corail/orange vif
            btnBorder = 0xFD20;
            btnTitle = "RELÂCHER";
            break;
        case ConvUIState::PROCESSING:
        case ConvUIState::SPEAKING:
            btnBg = 0xC800;      // Rouge vif prioritaire
            btnBorder = 0xF9A6;
            btnTitle = "STOP";
            drawStopIcon = true;
            break;
        case ConvUIState::STOPPED:
            btnBg = 0x1B3F;
            btnBorder = 0x5DFB;
            btnTitle = "PARLER";
            drawMicIcon = true;
            break;
        case ConvUIState::ERROR_STATE:
            btnBg = 0x8800;      // Rouge sombre
            btnBorder = 0xBA64;
            btnTitle = "RÉESSAYER";
            break;
        case ConvUIState::INITIALIZING:
        default:
            btnBg = 0x2965;      // Gris neutre désactivé
            btnBorder = 0x4208;
            btnTitle = "CONNEXION";
            textFg = 0xAD55;
            break;
    }

    _display->fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, btnBg);
    _display->drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 8, btnBorder);

    // Dessin de l'icône microphone pour PARLER
    if (drawMicIcon) {
        int micX = BTN_X + 92;
        int micY = BTN_Y + 11;
        _display->drawRoundRect(micX, micY, 6, 9, 3, TFT_WHITE);
        _display->drawFastVLine(micX - 2, micY + 3, 4, TFT_WHITE);
        _display->drawFastVLine(micX + 7, micY + 3, 4, TFT_WHITE);
        _display->drawFastHLine(micX - 1, micY + 7, 8, TFT_WHITE);
        _display->drawFastVLine(micX + 2, micY + 8, 4, TFT_WHITE);
        _display->drawFastHLine(micX, micY + 12, 6, TFT_WHITE);
    }

    // Dessin de l'icône carré STOP
    if (drawStopIcon) {
        int sqX = BTN_X + 104;
        int sqY = BTN_Y + 13;
        _display->fillRect(sqX, sqY, 9, 9, TFT_WHITE);
    }

    _display->setTextSize(2);
    _display->setTextColor(textFg, btnBg);
    _display->setTextDatum(MC_DATUM);

    // Décalage léger du texte si une icône est présente
    int textCenterX = BTN_X + (BTN_W / 2);
    if (drawMicIcon) {
        textCenterX += 10;
    } else if (drawStopIcon) {
        textCenterX += 10;
    }

    _display->drawString(btnTitle, textCenterX, BTN_Y + (BTN_H / 2));
    _display->setTextDatum(TL_DATUM);
}

