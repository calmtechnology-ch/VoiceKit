#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include "VoxCoreVoice.h"
#include "core/LiveSessionController.h"
#include "core/ConversationController.h"
#include "core/OpenAIRealtimeClient.h"
#include "core/GeminiVoiceProvider.h"
#include "core/Utf8Utils.h"

// =============================================================================
// SUITE 148 : VoiceKit v0.6.5.2 — Transcription Assembly & Realtime Finalization
// =============================================================================
inline void run_transcription_assembly_v0652_tests() {
    uint32_t failuresBefore148 = s_testsFailed;
    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  [TEST SUITE 148] VoiceKit v0.6.5.2 — Transcription Assembly" << std::endl;
    std::cout << "===========================================================" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 1 : Cas réel reproduit (OpenAI Realtime gpt-realtime-2.1-mini)
    // Séquence : ["Nous", " sommes", " en", " ", "202", "6", " cette", " année", "."]
    // Suivi de .done puis response.done avec le même transcript
    // Résultat intermédiaire et final : aucun "en2026", aucune répétition de phrase.
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST 1 (v0.6.5.2) : Reproduction exacte OpenAI Realtime ---" << std::endl;

        // 1a. Test unitaire au niveau LiveSessionController::appendTranscriptionFragment
        String fragTarget = "";
        const char* deltas[] = {"Nous", " sommes", " en", " ", "202", "6", " cette", " année", "."};
        for (size_t i = 0; i < sizeof(deltas) / sizeof(deltas[0]); ++i) {
            LiveSessionController::appendTranscriptionFragment(fragTarget, deltas[i]);
            if (i == 2) {
                TEST_ASSERT(fragTarget == "Nous sommes en", "1a.1 'Nous sommes en' avant espace");
            } else if (i == 3) {
                TEST_ASSERT(fragTarget == "Nous sommes en ", "1a.2 Espace preserve apres 'en '");
            } else if (i == 4) {
                TEST_ASSERT(fragTarget == "Nous sommes en 202", "1a.3 'en 202' preserve avec espace");
            } else if (i == 5) {
                TEST_ASSERT(fragTarget == "Nous sommes en 2026", "1a.4 'en 2026' preserve sans fusion 'en2026'");
            }
        }
        TEST_ASSERT(fragTarget == "Nous sommes en 2026 cette année.", "1a.5 Phrase complete exacte au niveau append");

        // 1b. Test d'intégration de bout en bout avec OpenAIRealtimeClient, LiveSession & ConversationController
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.provider = VoiceProviderType::OPENAI;
        cfg.apiKey = "sk-test-realtime-v0652";
        cfg.enableConversation = true;
        cfg.conversationInstruction = "Tu es un assistant vocal concis.";

        TEST_ASSERT(voice.begin(cfg), "1b.1 Initialisation VoxCoreVoice OpenAI");

        MockLiveTransport transport;
        voice.getOpenAIClient().setTransport(&transport);

        // Simulation connexion et handshake
        voice.getOpenAIClient().connect();
        const char* sessCreated = "{\"type\":\"session.created\",\"session\":{\"id\":\"sess_0652\"}}";
        transport.simulateIncoming(sessCreated, strlen(sessCreated));
        const char* sessUpdated = "{\"type\":\"session.updated\",\"session\":{\"id\":\"sess_0652\"}}";
        transport.simulateIncoming(sessUpdated, strlen(sessUpdated));

        TEST_ASSERT(voice.getOpenAIClient().isConnected(), "1b.2 OpenAI connecte");
        TEST_ASSERT(voice.getOpenAIClient().isSetupComplete(), "1b.3 OpenAI setup complete");

        // Démarrage tour conversationnel
        TEST_ASSERT(voice.startListening(), "1b.4 startListening");
        voice.stopListening();

        std::vector<String> streamedSnapshots;
        voice.onAssistantText([&streamedSnapshots](const String& text) {
            streamedSnapshots.push_back(text);
        });

        // Arrivée de response.created
        const char* respCreated = "{\"type\":\"response.created\",\"response\":{\"id\":\"resp_0652_real\"}}";
        transport.simulateIncoming(respCreated, strlen(respCreated));

        // Streaming des deltas OpenAI réels
        const char* deltaFrames[] = {
            "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"resp_0652_real\",\"delta\":\"Nous\"}",
            "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"resp_0652_real\",\"delta\":\" sommes\"}",
            "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"resp_0652_real\",\"delta\":\" en\"}",
            "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"resp_0652_real\",\"delta\":\" \"}",
            "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"resp_0652_real\",\"delta\":\"202\"}",
            "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"resp_0652_real\",\"delta\":\"6\"}",
            "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"resp_0652_real\",\"delta\":\" cette\"}",
            "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"resp_0652_real\",\"delta\":\" année\"}",
            "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"resp_0652_real\",\"delta\":\".\"}"
        };

        for (size_t i = 0; i < sizeof(deltaFrames) / sizeof(deltaFrames[0]); ++i) {
            transport.simulateIncoming(deltaFrames[i], strlen(deltaFrames[i]));
        }

        // Vérification des snapshots intermédiaires : "en2026" ne doit JAMAIS apparaître
        for (const auto& snap : streamedSnapshots) {
            TEST_ASSERT(snap.indexOf("en2026") < 0, "1b.5 Aucun snapshot ne contient 'en2026'");
        }

        // Arrivée de response.output_audio_transcript.done
        const char* doneFrame = "{\"type\":\"response.output_audio_transcript.done\",\"response_id\":\"resp_0652_real\",\"transcript\":\"Nous sommes en 2026 cette année.\"}";
        transport.simulateIncoming(doneFrame, strlen(doneFrame));

        // Arrivée immédiate de response.done contenant le même transcript
        const char* respDoneFrame = "{\"type\":\"response.done\",\"response\":{\"id\":\"resp_0652_real\",\"status\":\"completed\",\"output\":[{\"content\":[{\"transcript\":\"Nous sommes en 2026 cette année.\"}]}]}}";
        transport.simulateIncoming(respDoneFrame, strlen(respDoneFrame));

        String finalAsstText = voice.getConversationMetrics().assistantText;
        TEST_ASSERT(finalAsstText == "Nous sommes en 2026 cette année.", "1b.6 Transcription finale sans duplication");
        TEST_ASSERT(finalAsstText.indexOf("cette année.Nous sommes") < 0, "1b.7 Aucune répétition de phrase");

        voice.getOpenAIClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // TEST 2 : Correction finale divergente (Authoritative replacement)
    // Les deltas produisent un texte légèrement divergent du .done final
    // (ex: ponctuation ou accent corrigé), .done doit remplacer sans concaténer.
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST 2 (v0.6.5.2) : Correction finale divergente ---" << std::endl;

        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.provider = VoiceProviderType::OPENAI;
        cfg.apiKey = "sk-test-divergent";
        cfg.enableConversation = true;
        voice.begin(cfg);

        MockLiveTransport transport;
        voice.getOpenAIClient().setTransport(&transport);
        voice.getOpenAIClient().connect();
        const char* sCreated = "{\"type\":\"session.created\",\"session\":{\"id\":\"s_div\"}}";
        transport.simulateIncoming(sCreated, strlen(sCreated));
        const char* sUp = "{\"type\":\"session.updated\",\"session\":{\"id\":\"s_div\"}}";
        transport.simulateIncoming(sUp, strlen(sUp));

        voice.startListening();
        voice.stopListening();

        const char* rCreated = "{\"type\":\"response.created\",\"response\":{\"id\":\"r_div\"}}";
        transport.simulateIncoming(rCreated, strlen(rCreated));

        // Deltas avec une divergence textuelle (ex: casse ou mot différent)
        const char* d1 = "{\"type\":\"response.output_audio_transcript.delta\",\"response_id\":\"r_div\",\"delta\":\"il fait beau\"}";
        transport.simulateIncoming(d1, strlen(d1));

        TEST_ASSERT(voice.getConversationMetrics().assistantText == "il fait beau", "2.1 Delta partiel enregistre");

        // .done arrive avec ponctuation et majuscule faisant foi
        const char* doneDivergent = "{\"type\":\"response.output_audio_transcript.done\",\"response_id\":\"r_div\",\"transcript\":\"Il fait très beau aujourd'hui.\"}";
        transport.simulateIncoming(doneDivergent, strlen(doneDivergent));

        TEST_ASSERT(voice.getConversationMetrics().assistantText == "Il fait très beau aujourd'hui.", "2.2 .done remplace proprement le texte divergent");
        TEST_ASSERT(voice.getConversationMetrics().assistantText.indexOf("il fait beauIl") < 0, "2.3 Aucune concaténation += sauvage");

        // response.done avec le même texte : ne doit rien dupliquer
        const char* respDoneDiv = "{\"type\":\"response.done\",\"response\":{\"id\":\"r_div\",\"status\":\"completed\",\"output\":[{\"content\":[{\"transcript\":\"Il fait très beau aujourd'hui.\"}]}]}}";
        transport.simulateIncoming(respDoneDiv, strlen(respDoneDiv));

        TEST_ASSERT(voice.getConversationMetrics().assistantText == "Il fait très beau aujourd'hui.", "2.4 response.done idempotent");

        voice.getOpenAIClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // TEST 3 : Nombres et tokens courts consécutifs (Anti-fusion overlap)
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST 3 (v0.6.5.2) : Nombres et tokens courts consecutifs ---" << std::endl;

        // 3a. ["Il", " est", " ", "20", "h", "00"] -> "Il est 20h00"
        String t1 = "";
        const char* d3a[] = {"Il", " est", " ", "20", "h", "00"};
        for (const char* d : d3a) {
            LiveSessionController::appendTranscriptionFragment(t1, d);
        }
        TEST_ASSERT(t1 == "Il est 20h00", "3a. 'Il est 20h00' preserve sans chiffre avale");

        // 3b. ["En", " ", "20", "06"] -> "En 2006" (au lieu de 206)
        String t2 = "";
        const char* d3b[] = {"En", " ", "20", "06"};
        for (const char* d : d3b) {
            LiveSessionController::appendTranscriptionFragment(t2, d);
        }
        TEST_ASSERT(t2 == "En 2006", "3b. 'En 2006' preserve sans fusion '0' -> '206'");

        // 3c. ["11", "11"] -> "1111" (au lieu de 111)
        String t3 = "";
        LiveSessionController::appendTranscriptionFragment(t3, "11");
        LiveSessionController::appendTranscriptionFragment(t3, "11");
        TEST_ASSERT(t3 == "1111", "3c. '1111' preserve sans avaler de '1'");

        // 3d. Tokens alphabétiques consécutifs avec même lettre de bordure
        String t4 = "";
        LiveSessionController::appendTranscriptionFragment(t4, "Elle chante ");
        LiveSessionController::appendTranscriptionFragment(t4, "et danse");
        TEST_ASSERT(t4 == "Elle chante et danse", "3d. 'chante et danse' non fusionne sur 'e'");

        String t5 = "";
        LiveSessionController::appendTranscriptionFragment(t5, "Il regarde ");
        LiveSessionController::appendTranscriptionFragment(t5, "en face");
        TEST_ASSERT(t5 == "Il regarde en face", "3e. 'regarde en face' non fusionne sur 'e'");
    }

    // -------------------------------------------------------------------------
    // TEST 4 : Espaces multiples / doublons
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST 4 (v0.6.5.2) : Espaces multiples et doublons ---" << std::endl;

        // 4a. Deux deltas " " consécutifs
        String s1 = "Bonjour";
        LiveSessionController::appendTranscriptionFragment(s1, " ");
        TEST_ASSERT(s1 == "Bonjour ", "4a.1 Premier espace ajoute");
        LiveSessionController::appendTranscriptionFragment(s1, " ");
        TEST_ASSERT(s1 == "Bonjour ", "4a.2 Second espace consecutif ignore sans double espace");
        LiveSessionController::appendTranscriptionFragment(s1, "2026");
        TEST_ASSERT(s1 == "Bonjour 2026", "4a.3 Nombre correctement attache");

        // 4b. " en " suivi de " " puis "2026"
        String s2 = "Nous sommes";
        LiveSessionController::appendTranscriptionFragment(s2, " en ");
        TEST_ASSERT(s2 == "Nous sommes en ", "4b.1 ' en ' preserve l'espace terminal");
        LiveSessionController::appendTranscriptionFragment(s2, " ");
        TEST_ASSERT(s2 == "Nous sommes en ", "4b.2 Delta ' ' ignore sur cible finissant deja par espace");
        LiveSessionController::appendTranscriptionFragment(s2, "2026");
        TEST_ASSERT(s2 == "Nous sommes en 2026", "4b.3 'Nous sommes en 2026' sans espace manquant ni double");

        // 4c. " en " suivi de " 2026" (espace à la fois en fin de cible et début de delta)
        String s3 = "Nous sommes en ";
        LiveSessionController::appendTranscriptionFragment(s3, " 2026");
        TEST_ASSERT(s3 == "Nous sommes en 2026", "4c. Espace double a la frontiere de deltas proprement coalese");

        // 4d. Ponctuation après mot et espace suivant
        String s4 = "Bonjour";
        LiveSessionController::appendTranscriptionFragment(s4, ".");
        LiveSessionController::appendTranscriptionFragment(s4, " Comment");
        TEST_ASSERT(s4 == "Bonjour. Comment", "4d. Ponctuation et espacement de phrase preservés");
    }

    // -------------------------------------------------------------------------
    // TEST 5 : Troncature UTF-8 sur maxLength
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST 5 (v0.6.5.2) : Troncature UTF-8 sur maxLength ---" << std::endl;

        String frenchSample = "Élève français à l’école d’ingénieurs d’été : cœur, déjà, fête.";
        TEST_ASSERT(Utf8Utils::isValidUtf8(frenchSample), "5.1 Phrase source UTF-8 valide");

        // Tester la troncature à chaque longueur d'octet de 0 à len+4
        for (size_t maxLen = 0; maxLen <= frenchSample.length() + 4; ++maxLen) {
            String trunc = Utf8Utils::truncateUtf8(frenchSample, maxLen);
            TEST_ASSERT(trunc.length() <= maxLen, "5.2 Longueur <= maxLen");
            TEST_ASSERT(Utf8Utils::isValidUtf8(trunc), "5.3 Troncature strictement conforme RFC 3629 UTF-8");
        }

        // Troncature spécifique au milieu d'un caractère 2 octets (ex: 'é' = 0xC3 0xA9)
        String cutE = "caf\xC3\xA9";
        String truncCutE = Utf8Utils::truncateUtf8(cutE, 4); // max 4 octets, 'café' fait 5 octets
        TEST_ASSERT(truncCutE == "caf", "5.4 'café' tronqué à 4 octets coupe proprement avant 'é' -> 'caf'");
        TEST_ASSERT(Utf8Utils::isValidUtf8(truncCutE), "5.5 'caf' est UTF-8 valide");

        // Troncature dans LiveSessionController::appendTranscriptionFragment
        String longTarget = "Début de transcription";
        String longFrag = " contenant des caractères accentués : été, fête, déjà et plus encore...";
        LiveSessionController::appendTranscriptionFragment(longTarget, longFrag, 35);
        TEST_ASSERT(longTarget.length() <= 35, "5.6 appendTranscriptionFragment borne a maxLength");
        TEST_ASSERT(Utf8Utils::isValidUtf8(longTarget), "5.7 appendTranscriptionFragment produit un UTF-8 valide");
    }

    // -------------------------------------------------------------------------
    // TEST 6 : Non-régression Gemini Live
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- TEST 6 (v0.6.5.2) : Non-regression Gemini Live ---" << std::endl;

        // 6a. Transcription cumulative Gemini Live
        String gemCum = "Je suis";
        LiveSessionController::appendTranscriptionFragment(gemCum, "Je suis prêt à vous aider.");
        TEST_ASSERT(gemCum == "Je suis prêt à vous aider.", "6a. Remplacement cumulatif Gemini Live");

        // 6b. Retransmission identique exacte
        String gemId = "Il est 15 heures.";
        LiveSessionController::appendTranscriptionFragment(gemId, "Il est 15 heures.");
        TEST_ASSERT(gemId == "Il est 15 heures.", "6b. Retransmission identique non dupliquée");

        // 6c. Retransmission sans point final
        String gemNoDot = "Il est 15 heures.";
        LiveSessionController::appendTranscriptionFragment(gemNoDot, "Il est 15 heures");
        TEST_ASSERT(gemNoDot == "Il est 15 heures.", "6c. Retransmission sans point preserve le point existant");

        // 6d. Intégration GeminiVoiceProvider avec simulateur
        GeminiLiveClient gClient;
        MockLiveTransport gTransport;
        gClient.setTransport(&gTransport);
        gClient.begin("test_api_key", "models/gemini-3.8-live", "Aoede");
        gClient.connect();
        const char* setupMsg = "{\"setupComplete\":{}}";
        gClient.handleIncomingMessage(setupMsg, strlen(setupMsg));
        gClient.sendTextMessage("Bonjour");

        GeminiVoiceProvider gProvider(&gClient);

        String capturedText = "";
        gProvider.setOnOutputTranscriptionWithFinal([&capturedText](const String& text, bool isFinal) {
            (void)isFinal;
            capturedText = text;
        });

        // Simulation d'une trame Gemini Live outputTranscription
        const char* geminiMsg = "{\"serverContent\":{\"outputTranscription\":{\"text\":\"Bonjour depuis Gemini Live\"}}}";
        gClient.handleIncomingMessage(geminiMsg, strlen(geminiMsg));

        TEST_ASSERT(capturedText == "Bonjour depuis Gemini Live", "6d. GeminiVoiceProvider propage outputTranscription via nouveau callback");

        gClient.setTransport(nullptr);
    }

    if (s_testsFailed == failuresBefore148) {
        std::cout << "\n[TEST SUITE 148] SUCCES INTEGRAL DES TESTS (v0.6.5.2)" << std::endl;
    } else {
        std::cout << "\n[TEST SUITE 148] ECHEC DES TESTS (v0.6.5.2)" << std::endl;
    }
}
