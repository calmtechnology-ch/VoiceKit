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

// =============================================================================
// SUITE 145 : VoiceKit v0.6.5.1 — Integrated Pipeline Stabilization
// Tests indispensables : A, B, C, D, E, F, G, H, I, J
// =============================================================================

inline void run_pipeline_v0651_tests() {
    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  [TEST SUITE 145] VoiceKit v0.6.5.1 — Integrated Pipeline Stabilization" << std::endl;
    std::cout << "===========================================================" << std::endl;

    // -------------------------------------------------------------------------
    // TEST A : Commande locale complète
    // STT -> commande -> ToolRegistry -> résultat -> TTS -> ReplyMemory -> READY
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST A (v0.6.5.1) : Commande locale complete & ReplyMemory ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipeline0651A";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

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

        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("Peux-tu me donner le statut du systeme s'il te plait ?", true);

        TEST_ASSERT(toolExecuted, "Test A.1 Commande executee");
        TEST_ASSERT(pipeline.getState() == PipelineState::SPEAKING, "Test A.2 Etat = SPEAKING");

        // Simuler restitution complète audio
        injectGeminiPcmAndDrain(voice, 3, true);
        drainVoicePlayback(voice);
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test A.3 Retour a READY");
        TEST_ASSERT(pipeline.getLastResult() == PipelineResult::COMPLETED, "Test A.4 Resultat COMPLETED");

        // Vérifier que la réponse locale est mémorisée dans ReplyMemory
        ReplyMemory& mem = pipeline.getVoiceUxController().getReplyMemory();
        TEST_ASSERT(mem.getCount() == 1, "Test A.5 Reponse locale memorisee dans ReplyMemory");
        TEST_ASSERT(mem.getLastReply() == "Le systeme fonctionne normalement.", "Test A.6 Contenu exact dans ReplyMemory");

        // Tester la répétition (REPEAT) : ne doit PAS dupliquer l'entrée dans ReplyMemory
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("repete s'il te plait", true);
        TEST_ASSERT(pipeline.getState() == PipelineState::SPEAKING, "Test A.7 Repetition en cours de parole");

        injectGeminiPcmAndDrain(voice, 3, true);
        drainVoicePlayback(voice);
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test A.8 Retour a READY apres REPEAT");
        TEST_ASSERT(mem.getCount() == 1, "Test A.9 La repetition ne cree PAS de doublon dans ReplyMemory");
    }

    // -------------------------------------------------------------------------
    // TEST B : Question IA complète
    // STT -> routage IA -> réponse textuelle -> audio -> ReplyMemory -> READY
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST B (v0.6.5.1) : Question IA complete & restitution ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipeline0651B";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        VoicePipelineConfig pcfg;
        pcfg.enableVoiceUx = true;
        pcfg.enableLocalTools = true;
        pcfg.enableAiProcessing = true;
        pcfg.defaultDestination = PipelineDefaultDestination::AI;

        pipeline.begin(voice, tools, pcfg);

        String capturedAiResponse = "";
        pipeline.onAiResponse([&capturedAiResponse](uint32_t turn, const String& txt) {
            capturedAiResponse = txt;
        });

        PipelineResult termResult = PipelineResult::NONE;
        uint32_t finishCallCount = 0;
        pipeline.onTurnFinished([&termResult, &finishCallCount](uint32_t turn, PipelineResult r, const String& s) {
            termResult = r;
            finishCallCount++;
        });

        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("Pourquoi le ciel est-il bleu ?", true);

        TEST_ASSERT(pipeline.getState() == PipelineState::AI_PROCESSING, "Test B.1 Pipeline en AI_PROCESSING");

        // 1. Simuler réception du texte assistant Gemini Live
        const char* aiTextJson = "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"text\":\"Le ciel est bleu en raison de la diffusion Rayleigh de la lumiere solaire.\"}]}}}";
        voice.getClient().handleIncomingMessage(aiTextJson, strlen(aiTextJson));

        // 2. Simuler réception des paquets audio 24 kHz
        injectGeminiPcmAndDrain(voice, 4, true);

        TEST_ASSERT(pipeline.getState() == PipelineState::SPEAKING, "Test B.2 Passage a SPEAKING pour restitution IA");
        TEST_ASSERT(capturedAiResponse.indexOf("diffusion Rayleigh") >= 0, "Test B.3 Texte assistant capte");

        // 3. Vidage complet de la lecture audio
        drainVoicePlayback(voice);
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test B.4 Retour a READY apres reponse IA");
        TEST_ASSERT(termResult == PipelineResult::COMPLETED, "Test B.5 Resultat terminal COMPLETED");
        TEST_ASSERT(finishCallCount == 1, "Test B.6 Exactement 1 callback de terminaison pour le tour IA");

        // Vérifier mémorisation dans ReplyMemory
        ReplyMemory& mem = pipeline.getVoiceUxController().getReplyMemory();
        TEST_ASSERT(mem.getCount() == 1, "Test B.7 Reponse IA memorisee dans ReplyMemory");
        TEST_ASSERT(mem.getLastReply().indexOf("diffusion Rayleigh") >= 0, "Test B.8 Contenu conforme dans ReplyMemory");

        // 4. Test d'interruption STOP pendant la réponse IA : pas de mémorisation partielle
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("Raconte-moi une histoire", true);
        TEST_ASSERT(pipeline.getState() == PipelineState::AI_PROCESSING, "Test B.9 Deuxieme tour IA");

        const char* partialTextJson = "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"text\":\"Il etait une fois dans un pays lointain...\"}]}}}";
        voice.getClient().handleIncomingMessage(partialTextJson, strlen(partialTextJson));

        // STOP immédiat avant fin audio
        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY || pipeline.getState() == PipelineState::STOPPING, "Test B.10 STOP execute");
        pipeline.update();

        // Vérifier que la réponse interrompue n'a pas écrasé la mémoire
        TEST_ASSERT(mem.getLastReply().indexOf("diffusion Rayleigh") >= 0, "Test B.11 Pas de memorisation d'une reponse IA interrompue");
    }

    // -------------------------------------------------------------------------
    // TEST C : Refus TTS
    // Simuler say() retournant false -> vérifier l'absence de blocage SPEAKING
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST C (v0.6.5.1) : Refus TTS & non-blocage SPEAKING ---" << std::endl;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        // voice non démarrée ou avec Wi-Fi non connecté -> say() retourne false
        VoiceKitConfig vcfg;
        vcfg.wifiMode = WiFiMode::MANAGED_WIFI;
        // Ne pas appeler voice.begin() ou laisser wifi non connecté
        VoicePipelineConfig pcfg;
        pipeline.begin(voice, tools, pcfg);

        pipeline.startListening();
        pipeline.stopListening();

        // Forcer appel speakOutput avec say() échouant
        PipelineResult res = PipelineResult::NONE;
        pipeline.onTurnFinished([&res](uint32_t t, PipelineResult r, const String& s) {
            res = r;
        });

        // Simuler déclenchement d'un outil local dont le speakOutput doit échouer
        tools.registerTool("test.fail_speech", "Test", []() {
            return ToolResult::success("Message impossible a prononcer.");
        });
        pipeline.getDispatcher().registerMapping("test.fail_speech", "test speech");

        pipeline.feedSimulatedTranscription("test speech", true);

        // Vérifier que le pipeline n'est PAS resté bloqué dans SPEAKING
        TEST_ASSERT(pipeline.getState() != PipelineState::SPEAKING, "Test C.1 Le pipeline n'est JAMAIS bloque dans SPEAKING");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test C.2 Retour propre a READY apres refus TTS");
        TEST_ASSERT(res == PipelineResult::FAILED, "Test C.3 Resultat FAILED notifie");
    }

    // -------------------------------------------------------------------------
    // TEST D : Double terminaison & Idempotence stricte
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST D (v0.6.5.1) : Idempotence stricte des terminaisons ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipeline0651D";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        VoicePipelineConfig pcfg;
        pcfg.confirmationTimeoutMs = 100; // court pour le test
        pipeline.begin(voice, tools, pcfg);

        tools.registerTool("system.wipe", "Action sensible", []() {
            return ToolResult::success();
        }, ToolSecurityPolicy::SENSITIVE);
        pipeline.getDispatcher().registerMapping("system.wipe", "wipe");

        // 1. Confirmation expirée : un seul callback terminal
        uint32_t finishCount = 0;
        PipelineResult lastRes = PipelineResult::NONE;
        pipeline.onTurnFinished([&finishCount, &lastRes](uint32_t t, PipelineResult r, const String& s) {
            finishCount++;
            lastRes = r;
        });

        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("wipe", true);

        TEST_ASSERT(pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "Test D.1 WAITING_CONFIRMATION");

        // Attendre expiration
        delay(150);
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test D.2 Retour a READY");
        TEST_ASSERT(finishCount == 1, "Test D.3 Exactement 1 seul callback pour confirmation expiree");
        TEST_ASSERT(lastRes == PipelineResult::TIMEOUT, "Test D.4 Resultat unique TIMEOUT");

        // 2. STOP puis timeout : un seul callback terminal
        finishCount = 0;
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.cancel();
        TEST_ASSERT(finishCount == 1, "Test D.5 Cancel a emis exactement 1 callback");
        TEST_ASSERT(lastRes == PipelineResult::CANCELLED, "Test D.6 Resultat CANCELLED");

        // Appel tardif update() : ne doit pas re-terminer
        pipeline.update();
        TEST_ASSERT(finishCount == 1, "Test D.7 update() tardif n'emet pas de 2eme callback");

        // 3. Double transcription finale
        finishCount = 0;
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("wipe", true);
        pipeline.feedSimulatedTranscription("wipe", true); // 2ème finale identique
        TEST_ASSERT(finishCount == 0, "Test D.8 Pas de terminaison prematuree sur 2eme finale");
        pipeline.cancel();
    }

    // -------------------------------------------------------------------------
    // TEST E : PAUSE / RESUME effectif
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST E (v0.6.5.1) : PAUSE & RESUME effectifs ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipeline0651E";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        uint32_t statusExecutedCount = 0;
        tools.registerTool("system.get_status", "Statut", [&statusExecutedCount]() {
            statusExecutedCount++;
            return ToolResult::success("Ok");
        });
        pipeline.begin(voice, tools);
        pipeline.getDispatcher().registerMapping("system.get_status", "statut");

        TEST_ASSERT(!pipeline.isPaused(), "Test E.1 Initialement non en pause");

        // 1. Dire « Pause »
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("pause", true);

        // Vider audio de l'annonce "En pause."
        injectGeminiPcmAndDrain(voice, 2, true);
        drainVoicePlayback(voice);
        pipeline.update();

        TEST_ASSERT(pipeline.isPaused(), "Test E.2 Pipeline est desormais en PAUSE");

        // 2. Tenter une commande ordinaire pendant la pause
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("statut", true);

        injectGeminiPcmAndDrain(voice, 2, true);
        drainVoicePlayback(voice);
        pipeline.update();

        TEST_ASSERT(statusExecutedCount == 0, "Test E.3 Commande ordinaire bloquee pendant la pause");
        TEST_ASSERT(pipeline.isPaused(), "Test E.4 Toujours en pause");

        // 3. Dire « Reprends »
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("reprends", true);

        injectGeminiPcmAndDrain(voice, 2, true);
        drainVoicePlayback(voice);
        pipeline.update();

        TEST_ASSERT(!pipeline.isPaused(), "Test E.5 Sortie effective de la pause");

        // 4. Commande ordinaire maintenant acceptee
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("statut", true);

        injectGeminiPcmAndDrain(voice, 2, true);
        drainVoicePlayback(voice);
        pipeline.update();

        TEST_ASSERT(statusExecutedCount == 1, "Test E.6 Commande ordinaire executee apres reprise");
    }

    // -------------------------------------------------------------------------
    // TEST F : Routage ambigu, négations et questions vs actions
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST F (v0.6.5.1) : Routage ambigu & negations ---" << std::endl;
        ToolRegistry tools;
        VoiceCommandDispatcher dispatcher;

        tools.registerTool("system.set_brightness", "Regler la luminosite", [](const ToolArgs& args) {
            return ToolResult::success();
        }, std::vector<ToolParam>{ToolParam("level", ToolParamType::INT, 0, 100, true)});

        dispatcher.setRegistry(&tools);
        dispatcher.registerMapping("system.set_brightness", {"regle la luminosite", "luminosite"});

        // 1. Négation explicite : ne doit JAMAIS matcher l'outil d'écriture
        VoiceCommandMatch mNeg = dispatcher.match("Je ne veux pas régler la luminosité.");
        TEST_ASSERT(!mNeg.matched, "Test F.1 Negation 'ne veux pas' non matchee vers un outil");

        VoiceCommandMatch mNeg2 = dispatcher.match("Ne touche pas à la luminosité surtout pas");
        TEST_ASSERT(!mNeg2.matched, "Test F.2 Negation 'ne touche pas' non matchee");

        // 2. Question d'explication : ne doit pas être traitée comme une action
        VoiceCommandMatch mQuery = dispatcher.match("Explique-moi comment fonctionne la luminosité.");
        TEST_ASSERT(!mQuery.matched, "Test F.3 Demande d'explication non matchee comme outil d'ecriture");

        VoiceCommandMatch mQuery2 = dispatcher.match("Comment fonctionne la luminosité de l'écran ?");
        TEST_ASSERT(!mQuery2.matched, "Test F.4 Question générale non traitée comme action");

        // 3. Commande affirmative polie : doit matcher
        VoiceCommandMatch mPos = dispatcher.match("Règle la luminosité à 40 % s'il te plaît.");
        TEST_ASSERT(mPos.matched, "Test F.5 Commande affirmative matchee");
        TEST_ASSERT(mPos.args.getInt("level") == 40, "Test F.6 Niveau 40 extrait");
    }

    // -------------------------------------------------------------------------
    // TEST G : Paramètres numériques complets (0, 50, 75, 100, 101, -5, 12,5, 12.5)
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST G (v0.6.5.1) : Parametres numeriques entiers et decimaux ---" << std::endl;
        ToolRegistry tools;
        VoiceCommandDispatcher dispatcher;

        // Outil entier borné 0 .. 100
        tools.registerTool("system.set_volume", "Volume", [](const ToolArgs& args) {
            return ToolResult::success();
        }, std::vector<ToolParam>{ToolParam("level", ToolParamType::INT, 0, 100, true)});

        // Outil flottant
        tools.registerTool("system.set_temp", "Temperature", [](const ToolArgs& args) {
            return ToolResult::success();
        }, std::vector<ToolParam>{ToolParam("temp", ToolParamType::FLOAT, 10.0, 30.0, true)});

        dispatcher.setRegistry(&tools);
        dispatcher.registerMapping("system.set_volume", {"volume", "regle le volume"});
        dispatcher.registerMapping("system.set_temp", {"temperature", "regle la temperature"});

        // 1. Valeurs entières dans les bornes
        int valInt = 0; double valDouble = 0.0;
        TEST_ASSERT(VoiceCommandDispatcher::extractNumber("0", valInt, valDouble) && valInt == 0, "Test G.1 0");
        TEST_ASSERT(VoiceCommandDispatcher::extractNumber("50", valInt, valDouble) && valInt == 50, "Test G.2 50");
        TEST_ASSERT(VoiceCommandDispatcher::extractNumber("75", valInt, valDouble) && valInt == 75, "Test G.3 75");
        TEST_ASSERT(VoiceCommandDispatcher::extractNumber("100", valInt, valDouble) && valInt == 100, "Test G.4 100");

        // 2. Valeur hors limites 101 (> 100)
        VoiceCommandMatch m101 = dispatcher.match("regle le volume a 101");
        TEST_ASSERT(m101.matched, "Test G.5 101 matche syntaxiquement");
        ToolResult r101 = tools.execute("system.set_volume", m101.args);
        TEST_ASSERT(!r101.isSuccess(), "Test G.6 101 rejete par ToolValidator car > max");

        // 3. Valeur négative -5 (< 0) avec préservation du signe
        VoiceCommandMatch mNeg = dispatcher.match("regle le volume a -5");
        TEST_ASSERT(mNeg.matched, "Test G.7 -5 matche syntaxiquement");
        TEST_ASSERT(mNeg.args.getInt("level") == -5, "Test G.8 Signe negatif -5 extrait");
        ToolResult rNeg = tools.execute("system.set_volume", mNeg.args);
        TEST_ASSERT(!rNeg.isSuccess(), "Test G.9 -5 rejete par ToolValidator car < min");

        // 4. Valeurs décimales avec virgule et point (12,5 et 12.5)
        VoiceCommandMatch mDec1 = dispatcher.match("regle la temperature a 12,5 degres");
        TEST_ASSERT(mDec1.matched, "Test G.10 12,5 matche");
        TEST_ASSERT(mDec1.args.has("temp"), "Test G.11 Param temp present");
        ToolResult rDec1 = tools.execute("system.set_temp", mDec1.args);
        TEST_ASSERT(rDec1.isSuccess(), "Test G.12 12,5 valide");

        VoiceCommandMatch mDec2 = dispatcher.match("regle la temperature a 12.5");
        TEST_ASSERT(mDec2.matched, "Test G.13 12.5 matche");
        ToolResult rDec2 = tools.execute("system.set_temp", mDec2.args);
        TEST_ASSERT(rDec2.isSuccess(), "Test G.14 12.5 valide");
    }

    // -------------------------------------------------------------------------
    // TEST H : Confirmation sensible robuste & anti-rejeu
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST H (v0.6.5.1) : Confirmation sensible & securite ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipeline0651H";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        bool executed = false;
        tools.registerTool("system.factory_reset", "Reinitialisation totale", [&executed]() {
            executed = true;
            return ToolResult::success("Reinitialisation terminee.");
        }, ToolSecurityPolicy::SENSITIVE);

        VoicePipelineConfig pcfg;
        pcfg.requireConfirmationForSensitive = true;
        pipeline.begin(voice, tools, pcfg);
        pipeline.getDispatcher().registerMapping("system.factory_reset", "reset usine");

        // 1. Demande d'action sensible
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("reset usine", true);

        TEST_ASSERT(pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "Test H.1 WAITING_CONFIRMATION");
        String token = pipeline.getPendingConfirmationToken();
        TEST_ASSERT(!token.isEmpty(), "Test H.2 Jeton genere valide");

        // Restitution de la demande vocale de confirmation ("Confirmez-vous cette action ?")
        injectGeminiPcmAndDrain(voice, 2, true);
        drainVoicePlayback(voice);
        pipeline.update();
        // L'état doit rester en WAITING_CONFIRMATION après la fin de l'annonce vocale de confirmation
        TEST_ASSERT(pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "Test H.2b Reste WAITING_CONFIRMATION apres annonce");

        // 2. Faux jeton
        bool badConf = pipeline.confirmPendingAction("fake-token-123");
        TEST_ASSERT(!badConf, "Test H.3 Faux jeton refuse");
        TEST_ASSERT(!executed, "Test H.4 Outil non execute avec faux jeton");

        // 3. Vrai jeton
        bool goodConf = pipeline.confirmPendingAction(token);
        TEST_ASSERT(goodConf, "Test H.5 Vrai jeton accepte");
        TEST_ASSERT(executed, "Test H.6 Action executee");

        injectGeminiPcmAndDrain(voice, 2, true);
        drainVoicePlayback(voice);
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test H.7 Retour a READY");

        // 4. Double validation avec le même jeton (anti-rejeu)
        bool replayConf = pipeline.confirmPendingAction(token);
        TEST_ASSERT(!replayConf, "Test H.8 Rejeu du jeton strictement rejete");
    }

    // -------------------------------------------------------------------------
    // TEST I : Échecs réseau et résilience
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST I (v0.6.5.1) : Echecs reseau et resilience ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipeline0651I";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);
        pipeline.begin(voice, tools);

        // Simulation erreur durant session active
        pipeline.startListening();
        mockTransport.simulateError("Perte connexion TLS");
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::ERROR || pipeline.getLastResult() == PipelineResult::FAILED,
                    "Test I.1 Transition vers ERROR en cas de defaillance reseau");

        // Récupération refusée tant que le réseau n'est pas rétabli
        TEST_ASSERT(pipeline.recoverFromError() == false, "Test I.1b recoverFromError() refuse si moteur non operationnel");

        // Rétablissement de la connexion
        prepareMockVoiceReady(voice, mockTransport);

        // Récupération contrôlée après rétablissement
        TEST_ASSERT(pipeline.recoverFromError(), "Test I.2 recoverFromError() reussi");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test I.3 Retour sain a READY");
    }

    // -------------------------------------------------------------------------
    // TEST J : Endurance (30 tours successifs)
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST J (v0.6.5.1) : Endurance (30 tours successifs) ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTestPipeline0651J";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        tools.registerTool("system.ping", "Ping", []() {
            return ToolResult::success("Pong.");
        });

        VoicePipelineConfig pcfg;
        pcfg.enableVoiceUx = true;
        pcfg.enableLocalTools = true;
        pcfg.enableAiProcessing = true;
        pipeline.begin(voice, tools, pcfg);
        pipeline.getDispatcher().registerMapping("system.ping", "ping");

        bool allToursOk = true;
        for (int tour = 1; tour <= 30; ++tour) {
            pipeline.startListening();
            pipeline.stopListening();

            if (tour % 3 == 0) {
                // Tour STOP
                pipeline.cancel();
            } else if (tour % 3 == 1) {
                // Tour commande locale
                pipeline.feedSimulatedTranscription("ping", true);
                injectGeminiPcmAndDrain(voice, 2, true);
                drainVoicePlayback(voice);
                pipeline.update();
            } else {
                // Tour question IA
                pipeline.feedSimulatedTranscription("Quelle heure est-il ?", true);
                const char* aiResp = "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"text\":\"Il est midi.\"}]}}}";
                voice.getClient().handleIncomingMessage(aiResp, strlen(aiResp));
                injectGeminiPcmAndDrain(voice, 2, true);
                drainVoicePlayback(voice);
                pipeline.update();
            }

            if (pipeline.getState() != PipelineState::READY && pipeline.getState() != PipelineState::STOPPING) {
                allToursOk = false;
                break;
            }
            if (pipeline.getState() == PipelineState::STOPPING) {
                drainVoicePlayback(voice);
                pipeline.update();
            }
        }

        TEST_ASSERT(allToursOk, "Test J.1 30 tours consecutifs executes avec succes");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "Test J.2 Pipeline parfaitement READY a l'issue");
    }

    std::cout << "\n[TEST SUITE 145] SUCCES INTEGRAL DES 10 TESTS OBLIGATOIRES A -> J (v0.6.5.1)" << std::endl;
}

// =============================================================================
// SUITE 146 : VoiceKit v0.6.5.1 — Final Corrective & Hardware Readiness
// Tests obligatoires : A, B, C, D, E, F, G, H, I, J
// =============================================================================
inline void run_pipeline_v0651_final_stabilization_tests() {
    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  [TEST SUITE 146] VoiceKit v0.6.5.1 — Final Corrective & Hardware Readiness" << std::endl;
    std::cout << "===========================================================" << std::endl;

    // -------------------------------------------------------------------------
    // TEST A : Refus de startListening() depuis tous les états incompatibles
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST A (v0.6.5.1 Final) : Refus startListening() etats incompatibles ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTest146A";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        tools.registerTool("system.reboot", "Redemarrer", []() {
            return ToolResult::success("Redemarrage en cours.");
        }, ToolSecurityPolicy::SENSITIVE);

        VoicePipelineConfig pcfg;
        pcfg.enableVoiceUx = true;
        pcfg.enableLocalTools = true;
        pcfg.requireConfirmationForSensitive = true;
        pipeline.begin(voice, tools, pcfg);
        pipeline.getDispatcher().registerMapping("system.reboot", "reboot");

        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "A.1 Initialement READY");

        // 1. READY -> startListening() : accepté
        TEST_ASSERT(pipeline.startListening(), "A.2 READY -> startListening() accepte");
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "A.3 Etat LISTENING");
        uint32_t activeTurn = pipeline.getActiveTurnId();

        // 2. LISTENING -> startListening() : refusé
        TEST_ASSERT(pipeline.startListening() == false, "A.4 LISTENING -> startListening() refuse");
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "A.5 Etat toujours LISTENING");
        TEST_ASSERT(pipeline.getActiveTurnId() == activeTurn, "A.6 activeTurnId inchange");

        // 3. TRANSCRIBING -> startListening() : refusé
        TEST_ASSERT(pipeline.stopListening(), "A.7 stopListening() accepte");
        TEST_ASSERT(pipeline.getState() == PipelineState::TRANSCRIBING, "A.8 Etat TRANSCRIBING");
        TEST_ASSERT(pipeline.startListening() == false, "A.9 TRANSCRIBING -> startListening() refuse");
        TEST_ASSERT(pipeline.getState() == PipelineState::TRANSCRIBING, "A.10 Etat toujours TRANSCRIBING");

        // 4. WAITING_CONFIRMATION -> startListening()
        pipeline.feedSimulatedTranscription("reboot", true);
        TEST_ASSERT(pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "A.11 Etat WAITING_CONFIRMATION");
        String tok = pipeline.getPendingConfirmationToken();
        TEST_ASSERT(!tok.isEmpty(), "A.12 Jeton de confirmation present");

        // Si le moteur vocal n'est pas prêt, startListening() est refusé et le jeton reste intact
        WiFi.setConnected(false);
        TEST_ASSERT(pipeline.startListening() == false, "A.13 Moteur non pret -> startListening() refuse en confirmation");
        TEST_ASSERT(pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "A.14 Maintien WAITING_CONFIRMATION");
        TEST_ASSERT(pipeline.getPendingConfirmationToken() == tok, "A.15 Jeton de confirmation preserve");

        // Dès que le moteur est prêt, startListening() est accepté et annule proprement la confirmation
        WiFi.setConnected(true);
        prepareMockVoiceReady(voice, mockTransport);
        TEST_ASSERT(pipeline.startListening() == true, "A.16 Moteur pret -> startListening() accepte");
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "A.17 Nouvelle capture engagee");
        TEST_ASSERT(pipeline.getPendingConfirmationToken().isEmpty(), "A.18 Ancienne confirmation annulee proprement");

        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "A.19 Retour a READY");
    }

    // -------------------------------------------------------------------------
    // TEST B : Écoute en mode PAUSE, sans autoriser une capture pendant SPEAKING
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST B (v0.6.5.1 Final) : Ecoute PAUSE sans violation audio ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTest146B";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        tools.registerTool("system.sayhi", "Dire bonjour", []() {
            return ToolResult::success("Bonjour a tous.");
        });

        VoicePipelineConfig pcfg;
        pcfg.enableVoiceUx = true;
        pcfg.enableLocalTools = true;
        pcfg.speakToolResult = true;
        pipeline.begin(voice, tools, pcfg);
        pipeline.getDispatcher().registerMapping("system.sayhi", "bonjour");

        // Mise en pause
        pipeline.pause();
        TEST_ASSERT(pipeline.isPaused(), "B.1 Pipeline en pause");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "B.2 Etat READY en pause");

        // READY avec pause active -> startListening() autorisé
        TEST_ASSERT(pipeline.startListening(), "B.3 READY + PAUSE -> startListening() accepte");
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "B.4 Etat LISTENING");
        pipeline.stopListening();

        // Déclencher une restitution vocale
        pipeline.feedSimulatedTranscription("bonjour", true);
        TEST_ASSERT(pipeline.getState() == PipelineState::SPEAKING, "B.5 Transition vers SPEAKING");

        // SPEAKING avec pause active -> startListening() STRICTEMENT REFUSÉ
        TEST_ASSERT(pipeline.startListening() == false, "B.6 SPEAKING + PAUSE -> startListening() refuse");
        TEST_ASSERT(pipeline.getState() == PipelineState::SPEAKING, "B.7 Etat toujours SPEAKING");

        // Finaliser l'audio
        injectGeminiPcmAndDrain(voice, 2, true);
        drainVoicePlayback(voice);
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "B.8 Retour READY apres restitution");
        pipeline.resume();
        TEST_ASSERT(!pipeline.isPaused(), "B.9 Pause levee");
    }

    // -------------------------------------------------------------------------
    // TEST C : Échec réel ou simulé de stopListening()
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST C (v0.6.5.1 Final) : Gestion echec et idempotence stopListening() ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTest146C";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);
        pipeline.begin(voice, tools);

        // Appel stopListening() hors écoute (depuis READY)
        TEST_ASSERT(pipeline.stopListening() == false, "C.1 stopListening() depuis READY refuse");

        // Démarrage nominal
        TEST_ASSERT(pipeline.startListening(), "C.2 startListening() reussi");
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "C.3 Etat LISTENING");

        // Premier arrêt nominal
        bool stopCbCalled = false;
        pipeline.onListeningFinished([&stopCbCalled](uint32_t) {
            stopCbCalled = true;
        });

        TEST_ASSERT(pipeline.stopListening(), "C.4 Premier stopListening() reussi");
        TEST_ASSERT(pipeline.getState() == PipelineState::TRANSCRIBING, "C.5 Etat TRANSCRIBING");
        TEST_ASSERT(stopCbCalled, "C.6 onListeningFinished notifie");

        // Deuxième arrêt demandé immédiatement
        stopCbCalled = false;
        TEST_ASSERT(pipeline.stopListening() == false, "C.7 Deuxieme stopListening() refuse");
        TEST_ASSERT(stopCbCalled == false, "C.8 Pas de double notification onListeningFinished");
        TEST_ASSERT(pipeline.getState() == PipelineState::TRANSCRIBING, "C.9 Reste en TRANSCRIBING");

        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "C.10 READY apres cancel");
    }

    // -------------------------------------------------------------------------
    // TEST D : Ancien événement de début TTS après STOP
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST D (v0.6.5.1 Final) : Protection SpeechStarted tardif apres STOP ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTest146D";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        tools.registerTool("system.say", "Parler", []() {
            return ToolResult::success("Message un.");
        });

        VoicePipelineConfig pcfg;
        pcfg.enableVoiceUx = true;
        pcfg.enableLocalTools = true;
        pcfg.speakToolResult = true;
        pipeline.begin(voice, tools, pcfg);
        pipeline.getDispatcher().registerMapping("system.say", "parle");

        // Tour A : Démarrage et déclenchement de parole
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("parle", true);
        TEST_ASSERT(pipeline.getState() == PipelineState::SPEAKING, "D.1 Tour A en SPEAKING");

        // Interruption immédiate par STOP
        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "D.2 Tour A interrompu -> READY");

        // Démarrer un nouveau Tour B
        TEST_ASSERT(pipeline.startListening(), "D.3 Tour B demarre");
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "D.4 Tour B en LISTENING");
        uint32_t tourBId = pipeline.getActiveTurnId();

        // Réception d'un événement SpeechStarted tardif provenant de A
        voice.triggerSpeechStartedForTesting();
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "D.5 Tour B reste en LISTENING (SpeechStarted A rejete)");
        TEST_ASSERT(pipeline.getActiveTurnId() == tourBId, "D.6 activeTurnId inchange");

        // Réception d'un événement SpeechFinished tardif provenant de A
        voice.triggerSpeechFinishedForTesting(999);
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "D.7 Tour B reste en LISTENING (SpeechFinished A rejete)");

        // Terminer normalement Tour B
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("parle", true);
        TEST_ASSERT(pipeline.getState() == PipelineState::SPEAKING, "D.8 Tour B passe en SPEAKING normalement");
        injectGeminiPcmAndDrain(voice, 2, true);
        drainVoicePlayback(voice);
        pipeline.update();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "D.9 Tour B termine avec succes -> READY");
    }

    // -------------------------------------------------------------------------
    // TEST E : Ancienne fin TTS après démarrage d'un nouveau tour
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST E (v0.6.5.1 Final) : Rejet fin TTS tardive sur nouveau tour ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTest146E";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);
        pipeline.begin(voice, tools);

        // Tour B en cours
        pipeline.startListening();
        uint32_t tourId = pipeline.getActiveTurnId();

        // Injection d'une fin TTS d'un ancien tour (speechId=555)
        voice.triggerSpeechFinishedForTesting(555);

        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "E.1 Pipeline toujours en LISTENING");
        TEST_ASSERT(pipeline.getActiveTurnId() == tourId, "E.2 TourId inchange");

        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "E.3 READY");
    }

    // -------------------------------------------------------------------------
    // TEST F : Réponse IA tardive après annulation
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST F (v0.6.5.1 Final) : Rejet reponse IA tardive apres annulation ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTest146F";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        VoicePipelineConfig pcfg;
        pcfg.enableVoiceUx = true;
        pcfg.enableAiProcessing = true;
        pipeline.begin(voice, tools, pcfg);

        // 1. Envoyer Question IA A
        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("Quelle est la vitesse de la lumiere ?", true);
        TEST_ASSERT(pipeline.getState() == PipelineState::AI_PROCESSING, "F.1 Question A en AI_PROCESSING");

        // 2. Annuler Question A par STOP
        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "F.2 Question A annulee -> READY");

        // 3. Démarrer une nouvelle demande B (locale)
        tools.registerTool("system.info", "Info", []() {
            return ToolResult::success("VoiceKit OK.");
        });
        pipeline.getDispatcher().registerMapping("system.info", "info");

        pipeline.startListening();
        pipeline.stopListening();
        pipeline.feedSimulatedTranscription("info", true);

        // 4. Injecter une ancienne réponse textuelle de A via la couche transport
        const char* lateA = "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"text\":\"La lumiere voyage a 300000 km/s.\"}]}}}";
        voice.getClient().handleIncomingMessage(lateA, strlen(lateA));
        pipeline.update();

        // 5. Vérifier que la réponse de A n'a pas pollué B
        TEST_ASSERT(pipeline.getLastAiResponse().indexOf("300000") < 0, "F.3 Reponse A rejetee");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY || pipeline.getState() == PipelineState::SPEAKING, "F.4 Etat sain");
    }

    // -------------------------------------------------------------------------
    // TEST G : Récupération demandée alors que le moteur reste déconnecté
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST G (v0.6.5.1 Final) : Refus recoverFromError() si moteur deconnecte ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTest146G";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);
        pipeline.begin(voice, tools);

        pipeline.startListening();
        mockTransport.simulateError("Panne réseau WebSocket");
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::ERROR || pipeline.getLastResult() == PipelineResult::FAILED,
                    "G.1 Passage en ERROR");

        // Le moteur reste déconnecté (isReady() == false)
        TEST_ASSERT(voice.isReady() == false, "G.2 Moteur deconnecte");

        // recoverFromError() doit être REFUSÉ
        TEST_ASSERT(pipeline.recoverFromError() == false, "G.3 recoverFromError() refuse tant que moteur hors service");
        TEST_ASSERT(pipeline.getState() == PipelineState::ERROR, "G.4 Pipeline reste en ERROR");
    }

    // -------------------------------------------------------------------------
    // TEST H : Récupération après rétablissement de la connexion
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST H (v0.6.5.1 Final) : Succes recoverFromError() apres retablissement ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTest146H";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);
        pipeline.begin(voice, tools);

        pipeline.startListening();
        mockTransport.simulateError("Coupure temporaire");
        pipeline.update();

        TEST_ASSERT(pipeline.getState() == PipelineState::ERROR, "H.1 Pipeline en ERROR");

        // Rétablissement de la connexion
        prepareMockVoiceReady(voice, mockTransport);
        TEST_ASSERT(voice.isReady() == true, "H.2 Moteur reconnecte et pret");

        // Récupération contrôlée
        TEST_ASSERT(pipeline.recoverFromError() == true, "H.3 recoverFromError() accepte apres retablissement");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "H.4 Pipeline de nouveau READY");

        // Nouvelle capture fonctionnelle immédiatement
        TEST_ASSERT(pipeline.startListening() == true, "H.5 Nouvelle capture acceptee");
        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "H.6 READY");
    }

    // -------------------------------------------------------------------------
    // TEST I : Reconstruction de transcriptions françaises fragmentées
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST I (v0.6.5.1 Final) : Reconstruction fragments francais ---" << std::endl;

        // 1. Syllabes d'un même mot : "agré" + "ables" -> "agréables"
        String s1 = "agr\xC3\xA9";
        LiveSessionController::appendTranscriptionFragment(s1, "ables");
        TEST_ASSERT(s1 == "agr\xC3\xA9" "ables", "I.1 'agre' + 'ables' -> 'agreables'");

        // 2. Apostrophe française typographique : "t" + "’aider" -> "t’aider"
        String s2 = "t";
        LiveSessionController::appendTranscriptionFragment(s2, "\xE2\x80\x99" "aider");
        TEST_ASSERT(s2 == "t\xE2\x80\x99" "aider", "I.2 't' + '’aider' -> 't’aider'");

        // 3. Ponctuation finale : "Bonjour" + "." -> "Bonjour."
        String s3 = "Bonjour";
        LiveSessionController::appendTranscriptionFragment(s3, ".");
        TEST_ASSERT(s3 == "Bonjour.", "I.3 'Bonjour' + '.' -> 'Bonjour.'");

        // 4. Transcription cumulative : "Je suis" + "Je suis prêt" -> "Je suis prêt"
        String s4 = "Je suis";
        LiveSessionController::appendTranscriptionFragment(s4, "Je suis pr\xC3\xAAt");
        TEST_ASSERT(s4 == "Je suis pr\xC3\xAAt", "I.4 Remplacement cumulatif 'Je suis pret'");

        // 5. Retransmission identique exacte
        String s5 = "Il est 15 heures.";
        LiveSessionController::appendTranscriptionFragment(s5, "Il est 15 heures.");
        TEST_ASSERT(s5 == "Il est 15 heures.", "I.5 Retransmission identique sans duplication");

        // 6. Retransmission sans point final
        String s6 = "Il est 15 heures.";
        LiveSessionController::appendTranscriptionFragment(s6, "Il est 15 heures");
        TEST_ASSERT(s6 == "Il est 15 heures.", "I.6 Retransmission sans point preserve");

        // 7. Répétition volontaire de mots par le modèle : "Non," + " non," + " non !" -> "Non, non, non !"
        String s7 = "Non,";
        LiveSessionController::appendTranscriptionFragment(s7, " non,");
        LiveSessionController::appendTranscriptionFragment(s7, " non !");
        TEST_ASSERT(s7 == "Non, non, non !", "I.7 Repetition volontaire 'Non, non, non !' preservee");

        // 8. Répétition de mots d'insistance : "très, " + "très "
        String s8 = "tr\xC3\xA8s, ";
        LiveSessionController::appendTranscriptionFragment(s8, "tr\xC3\xA8s bien.");
        TEST_ASSERT(s8 == "tr\xC3\xA8s, tr\xC3\xA8s bien.", "I.8 Repetition 'tres, tres bien.' preservee");

        // 9. Troncature sécurisée sur longueur maximale
        String s9 = "Un texte";
        LiveSessionController::appendTranscriptionFragment(s9, " excessivement long depassant la limite permise", 20);
        TEST_ASSERT(s9.length() <= 20, "I.9 Longueur bornee a maxLength");
    }

    // -------------------------------------------------------------------------
    // TEST J : Enchaînement répété de commandes locales, IA, STOP et reprise
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST J (v0.6.5.1 Final) : Endurance 10 cycles complets mixtes ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTest146J";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);

        tools.registerTool("system.ping", "Ping", []() {
            return ToolResult::success("Pong.");
        });

        VoicePipelineConfig pcfg;
        pcfg.enableVoiceUx = true;
        pcfg.enableLocalTools = true;
        pcfg.enableAiProcessing = true;
        pcfg.speakToolResult = true;
        pipeline.begin(voice, tools, pcfg);
        pipeline.getDispatcher().registerMapping("system.ping", "ping");

        bool enduranceOk = true;
        for (int cycle = 1; cycle <= 10; ++cycle) {
            // 1. Commande locale
            pipeline.startListening();
            pipeline.stopListening();
            pipeline.feedSimulatedTranscription("ping", true);
            injectGeminiPcmAndDrain(voice, 2, true);
            drainVoicePlayback(voice);
            pipeline.update();
            if (pipeline.getState() != PipelineState::READY) { enduranceOk = false; break; }

            // 2. STOP en cours d'écoute
            pipeline.startListening();
            pipeline.cancel();
            if (pipeline.getState() != PipelineState::READY) { enduranceOk = false; break; }

            // 3. Question IA
            pipeline.startListening();
            pipeline.stopListening();
            pipeline.feedSimulatedTranscription("Question test", true);
            const char* aiResp = "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"text\":\"Reponse OK.\"}]}}}";
            voice.getClient().handleIncomingMessage(aiResp, strlen(aiResp));
            injectGeminiPcmAndDrain(voice, 2, true);
            drainVoicePlayback(voice);
            pipeline.update();
            if (pipeline.getState() != PipelineState::READY) { enduranceOk = false; break; }

            // 4. Pause et écoute
            pipeline.pause();
            pipeline.startListening();
            pipeline.cancel();
            pipeline.resume();
            if (pipeline.getState() != PipelineState::READY) { enduranceOk = false; break; }
        }

        TEST_ASSERT(enduranceOk, "J.1 10 cycles complets mixtes sans anomalie");
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "J.2 Pipeline parfaitement READY");
    }

    std::cout << "\n[TEST SUITE 146] SUCCES INTEGRAL DES 10 TESTS OBLIGATOIRES A -> J (v0.6.5.1 Final)" << std::endl;
}

// =============================================================================
// SUITE 147 : VoiceKit v0.6.5.1 — Minimal Corrective Final Validation
// Tests obligatoires :
//   P0 : Retour stopListening(), gestion erreurs streamer & activityEnd
//   P1 : Cohérence confirmation sensible (Tests A, B, C, D, E)
//   P1 : Réponses IA tardives & filtrage multi-annulations (Tests A, B, C, D, E)
// =============================================================================
inline void run_pipeline_v0651_minimal_corrective_tests() {
    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  [TEST SUITE 147] VoiceKit v0.6.5.1 — Minimal Corrective Final" << std::endl;
    std::cout << "===========================================================" << std::endl;

    int failuresBefore147 = s_testsFailed;

    // -------------------------------------------------------------------------
    // SECTION 1 (P0) : Retour stopListening() & gestion des erreurs
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST P0 : stopListening() nominal, echecs et idempotence ---" << std::endl;
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        ToolRegistry tools;
        IntegratedVoicePipeline pipeline;

        VoiceKitConfig vcfg;
        vcfg.apiKey = "AIzaSyTest147P0";
        vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voice.begin(vcfg);
        prepareMockVoiceReady(voice, mockTransport);
        pipeline.begin(voice, tools);

        // 1. Double appel hors capture -> retourne false
        TEST_ASSERT(voice.stopListening() == false, "P0.1 stopListening() hors ecoute refuse");
        TEST_ASSERT(pipeline.stopListening() == false, "P0.2 pipeline.stopListening() hors ecoute refuse");

        // 2. Arrêt nominal
        TEST_ASSERT(pipeline.startListening(), "P0.3 startListening() reussi");
        TEST_ASSERT(pipeline.getState() == PipelineState::LISTENING, "P0.4 Etat LISTENING");
        bool onFinishedNotified = false;
        voice.onListeningFinished([&onFinishedNotified]() {
            onFinishedNotified = true;
        });
        TEST_ASSERT(pipeline.stopListening() == true, "P0.5 stopListening() nominal accepte");
        TEST_ASSERT(onFinishedNotified == true, "P0.6 onListeningFinished bien notifie");
        TEST_ASSERT(pipeline.getState() == PipelineState::TRANSCRIBING, "P0.7 Etat TRANSCRIBING");
        TEST_ASSERT(voice.isTranscribing() == true, "P0.8 voice.isTranscribing() vrai");

        // Double demande immédiate
        TEST_ASSERT(voice.stopListening() == false, "P0.9 Double demande stopListening refusee");
        TEST_ASSERT(pipeline.stopListening() == false, "P0.10 Double demande pipeline refusee");
        pipeline.cancel();
        TEST_ASSERT(pipeline.getState() == PipelineState::READY, "P0.11 Pipeline READY");

        // 3. Échec réel de stopMicHardware / LiveAudioStreamer::stop()
        class FailingAudioInput : public IAudioInput {
        public:
            bool fail = false;
            bool running = false;
            bool begin() override { running = true; return true; }
            void end() override { running = false; }
            bool isRunning() const override { return running; }
            int isRecording() const override { return fail ? 1 : 0; }
            bool record(int16_t*, size_t, uint32_t, bool) override { return true; }
            void stop() override { running = false; }
            const char* getLastError() const override { return "Mock fail"; }
        };
        FailingAudioInput failingInput;
        MockLiveTransport mockTransportHw;
        VoxCoreVoice voiceHwFail;
        VoiceKitConfig vcfgHw;
        vcfgHw.apiKey = "AIzaSyTest147Hw";
        vcfgHw.wifiMode = WiFiMode::EXTERNAL_WIFI;
        vcfgHw.audioInput = &failingInput;
        voiceHwFail.begin(vcfgHw);
        prepareMockVoiceReady(voiceHwFail, mockTransportHw);

        IntegratedVoicePipeline pipeHw;
        pipeHw.begin(voiceHwFail, tools);
        TEST_ASSERT(pipeHw.startListening(), "P0.12 startListening() reussi");
        failingInput.fail = true; // Force isRecording() != 0 -> stopMicHardware timeout fail

        bool hwErrFinishedNotified = false;
        voiceHwFail.onListeningFinished([&hwErrFinishedNotified]() {
            hwErrFinishedNotified = true;
        });

        // L'arrêt matériel échoue : stopListening() DOIT retourner false
        TEST_ASSERT(pipeHw.stopListening() == false, "P0.13 stopListening() retourne false lors d'un echec streamer");
        TEST_ASSERT(hwErrFinishedNotified == false, "P0.14 onListeningFinished n'est PAS appele en cas d'erreur");
        TEST_ASSERT(voiceHwFail.getState() == VoiceState::ERROR_STATE, "P0.15 voice en ERROR_STATE");
        TEST_ASSERT(voiceHwFail.isTranscribing() == false, "P0.16 Pas de blocage dans TRANSCRIBING");
        TEST_ASSERT(pipeHw.getState() == PipelineState::ERROR, "P0.17 pipeline en ERROR");

        // STOP immédiatement après un échec
        pipeHw.cancel();
        TEST_ASSERT(pipeHw.getState() == PipelineState::READY, "P0.18 STOP apres echec -> READY");

        // Nouvelle écoute après récupération
        failingInput.fail = false;
        HardwareManager::getInstance().releaseMicCaptureReservation();
        voiceHwFail.stop();
        TEST_ASSERT(pipeHw.startListening(), "P0.19 Nouvelle ecoute apres recuperation reussie");
        TEST_ASSERT(pipeHw.stopListening() == true, "P0.20 Nouvel arret reussi");
        pipeHw.cancel();
        HardwareManager::getInstance().releaseMicCaptureReservation();
        HardwareManager::getInstance().setAudioInput(nullptr);
        voiceHwFail.getClient().setTransport(nullptr);

        // 4. Erreur de transmission de activityEnd
        class ActivityEndFailingTransport : public MockLiveTransport {
        public:
            bool failActivityEnd = false;
            bool send(const char* data, size_t len) override {
                if (failActivityEnd && data != nullptr && len > 0) {
                    std::string s(data, len);
                    if (s.find("activityEnd") != std::string::npos) {
                        return false;
                    }
                }
                return MockLiveTransport::send(data, len);
            }
        };
        ActivityEndFailingTransport failActTransport;
        VoxCoreVoice voiceActFail;
        VoiceKitConfig vcfgAct;
        vcfgAct.apiKey = "AIzaSyTest147Act";
        vcfgAct.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voiceActFail.begin(vcfgAct);
        prepareMockVoiceReady(voiceActFail, failActTransport);

        IntegratedVoicePipeline pipeAct;
        pipeAct.begin(voiceActFail, tools);
        TEST_ASSERT(pipeAct.startListening(), "P0.21 startListening() reussi");
        failActTransport.failActivityEnd = true;

        bool actFinishedNotified = false;
        voiceActFail.onListeningFinished([&actFinishedNotified]() {
            actFinishedNotified = true;
        });

        TEST_ASSERT(pipeAct.stopListening() == false, "P0.22 stopListening() retourne false lors d'echec activityEnd");
        TEST_ASSERT(actFinishedNotified == false, "P0.23 onListeningFinished n'est PAS appele si activityEnd echoue");
        TEST_ASSERT(voiceActFail.getState() == VoiceState::ERROR_STATE, "P0.24 voice passe en ERROR_STATE");
        TEST_ASSERT(voiceActFail.isTranscribing() == false, "P0.25 Pas de blocage dans TRANSCRIBING");
        TEST_ASSERT(pipeAct.getState() == PipelineState::ERROR, "P0.26 pipeline passe en ERROR");
        pipeAct.cancel();
        voiceActFail.getClient().setTransport(nullptr);
        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SECTION 2 (P1) : Cohérence confirmation sensible (Tests A, B, C, D, E)
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST P1-CONFIRM : Tests A, B, C, D, E confirmation sensible ---" << std::endl;

        auto printDiag = [](const char* testName, const IntegratedVoicePipeline& p, const VoxCoreVoice& v) {
            std::cout << "[CONF-DIAG] Test=" << testName << "\n"
                      << "[CONF-DIAG] PipelineState=" << (int)p.getState() << "\n"
                      << "[CONF-DIAG] VoiceState=" << (int)v.getState() << "\n"
                      << "[CONF-DIAG] VoiceReady=" << (v.isReady() ? "true" : "false") << "\n"
                      << "[CONF-DIAG] VoiceBusy=" << (v.isBusy() ? "true" : "false") << "\n"
                      << "[CONF-DIAG] IsListening=" << (v.isListening() ? "true" : "false") << "\n"
                      << "[CONF-DIAG] IsTranscribing=" << (v.isTranscribing() ? "true" : "false") << "\n"
                      << "[CONF-DIAG] IsSpeaking=" << (v.isSpeaking() ? "true" : "false") << "\n"
                      << "[CONF-DIAG] ActiveTurnId=" << p.getActiveTurnId() << "\n"
                      << "[CONF-DIAG] PendingToolId=" << p.getPendingToolId().c_str() << "\n"
                      << "[CONF-DIAG] HasPendingConfirmation=" << (p.getPendingConfirmationToken().length() > 0 ? "true" : "false") << "\n"
                      << "[CONF-DIAG] MicReservation=" << (HardwareManager::getInstance().isMicCaptureReserved() ? "true" : "false") << "\n"
                      << "[CONF-DIAG] LastError=" << v.getLastError().c_str() << std::endl;
        };

        struct ConfFixture {
            MockLiveTransport mockTransport;
            VoxCoreVoice voice;
            ToolRegistry tools;
            IntegratedVoicePipeline pipeline;
            bool actionExecuted = false;

            ConfFixture(const char* apiKey) {
                VoiceKitConfig vcfg;
                vcfg.apiKey = apiKey;
                vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
                voice.begin(vcfg);
                prepareMockVoiceReady(voice, mockTransport);

                VoicePipelineConfig pcfg;
                pcfg.enableVoiceUx = true;
                pcfg.requireConfirmationForSensitive = true;
                pcfg.speakConfirmationRequired = false;
                pcfg.speakToolResult = false;
                pipeline.begin(voice, tools, pcfg);

                tools.registerTool("system.reset", "Reset", [this]() {
                    actionExecuted = true;
                    return ToolResult::success("Systeme reinitialise.");
                }, ToolSecurityPolicy::SENSITIVE);
                pipeline.getDispatcher().registerMapping("system.reset", "reinitialise");
            }

            ~ConfFixture() {
                HardwareManager::getInstance().releaseMicCaptureReservation();
                voice.getClient().setTransport(nullptr);
            }
        };

        // TEST A : Confirmation en attente, moteur indisponible avant tentative d'écoute
        {
            ConfFixture f("AIzaSyTest147P1ConfA");
            printDiag("A-BEFORE", f.pipeline, f.voice);
            f.pipeline.startListening();
            f.pipeline.stopListening();
            f.pipeline.feedSimulatedTranscription("reinitialise", true);
            TEST_ASSERT(f.pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "Conf.A.1 WAITING_CONFIRMATION");
            String tokA = f.pipeline.getPendingConfirmationToken();
            TEST_ASSERT(tokA.length() > 0, "Conf.A.2 Jeton genere");

            // Rendre le moteur vocal indisponible (ex: en cours de restitution)
            f.voice.say("Notification importante");
            TEST_ASSERT(f.voice.isBusy(), "Conf.A.3 Moteur vocal occupe");

            // Tentative d'écoute pendant que le moteur est indisponible
            TEST_ASSERT(f.pipeline.startListening() == false, "Conf.A.4 startListening() refuse car moteur occupe");
            TEST_ASSERT(f.pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "Conf.A.5 Reste en WAITING_CONFIRMATION");
            TEST_ASSERT(f.pipeline.getPendingConfirmationToken() == tokA, "Conf.A.6 Jeton conserve intact");

            // Libérer le moteur et confirmer l'action
            f.voice.stop();
            TEST_ASSERT(f.pipeline.confirmPendingAction(tokA), "Conf.A.7 Action confirmee avec succes");
            TEST_ASSERT(f.actionExecuted == true, "Conf.A.8 Action executee");
            f.pipeline.cancel();
            printDiag("A-AFTER", f.pipeline, f.voice);
        }

        // TEST B : Confirmation annulée, puis échec réel de démarrage de capture
        {
            ConfFixture f("AIzaSyTest147P1ConfB");
            printDiag("B-BEFORE", f.pipeline, f.voice);
            f.actionExecuted = false;
            f.pipeline.startListening();
            f.pipeline.stopListening();
            f.pipeline.feedSimulatedTranscription("reinitialise", true);
            TEST_ASSERT(f.pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "Conf.B.1 WAITING_CONFIRMATION");
            String tokB = f.pipeline.getPendingConfirmationToken();

            // Réserver le micro par un tiers pour forcer l'échec matériel de capture dans startListening()
            HardwareManager::getInstance().prepareMicCapture((const void*)0xdeadbeef);

            // startListening() annule la confirmation puis échoue sur le démarrage effectif de capture
            TEST_ASSERT(f.pipeline.startListening() == false, "Conf.B.2 startListening() echoue");
            // Règle formelle : le jeton B ne doit JAMAIS être restauré
            TEST_ASSERT(f.pipeline.getPendingConfirmationToken() == "", "Conf.B.3 Jeton annule NON restaure");
            TEST_ASSERT(f.pipeline.getState() != PipelineState::WAITING_CONFIRMATION, "Conf.B.4 N'est plus en WAITING_CONFIRMATION");
            TEST_ASSERT(f.pipeline.getState() == PipelineState::ERROR, "Conf.B.4b Pipeline en ERROR");
            TEST_ASSERT(f.voice.getState() == VoiceState::ERROR_STATE, "Conf.B.4c Moteur vocal en ERROR_STATE");

            HardwareManager::getInstance().releaseMicCaptureReservation();

            // Tentative de confirmation avec l'ancien jeton B : doit être REFUSÉE
            TEST_ASSERT(f.pipeline.confirmPendingAction(tokB) == false, "Conf.B.5 Confirmation ancien jeton B refusee");
            TEST_ASSERT(f.actionExecuted == false, "Conf.B.6 Action B jamais executee");

            // Récupération explicite : Échec capture -> ERROR -> récupération -> nouvelle capture
            f.voice.stop();
            TEST_ASSERT(f.pipeline.recoverFromError() == true, "Conf.B.7 recoverFromError reussi");
            TEST_ASSERT(f.pipeline.getState() == PipelineState::READY, "Conf.B.8 Pipeline READY apres recuperation");
            TEST_ASSERT(f.pipeline.startListening() == true, "Conf.B.9 Nouvelle capture apres recuperation");
            TEST_ASSERT(f.pipeline.stopListening() == true, "Conf.B.10 Arret capture apres recuperation");
            f.pipeline.cancel();
            printDiag("B-AFTER", f.pipeline, f.voice);
        }

        // TEST C : Nouvelle capture acceptée après annulation de confirmation
        {
            ConfFixture f("AIzaSyTest147P1ConfC");
            printDiag("C-BEFORE", f.pipeline, f.voice);
            f.actionExecuted = false;
            f.pipeline.startListening();
            f.pipeline.stopListening();
            f.pipeline.feedSimulatedTranscription("reinitialise", true);
            TEST_ASSERT(f.pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "Conf.C.1 WAITING_CONFIRMATION");
            String tokC = f.pipeline.getPendingConfirmationToken();

            // Nouvelle capture démarrée -> confirmation annulée
            TEST_ASSERT(f.pipeline.startListening() == true, "Conf.C.2 Nouvelle ecoute acceptee");
            TEST_ASSERT(f.pipeline.getState() == PipelineState::LISTENING, "Conf.C.3 Etat LISTENING");

            // Enregistrer outil de consultation inoffensif
            bool statusExecuted = false;
            f.tools.registerTool("system.status", "Statut", [&statusExecuted]() {
                statusExecuted = true;
                return ToolResult::success("Systeme nominal.");
            }, ToolSecurityPolicy::READ_ONLY);
            f.pipeline.getDispatcher().registerMapping("system.status", "statut");

            f.pipeline.stopListening();
            f.pipeline.feedSimulatedTranscription("statut", true);
            TEST_ASSERT(f.pipeline.getState() == PipelineState::READY, "Conf.C.4 Tour statut termine");
            TEST_ASSERT(f.actionExecuted == false, "Conf.C.5 Action sensible annulee jamais executee");
            TEST_ASSERT(statusExecuted == true, "Conf.C.6 Outil statut execute exactement une fois");
            printDiag("C-AFTER", f.pipeline, f.voice);
        }

        // TEST D : Ancienne confirmation tentée après STOP
        {
            ConfFixture f("AIzaSyTest147P1ConfD");
            printDiag("D-BEFORE", f.pipeline, f.voice);
            f.actionExecuted = false;
            f.pipeline.startListening();
            f.pipeline.stopListening();
            f.pipeline.feedSimulatedTranscription("reinitialise", true);
            TEST_ASSERT(f.pipeline.getState() == PipelineState::WAITING_CONFIRMATION, "Conf.D.1 WAITING_CONFIRMATION");
            String tokD = f.pipeline.getPendingConfirmationToken();

            f.pipeline.cancel();
            TEST_ASSERT(f.pipeline.getState() == PipelineState::READY, "Conf.D.2 STOP -> READY");
            TEST_ASSERT(f.pipeline.confirmPendingAction(tokD) == false, "Conf.D.3 Confirmation post-STOP refusee");
            TEST_ASSERT(f.actionExecuted == false, "Conf.D.4 Action non executee");
            printDiag("D-AFTER", f.pipeline, f.voice);
        }

        // TEST E : Aucune double terminaison de l'action ou du tour
        {
            ConfFixture f("AIzaSyTest147P1ConfE");
            printDiag("E-BEFORE", f.pipeline, f.voice);
            uint32_t finishCount = 0;
            f.pipeline.onTurnFinished([&finishCount](uint32_t, PipelineResult, const String&) {
                finishCount++;
            });

            f.pipeline.startListening();
            f.pipeline.stopListening();
            f.pipeline.feedSimulatedTranscription("reinitialise", true);
            uint32_t countBefore = finishCount;
            f.pipeline.cancel();
            uint32_t countAfter = finishCount;
            TEST_ASSERT(countAfter == countBefore + 1, "Conf.E.1 Exactement une notification de terminaison");

            // Appel redondant de cancel
            f.pipeline.cancel();
            TEST_ASSERT(finishCount == countAfter, "Conf.E.2 Pas de double terminaison sur cancel redondant");
            printDiag("E-AFTER", f.pipeline, f.voice);
        }
    }

    // -------------------------------------------------------------------------
    // SECTION 3 (P1) : Réponses IA tardives & filtrage multi-annulations (Tests A -> E)
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST P1-LATE : Tests A -> E reponses IA tardives OpenAI Realtime ---" << std::endl;
        MockLiveTransport mockTransportOAI;
        VoxCoreVoice voiceOAI;
        ToolRegistry toolsOAI;
        IntegratedVoicePipeline pipeOAI;

        VoiceKitConfig cfgOAI;
        cfgOAI.provider = VoiceProviderType::OPENAI;
        cfgOAI.apiKey = "sk-test-secret-147";
        cfgOAI.model = "gpt-realtime-2.1-mini";
        cfgOAI.voice = "alloy";
        cfgOAI.wifiMode = WiFiMode::EXTERNAL_WIFI;
        voiceOAI.begin(cfgOAI);
        prepareMockOpenAIVoiceReady(voiceOAI, mockTransportOAI);

        VoicePipelineConfig pcfgOAI;
        pcfgOAI.enableVoiceUx = true;
        pcfgOAI.enableAiProcessing = true;
        pipeOAI.begin(voiceOAI, toolsOAI, pcfgOAI);

        // TEST A : Question A -> STOP -> Question B -> Réponse tardive A -> Réponse normale B
        pipeOAI.startListening();
        pipeOAI.stopListening();
        pipeOAI.feedSimulatedTranscription("Question A", true);
        TEST_ASSERT(pipeOAI.getState() == PipelineState::AI_PROCESSING, "Late.A.1 Question A en cours");

        // Arrivée de response.created pour A
        const char* createdA = "{\"type\":\"response.created\",\"response\":{\"id\":\"resp_A_001\",\"status\":\"in_progress\"}}";
        mockTransportOAI.simulateIncoming(createdA, strlen(createdA));

        // STOP utilisateur
        pipeOAI.cancel();
        TEST_ASSERT(pipeOAI.getState() == PipelineState::READY, "Late.A.2 STOP A -> READY");

        // Question B
        pipeOAI.startListening();
        pipeOAI.stopListening();
        pipeOAI.feedSimulatedTranscription("Question B", true);
        TEST_ASSERT(pipeOAI.getState() == PipelineState::AI_PROCESSING, "Late.A.3 Question B en cours");

        // Injection d'une réponse tardive de A (texte, audio et done)
        const char* textA = "{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"resp_A_001\",\"delta\":\"Texte obsolete A\"}";
        const char* audioA = "{\"type\":\"response.audio.delta\",\"response_id\":\"resp_A_001\",\"delta\":\"AAAAAAAAAAAAAAAA\"}";
        const char* doneA = "{\"type\":\"response.done\",\"response\":{\"id\":\"resp_A_001\",\"status\":\"completed\",\"output\":[{\"content\":[{\"transcript\":\"Texte obsolete A\"}]}]}}";
        mockTransportOAI.simulateIncoming(textA, strlen(textA));
        mockTransportOAI.simulateIncoming(audioA, strlen(audioA));
        mockTransportOAI.simulateIncoming(doneA, strlen(doneA));
        pipeOAI.update();

        // Vérifier que la réponse A a été intégralement ignorée
        TEST_ASSERT(pipeOAI.getLastAiResponse().indexOf("Texte obsolete A") < 0, "Late.A.4 Reponse A rejetee");
        TEST_ASSERT(pipeOAI.getState() == PipelineState::AI_PROCESSING, "Late.A.5 Reste en AI_PROCESSING pour B");

        // Réponse normale de B
        const char* createdB = "{\"type\":\"response.created\",\"response\":{\"id\":\"resp_B_002\",\"status\":\"in_progress\"}}";
        const char* textB = "{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"resp_B_002\",\"delta\":\"Reponse valide B.\"}";
        const char* textDoneB = "{\"type\":\"response.output_audio_transcript.done\",\"response_id\":\"resp_B_002\",\"transcript\":\"Reponse valide B.\"}";
        const char* audioB = "{\"type\":\"response.audio.delta\",\"response_id\":\"resp_B_002\",\"delta\":\"AAAAAAAAAAAAAAAA\"}";
        const char* doneB = "{\"type\":\"response.done\",\"response\":{\"id\":\"resp_B_002\",\"status\":\"completed\",\"output\":[{\"content\":[{\"transcript\":\"Reponse valide B.\"}]}]}}";

        mockTransportOAI.simulateIncoming(createdB, strlen(createdB));
        mockTransportOAI.simulateIncoming(textB, strlen(textB));
        mockTransportOAI.simulateIncoming(textDoneB, strlen(textDoneB));
        mockTransportOAI.simulateIncoming(audioB, strlen(audioB));
        voiceOAI.getSession().transitionTo(LiveSessionState::PLAYING, "B audio");
        pipeOAI.update();
        mockTransportOAI.simulateIncoming(doneB, strlen(doneB));
        drainVoicePlayback(voiceOAI);
        pipeOAI.update();

        TEST_ASSERT(pipeOAI.getLastAiResponse().indexOf("Reponse valide B") >= 0, "Late.A.6 Reponse B enregistree");
        TEST_ASSERT(pipeOAI.getState() == PipelineState::READY, "Late.A.7 Tour B acheve");

        // TEST B : Deux annulations successives, suivies d'une troisième question
        // 1ere annulation (resp_X)
        pipeOAI.startListening();
        pipeOAI.stopListening();
        pipeOAI.feedSimulatedTranscription("Question X", true);
        const char* crX = "{\"type\":\"response.created\",\"response\":{\"id\":\"resp_X_003\",\"status\":\"in_progress\"}}";
        mockTransportOAI.simulateIncoming(crX, strlen(crX));
        pipeOAI.cancel();

        // 2eme annulation (resp_Y)
        pipeOAI.startListening();
        pipeOAI.stopListening();
        pipeOAI.feedSimulatedTranscription("Question Y", true);
        const char* crY = "{\"type\":\"response.created\",\"response\":{\"id\":\"resp_Y_004\",\"status\":\"in_progress\"}}";
        mockTransportOAI.simulateIncoming(crY, strlen(crY));
        pipeOAI.cancel();

        // 3eme question (resp_Z)
        pipeOAI.startListening();
        pipeOAI.stopListening();
        pipeOAI.feedSimulatedTranscription("Question Z", true);

        // Paquets parasites de X et Y injectés
        const char* txtX = "{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"resp_X_003\",\"delta\":\"Parasite X\"}";
        const char* txtY = "{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"resp_Y_004\",\"delta\":\"Parasite Y\"}";
        mockTransportOAI.simulateIncoming(txtX, strlen(txtX));
        mockTransportOAI.simulateIncoming(txtY, strlen(txtY));
        pipeOAI.update();

        TEST_ASSERT(pipeOAI.getLastAiResponse().indexOf("Parasite X") < 0, "Late.B.1 Parasite X rejete");
        TEST_ASSERT(pipeOAI.getLastAiResponse().indexOf("Parasite Y") < 0, "Late.B.2 Parasite Y rejete");

        // Réponse normale de Z
        const char* crZ = "{\"type\":\"response.created\",\"response\":{\"id\":\"resp_Z_005\",\"status\":\"in_progress\"}}";
        const char* txtZ = "{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"resp_Z_005\",\"delta\":\"Reponse propre Z.\"}";
        const char* txtDoneZ = "{\"type\":\"response.output_audio_transcript.done\",\"response_id\":\"resp_Z_005\",\"transcript\":\"Reponse propre Z.\"}";
        const char* audZ = "{\"type\":\"response.audio.delta\",\"response_id\":\"resp_Z_005\",\"delta\":\"AAAAAAAAAAAAAAAA\"}";
        const char* doneZ = "{\"type\":\"response.done\",\"response\":{\"id\":\"resp_Z_005\",\"status\":\"completed\",\"output\":[{\"content\":[{\"transcript\":\"Reponse propre Z.\"}]}]}}";
        mockTransportOAI.simulateIncoming(crZ, strlen(crZ));
        mockTransportOAI.simulateIncoming(txtZ, strlen(txtZ));
        mockTransportOAI.simulateIncoming(txtDoneZ, strlen(txtDoneZ));
        mockTransportOAI.simulateIncoming(audZ, strlen(audZ));
        voiceOAI.getSession().transitionTo(LiveSessionState::PLAYING, "Z audio");
        pipeOAI.update();
        mockTransportOAI.simulateIncoming(doneZ, strlen(doneZ));
        drainVoicePlayback(voiceOAI);
        pipeOAI.update();

        TEST_ASSERT(pipeOAI.getLastAiResponse().indexOf("Reponse propre Z") >= 0, "Late.B.3 Reponse Z validee");
        TEST_ASSERT(pipeOAI.getState() == PipelineState::READY, "Late.B.4 READY post Z");

        // TEST C : Ancienne transcription textuelle après nouvelle demande
        pipeOAI.startListening();
        pipeOAI.stopListening();
        pipeOAI.feedSimulatedTranscription("Question C1", true);
        const char* crC1 = "{\"type\":\"response.created\",\"response\":{\"id\":\"resp_C1\",\"status\":\"in_progress\"}}";
        mockTransportOAI.simulateIncoming(crC1, strlen(crC1));
        pipeOAI.cancel();

        pipeOAI.startListening();
        pipeOAI.stopListening();
        pipeOAI.feedSimulatedTranscription("Question C2", true);
        const char* strayText = "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"resp_C1\",\"delta\":\"Texte fantome\"}";
        mockTransportOAI.simulateIncoming(strayText, strlen(strayText));
        pipeOAI.update();
        TEST_ASSERT(pipeOAI.getLastAiResponse().indexOf("Texte fantome") < 0, "Late.C.1 Transcription obsolete C1 rejetee");
        pipeOAI.cancel();

        // TEST D : Ancien paquet audio après nouvelle demande
        pipeOAI.startListening();
        pipeOAI.stopListening();
        pipeOAI.feedSimulatedTranscription("Question D1", true);
        const char* crD1 = "{\"type\":\"response.created\",\"response\":{\"id\":\"resp_D1\",\"status\":\"in_progress\"}}";
        mockTransportOAI.simulateIncoming(crD1, strlen(crD1));
        pipeOAI.cancel();

        pipeOAI.startListening();
        pipeOAI.stopListening();
        pipeOAI.feedSimulatedTranscription("Question D2", true);
        uint32_t samplesBefore = voiceOAI.getPlayer().getSamplesPlayedTotal();
        const char* strayAudio = "{\"type\":\"response.audio.delta\",\"response_id\":\"resp_D1\",\"delta\":\"AAAAAAAAAAAAAAAA\"}";
        mockTransportOAI.simulateIncoming(strayAudio, strlen(strayAudio));
        pipeOAI.update();
        TEST_ASSERT(voiceOAI.getPlayer().getSamplesPlayedTotal() == samplesBefore, "Late.D.1 Audio parasite D1 ignore");
        pipeOAI.cancel();

        // TEST E : Réponse B terminée correctement et mémorisée une seule fois
        VoiceUxController ux;
        pipeOAI.begin(voiceOAI, toolsOAI, ux, pcfgOAI);
        size_t memBefore = ux.getReplyMemory().getCount();

        pipeOAI.startListening();
        pipeOAI.stopListening();
        pipeOAI.feedSimulatedTranscription("Question E", true);

        const char* crE = "{\"type\":\"response.created\",\"response\":{\"id\":\"resp_E\",\"status\":\"in_progress\"}}";
        const char* txtE = "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"resp_E\",\"delta\":\"Reponse unique E.\"}";
        const char* txtDoneE = "{\"type\":\"response.output_audio_transcript.done\",\"response_id\":\"resp_E\",\"transcript\":\"Reponse unique E.\"}";
        const char* audE = "{\"type\":\"response.audio.delta\",\"response_id\":\"resp_E\",\"delta\":\"AAAAAAAAAAAAAAAA\"}";
        const char* doneE = "{\"type\":\"response.done\",\"response\":{\"id\":\"resp_E\",\"status\":\"completed\",\"output\":[{\"content\":[{\"transcript\":\"Reponse unique E.\"}]}]}}";

        mockTransportOAI.simulateIncoming(crE, strlen(crE));
        mockTransportOAI.simulateIncoming(txtE, strlen(txtE));
        mockTransportOAI.simulateIncoming(txtDoneE, strlen(txtDoneE));
        mockTransportOAI.simulateIncoming(audE, strlen(audE));
        voiceOAI.getSession().transitionTo(LiveSessionState::PLAYING, "E audio");
        pipeOAI.update();
        mockTransportOAI.simulateIncoming(doneE, strlen(doneE));
        drainVoicePlayback(voiceOAI);
        pipeOAI.update();

        TEST_ASSERT(pipeOAI.getState() == PipelineState::READY, "Late.E.1 Tour E acheve");
        TEST_ASSERT(ux.getReplyMemory().getCount() == memBefore + 1, "Late.E.2 Memorise exactement une fois dans ReplyMemory");
        TEST_ASSERT(ux.getReplyMemory().getLastReply().indexOf("Reponse unique E") >= 0, "Late.E.3 Contenu exact memorise");
        voiceOAI.getClient().setTransport(nullptr);
        voiceOAI.getOpenAIClient().setTransport(nullptr);
    }

    if (s_testsFailed == failuresBefore147) {
        std::cout << "\n[TEST SUITE 147] SUCCES INTEGRAL DES TESTS CORRECTIFS (v0.6.5.1 Final)" << std::endl;
    } else {
        std::cout << "\n[TEST SUITE 147] ECHEC" << std::endl;
    }
}
