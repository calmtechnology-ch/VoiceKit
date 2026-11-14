#pragma once

#include "native_mock/Arduino.h"
#include "native_mock/WiFi.h"
#include "native_mock/MockLiveTransport.h"
#include "VoiceKit.h"
#include "VoxCoreVoice.h"
#include "core/pipeline/IntegratedVoicePipeline.h"
#include "core/tools/ToolRegistry.h"
#include "core/voiceux/VoiceUxLiveBridge.h"
#include <iostream>
#include <string>

// Définition d'un IAudioInput qui échoue à l'initialisation pour le test 13
class FailingMicAudioInput : public IAudioInput {
public:
    bool begin() override { return false; }
    void end() override {}
    bool isRunning() const override { return false; }
    int isRecording() const override { return 0; }
    bool record(int16_t*, size_t, uint32_t, bool) override { return false; }
    void stop() override {}
    const char* getLastError() const override { return "Mic init hardware failure"; }
};

inline void run_v0661_corrective_state_coordination_tests() {
    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  [TEST SUITE 151] VoiceKit v0.6.6.1 — Corrective State Coordination" << std::endl;
    std::cout << "===========================================================" << std::endl;

    // -------------------------------------------------------------------------
    // SCENARIO 1 : Initialisation vers READY
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 1 : Initialisation vers READY ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_1";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        TEST_ASSERT(voice.begin(cfg), "1.1 voice.begin() reussi");
        prepareMockVoiceReady(voice, transport);

        TEST_ASSERT(voice.getState() == VoiceState::READY, "1.2 Etat VoxCoreVoice == READY");
        TEST_ASSERT(voice.isReady() == true, "1.3 voice.isReady() == true");
        TEST_ASSERT(voice.isBusy() == false, "1.4 voice.isBusy() == false");

        VoiceKit vk;
        TEST_ASSERT(vk.begin(cfg), "1.5 vk.begin() reussi");
        prepareMockVoiceReady(vk.getEngine(), transport);
        TEST_ASSERT(vk.isReady() == true, "1.6 vk.isReady() == true");
        TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "1.7 vk mode initial == IDLE");

        voice.getClient().setTransport(nullptr);
        vk.getEngine().getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 2 : Cycle PTT normal
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 2 : Cycle PTT normal ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_2";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);
        pipeline.begin(voice, tools);

        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "2.1 Pipeline initialement READY");

        // 1. Appui PARLER -> LISTENING
        TEST_ASSERT(pipeline.startListening(), "2.2 startListening() accepte");
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "2.3 Pipeline en LISTENING");
        TEST_ASSERT(voice.isListening(), "2.4 Voice moteur en ecoute");
        uint32_t turnId = pipeline.getActiveTurnId();
        TEST_ASSERT(turnId > 0, "2.5 turnId actif attribue");

        // 2. Relâchement PARLER -> TRANSCRIBING
        TEST_ASSERT(pipeline.stopListening(), "2.6 stopListening() accepte");
        TEST_ASSERT(pipeline.getState() == PipelineState::TRANSCRIBING, "2.7 Pipeline en TRANSCRIBING");
        TEST_ASSERT(voice.isTranscribing(), "2.8 Voice moteur en attente transcription");

        // 3. Transcription reçue -> ROUTING -> AI_PROCESSING / SPEAKING
        bool turnCompleted = false;
        pipeline.onTurnFinished([&turnCompleted](uint32_t, PipelineResult res, const String&) {
            if (res == PipelineResult::COMPLETED) turnCompleted = true;
        });

        // Simuler transcription et réponse
        pipeline.feedSimulatedTranscription("Bonjour assistant", true);
        TEST_ASSERT(pipeline.getState() == PipelineState::AI_PROCESSING, "2.9 Pipeline en AI_PROCESSING");

        // Simuler premier son audio reçu
        voice.triggerSpeechStartedForTesting();
        TEST_ASSERT(pipeline.getState() == PipelineState::SPEAKING, "2.10 Pipeline en SPEAKING");

        // Simuler fin de lecture audio
        voice.triggerSpeechFinishedForTesting(voice.getActiveSpeechId());
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "2.11 Pipeline retourne a READY");
        TEST_ASSERT(turnCompleted, "2.12 Tour vocal acheve avec succes");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 3 : Double appui PTT
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 3 : Double appui PTT ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_3";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        TEST_ASSERT(voice.startListening(), "3.1 Premier startListening() accepte");
        uint32_t turn1 = voice.getActiveCaptureTurnId();
        TEST_ASSERT(turn1 > 0, "3.2 turn1 actif");
        TEST_ASSERT(voice.isListening(), "3.3 voice.isListening() == true");

        // Deuxième appui pendant que LISTENING est actif -> refus idempotent
        TEST_ASSERT(voice.startListening() == false, "3.4 Deuxieme startListening() refuse en LISTENING");
        TEST_ASSERT(voice.getActiveCaptureTurnId() == turn1, "3.5 turnId inchange apres refus");
        TEST_ASSERT(voice.isListening(), "3.6 voice.isListening() toujours true");

        // Bascule en TRANSCRIBING
        TEST_ASSERT(voice.stopListening(), "3.7 stopListening() accepte");
        TEST_ASSERT(voice.isTranscribing(), "3.8 voice.isTranscribing() == true");

        // Troisième appui pendant TRANSCRIBING -> refus idempotent
        TEST_ASSERT(voice.startListening() == false, "3.9 Troisieme startListening() refuse en TRANSCRIBING");
        TEST_ASSERT(voice.isTranscribing(), "3.10 voice.isTranscribing() toujours true");

        voice.cancelListening();
        TEST_ASSERT(voice.isReady(), "3.11 voice READY apres cancel");
        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 4 : Double relâchement
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 4 : Double relâchement ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_4";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        TEST_ASSERT(voice.startListening(), "4.1 startListening() accepte");
        TEST_ASSERT(voice.stopListening() == true, "4.2 Premier stopListening() accepte");
        TEST_ASSERT(voice.isTranscribing(), "4.3 En attente transcription");

        // Deuxième relâchement immédiat -> rejet propre idempotent
        TEST_ASSERT(voice.stopListening() == false, "4.4 Deuxieme stopListening() refuse proprement");
        TEST_ASSERT(voice.isTranscribing(), "4.5 Toujours en attente transcription");

        voice.cancelListening();
        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 5 : Transcription vide
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 5 : Transcription vide ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        bool toolExecuted = false;
        tools.registerTool("dummy.tool", "Dummy", [&toolExecuted]() {
            toolExecuted = true;
            return ToolResult::success("Ok");
        });
        IntegratedVoicePipeline pipeline;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_5";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);
        pipeline.begin(voice, tools);

        pipeline.startListening();
        pipeline.stopListening();

        bool rejectedOrCompleted = false;
        pipeline.onTurnFinished([&rejectedOrCompleted](uint32_t, PipelineResult res, const String&) {
            if (res == PipelineResult::REJECTED || res == PipelineResult::COMPLETED) {
                rejectedOrCompleted = true;
            }
        });

        // Injection d'une transcription vide ou hésitation seule
        pipeline.feedSimulatedTranscription("   ", true);
        TEST_ASSERT(toolExecuted == false, "5.1 Aucune commande executee sur texte vide");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "5.2 Pipeline retourne a READY");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 6 : Double transcription finale
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 6 : Double transcription finale ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_6";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        int finalCount = 0;
        voice.onTranscription([&finalCount](const String&, bool isFinal) {
            if (isFinal) finalCount++;
        });

        voice.startListening();
        voice.stopListening();
        TEST_ASSERT(voice.isTranscribing(), "6.1 En cours de transcription");

        // Première finale légitime
        voice.handleIncomingTranscription("Commande test", true);
        TEST_ASSERT(finalCount == 1, "6.2 Premiere transcription finale recue");
        TEST_ASSERT(voice.isReady(), "6.3 Moteur repasse a READY");

        // Deuxième finale dupliquée émise par le serveur pour le même tour
        voice.handleIncomingTranscription("Commande test", true);
        TEST_ASSERT(finalCount == 1, "6.4 Deuxieme finale dupliquee ignoree (finalCount reste 1)");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 7 : Callback STT tardif après STOP
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 7 : Callback STT tardif après STOP ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_7";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        int transcriptionCount = 0;
        voice.onTranscription([&transcriptionCount](const String&, bool) {
            transcriptionCount++;
        });

        voice.startListening();
        voice.stop(); // STOP pendant écoute
        TEST_ASSERT(voice.isReady(), "7.1 READY apres stop");

        // Callback réseau résiduel du tour annulé
        voice.handleIncomingTranscription("Texte orphelin", true);
        TEST_ASSERT(transcriptionCount == 0, "7.2 Callback STT tardif apres STOP ignore (transcriptionCount==0)");
        TEST_ASSERT(voice.isReady(), "7.3 Moteur toujours READY");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 8 : STOP pendant l'écoute
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 8 : STOP pendant l'écoute ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_8";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        voice.startListening();
        TEST_ASSERT(voice.isListening(), "8.1 En cours d'ecoute");
        TEST_ASSERT(voice.getStreamer().isStreaming(), "8.2 Streamer actif");

        voice.stop();
        TEST_ASSERT(voice.isListening() == false, "8.3 Micro ferme apres stop");
        TEST_ASSERT(voice.getStreamer().isStreaming() == false, "8.4 Streamer arrete apres stop");
        TEST_ASSERT(voice.isReady(), "8.5 Moteur READY apres stop");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 9 : STOP pendant la génération IA
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 9 : STOP pendant la génération IA ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_9";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);
        pipeline.begin(voice, tools);

        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("Question IA", true);
        TEST_ASSERT(pipeline.getState() == PipelineState::AI_PROCESSING, "9.1 En attente reponse IA");

        // STOP déclenché pendant l'attente de réponse IA
        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "9.2 Pipeline READY apres cancel");
        TEST_ASSERT(voice.isSpeaking() == false, "9.3 Voice n'est pas en restitution");

        // Arrivée ultérieure de son IA tardif
        voice.triggerSpeechStartedForTesting();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "9.4 Son tardif ne reactive pas le pipeline");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 10 : STOP pendant la restitution TTS
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 10 : STOP pendant la restitution TTS ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_10";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        voice.say("Bonjour le monde");
        injectGeminiPcmAndDrain(voice, 2, false);
        TEST_ASSERT(voice.isSpeaking(), "10.1 Restitution vocale active");

        voice.stop();
        TEST_ASSERT(voice.isSpeaking() == false, "10.2 Parole interrompue immediatement");
        TEST_ASSERT(voice.isReady(), "10.3 Moteur READY apres stop TTS");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 11 : Callback TTS tardif après STOP
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 11 : Callback TTS tardif après STOP ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_11";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        int finishedCount = 0;
        voice.onSpeechFinished([&finishedCount]() {
            finishedCount++;
        });

        voice.say("Annonce test");
        uint32_t activeSpeech = voice.getActiveSpeechId();
        TEST_ASSERT(activeSpeech > 0, "11.1 speechId attribue");
        injectGeminiPcmAndDrain(voice, 2, false);
        TEST_ASSERT(voice.isSpeaking(), "11.2 Restitution active");

        voice.stop();
        TEST_ASSERT(voice.getActiveSpeechId() == 0, "11.3 activeSpeechId reinitialise a 0 apres stop");
        TEST_ASSERT(voice.isSpeaking() == false, "11.4 Haut-parleur arrete");

        // Simulation d'une notification de fin DMA orpheline
        voice.triggerSpeechFinishedForTesting(activeSpeech);
        TEST_ASSERT(finishedCount == 0, "11.5 Callback TTS tardif apres STOP ignore (finishedCount==0)");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 12 : Timeout de transcription
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 12 : Timeout de transcription ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_12";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        cfg.sttFinalTimeoutMs = 100; // Timeout court pour le test
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        voice.startListening();
        voice.stopListening();
        TEST_ASSERT(voice.isTranscribing(), "12.1 En attente de transcription");

        // Avancer artificiellement le temps
        delay(150);
        voice.update();

        TEST_ASSERT(voice.isTranscribing() == false, "12.2 Sortie de TRANSCRIBING apres timeout");
        TEST_ASSERT(voice.isReady(), "12.3 Retour a READY sans invention de texte");
        TEST_ASSERT(voice.getLastFinalText().isEmpty(), "12.4 Aucun texte fabrique");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 13 : Échec de démarrage microphone
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 13 : Échec de démarrage microphone ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        FailingMicAudioInput failingMic;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_13";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        cfg.audioInput = &failingMic;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        TEST_ASSERT(voice.startListening() == false, "13.1 startListening() refuse si micro indisponible");
        TEST_ASSERT(voice.isListening() == false, "13.2 voice.isListening() == false");

        // Reconfiguration avec audioInput par défaut valide
        cfg.audioInput = nullptr;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        TEST_ASSERT(voice.startListening() == true, "13.3 startListening() accepte une fois micro fonctionnel");
        voice.cancelListening();

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 14 : Déconnexion réseau
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 14 : Déconnexion réseau ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_14";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        voice.startListening();
        TEST_ASSERT(voice.isListening(), "14.1 Capture en cours");

        // Coupure WebSocket en plein milieu de capture
        transport.simulateError("Connection dropped");
        voice.update();

        TEST_ASSERT(voice.isListening() == false, "14.2 Capture interrompue lors de la coupure");
        TEST_ASSERT(voice.getState() == VoiceState::ERROR_STATE || voice.getState() == VoiceState::CONNECTING_WIFI || voice.isConnecting(),
                    "14.3 Transition vers etat d'erreur / reconnexion");

        voice.stop();
        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 15 : Récupération après erreur
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 15 : Récupération après erreur ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_15";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);
        pipeline.begin(voice, tools);

        // Simuler une coupure de connexion
        pipeline.startListening();
        transport.simulateError("Panne réseau WebSocket");
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::ERROR || pipeline.getLastResult() == PipelineResult::FAILED,
                    "15.1 Pipeline passe en ERROR");
        TEST_ASSERT(voice.isReady() == false, "15.2 Voice moteur indisponible");

        // Tant que le moteur n'est pas reconnecté, recoverFromError() doit être rejeté
        TEST_ASSERT(pipeline.recoverFromError() == false, "15.3 recoverFromError() refuse tant que le reseau est hors-ligne");

        // Rétablir le transport et reconnecter
        prepareMockVoiceReady(voice, transport);
        TEST_ASSERT(voice.isReady(), "15.4 Voice moteur redevenu READY");

        // Récupération autorisée
        TEST_ASSERT(pipeline.recoverFromError() == true, "15.5 recoverFromError() accepte une fois le moteur dispo");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "15.6 Pipeline revenu a READY");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 16 : Exécution d'une commande locale
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 16 : Exécution d'une commande locale ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        int toolExec = 0;
        tools.registerTool("light.toggle", "Toggle Light", [&toolExec]() {
            toolExec++;
            return ToolResult::success("Lumiere allumee");
        });
        IntegratedVoicePipeline pipeline;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_16";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);
        pipeline.begin(voice, tools);
        pipeline.getDispatcher().registerMapping("light.toggle", "allume la lumière");

        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("allume la lumière", true);

        TEST_ASSERT(toolExec == 1, "16.1 Outil local light.toggle execute exactement une fois");
        TEST_ASSERT(pipeline.getState() == PipelineState::SPEAKING, "16.2 Pipeline restitue le resultat");

        voice.triggerSpeechFinishedForTesting(voice.getActiveSpeechId());
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "16.3 Pipeline retourne a READY");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 17 : Commande sensible en attente de confirmation
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 17 : Commande sensible en attente de confirmation ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        bool rebootDone = false;
        tools.registerTool("system.reboot", "Reboot System", [&rebootDone]() {
            rebootDone = true;
            return ToolResult::success("Redemarrage initie");
        }, ToolSecurityPolicy::SENSITIVE);

        IntegratedVoicePipeline pipeline;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_17";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);
        pipeline.begin(voice, tools);
        pipeline.getDispatcher().registerMapping("system.reboot", "redémarre");

        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("redémarre", true);

        TEST_ASSERT(pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "17.1 En attente de confirmation");
        TEST_ASSERT(rebootDone == false, "17.2 Outil non execute avant confirmation");
        TEST_ASSERT(pipeline.getPendingConfirmationToken().length() > 0, "17.3 Jeton de confirmation present");

        // Confirmation explicite
        TEST_ASSERT(pipeline.confirmPendingAction(), "17.4 confirmPendingAction() acceptee");
        TEST_ASSERT(rebootDone == true, "17.5 Action sensible executee apres confirmation");

        voice.triggerSpeechFinishedForTesting(voice.getActiveSpeechId());
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "17.6 Pipeline READY");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 18 : Annulation d'une confirmation
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 18 : Annulation d'une confirmation ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        bool wipeDone = false;
        tools.registerTool("system.wipe", "Wipe Data", [&wipeDone]() {
            wipeDone = true;
            return ToolResult::success("Donnees effacees");
        }, ToolSecurityPolicy::SENSITIVE);

        IntegratedVoicePipeline pipeline;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_18";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);
        pipeline.begin(voice, tools);
        pipeline.getDispatcher().registerMapping("system.wipe", "efface tout");

        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("efface tout", true);

        TEST_ASSERT(pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "18.1 En attente de confirmation");
        String token = pipeline.getPendingConfirmationToken();

        // Annulation par rejet explicite
        TEST_ASSERT(pipeline.rejectPendingAction(), "18.2 Rejet de la confirmation accepte");
        TEST_ASSERT(wipeDone == false, "18.3 Action sensible non executee");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "18.4 Pipeline READY apres rejet");

        // Tentative de réutiliser le jeton annulé
        ToolResult reuseRes = tools.confirm(token);
        TEST_ASSERT(reuseRes.isSuccess() == false, "18.5 Jeton annule inutilisable");
        TEST_ASSERT(wipeDone == false, "18.6 Action toujours non executee");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 19 : Changement de profil vocal lorsque le moteur est libre
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 19 : Changement de profil vocal moteur libre ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_19";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        TEST_ASSERT(voice.isReady() == true, "19.1 Moteur READY");

        VoiceProfile newProfile;
        newProfile.provider = VoiceProviderType::GEMINI;
        newProfile.voiceId = "Charon";
        newProfile.language = "fr-FR";
        newProfile.style = VoiceStyle::EXPRESSIVE;
        newProfile.tone = VoiceTone::WARM;

        bool applied = voice.applyVoiceProfile(newProfile, false);
        TEST_ASSERT(applied == true, "19.2 applyVoiceProfile() accepte quand moteur libre");
        voice.update();

        // Finalisation setup pour confirmer le profil
        const char* setupJson = "{\"serverContent\":{\"modelTurn\":null},\"setupComplete\":{}}";
        voice.getClient().handleIncomingMessage(setupJson, strlen(setupJson));
        voice.update();

        VoiceProfile active = voice.getVoiceProfile();
        TEST_ASSERT(active.voiceId == "Charon", "19.3 Voix Charon appliquee");
        TEST_ASSERT(active.style == VoiceStyle::EXPRESSIVE, "19.4 Style applique");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCENARIO 20 : Refus d'un changement de profil lorsque le moteur est occupé
    // -------------------------------------------------------------------------
    std::cout << "\n--- SCENARIO 20 : Refus changement profil moteur occupé ---" << std::endl;
    {
        MockLiveTransport transport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyTestKey0661_20";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(cfg);
        prepareMockVoiceReady(voice, transport);

        voice.startListening();
        TEST_ASSERT(voice.isListening(), "20.1 Moteur en cours d'ecoute (occupe)");

        VoiceProfile target;
        target.provider = VoiceProviderType::GEMINI;
        target.voiceId = "Fenrir";
        target.language = "fr-FR";

        bool rejected = voice.applyVoiceProfile(target, false);
        TEST_ASSERT(rejected == false, "20.2 applyVoiceProfile() strictement refuse pendant capture");
        TEST_ASSERT(voice.getVoiceProfile().voiceId != "Fenrir", "20.3 Voix precedente conservee sans alteration");

        voice.cancelListening();
        voice.getClient().setTransport(nullptr);
    }

    std::cout << "\n[TEST SUITE 151] SUCCES INTEGRAL DES 20 TESTS DE COORDINATION (v0.6.6.1)" << std::endl;
}
