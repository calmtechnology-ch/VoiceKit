#pragma once

#include <Arduino.h>
#include <M5GFX.h>
#include "VoiceOrb.h"

/**
 * @file VoiceUI.h
 * @brief Gestionnaire de composition de l'interface moderne OpenAIConversation v0.6.3.2.1.
 *
 * Responsabilités :
 * - Composition globale Dark mode premium (320x240 M5GFX sans LVGL).
 * - En-tête compact avec titre, indicateur Wi-Fi et badge d'état court.
 * - Intégration de l'orbe vocal VoiceOrb (Y = 35..108).
 * - Texte principal d'état centré en français (Y = 110..132).
 * - Deux cartes de conversation arrondies (Vous et Assistant) avec word-wrapping
 *   en pixels réels et rendu UTF-8 français irréprochable (Y = 136..196).
 * - Grand bouton tactile principal très visible avec icônes et rétroaction (Y = 200..235).
 * - Rafraîchissement ciblé par drapeaux sales (dirty flags) sans scintillement.
 * - Régulation de cadence d'affichage textuel (5-10 Hz) et instrumentation séparée (v0.6.3.2.1).
 */

enum class ConvUIState : uint8_t {
    INITIALIZING, ///< En attente Wi-Fi / session OpenAI Realtime
    READY,        ///< Prêt à dialoguer (Push-to-Talk)
    LISTENING,    ///< Enregistrement microphone actif (bouton maintenu)
    PROCESSING,   ///< Traitement serveur / réflexion OpenAI
    SPEAKING,     ///< Restitution audio en cours sur le haut-parleur
    STOPPED,      ///< Tour ou session interrompu par STOP
    ERROR_STATE   ///< Erreur de transport, audio ou réseau
};

const char* uiStateToString(ConvUIState st);

class VoiceUI {
public:
    VoiceUI();
    ~VoiceUI();

    bool begin(M5GFX* display);
    void setState(ConvUIState state);
    ConvUIState getState() const { return _state; }

    void setUserText(const String& text);
    void setAssistantText(const String& text);
    void setErrorMessage(const String& error);
    void setWifiConnected(bool connected);

    void update(uint32_t nowMs, uint32_t audioLevel = 0, bool throttle = false);
    void forceFullRedraw();

    // Hit-testing tactile pour le bouton principal
    bool isPointInButton(int32_t x, int32_t y) const;
    bool isPointNearbyButton(int32_t x, int32_t y, int margin = 15) const;

    // Métriques d'instrumentation graphique séparées (v0.6.3.2.1 — Correction C)
    uint32_t getOrbMaxUs() const { return _maxOrbUs; }
    uint32_t getHeaderMaxUs() const { return _maxHeaderUs; }
    uint32_t getStateTextMaxUs() const { return _maxStateTextUs; }
    uint32_t getUserCardMaxUs() const { return _maxUserCardUs; }
    uint32_t getAsstCardMaxUs() const { return _maxAsstCardUs; }
    uint32_t getMainBtnMaxUs() const { return _maxMainBtnUs; }
    void resetPerformanceMetrics() {
        _maxOrbUs = 0;
        _maxHeaderUs = 0;
        _maxStateTextUs = 0;
        _maxUserCardUs = 0;
        _maxAsstCardUs = 0;
        _maxMainBtnUs = 0;
    }

    // Dimensions et géométrie écran CoreS3 (320x240)
    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 240;

    static constexpr int HEADER_H = 30;

    static constexpr int STATE_TEXT_Y = 117;

    static constexpr int USER_CARD_X = 10;
    static constexpr int USER_CARD_Y = 136;
    static constexpr int USER_CARD_W = 300;
    static constexpr int USER_CARD_H = 28;

    static constexpr int ASST_CARD_X = 10;
    static constexpr int ASST_CARD_Y = 168;
    static constexpr int ASST_CARD_W = 300;
    static constexpr int ASST_CARD_H = 28;

    static constexpr int BTN_X = 10;
    static constexpr int BTN_Y = 200;
    static constexpr int BTN_W = 300;
    static constexpr int BTN_H = 35;

    // Cadence maximale de rafraîchissement textuel pendant streaming (~10 Hz)
    static constexpr uint32_t TEXT_UPDATE_INTERVAL_MS = 100;

    // Palette Dark Mode Premium
    static constexpr uint16_t COLOR_BG          = 0x0842; // Deep midnight navy (#080E14)
    static constexpr uint16_t COLOR_CARD_BG     = 0x0926; // Carte sombre contrastée (#0D1522)
    static constexpr uint16_t COLOR_CARD_BORDER = 0x19E8; // Bordure douce ardoise (#1E3C4E)
    static constexpr uint16_t COLOR_TEXT_PRI    = 0xFFFF; // Blanc pur
    static constexpr uint16_t COLOR_TEXT_SEC    = 0xDEDB; // Argent clair doux
    static constexpr uint16_t COLOR_TEXT_MUTED  = 0x6B6D; // Gris-bleu estompé

private:
    M5GFX* _display = nullptr;
    VoiceOrb _orb;
    ConvUIState _state = ConvUIState::INITIALIZING;

    String _userText;
    String _assistantText;
    String _errorMessage;
    bool _wifiConnected = false;

    // Régulation des mises à jour textuelles en flux
    String _pendingUserText;
    String _pendingAssistantText;
    bool _pendingUserUpdate = false;
    bool _pendingAsstUpdate = false;
    uint32_t _lastUserDrawMs = 0;
    uint32_t _lastAsstDrawMs = 0;

    // Drapeaux de rafraîchissement partiel
    bool _dirtyFull = true;
    bool _dirtyHeader = true;
    bool _dirtyStateText = true;
    bool _dirtyUserCard = true;
    bool _dirtyAsstCard = true;
    bool _dirtyBtn = true;

    // Métriques séparées (µs)
    uint32_t _maxOrbUs = 0;
    uint32_t _maxHeaderUs = 0;
    uint32_t _maxStateTextUs = 0;
    uint32_t _maxUserCardUs = 0;
    uint32_t _maxAsstCardUs = 0;
    uint32_t _maxMainBtnUs = 0;

    void drawStaticBackground();
    void drawHeader();
    void drawStateText();
    void drawUserCard();
    void drawAssistantCard();
    void drawMainButton();
};
