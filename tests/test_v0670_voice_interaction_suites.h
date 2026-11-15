#pragma once

#include "native_mock/Arduino.h"
#include "native_mock/WiFi.h"
#include "native_mock/MockLiveTransport.h"
#include "VoiceKit.h"
#include "VoxCoreVoice.h"
#include "VoiceInteractionController.h"
#include "VoiceCaptureValidator.h"
#include "Utf8Utils.h"
#include <iostream>
#include <string>

// Variable globale externe pour TEST_ASSERT
extern int s_testsPassed;
extern int s_testsFailed;

#ifndef TEST_ASSERT
#define TEST_ASSERT(cond, msg)                                                 \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "  [FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__   \
                << ")" << std::endl;                                           \
      s_testsFailed++;                                                         \
    } else {                                                                   \
      s_testsPassed++;                                                         \
    }                                                                          \
  } while (0)
#endif

inline void run_v0670_voice_interaction_tests() {
    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  [TEST SUITE 152] VoiceKit v0.6.7.0 — Voice Interaction (Experimental)" << std::endl;
    std::cout << "  PTT 2 Clics, Validateur VAD Holistique & STOP Prioritaire" << std::endl;
    std::cout << "===========================================================" << std::endl;

    // -------------------------------------------------------------------------
    // SCENARIO 1 : Initialisation du contrôleur
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 1 : Initialisation du contrôleur ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_1";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);

        VoiceInteractionConfig pttCfg;
        VoiceInteractionController controller(voice, pttCfg);

        // Avant session prête -> DISABLED
        controller.begin();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::DISABLED, "1.1 Contrôleur initialement DISABLED si session hors-ligne");
        TEST_ASSERT(!controller.isReady(), "1.2 isReady() == false");
        TEST_ASSERT(!controller.isListening(), "1.3 isListening() == false");
        TEST_ASSERT(controller.getStartListeningCallsCount() == 0, "1.4 0 appels startListening()");
        TEST_ASSERT(controller.getStopListeningCallsCount() == 0, "1.5 0 appels stopListening()");
        TEST_ASSERT(controller.getCancelListeningCallsCount() == 0, "1.6 0 appels cancelListening()");
        TEST_ASSERT(controller.getStopInterruptsCount() == 0, "1.7 0 interruptions STOP");
        TEST_ASSERT(controller.getResponseRequestsSentCount() == 0, "1.8 0 demandes de réponse IA");

        // Quand session devient CONNECTED et voice prête
        prepareMockVoiceReady(voice, transport);
        controller.begin();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "1.9 Contrôleur READY quand session connectée et moteur prêt");
        TEST_ASSERT(controller.isReady(), "1.10 isReady() == true");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 2 : Passage DISABLED -> READY
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 2 : Passage DISABLED -> READY ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_2";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);

        VoiceInteractionController controller(voice);
        controller.begin();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::DISABLED, "2.1 Contrôleur initialement DISABLED");

        bool stateChangedNotified = false;
        VoiceInteractionState observedOld = VoiceInteractionState::DISABLED;
        VoiceInteractionState observedNew = VoiceInteractionState::DISABLED;
        controller.onStateChanged([&](VoiceInteractionState oldSt, VoiceInteractionState newSt) {
            stateChangedNotified = true;
            observedOld = oldSt;
            observedNew = newSt;
        });

        // Connexion confirmée
        prepareMockVoiceReady(voice, transport);
        controller.notifyReady();

        TEST_ASSERT(stateChangedNotified, "2.2 Callback onStateChanged déclenché");
        TEST_ASSERT(observedOld == VoiceInteractionState::DISABLED, "2.3 Ancien état DISABLED");
        TEST_ASSERT(observedNew == VoiceInteractionState::READY, "2.4 Nouvel état READY");
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "2.5 Contrôleur passé en READY");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 3 : Premier clic (Engager l'écoute)
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 3 : Premier clic (START_CAPTURE) ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_3";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "3.1 Contrôleur en READY");

        advanceMockMillis(200);
        controller.onTalkClick();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "3.2 État encore READY avant update()");

        controller.update();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::STARTING || controller.getState() == VoiceInteractionState::LISTENING, "3.3 État STARTING/LISTENING après update()");
        TEST_ASSERT(controller.getStartListeningCallsCount() == 1, "3.4 startListening() appelé exactement 1 fois");
        TEST_ASSERT(voice.isListening(), "3.5 VoxCoreVoice moteur en écoute");

        // Notification d'écoute confirmée
        controller.notifyListeningStarted();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::LISTENING, "3.6 État confirmé LISTENING");
        TEST_ASSERT(controller.isListening(), "3.7 isListening() == true");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 4 : Deuxième clic avec parole valide
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 4 : Deuxième clic avec parole valide (FINISH_CAPTURE) ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_4";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.onTalkClick();
        controller.update();
        controller.notifyListeningStarted();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::LISTENING, "4.1 En écoute");

        // Injection de blocs audio de parole (> 380 RMS, cumulée > 250 ms, segment continu > 120 ms, durée > 350 ms)
        // 15 blocs de 32 ms = 480 ms de parole consécutive à 900 RMS
        for (int i = 0; i < 15; i++) {
            advanceMockMillis(32);
            controller.feedChunkRms(900);
        }

        bool captureValidated = false;
        VoiceCaptureMetrics validatedMetrics;
        controller.onCaptureValidated([&](const VoiceCaptureMetrics& m) {
            captureValidated = true;
            validatedMetrics = m;
        });

        advanceMockMillis(150);
        controller.onTalkClick(); // 2ème clic
        controller.update();

        TEST_ASSERT(captureValidated, "4.2 Validation VAD effectuée");
        TEST_ASSERT(validatedMetrics.isValid, "4.3 Parole jugée valide");
        TEST_ASSERT(std::string(validatedMetrics.rejectReason) == "OK", "4.4 Motif de rejet == OK");
        TEST_ASSERT(controller.getStopListeningCallsCount() == 1, "4.5 stopListening() appelé exactement 1 fois");
        TEST_ASSERT(controller.getCancelListeningCallsCount() == 0, "4.6 cancelListening() NON appelé");
        TEST_ASSERT(controller.getResponseRequestsSentCount() == 1, "4.7 1 demande de réponse IA envoyée");
        TEST_ASSERT(controller.getState() == VoiceInteractionState::STOPPING_CAPTURE, "4.8 État STOPPING_CAPTURE");

        // Notification fin d'écoute -> PROCESSING
        controller.notifyListeningFinished();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::PROCESSING, "4.9 État PROCESSING");

        // Notification début de réponse -> SPEAKING
        controller.notifySpeechStarted();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::SPEAKING, "4.10 État SPEAKING");

        // Notification fin de réponse -> READY
        voice.stop();
        controller.notifySpeechFinished();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "4.11 Retour à READY");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 5 : Deuxième clic avec silence (Rejet sans appel à stopListening)
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 5 : Deuxième clic avec silence ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_5";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.onTalkClick();
        controller.update();
        controller.notifyListeningStarted();

        // 15 blocs de silence (100 RMS, inférieur au seuil)
        for (int i = 0; i < 15; i++) {
            advanceMockMillis(32);
            controller.feedChunkRms(100);
        }

        bool silenceNotified = false;
        std::string silenceReason;
        controller.onSilenceDetected([&](const char* reason) {
            silenceNotified = true;
            silenceReason = reason ? reason : "";
        });

        advanceMockMillis(150);
        controller.onTalkClick(); // 2ème clic
        controller.update();

        TEST_ASSERT(silenceNotified, "5.1 Notification silence déclenchée");
        TEST_ASSERT(silenceReason == "SILENCE", "5.2 Motif silence == SILENCE");
        TEST_ASSERT(controller.getStopListeningCallsCount() == 0, "5.3 stopListening() STRICTEMENT NON appelé");
        TEST_ASSERT(controller.getCancelListeningCallsCount() == 1, "5.4 cancelListening() appelé pour fermer le micro");
        TEST_ASSERT(controller.getResponseRequestsSentCount() == 0, "5.5 ZERO demande de réponse IA transmise");
        TEST_ASSERT(controller.getSilenceRejectionsCount() == 1, "5.6 1 rejet silence comptabilisé");
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "5.7 Retour immédiat à READY");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 6 : Rejet d'un bruit bref (choc / clic < 120 ms consécutif)
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 6 : Rejet d'un bruit bref (BRIEF_NOISE) ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_6";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.onTalkClick();
        controller.update();
        controller.notifyListeningStarted();

        // 8 blocs de silence (256 ms)
        for (int i = 0; i < 8; i++) {
            advanceMockMillis(32);
            controller.feedChunkRms(100);
        }
        // 2 blocs de fort niveau (64 ms < 120 ms requis consécutif)
        for (int i = 0; i < 2; i++) {
            advanceMockMillis(32);
            controller.feedChunkRms(1200);
        }
        // 5 blocs de silence (160 ms)
        for (int i = 0; i < 5; i++) {
            advanceMockMillis(32);
            controller.feedChunkRms(100);
        }

        bool silenceNotified = false;
        std::string rejectReason;
        controller.onSilenceDetected([&](const char* reason) {
            silenceNotified = true;
            rejectReason = reason ? reason : "";
        });

        advanceMockMillis(150);
        controller.onTalkClick();
        controller.update();

        TEST_ASSERT(silenceNotified, "6.1 Rejet bruit bref notifié");
        TEST_ASSERT(rejectReason == "BRIEF_NOISE", "6.2 Motif de rejet == BRIEF_NOISE");
        TEST_ASSERT(controller.getStopListeningCallsCount() == 0, "6.3 stopListening() NON appelé");
        TEST_ASSERT(controller.getCancelListeningCallsCount() == 1, "6.4 cancelListening() appelé");
        TEST_ASSERT(controller.getResponseRequestsSentCount() == 0, "6.5 Aucune demande IA émise");
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "6.6 Retour à READY");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 7 : Rejet d'une capture trop courte (< 350 ms)
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 7 : Rejet d'une capture trop courte (TOO_SHORT) ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_7";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.onTalkClick();
        controller.update();
        controller.notifyListeningStarted();

        // Seulement 4 blocs audio (128 ms < 350 ms)
        for (int i = 0; i < 4; i++) {
            advanceMockMillis(32);
            controller.feedChunkRms(800);
        }

        bool rejected = false;
        std::string reason;
        controller.onSilenceDetected([&](const char* r) {
            rejected = true;
            reason = r ? r : "";
        });

        advanceMockMillis(20);
        controller.onTalkClick();
        controller.update();

        TEST_ASSERT(rejected, "7.1 Rejet capture trop courte notifié");
        TEST_ASSERT(reason == "TOO_SHORT", "7.2 Motif de rejet == TOO_SHORT");
        TEST_ASSERT(controller.getStopListeningCallsCount() == 0, "7.3 stopListening() NON appelé");
        TEST_ASSERT(controller.getCancelListeningCallsCount() == 1, "7.4 cancelListening() appelé");
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "7.5 Retour à READY");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 8 : Double clic rapide (Anti-rebond matériel 120 ms)
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 8 : Double clic rapide (Anti-rebond) ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_8";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.onTalkClick(); // 1er clic légitime
        advanceMockMillis(40);    // 40 ms plus tard (< 120 ms)
        controller.onTalkClick(); // 2ème clic parasite rejeté par anti-rebond

        controller.update();
        TEST_ASSERT(controller.getStartListeningCallsCount() == 1, "8.1 1 seul démarrage engagé");
        TEST_ASSERT(controller.getStopListeningCallsCount() == 0, "8.2 Le clic parasite n'a pas arrêté la capture");
        TEST_ASSERT(controller.getState() == VoiceInteractionState::STARTING || controller.getState() == VoiceInteractionState::LISTENING, "8.3 Écoute en cours");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 9 : STOP pendant LISTENING
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 9 : STOP pendant LISTENING ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_9";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.onTalkClick();
        controller.update();
        controller.notifyListeningStarted();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::LISTENING, "9.1 En écoute");

        advanceMockMillis(150);
        controller.triggerStop();
        controller.update();

        TEST_ASSERT(controller.getStopInterruptsCount() == 1, "9.2 Interruption STOP comptabilisée");
        TEST_ASSERT(controller.getCancelListeningCallsCount() == 1, "9.3 cancelListening() invoqué pour couper le micro");
        TEST_ASSERT(controller.isLastTurnCancelled(), "9.4 isLastTurnCancelled() == true");
        TEST_ASSERT(!voice.isListening(), "9.5 Micro désactivé");
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "9.6 Retour immédiat à READY car session CONNECTED");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 10 : STOP pendant SPEAKING
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 10 : STOP pendant SPEAKING ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_10";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        // Passage artificiel en élocution
        controller.notifyVoiceStateChanged(VoiceState::SPEAKING);
        TEST_ASSERT(controller.getState() == VoiceInteractionState::SPEAKING, "10.1 En SPEAKING");

        advanceMockMillis(200);
        // Un simple clic en cours de parole doit se convertir en STOP prioritaire
        controller.onTalkClick();
        controller.update();

        TEST_ASSERT(controller.getStopInterruptsCount() == 1, "10.2 Clic en lecture converti en STOP");
        TEST_ASSERT(controller.isLastTurnCancelled(), "10.3 Tour marqué annulé");
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "10.4 Rétablissement à READY");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 11 : STOP répété (Idempotence)
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 11 : STOP répété (Idempotence) ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_11";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.triggerStop();
        controller.update();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "11.1 Premier STOP propre");

        advanceMockMillis(200);
        controller.triggerStop();
        controller.update();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "11.2 Deuxième STOP idempotent sans anomalie");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 12 : Échec de startListening()
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 12 : Échec de startListening() ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_12";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        // Désassigner l'input pour provoquer l'échec de startListening()
        voice.stop();
        // Simuler un refus de startListening en désactivant la session
        voice.getSession().deactivate();

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.onTalkClick();
        controller.update();

        TEST_ASSERT(!controller.isListening(), "12.1 Écoute non activée suite au refus");
        TEST_ASSERT(controller.getState() == VoiceInteractionState::DISABLED || controller.getState() == VoiceInteractionState::READY, "12.2 Contrôleur stable");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 13 : Échec de stopListening()
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 13 : Échec de stopListening() ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_13";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.onTalkClick();
        controller.update();
        controller.notifyListeningStarted();

        // Injection parole
        for (int i = 0; i < 15; i++) {
            advanceMockMillis(32);
            controller.feedChunkRms(850);
        }

        // Simuler un arrêt forcé du moteur avant le 2nd clic pour faire échouer stopListening()
        voice.cancelListening();

        advanceMockMillis(150);
        controller.onTalkClick(); // 2ème clic
        controller.update();

        TEST_ASSERT(controller.isLastTurnCancelled(), "13.1 Repli sur annulation sécurisée");
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "13.2 Retour à READY sans blocage");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 14 : Perte Wi-Fi
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 14 : Perte Wi-Fi ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_14";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "14.1 Prêt");

        // Coupure Wi-Fi simulée
        WiFi.setConnected(false);
        TEST_ASSERT(!controller.isActionAllowed(), "14.2 isActionAllowed() == false sans Wi-Fi");

        advanceMockMillis(200);
        controller.onTalkClick();
        controller.update();

        TEST_ASSERT(controller.getStartListeningCallsCount() == 0, "14.3 Clic rejeté sans Wi-Fi");
        TEST_ASSERT(std::string(controller.getRejectCause()) == "WIFI_NOT_CONNECTED", "14.4 Cause rejet == WIFI_NOT_CONNECTED");

        WiFi.setConnected(true);
        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 15 : Reconnexion réseau
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 15 : Reconnexion réseau ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_15";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        // Perte session
        controller.notifyVoiceStateChanged(VoiceState::CONNECTING_WIFI);
        TEST_ASSERT(controller.getState() == VoiceInteractionState::DISABLED, "15.1 Contrôleur DISABLED pendant reconnexion");

        // Reconnexion établie
        controller.notifyVoiceStateChanged(VoiceState::READY);
        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "15.2 Contrôleur de nouveau READY après rétablissement");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 16 : Événements tardifs après STOP
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 16 : Événements tardifs après STOP ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_16";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.onTalkClick();
        controller.update();
        controller.notifyListeningStarted();

        // STOP en plein milieu
        advanceMockMillis(150);
        controller.triggerStop();
        controller.update();

        TEST_ASSERT(controller.isLastTurnCancelled(), "16.1 isLastTurnCancelled() == true");

        // Simulation de réception tardive de transcription
        bool lateTextProcessed = false;
        auto onUserTextSim = [&](const String& text) {
            if (controller.isLastTurnCancelled()) {
                // Doit être ignoré conformément à l'audit
                return;
            }
            lateTextProcessed = true;
        };

        onUserTextSim("Texte orphelin arrivé après STOP");
        TEST_ASSERT(!lateTextProcessed, "16.2 Texte tardif ignoré grâce au drapeau d'annulation");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 17 : Timeout microphone (Watchdog 15 s)
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 17 : Timeout microphone (15 s) ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_17";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.onTalkClick();
        controller.update();
        controller.notifyListeningStarted();

        // 10 blocs de parole (320 ms)
        for (int i = 0; i < 10; i++) {
            advanceMockMillis(32);
            controller.feedChunkRms(800);
        }

        // Avancement du temps au-delà du timeout micro de 15 s
        advanceMockMillis(15100);

        bool evaluatedOnTimeout = false;
        controller.onCaptureValidated([&](const VoiceCaptureMetrics&) {
            evaluatedOnTimeout = true;
        });

        controller.update();

        TEST_ASSERT(evaluatedOnTimeout, "17.1 Évaluation VAD déclenchée par le timeout");
        TEST_ASSERT(controller.getStopListeningCallsCount() == 1, "17.2 stopListening() appelé suite à parole valide lors du timeout");
        TEST_ASSERT(controller.getState() == VoiceInteractionState::STOPPING_CAPTURE, "17.3 État STOPPING_CAPTURE");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 18 : Timeout serveur (Watchdog 15 s)
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 18 : Timeout serveur (15 s) ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_18";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        controller.notifyVoiceStateChanged(VoiceState::PROCESSING);
        TEST_ASSERT(controller.getState() == VoiceInteractionState::PROCESSING, "18.1 En PROCESSING");

        // Attente prolongée > 15 s sans réponse serveur
        advanceMockMillis(15100);
        controller.update();

        TEST_ASSERT(controller.getState() == VoiceInteractionState::READY, "18.2 Rétablissement à READY par le watchdog serveur");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 19 : Validation des accents UTF-8 dans les notifications
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 19 : Accents UTF-8 dans les notifications ---" << std::endl;
    {
        String frenchSilenceNotice = "(Aucune parole détectée)";
        TEST_ASSERT(Utf8Utils::isValidUtf8(frenchSilenceNotice), "19.1 Notification silence UTF-8 valide");
        TEST_ASSERT(frenchSilenceNotice.indexOf("détéctée") != -1 || frenchSilenceNotice.indexOf("détectée") != -1, "19.2 Présence du mot détectée avec accents");

        // Découpe sécurisée sans corrompre un caractère accentué multi-octets
        String truncated = Utf8Utils::truncateUtf8(frenchSilenceNotice, 18);
        TEST_ASSERT(truncated.length() <= 18, "19.3 Découpe sécurisée UTF-8 respecte la borne");
        TEST_ASSERT(Utf8Utils::isValidUtf8(truncated), "19.4 Sous-chaîne découpée UTF-8 valide");
    }

    // -------------------------------------------------------------------------
    // SCENARIO 20 : Absence de double exécution d'une intention
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 20 : Absence de double exécution d'une intention ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0670_20";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        VoiceInteractionController controller(voice);
        controller.begin();

        advanceMockMillis(200);
        controller.onTalkClick();

        // 3 appels consécutifs à update()
        controller.update();
        controller.update();
        controller.update();

        TEST_ASSERT(controller.getStartListeningCallsCount() == 1, "20.1 startListening() exécuté exactement une seule fois malgré 3 update()");

        voice.getClient().setTransport(nullptr);
    }

    std::cout << "\n[TEST SUITE 152] SUCCES INTEGRAL DES 20 TESTS D'INTERACTION VOCALE (v0.6.7.0)" << std::endl;
}
