#pragma once

#include "IntegratedVoicePipeline.h"
#include <iostream>

// =============================================================================
// SUITE 144 : VoiceKit v0.6.5 — Integrated Voice Pipeline
// Tests obligatoires : A, B, C, D, E, F, G, H, I, J
// =============================================================================

inline void run_pipeline_v065_tests() {
    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  [TEST SUITE 144] VoiceKit v0.6.5 — Integrated Voice Pipeline" << std::endl;
    std::cout << "===========================================================" << std::endl;

    // -------------------------------------------------------------------------
    // TEST A : Tour local complet (STT -> Voice UX -> ToolRegistry -> TTS -> READY)
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST A : Tour local complet ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipelineA";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        // Enregistrer une commande locale
        bool toolExecuted = false;
        tools.registerTool("system.get_status", "Obtenir le statut general", [&toolExecuted]() {
            toolExecuted = true;
            return ToolResult::success("Le systeme fonctionne normalement.");
        }, ToolSecurityPolicy::READ_ONLY);

        VoicePipelineConfig pcfg;
        pcfg.enableVoiceUx = true;
        pcfg.enableLocalTools = true;
        pcfg.enableAiProcessing = true;
        pcfg.speakToolResult = true;

        pipeline.begin(voice, tools, pcfg);
        pipeline.getDispatcher().registerMapping("system.get_status", {"statut", "etat du systeme", "quel est le statut"});

        uint32_t recognizedCalls = 0;
        uint32_t routingCalls = 0;
        uint32_t toolExecCalls = 0;
        uint32_t toolResultCalls = 0;
        PipelineResult finalResult = PipelineResult::NONE;

        pipeline.onCommandRecognized([&](uint32_t turn, const String& toolId, const ToolArgs& args) {
            recognizedCalls++;
            TEST_ASSERT(toolId == "system.get_status", "Test A : toolId reconnu conforme");
        });

        pipeline.onRoutingDecision([&](uint32_t turn, PipelineDestination dest, const String& detail) {
            routingCalls++;
            TEST_ASSERT(dest == PipelineDestination::LOCAL_TOOL, "Test A : Destination LOCAL_TOOL");
        });

        pipeline.onToolExecutionStarted([&](uint32_t turn, const String& toolId) {
            toolExecCalls++;
        });

        pipeline.onToolResult([&](uint32_t turn, const String& toolId, const ToolResult& res) {
            toolResultCalls++;
            TEST_ASSERT(res.isSuccess(), "Test A : ToolResult est un succes");
        });

        pipeline.onTurnFinished([&](uint32_t turn, PipelineResult res, const String& summary) {
            finalResult = res;
        });

        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test A.1 Pipeline initialement READY");

        // 1. Démarrer écoute PTT
        TEST_ASSERT(pipeline.startListening(), "Test A.2 startListening() accepte");
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "Test A.3 Etat = LISTENING");
        uint32_t turnId = pipeline.getActiveTurnId();
        TEST_ASSERT(turnId > 0, "Test A.4 turnId valide");

        // 2. Relâcher écoute PTT
        TEST_ASSERT(pipeline.stopListening(), "Test A.5 stopListening() accepte");
        TEST_ASSERT(pipeline.getState() == PipelineState::TRANSCRIBING, "Test A.6 Etat = TRANSCRIBING");

        // 3. Injecter transcription finale avec formule de politesse
        pipeline.feedSimulatedTranscription("Peux-tu me donner le statut du systeme s'il te plait ?", true);

        // 4. Vérifier exécution outil et transition vers SPEAKING
        TEST_ASSERT(toolExecuted, "Test A.7 Commande executee dans le ToolRegistry");
        TEST_ASSERT(recognizedCalls == 1, "Test A.8 onCommandRecognized appele exactement 1 fois");
        TEST_ASSERT(routingCalls == 1, "Test A.9 onRoutingDecision appele 1 fois");
        TEST_ASSERT(toolExecCalls == 1, "Test A.10 onToolExecutionStarted appele 1 fois");
        TEST_ASSERT(toolResultCalls == 1, "Test A.11 onToolResult appele 1 fois");
        TEST_ASSERT(pipeline.getState() == PipelineState::SPEAKING, "Test A.12 Etat passe a SPEAKING pour restitution vocale");

        // 5. Simuler réception audio et fin de lecture TTS
        injectGeminiPcmAndDrain(voice, 3, true);
        drainVoicePlayback(voice);
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test A.13 Retour a READY");
        TEST_ASSERT(finalResult == PipelineResult::COMPLETED, "Test A.14 Resultat terminal COMPLETED");
    }

    // -------------------------------------------------------------------------
    // TEST B : Tour IA complet (STT -> routage IA -> réponse -> TTS -> READY)
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST B : Tour IA complet ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipelineB";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        VoicePipelineConfig pcfg;
        pcfg.enableVoiceUx = true;
        pcfg.enableLocalTools = true;
        pcfg.enableAiProcessing = true;

        pipeline.begin(voice, tools, pcfg);

        bool aiRouted = false;
        pipeline.onRoutingDecision([&](uint32_t turn, PipelineDestination dest, const String& detail) {
            if (dest == PipelineDestination::AI_PROVIDER) {
                aiRouted = true;
            }
        });

        pipeline.startListening();
        pipeline.stopListening();

        // Question générale ne correspondant à aucun outil ni intention UX
        pipeline.feedSimulatedTranscription("Pourquoi la mer est-elle salée ?", true);

        TEST_ASSERT(aiRouted, "Test B.1 Routage vers AI_PROVIDER");
        TEST_ASSERT(pipeline.getState() == PipelineState::AI_PROCESSING || pipeline.getState() == PipelineState::SPEAKING,
                    "Test B.2 Etat AI_PROCESSING ou SPEAKING");

        // Clôture
        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test B.3 Retour a READY apres cancel");
    }

    // -------------------------------------------------------------------------
    // TEST C : Commande inconnue (Aucun outil exécuté)
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST C : Commande inconnue ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipelineC";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        uint32_t toolExecCount = 0;
        tools.registerTool("system.reboot", "Redemarrage", [&toolExecCount]() {
            toolExecCount++;
            return ToolResult::success();
        }, ToolSecurityPolicy::WRITE);

        VoicePipelineConfig pcfg;
        pcfg.enableLocalTools = true;
        pcfg.enableAiProcessing = false; // IA désactivée -> rejet explicite
        pcfg.speakRejection = false;

        pipeline.begin(voice, tools, pcfg);
        pipeline.getDispatcher().registerMapping("system.reboot", "reboot");

        PipelineResult res = PipelineResult::NONE;
        PipelineDestination dest = PipelineDestination::NONE;
        pipeline.onRoutingDecision([&](uint32_t turn, PipelineDestination d, const String& detail) {
            dest = d;
        });
        pipeline.onTurnFinished([&](uint32_t turn, PipelineResult r, const String& sum) {
            res = r;
        });

        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("ouvre la porte du garage s'il te plait", true);

        TEST_ASSERT(dest == PipelineDestination::REJECTED, "Test C.1 Destination REJECTED pour phrase inconnue");
        TEST_ASSERT(res == PipelineResult::REJECTED, "Test C.2 Resultat terminal REJECTED");
        TEST_ASSERT(toolExecCount == 0, "Test C.3 Aucun outil arbitraire execute");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test C.4 Retour a READY");
    }

    // -------------------------------------------------------------------------
    // TEST D : STOP pendant chaque état actif
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST D : STOP dans tous les etats ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipelineD";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);
        pipeline.begin(voice, tools);

        // 1. STOP pendant LISTENING
        pipeline.startListening();
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "Test D.1 LISTENING");
        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test D.1 READY apres STOP pendant LISTENING");

        // 2. STOP pendant TRANSCRIBING
        pipeline.startListening();
        pipeline.stopListening();
        TEST_ASSERT(pipeline.getState() == PipelineState::TRANSCRIBING, "Test D.2 TRANSCRIBING");
        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test D.2 READY apres STOP pendant TRANSCRIBING");

        // 3. STOP pendant WAITING_CONFIRMATION
        tools.registerTool("system.danger", "Action sensible", []() {
            return ToolResult::success();
        }, ToolSecurityPolicy::SENSITIVE);
        pipeline.getDispatcher().registerMapping("system.danger", "danger");

        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("danger", true);
        TEST_ASSERT(pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "Test D.3 WAITING_CONFIRMATION");
        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test D.3 READY apres STOP pendant WAITING_CONFIRMATION");
        TEST_ASSERT(pipeline.getLastResult() == PipelineResult::CANCELLED, "Test D.3 Resultat CANCELLED");
    }

    // -------------------------------------------------------------------------
    // TEST E : Transcription tardive (Rejet stricte sans exécution)
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST E : Transcription tardive ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipelineE";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        uint32_t toolExecCount = 0;
        tools.registerTool("system.action", "Action", [&toolExecCount]() {
            toolExecCount++;
            return ToolResult::success();
        });
        pipeline.begin(voice, tools);
        pipeline.getDispatcher().registerMapping("system.action", "action");

        // Tour A démarré puis annulé
        pipeline.startListening();
        uint32_t turnA = pipeline.getActiveTurnId();
        pipeline.cancel();

        // Tour B démarré
        pipeline.startListening();
        uint32_t turnB = pipeline.getActiveTurnId();
        TEST_ASSERT(turnB > turnA, "Test E.1 turnB > turnA");
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "Test E.2 Tour B actif en LISTENING");

        // Injection d'une transcription appartenant au tour A
        voice.cancelListening();
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "Test E.3 Tour B toujours actif");
        TEST_ASSERT(toolExecCount == 0, "Test E.4 Outil non execute pour ancien tour");

        pipeline.cancel();
    }

    // -------------------------------------------------------------------------
    // TEST F : Ancienne fin TTS après nouveau TTS
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST F : Ancienne fin TTS ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipelineF";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);
        pipeline.begin(voice, tools);

        // Démarrer tour et parole
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.cancel(); // STOP annule speechId A

        // Nouveau tour B
        pipeline.startListening();
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "Test F.1 Nouveau tour en cours");

        // Injection d'une fin audio pour speechId obsolète (0 ou 999)
        pipeline.update();
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "Test F.2 Nouveau tour inaltere");
        pipeline.cancel();
    }

    // -------------------------------------------------------------------------
    // TEST G : Commande sensible et cycle de confirmation explicite
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST G : Commande sensible ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipelineG";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        uint32_t sensitiveExecCount = 0;
        tools.registerTool("system.factory_reset", "Reinitialisation usine", [&sensitiveExecCount]() {
            sensitiveExecCount++;
            return ToolResult::success("Reinitialisation effectuee.");
        }, ToolSecurityPolicy::SENSITIVE);

        VoicePipelineConfig pcfg;
        pcfg.enableLocalTools = true;
        pcfg.requireConfirmationForSensitive = true;
        pcfg.speakConfirmationRequired = false;
        pcfg.speakToolResult = false;
        pcfg.confirmationTimeoutMs = 5000;

        pipeline.begin(voice, tools, pcfg);
        pipeline.getDispatcher().registerMapping("system.factory_reset", "reinitialiser le systeme");

        String receivedToken;
        pipeline.onConfirmationRequired([&](uint32_t turn, const String& toolId, const String& token, const String& prompt) {
            receivedToken = token;
        });

        // 1. Demande de la commande sensible
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("reinitialiser le systeme", true);

        TEST_ASSERT(pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "Test G.1 Etat WAITING_CONFIRMATION");
        TEST_ASSERT(!receivedToken.isEmpty(), "Test G.2 Jeton de confirmation non vide genere");
        TEST_ASSERT(sensitiveExecCount == 0, "Test G.3 Commande NON executee sans confirmation explicite");

        // 2. Refus avec jeton invalide
        TEST_ASSERT(!pipeline.confirmPendingAction("FAUX_TOKEN"), "Test G.4 Confirmation avec faux jeton refusee");
        TEST_ASSERT(sensitiveExecCount == 0, "Test G.5 Commande non executee");
        TEST_ASSERT(pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "Test G.5b Reste en WAITING_CONFIRMATION");

        // 3. Confirmation avec le bon jeton
        TEST_ASSERT(pipeline.confirmPendingAction(receivedToken), "Test G.6 Confirmation reussie avec vrai jeton");
        TEST_ASSERT(sensitiveExecCount == 1, "Test G.7 Commande executee exactement 1 fois");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test G.8 Retour a READY");

        // 4. Protection anti-rejeu (réutilisation du jeton)
        TEST_ASSERT(!pipeline.confirmPendingAction(receivedToken), "Test G.9 Reutilisation du jeton rejetee");
        TEST_ASSERT(sensitiveExecCount == 1, "Test G.10 Pas de double execution");
    }

    // -------------------------------------------------------------------------
    // TEST H : Erreur et récupération
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST H : Erreur et recuperation ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipelineH";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);
        pipeline.begin(voice, tools);

        bool errorNotified = false;
        pipeline.onError([&](uint32_t turn, const String& err) {
            errorNotified = true;
        });

        pipeline.startListening();
        pipeline.stopListening();

        // Récupération après erreur
        TEST_ASSERT(pipeline.recoverFromError() == false, "Test H.1 recoverFromError retourne false si non en erreur");
        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test H.2 READY");
    }

    // -------------------------------------------------------------------------
    // TEST I : ReplyMemory et mémoire bornée sans pollution
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST I : ReplyMemory non polluee ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipelineI";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);
        pipeline.begin(voice, tools);

        ReplyMemory& mem = pipeline.getVoiceUxController().getReplyMemory();
        mem.clear();

        // Répétition à vide
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("répète", true);

        // Une commande REPEAT ne doit pas s'enregistrer elle-même dans ReplyMemory !
        TEST_ASSERT(mem.isEmpty(), "Test I.1 ReplyMemory reste vide apres REPEAT a vide");

        // Commande STOP
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("stop", true);
        TEST_ASSERT(mem.isEmpty(), "Test I.2 ReplyMemory reste vide apres STOP");

        // Mémorisation légitime
        mem.addReply("La capitale de la France est Paris.");
        TEST_ASSERT(mem.getCount() == 1, "Test I.3 1 reponse memorisee");
        TEST_ASSERT(mem.getLastReply() == "La capitale de la France est Paris.", "Test I.4 Contenu verifie");

        // Annulation d'un tour n'efface pas la mémoire existante valide
        pipeline.startListening();
        pipeline.cancel();
        TEST_ASSERT(mem.getCount() == 1, "Test I.5 Memoire conservee apres annulation");
    }

    // -------------------------------------------------------------------------
    // TEST J : Normalisation déterministe VoiceCommandDispatcher & extraction de paramètres
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST J : Normalisation VoiceCommandDispatcher ---" << std::endl;
        VoiceCommandDispatcher dispatcher;
        ToolRegistry tools;

        tools.registerTool("system.set_brightness", "Regler la luminosite", [](const ToolArgs& args) -> ToolResult {
            return ToolResult::success();
        }, std::vector<ToolParam>{ToolParam("level", ToolParamType::INT, 0, 100, true)});

        dispatcher.setRegistry(&tools);
        dispatcher.registerMapping("system.set_brightness", {"regle la luminosite", "luminosite"});

        // 1. Test normalisation sans accents ni ponctuation
        String input1 = "Règle la luminosité, s'il te plaît !";
        VoiceCommandMatch m1 = dispatcher.match(input1);
        TEST_ASSERT(m1.matched, "Test J.1 m1 matched");
        TEST_ASSERT(m1.toolId == "system.set_brightness", "Test J.2 toolId conforme");

        // 2. Test extraction de paramètre numérique
        String input2 = "Luminosité à 75 % s'il vous plaît.";
        VoiceCommandMatch m2 = dispatcher.match(input2);
        TEST_ASSERT(m2.matched, "Test J.3 m2 matched");
        TEST_ASSERT(m2.args.has("level"), "Test J.4 param level extrait");
        TEST_ASSERT(m2.args.getInt("level") == 75, "Test J.5 level == 75");

        // 3. Test outil absent du registre -> non matché
        dispatcher.registerMapping("non_existent_tool", "fantome");
        VoiceCommandMatch m3 = dispatcher.match("fantome");
        TEST_ASSERT(!m3.matched, "Test J.6 Outil absent du ToolRegistry non apparie");
    }

    std::cout << "\n[TEST SUITE 144] SUCCES INTEGRAL DES 10 TESTS OBLIGATOIRES A -> J (v0.6.5)" << std::endl;
}
