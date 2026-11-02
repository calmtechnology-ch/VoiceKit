/**
 * @file PTTController.h
 * @brief Machine a etats Push-to-Talk (PTT) independante et prevention des blocages.
 */

#pragma once

#include <Arduino.h>
#include <VoxCoreVoice.h>
#include <WiFi.h>
#include "Diagnostics.h"
#include "AppConfig.h"

enum class PTTState : uint8_t {
    DISABLED,          ///< Bouton desactive (connexion Wi-Fi / session en cours)
    READY,             ///< Pret a engager une prise de parole
    STARTING,          ///< Demande de capture initiee, attente confirmation moteur
    LISTENING,         ///< Microphone actif, capture en cours
    STOPPING_CAPTURE,  ///< Fin de parole demandee, transmission vers OpenAI
    PROCESSING,        ///< Traitement et synthese serveur en cours
    SPEAKING,          ///< Assistant en cours d'elocution sur haut-parleur
    INTERRUPTING,      ///< Arret prioritaire STOP en cours d'execution
    RECOVERING,        ///< Nettoyage et reprise controlee apres anomalie
    ERROR_STATE        ///< Etat d'erreur transport ou service
};

const char* pttStateToString(PTTState st);

class PTTController {
public:
    PTTController(VoxCoreVoice& voiceEngine, Diagnostics& diag);

    void begin();

    // Intentions recues depuis les evenements d'interface (LVGL ou Serie)
    void onButtonPress(uint32_t contactId, int32_t x, int32_t y);
    void onButtonRelease(uint32_t contactId, int32_t x, int32_t y);
    void onButtonPressLost(uint32_t contactId, int32_t x, int32_t y);

    // Notifications recues depuis les callbacks VoiceKit
    void notifyVoiceStateChanged(VoiceState newState);
    void notifyListeningStarted();
    void notifyListeningFinished();
    void notifySpeechStarted();
    void notifySpeechFinished();
    void notifyError(const String& err);
    void notifyReady();

    // Cycle principal non bloquant (appele dans loop())
    void update(uint32_t nowMs, bool isPhysicalTouchActive);

    // Commande prioritaire directe (ex: via Serie 'S' ou STOP)
    void triggerStop();

    // Commande serie de test
    void triggerSerialPress();
    void triggerSerialRelease();

    // Accesseurs
    PTTState getState() const { return _state; }
    bool isListening() const { return _state == PTTState::LISTENING; }
    bool isActionAllowed() const;
    const char* getRejectCause() const;

private:
    VoxCoreVoice& _voice;
    Diagnostics& _diag;

    PTTState _state = PTTState::DISABLED;
    PTTState _prevState = PTTState::DISABLED;

    // Intentions differees venant des callbacks UI
    volatile bool _reqPress = false;
    volatile bool _reqRelease = false;
    volatile bool _reqStop = false;
    volatile bool _reqRecover = false;

    // Contexte du contact tactile
    uint32_t _curContactId = 0;
    int32_t _curTouchX = 0;
    int32_t _curTouchY = 0;
    uint32_t _pressStartTimeMs = 0;
    uint32_t _listeningStartTimeMs = 0;
    uint32_t _processingStartTimeMs = 0;
    uint32_t _stoppedStartTimeMs = 0;
    uint32_t _stopStartTimeMs = 0;
    uint32_t _errorStartTimeMs = 0;

    // Drapeaux logiques
    bool _serialListening = false;
    bool _touchListening = false;

    void setState(PTTState newState);
    void processIntents(uint32_t nowMs);
    void checkWatchdogs(uint32_t nowMs, bool isPhysicalTouchActive);
};

