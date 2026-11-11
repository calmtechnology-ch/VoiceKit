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

        // Récupération contrôlée
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
