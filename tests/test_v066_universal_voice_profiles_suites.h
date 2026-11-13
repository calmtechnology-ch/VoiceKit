#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include "VoiceProfileManager.h"
#include "VoxCoreVoice.h"
#include "VoiceKit.h"
#include "core/pipeline/IntegratedVoicePipeline.h"
#include "core/tools/ToolRegistry.h"

// =============================================================================
// SUITE 150 : VoiceKit v0.6.6 — Universal Voice Profiles (Scénarios 1 à 20)
// =============================================================================
inline void run_universal_voice_profiles_v066_tests() {
    uint32_t failuresBefore150 = s_testsFailed;
    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  [TEST SUITE 150] VoiceKit v0.6.6 — Universal Voice Profiles" << std::endl;
    std::cout << "===========================================================" << std::endl;

    WiFi.setConnected(true);
    setMockMillisOffset(1700000000UL * 1000UL);
    VoiceProfileManager::resetMockNvsForTesting();

    // -------------------------------------------------------------------------
    // Catalogue, filtrage par genre perçu et extensibilité sans réallocation
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- Catalogue VoiceProfileManager, filtrage genre & extensibilite ---" << std::endl;
        VoiceProfileManager mgr;

        // Vérification catalogue Gemini (Aoede, Kore, Puck, Charon, Fenrir, Leda...)
        TEST_ASSERT(mgr.isValidVoiceForProvider(VoiceProviderType::GEMINI, "Aoede"), "Gemini voix Aoede valide");
        TEST_ASSERT(mgr.isValidVoiceForProvider(VoiceProviderType::GEMINI, "Kore"), "Gemini voix Kore valide");
        TEST_ASSERT(mgr.isValidVoiceForProvider(VoiceProviderType::GEMINI, "Puck"), "Gemini voix Puck valide");
        TEST_ASSERT(mgr.isValidVoiceForProvider(VoiceProviderType::GEMINI, "Charon"), "Gemini voix Charon valide");
        TEST_ASSERT(mgr.isValidVoiceForProvider(VoiceProviderType::GEMINI, "Fenrir"), "Gemini voix Fenrir valide");
        TEST_ASSERT(mgr.isValidVoiceForProvider(VoiceProviderType::GEMINI, "Leda"), "Gemini voix Leda valide");

        // Vérification catalogue OpenAI (alloy, ash, ballad, coral, echo, sage, shimmer, verse, marin, cedar)
        const char* openAiVoices[] = {"alloy", "ash", "ballad", "coral", "echo", "sage", "shimmer", "verse", "marin", "cedar"};
        for (size_t i = 0; i < sizeof(openAiVoices) / sizeof(openAiVoices[0]); ++i) {
            TEST_ASSERT(mgr.isValidVoiceForProvider(VoiceProviderType::OPENAI, openAiVoices[i]),
                        "OpenAI voix du catalogue valide");
        }

        // Filtrage sans allocation dynamique (tampon statique) par genre perçu
        VoiceDescriptor femBuf[16];
        size_t femCount = mgr.getVoicesForProvider(VoiceProviderType::GEMINI, VoiceGender::FEMININE, femBuf, 16);
        TEST_ASSERT(femCount >= 3, "Filtrage Gemini FEMININE retourne >= 3 voix");
        for (size_t i = 0; i < femCount; ++i) {
            TEST_ASSERT(femBuf[i].gender == VoiceGender::FEMININE, "Voix filtree est bien FEMININE");
        }

        VoiceDescriptor mascBuf[16];
        size_t mascCount = mgr.getVoicesForProvider(VoiceProviderType::OPENAI, VoiceGender::MASCULINE, mascBuf, 16);
        TEST_ASSERT(mascCount >= 3, "Filtrage OpenAI MASCULINE retourne >= 3 voix");
        for (size_t i = 0; i < mascCount; ++i) {
            TEST_ASSERT(mascBuf[i].gender == VoiceGender::MASCULINE, "Voix OpenAI filtree est bien MASCULINE");
        }

        VoiceDescriptor neutBuf[16];
        size_t neutCount = mgr.getVoicesForProvider(VoiceProviderType::OPENAI, VoiceGender::NEUTRAL, neutBuf, 16);
        TEST_ASSERT(neutCount >= 2, "Filtrage OpenAI NEUTRAL retourne >= 2 voix");

        // Enregistrement voix & langue personnalisées (extensibilité)
        TEST_ASSERT(mgr.registerCustomVoice("Sulafat", VoiceProviderType::GEMINI, VoiceGender::FEMININE, "Sulafat", "Douce"),
                    "Enregistrement voix Gemini personnalisee Sulafat");
        TEST_ASSERT(mgr.isValidVoiceForProvider(VoiceProviderType::GEMINI, "Sulafat"),
                    "Sulafat reconnue pour Gemini");
        TEST_ASSERT(!mgr.isValidVoiceForProvider(VoiceProviderType::OPENAI, "Sulafat"),
                    "Sulafat rejetee pour OpenAI");

        // Vérification des 7 langues obligatoires + BCP-47 extensible
        const char* reqLangs[] = {"fr-FR", "fr-CH", "en-US", "en-GB", "de-DE", "it-IT", "es-ES"};
        for (size_t i = 0; i < sizeof(reqLangs) / sizeof(reqLangs[0]); ++i) {
            TEST_ASSERT(mgr.findLanguage(reqLangs[i]) != nullptr, "Langue obligatoire presente dans catalogue");
            TEST_ASSERT(VoiceProfileManager::isValidLanguageCode(reqLangs[i]), "Code BCP-47 obligatoire valide");
        }
        TEST_ASSERT(VoiceProfileManager::isValidLanguageCode("pt-BR"), "BCP-47 extensible pt-BR accepte");
        TEST_ASSERT(VoiceProfileManager::isValidLanguageCode("ja-JP"), "BCP-47 extensible ja-JP accepte");
        TEST_ASSERT(!VoiceProfileManager::isValidLanguageCode("123-invalid!"), "Code langue malforme rejete");
        TEST_ASSERT(!VoiceProfileManager::isValidLanguageCode(""), "Code langue vide rejete");
    }

    // -------------------------------------------------------------------------
    // SCÉNARIOS 1, 2, 3 : Gemini Live (fr-FR Aoede, de-DE Charon, en-US Puck)
    // + vérification absence speechConfig.languageCode + non-traduction TTS
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- SCENARIOS 1, 2, 3 : Gemini Live (Aoede fr-FR, Charon de-DE, Puck en-US) ---" << std::endl;
        MockLiveTransport geminiTransport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.provider = VoiceProviderType::GEMINI;
        cfg.geminiApiKey = "AIzaSyTestGeminiV066";
        cfg.voice = "Aoede";
        cfg.language = "fr-FR";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

        TEST_ASSERT(voice.begin(cfg), "Scenario 1 : begin() Gemini fr-FR Aoede");
        prepareMockVoiceReady(voice, geminiTransport);

        std::string setupFr = geminiTransport.getLastSent();
        TEST_ASSERT(setupFr.find("\"voiceName\":\"Aoede\"") != std::string::npos,
                    "Scenario 1 : prebuiltVoiceConfig.voiceName == Aoede");
        TEST_ASSERT(setupFr.find("\"languageCode\"") == std::string::npos,
                    "Scenario 1 : speechConfig.languageCode absent sur modele natif Bidi");
        TEST_ASSERT(setupFr.find("gender") == std::string::npos,
                    "Scenario 1 : aucun champ gender non-standard envoye a Gemini");
        TEST_ASSERT(setupFr.find("francais (fr-FR)") != std::string::npos,
                    "Scenario 1 : directive francais (fr-FR) presente");

        // Scénario 2 : Gemini, allemand (de-DE), voix Charon
        VoiceProfile profDe;
        profDe.provider = VoiceProviderType::GEMINI;
        profDe.language = "de-DE";
        profDe.voiceId = "Charon";
        profDe.preferredGender = VoiceGender::MASCULINE;
        profDe.style = VoiceStyle::PROFESSIONAL;
        profDe.tone = VoiceTone::SERIOUS;

        VoiceProfile appliedSnapshot;
        int appliedCount = 0;
        voice.onProfileApplied([&](const VoiceProfile& p) {
            appliedSnapshot = p;
            appliedCount++;
        });

        TEST_ASSERT(voice.applyVoiceProfile(profDe), "Scenario 2 : applyVoiceProfile(Gemini, de-DE, Charon) accepte");
        TEST_ASSERT(voice.isProfileChangePending(), "Scenario 2 : en attente de confirmation setupComplete");
        TEST_ASSERT(appliedCount == 0, "Scenario 2 : non declare applique avant setupComplete");
        TEST_ASSERT(voice.getVoice() == "Aoede", "Scenario 2 : ancienne voix Aoede conservee avant setupComplete");

        const char* setupCompJson = "{\"setupComplete\":{}}";
        voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
        voice.update();

        TEST_ASSERT(voice.isReady(), "Scenario 2 : session prete apres setupComplete");
        TEST_ASSERT(appliedCount == 1 && appliedSnapshot.voiceId == "Charon" && appliedSnapshot.language == "de-DE",
                    "Scenario 2 : onProfileApplied declenche avec Charon / de-DE");

        std::string setupDe = geminiTransport.getLastSent();
        TEST_ASSERT(setupDe.find("\"voiceName\":\"Charon\"") != std::string::npos,
                    "Scenario 2 : voiceName == Charon");
        TEST_ASSERT(setupDe.find("allemand (de-DE)") != std::string::npos,
                    "Scenario 2 : directive allemand (de-DE) presente");
        TEST_ASSERT(setupDe.find("Tu es un moteur de synthese vocale") == std::string::npos,
                    "Scenario 2 : instruction par defaut francaise remplacee de facon coherente en allemand");

        // Vérification que say() en mode TTS prononce le texte demandé SANS le traduire
        const char* rawTtsText = "Guten Tag, Systemprüfung erfolgreich.";
        TEST_ASSERT(voice.say(rawTtsText), "Scenario 2 : say() accepte en allemand");
        std::string ttsPayload = geminiTransport.getLastSent();
        TEST_ASSERT(ttsPayload.find(rawTtsText) != std::string::npos,
                    "Scenario 2 : texte TTS transmis mot pour mot sans traduction ni alteration");
        voice.stop();

        // Scénario 3 : Gemini, anglais (en-US), voix Puck + mode conversation multilingue
        VoiceProfile profEn;
        profEn.provider = VoiceProviderType::GEMINI;
        profEn.language = "en-US";
        profEn.voiceId = "Puck";
        profEn.style = VoiceStyle::NATURAL;
        profEn.tone = VoiceTone::WARM;

        TEST_ASSERT(voice.applyVoiceProfile(profEn), "Scenario 3 : applyVoiceProfile(Gemini, en-US, Puck) accepte");
        voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
        voice.update();
        TEST_ASSERT(voice.getVoice() == "Puck" && voice.getLanguage() == "en-US",
                    "Scenario 3 : Puck / en-US actifs");

        TEST_ASSERT(voice.enableConversation(true), "Scenario 3 : passage en mode conversation en-US");
        voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
        voice.update();
        std::string convSetupEn = geminiTransport.getLastSent();
        TEST_ASSERT(convSetupEn.find("English (en-US)") != std::string::npos,
                    "Scenario 3 : instruction conversationnelle en anglais coherent");
        TEST_ASSERT(convSetupEn.find("anglais (en-US)") != std::string::npos,
                    "Scenario 3 : directive vocale anglais (en-US) presente");

        // Vérification fr-CH (français suisse)
        VoiceProfile profCh = profEn;
        profCh.language = "fr-CH";
        profCh.voiceId = "Leda";
        TEST_ASSERT(voice.applyVoiceProfile(profCh), "applyVoiceProfile fr-CH / Leda accepte");
        voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
        voice.update();
        std::string setupCh = geminiTransport.getLastSent();
        TEST_ASSERT(setupCh.find("francais suisse (fr-CH)") != std::string::npos,
                    "Directive francais suisse (fr-CH) generee proprement");

        voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCÉNARIOS 4, 5, 6 : OpenAI Realtime (fr-FR alloy, en-US marin, it-IT cedar)
    // + contrainte de verrouillage de voix après émission audio + séparation langue STT
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- SCENARIOS 4, 5, 6 : OpenAI Realtime (alloy fr-FR, marin en-US, cedar it-IT) ---" << std::endl;
        MockLiveTransport openAiTransport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.provider = VoiceProviderType::OPENAI;
        cfg.openAiApiKey = "sk-test-openai-v066-valid";
        cfg.voice = "alloy";
        cfg.language = "fr-FR";
        cfg.inputTranscriptionLanguage = "de-DE"; // Test séparation explicite langue STT vs synthèse
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

        TEST_ASSERT(voice.begin(cfg), "Scenario 4 : begin() OpenAI fr-FR alloy");
        voice.getOpenAIClient().setTransport(&openAiTransport);
        TEST_ASSERT(voice.getSession().activate(), "Scenario 4 : activation session OpenAI");

        const char* sessCreated = "{\"type\":\"session.created\",\"session\":{\"id\":\"sess_v066_1\"}}";
        openAiTransport.simulateIncoming(sessCreated, strlen(sessCreated));
        std::string updFr = openAiTransport.getLastSent();
        TEST_ASSERT(updFr.find("\"voice\":\"alloy\"") != std::string::npos,
                    "Scenario 4 : audio.output.voice == alloy");
        TEST_ASSERT(updFr.find("\"language\":\"de\"") != std::string::npos,
                    "Scenario 4 : langue transcription d'entree (de) distincte de la langue de synthese (fr-FR)");
        TEST_ASSERT(updFr.find("francais (fr-FR)") != std::string::npos,
                    "Scenario 4 : directives de synthese en francais (fr-FR) presentes dans instructions");
        TEST_ASSERT(updFr.find("gender") == std::string::npos,
                    "Scenario 4 : aucun champ gender transmis a OpenAI");

        const char* sessUpdated = "{\"type\":\"session.updated\",\"session\":{\"id\":\"sess_v066_1\"}}";
        openAiTransport.simulateIncoming(sessUpdated, strlen(sessUpdated));
        voice.update();
        TEST_ASSERT(voice.isReady(), "Scenario 4 : OpenAI session prete");

        // Scénario 5 : OpenAI, anglais (en-US), voix marin (aucun audio encore émis -> session.update en place autorisé !)
        uint32_t attemptsBeforeMarin = voice.getOpenAIClient().getConnectAttempts();
        VoiceProfile profMarin;
        profMarin.provider = VoiceProviderType::OPENAI;
        profMarin.language = "en-US";
        profMarin.voiceId = "marin";
        profMarin.style = VoiceStyle::EXPRESSIVE;
        profMarin.tone = VoiceTone::CHEERFUL;

        TEST_ASSERT(voice.applyVoiceProfile(profMarin), "Scenario 5 : applyVoiceProfile(OpenAI, en-US, marin)");
        TEST_ASSERT(voice.getOpenAIClient().getConnectAttempts() == attemptsBeforeMarin,
                    "Scenario 5 : pas de reconnexion inutile avant emission audio (session.update en place)");
        openAiTransport.simulateIncoming(sessUpdated, strlen(sessUpdated));
        voice.update();
        TEST_ASSERT(voice.isReady() && voice.getVoice() == "marin" && voice.getLanguage() == "en-US",
                    "Scenario 5 : voix marin / en-US confirmee");

        // Simuler une émission audio OpenAI sur cette session (verrouille la voix de session côté OpenAI Realtime)
        TEST_ASSERT(voice.say("Hello world"), "Scenario 5 : emission say() sur session OpenAI");
        const char* audioDelta = "{\"type\":\"response.output_audio.delta\",\"delta\":\"AAAA\"}";
        openAiTransport.simulateIncoming(audioDelta, strlen(audioDelta));
        TEST_ASSERT(voice.getOpenAIClient().hasAudioEmittedInSession(),
                    "Scenario 5 : OpenAI detecte que de l'audio a ete emis dans la session");
        TEST_ASSERT(!voice.getOpenAIClient().canUpdateVoiceInCurrentSession("cedar"),
                    "Scenario 5 : interdiction de modifier la voix via session.update apres emission audio");
        TEST_ASSERT(voice.getOpenAIClient().canUpdateVoiceInCurrentSession("marin"),
                    "Scenario 5 : mise a jour des instructions avec la meme voix marin reste permise");
        voice.stop();

        // Scénario 6 : OpenAI, italien (it-IT), voix cedar APRÈS émission audio -> DOIT effectuer une reconnexion propre !
        uint32_t attemptsBeforeCedar = voice.getOpenAIClient().getConnectAttempts();
        VoiceProfile profCedar;
        profCedar.provider = VoiceProviderType::OPENAI;
        profCedar.language = "it-IT";
        profCedar.voiceId = "cedar";
        profCedar.style = VoiceStyle::NARRATIVE;
        profCedar.tone = VoiceTone::CALM;

        TEST_ASSERT(voice.applyVoiceProfile(profCedar), "Scenario 6 : applyVoiceProfile(OpenAI, it-IT, cedar) apres audio");
        TEST_ASSERT(voice.getOpenAIClient().getConnectAttempts() == attemptsBeforeCedar + 1,
                    "Scenario 6 : reconnexion securisee effectuee pour changer de voix OpenAI apres audio");

        openAiTransport.simulateIncoming(sessCreated, strlen(sessCreated));
        std::string updIt = openAiTransport.getLastSent();
        TEST_ASSERT(updIt.find("\"voice\":\"cedar\"") != std::string::npos,
                    "Scenario 6 : nouvelle session OpenAI configuree avec voix cedar");
        TEST_ASSERT(updIt.find("italien (it-IT)") != std::string::npos,
                    "Scenario 6 : directive italien (it-IT) presente dans session.update");

        openAiTransport.simulateIncoming(sessUpdated, strlen(sessUpdated));
        voice.update();
        TEST_ASSERT(voice.isReady() && voice.getVoice() == "cedar" && voice.getLanguage() == "it-IT",
                    "Scenario 6 : voix cedar / it-IT active apres confirmation serveur");

        voice.getOpenAIClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCÉNARIOS 7, 8, 9 : Style, Ton, Accent + préservation des règles métier
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- SCENARIOS 7, 8, 9 : Style, Ton, Accent & preservation regles metier ---" << std::endl;
        MockLiveTransport openAiTransport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.provider = VoiceProviderType::OPENAI;
        cfg.openAiApiKey = "sk-test-style-tone-accent";
        cfg.voice = "sage";
        cfg.language = "es-ES";
        cfg.systemInstruction = "REGLE_SECURITE_METIER_IMMUTABLE: Ne jamais divulguer le code PIN.";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

        voice.begin(cfg);
        voice.getOpenAIClient().setTransport(&openAiTransport);
        voice.getSession().activate();
        const char* sessCreated = "{\"type\":\"session.created\",\"session\":{\"id\":\"sess_sta\"}}";
        const char* sessUpdated = "{\"type\":\"session.updated\",\"session\":{\"id\":\"sess_sta\"}}";
        openAiTransport.simulateIncoming(sessCreated, strlen(sessCreated));
        openAiTransport.simulateIncoming(sessUpdated, strlen(sessUpdated));
        voice.update();

        // Scénario 7 : Modification du style vocal
        TEST_ASSERT(voice.setStyle(VoiceStyle::NARRATIVE), "Scenario 7 : setStyle(NARRATIVE) accepte");
        openAiTransport.simulateIncoming(sessUpdated, strlen(sessUpdated));
        voice.update();
        TEST_ASSERT(voice.getStyle() == VoiceStyle::NARRATIVE, "Scenario 7 : Style NARRATIVE actif");

        // Scénario 8 : Modification du ton vocal
        TEST_ASSERT(voice.setTone(VoiceTone::WARM), "Scenario 8 : setTone(WARM) accepte");
        openAiTransport.simulateIncoming(sessUpdated, strlen(sessUpdated));
        voice.update();
        TEST_ASSERT(voice.getTone() == VoiceTone::WARM, "Scenario 8 : Ton WARM actif");

        // Scénario 9 : Modification de l'accent
        TEST_ASSERT(voice.setAccent("andalou leger"), "Scenario 9 : setAccent('andalou leger') accepte");
        std::string sentSta = openAiTransport.getLastSent();
        openAiTransport.simulateIncoming(sessUpdated, strlen(sessUpdated));
        voice.update();
        TEST_ASSERT(voice.getAccent() == "andalou leger", "Scenario 9 : Accent 'andalou leger' actif");

        // Les règles métier / sécurité ne sont jamais écrasées par les paramètres vocaux
        TEST_ASSERT(sentSta.find("REGLE_SECURITE_METIER_IMMUTABLE") != std::string::npos,
                    "Regle metier / securite strictement preservee apres changements de style/ton/accent");
        TEST_ASSERT(sentSta.find("narratif") != std::string::npos &&
                    sentSta.find("chaleureuse") != std::string::npos &&
                    sentSta.find("andalou leger") != std::string::npos &&
                    sentSta.find("espagnol (es-ES)") != std::string::npos,
                    "Directives espagnol + narratif + chaleureux + accent presentes ensemble");

        voice.getOpenAIClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCÉNARIOS 10, 11, 12, 13, 14, 15, 18 :
    // Rejets invalides, croisement fournisseurs, refus occupé, bascule GEMINI<->OPENAI,
    // coupure réseau avec rollback, reprise après erreur, absence de clé API
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- SCENARIOS 10..15 & 18 : Validation, bascule fournisseurs, coupure reseau & rollback ---" << std::endl;
        MockLiveTransport geminiTransport;
        MockLiveTransport openAiTransport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.provider = VoiceProviderType::GEMINI;
        cfg.geminiApiKey = "AIzaSyGeminiValidKey066";
        cfg.openAiApiKey = "sk-OpenAiValidKey066";
        cfg.voice = "Kore";
        cfg.language = "fr-FR";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

        voice.begin(cfg);
        voice.getClient().setTransport(&geminiTransport);
        voice.getOpenAIClient().setTransport(&openAiTransport);
        prepareMockVoiceReady(voice, geminiTransport);

        String lastProfileErr = "";
        int profileErrCount = 0;
        voice.onProfileError([&](const String& e) {
            lastProfileErr = e;
            profileErrCount++;
        });

        // Scénario 10 : Sélection d'une voix inexistante
        TEST_ASSERT(!voice.setVoice("VoixTotalementInexistante"),
                    "Scenario 10 : voix inexistante rejetee");
        TEST_ASSERT(profileErrCount == 1 && !lastProfileErr.isEmpty(),
                    "Scenario 10 : onProfileError declenche");
        TEST_ASSERT(voice.getVoice() == "Kore", "Scenario 10 : voix active Kore inchangee");

        // Scénario 11 : Sélection d'une voix Gemini pour OpenAI (et voix OpenAI pour Gemini)
        TEST_ASSERT(!voice.setVoice("alloy"),
                    "Scenario 11a : voix OpenAI 'alloy' rejetee quand le fournisseur est Gemini");
        VoiceProfile badCross;
        badCross.provider = VoiceProviderType::OPENAI;
        badCross.voiceId = "Aoede"; // Voix Gemini !
        badCross.language = "fr-FR";
        TEST_ASSERT(!voice.applyVoiceProfile(badCross),
                    "Scenario 11b : voix Gemini 'Aoede' rejetee pour le fournisseur OpenAI");
        TEST_ASSERT(voice.getProviderType() == VoiceProviderType::GEMINI && voice.getVoice() == "Kore",
                    "Scenario 11 : profil actif Kore / GEMINI intact");

        // Scénario 12 : Tentative de changement pendant une réponse audio active
        TEST_ASSERT(voice.say("Reponse vocale en cours de lecture."), "Scenario 12 : lancement say()");
        std::vector<int16_t> pcmBlock(2400, 1200);
        voice.getPlayer().feedPcm(pcmBlock.data(), pcmBlock.size());
        TEST_ASSERT(voice.isBusy(), "Scenario 12 : moteur occupe / en lecture");
        TEST_ASSERT(!voice.setProvider(VoiceProviderType::OPENAI, "coral"),
                    "Scenario 12 : changement de fournisseur/profil REFUSE pendant reponse audio");
        TEST_ASSERT(voice.getProviderType() == VoiceProviderType::GEMINI && voice.getVoice() == "Kore",
                    "Scenario 12 : aucune interruption brutale, profil Kore conserve");
        voice.stop();

        // Scénario 13 : Changement de fournisseur au repos (GEMINI -> OPENAI) sans double session
        TEST_ASSERT(voice.setProvider(VoiceProviderType::OPENAI, "coral"),
                    "Scenario 13 : bascule GEMINI -> OPENAI (coral) acceptee au repos");
        TEST_ASSERT(!voice.getClient().isConnected(),
                    "Scenario 13 : ancien client Gemini proprement deconnecte (zero double session)");
        TEST_ASSERT(voice.isProfileChangePending(), "Scenario 13 : transition en attente de confirmation OpenAI");
        TEST_ASSERT(voice.getProviderType() == VoiceProviderType::GEMINI,
                    "Scenario 13 : ancien fournisseur conserve tant que OpenAI n'a pas confirme");

        const char* sessCreated = "{\"type\":\"session.created\",\"session\":{\"id\":\"sess_sw\"}}";
        const char* sessUpdated = "{\"type\":\"session.updated\",\"session\":{\"id\":\"sess_sw\"}}";
        openAiTransport.simulateIncoming(sessCreated, strlen(sessCreated));
        openAiTransport.simulateIncoming(sessUpdated, strlen(sessUpdated));
        voice.update();

        TEST_ASSERT(voice.isReady(), "Scenario 13 : nouvelle session OpenAI prete");
        TEST_ASSERT(voice.getProviderType() == VoiceProviderType::OPENAI && voice.getVoice() == "coral",
                    "Scenario 13 : fournisseur OPENAI et voix coral confirmes");

        // Scénario 14 : Déconnexion réseau pendant un changement de profil (OPENAI coral -> GEMINI Fenrir)
        VoiceProfile toGeminiFenrir;
        toGeminiFenrir.provider = VoiceProviderType::GEMINI;
        toGeminiFenrir.voiceId = "Fenrir";
        toGeminiFenrir.language = "en-GB";
        int errBeforeDrop = profileErrCount;

        TEST_ASSERT(voice.applyVoiceProfile(toGeminiFenrir),
                    "Scenario 14 : debut transition OPENAI coral -> GEMINI Fenrir");
        TEST_ASSERT(voice.isProfileChangePending(), "Scenario 14 : changement en cours");

        // Simuler une coupure brutale du transport WebSocket avant setupComplete !
        geminiTransport.disconnect();
        voice.update();

        TEST_ASSERT(!voice.isProfileChangePending(),
                    "Scenario 14 : _profileChangePending remis a false apres coupure reseau");
        TEST_ASSERT(profileErrCount == errBeforeDrop + 1,
                    "Scenario 14 : onProfileError emis suite a la coupure reseau");
        TEST_ASSERT(voice.getProviderType() == VoiceProviderType::OPENAI && voice.getVoice() == "coral",
                    "Scenario 14 : rollback atomique effectue vers OPENAI / coral");

        // Scénario 15 : Reconnexion propre après une erreur
        TEST_ASSERT(voice.getSession().activate(), "Scenario 15 : reactivation session apres erreur");
        openAiTransport.simulateIncoming(sessCreated, strlen(sessCreated));
        openAiTransport.simulateIncoming(sessUpdated, strlen(sessUpdated));
        voice.update();
        TEST_ASSERT(voice.isReady() && voice.getProviderType() == VoiceProviderType::OPENAI && voice.getVoice() == "coral",
                    "Scenario 15 : session OpenAI pleinement operationnelle apres erreur");

        // Scénario 18 : Absence de clé API lors d'un changement de fournisseur
        voice.setProviderApiKey(VoiceProviderType::GEMINI, ""); // Efface la clé Gemini
        TEST_ASSERT(!voice.hasProviderApiKey(VoiceProviderType::GEMINI), "Scenario 18 : cle Gemini absente");
        TEST_ASSERT(!voice.setProvider(VoiceProviderType::GEMINI, "Aoede"),
                    "Scenario 18 : bascule vers Gemini refusee proprement sans cle API");
        TEST_ASSERT(voice.getLastError().indexOf("Cle API manquante") >= 0,
                    "Scenario 18 : message d'erreur explicite sur l'absence de cle API");
        TEST_ASSERT(voice.getProviderType() == VoiceProviderType::OPENAI && voice.getVoice() == "coral",
                    "Scenario 18 : session OpenAI intacte apres refus cle manquante");

        voice.getClient().setTransport(nullptr);
        voice.getOpenAIClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // SCÉNARIOS 16 & 17 : Sauvegarde NVS, Restauration au démarrage, Favoris,
    // Absence absolue de clé API en NVS, Schéma incompatible & Corruption CRC32
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- SCENARIOS 16 & 17 : Persistance NVS, Restauration boot, Favoris & Corruption ---" << std::endl;
        VoiceProfileManager::resetMockNvsForTesting();

        const char* secretGeminiKey = "AIzaSy_SUPER_SECRET_NEVER_IN_NVS_999";
        const char* secretOpenAiKey = "sk-proj-SUPER_SECRET_NEVER_IN_NVS_888";

        {
            VoxCoreVoice vSave;
            VoiceKitConfig cfg;
            cfg.provider = VoiceProviderType::OPENAI;
            cfg.geminiApiKey = secretGeminiKey;
            cfg.openAiApiKey = secretOpenAiKey;
            cfg.voice = "shimmer";
            cfg.language = "fr-CH";
            cfg.style = VoiceStyle::EXPRESSIVE;
            cfg.tone = VoiceTone::CHEERFUL;
            cfg.accent = "genevois";
            cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
            vSave.begin(cfg);
            vSave.setPreferredGender(VoiceGender::FEMININE);

            // Scénario 16 : Sauvegarde NVS du profil actif + d'un favori
            TEST_ASSERT(vSave.saveVoiceProfile(), "Scenario 16 : saveVoiceProfile() reussi");
            TEST_ASSERT(vSave.hasSavedVoiceProfile(), "Scenario 16 : hasSavedVoiceProfile() true");
            TEST_ASSERT(vSave.saveFavoriteProfile(1), "Scenario 16 : saveFavoriteProfile(slot 1) reussi");

            // Vérification critique de sécurité : AUCUNE clé API ne doit figurer dans la NVS !
            String rawDump = VoiceProfileManager::getRawMockNvsDumpForTesting("vk_vprof");
            TEST_ASSERT(rawDump.indexOf(secretGeminiKey) < 0 && rawDump.indexOf(secretOpenAiKey) < 0 &&
                        rawDump.indexOf("AIzaSy") < 0 && rawDump.indexOf("sk-proj") < 0,
                        "Scenario 16 SECURITE : aucune cle API n'est jamais enregistree dans les profils NVS");
        }

        // Scénario 17 : Simulation de redémarrage du CoreS3 avec restoreSavedProfileOnBoot = true
        {
            VoxCoreVoice vBoot;
            VoiceKitConfig bootCfg;
            bootCfg.provider = VoiceProviderType::GEMINI; // Défaut initial différent pour prouver la restauration !
            bootCfg.geminiApiKey = secretGeminiKey;
            bootCfg.openAiApiKey = secretOpenAiKey;
            bootCfg.restoreSavedProfileOnBoot = true;
            bootCfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

            TEST_ASSERT(vBoot.begin(bootCfg), "Scenario 17 : redemarrage avec restoreSavedProfileOnBoot reussi");
            VoiceProfile restored = vBoot.getVoiceProfile();
            TEST_ASSERT(restored.provider == VoiceProviderType::OPENAI, "Scenario 17 : fournisseur OPENAI restaure");
            TEST_ASSERT(restored.voiceId == "shimmer", "Scenario 17 : voix shimmer restauree");
            TEST_ASSERT(restored.language == "fr-CH", "Scenario 17 : langue fr-CH restauree");
            TEST_ASSERT(restored.preferredGender == VoiceGender::FEMININE, "Scenario 17 : genre FEMININE restaure");
            TEST_ASSERT(restored.style == VoiceStyle::EXPRESSIVE, "Scenario 17 : style EXPRESSIVE restaure");
            TEST_ASSERT(restored.tone == VoiceTone::CHEERFUL, "Scenario 17 : ton CHEERFUL restaure");
            TEST_ASSERT(restored.accent == "genevois", "Scenario 17 : accent genevois restaure");

            // Retour au profil par défaut
            TEST_ASSERT(vBoot.resetVoiceProfile(true), "Scenario 17 : resetVoiceProfile() reussi");
            TEST_ASSERT(!vBoot.hasSavedVoiceProfile(), "Scenario 17 : profil NVS actif efface");
            TEST_ASSERT(vBoot.getVoice() == "alloy" && vBoot.getLanguage() == "fr-FR",
                        "Scenario 17 : retour au profil OpenAI par defaut (alloy / fr-FR)");

            // Restauration depuis le favori slot 1
            TEST_ASSERT(vBoot.loadFavoriteProfile(1, true), "Scenario 17 : chargement favori slot 1 reussi");
            TEST_ASSERT(vBoot.getVoice() == "shimmer" && vBoot.getAccent() == "genevois",
                        "Scenario 17 : favori slot 1 restaure avec succes");
        }

        // Test de robustesse : données NVS corrompues (CRC invalide) et schéma futur incompatible (v99)
        {
            VoiceProfileManager::injectCorruptedMockNvsForTesting(
                "vk_vprof", VOICEKIT_PROFILE_SCHEMA_VERSION, "OPENAI\nfr-FR\nalloy\n0\n0\n0\n", 0xDEADBEEFU);

            VoxCoreVoice vCorrupt;
            VoiceKitConfig cfg;
            cfg.provider = VoiceProviderType::GEMINI;
            cfg.geminiApiKey = secretGeminiKey;
            cfg.restoreSavedProfileOnBoot = true;
            cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
            TEST_ASSERT(vCorrupt.begin(cfg), "Corruption CRC NVS ne bloque jamais le demarrage");
            TEST_ASSERT(vCorrupt.getProviderType() == VoiceProviderType::GEMINI && vCorrupt.getVoice() == "Aoede",
                        "Repli sur configuration valide par defaut apres corruption CRC");

            VoiceProfileManager::injectCorruptedMockNvsForTesting(
                "vk_vprof", 99, "OPENAI\nfr-FR\nmarin\n0\n0\n0\n", 12345U);
            VoiceProfile outProf;
            bool wasRestored = true;
            VoiceProfileManager mgr;
            mgr.loadActiveProfile(outProf, VoiceProviderType::OPENAI, &wasRestored, "vk_vprof");
            TEST_ASSERT(!wasRestored && outProf.provider == VoiceProviderType::OPENAI && outProf.voiceId == "alloy",
                        "Version de schema NVS incompatible (99) ignoree proprement avec repli par defaut");
        }
        VoiceProfileManager::resetMockNvsForTesting();
    }

    // -------------------------------------------------------------------------
    // SCÉNARIOS 19 & 20 : Conservation des callbacks, PTT sans régression,
    // Wake Word, LocalToolRegistry / IntegratedVoicePipeline & Façade VoiceKit
    // -------------------------------------------------------------------------
    {
        std::cout << "\n--- SCENARIOS 19 & 20 : Conservation callbacks, PTT, Outils locaux & Facade VoiceKit ---" << std::endl;
        MockLiveTransport geminiTransport;
        MockLiveTransport openAiTransport;
        VoiceKit vk;
        VoiceKitConfig cfg;
        cfg.provider = VoiceProviderType::GEMINI;
        cfg.geminiApiKey = "AIzaSyFacadeTestKey066";
        cfg.openAiApiKey = "sk-FacadeTestKey066";
        cfg.voice = "Aoede";
        cfg.language = "fr-FR";
        cfg.enableWakeWord = true;
        cfg.wakeWordEngine = WakeWordEngineType::TEST;
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

        int readyCount = 0;
        int profileAppliedCount = 0;
        int transcriptionCount = 0;
        String lastTranscribed = "";

        vk.onReady([&]() { readyCount++; });
        vk.onProfileApplied([&](const VoiceProfile&) { profileAppliedCount++; });
        vk.onTranscription([&](const String& txt, bool isFinal) {
            if (isFinal) {
                transcriptionCount++;
                lastTranscribed = txt;
            }
        });

        TEST_ASSERT(vk.begin(cfg), "Scenario 19/20 : VoiceKit facade begin()");
        vk.getEngine().getClient().setTransport(&geminiTransport);
        vk.getEngine().getOpenAIClient().setTransport(&openAiTransport);
        prepareMockVoiceReady(vk.getEngine(), geminiTransport);
        TEST_ASSERT(readyCount == 1, "Scenario 19 : callback onReady initial actif");
        TEST_ASSERT(vk.isWakeWordEnabled(), "Scenario 19 : Wake Word actif avant changement de profil");

        // Bascule dynamique via la façade VoiceKit : GEMINI (Aoede) -> OPENAI (ash, en-GB)
        VoiceProfile ukProfile;
        ukProfile.provider = VoiceProviderType::OPENAI;
        ukProfile.language = "en-GB";
        ukProfile.voiceId = "ash";
        ukProfile.preferredGender = VoiceGender::MASCULINE;
        ukProfile.style = VoiceStyle::PROFESSIONAL;
        ukProfile.tone = VoiceTone::CALM;

        TEST_ASSERT(vk.applyVoiceProfile(ukProfile), "Scenario 19 : vk.applyVoiceProfile(OPENAI, en-GB, ash)");
        const char* sessCreated = "{\"type\":\"session.created\",\"session\":{\"id\":\"sess_ptt\"}}";
        const char* sessUpdated = "{\"type\":\"session.updated\",\"session\":{\"id\":\"sess_ptt\"}}";
        openAiTransport.simulateIncoming(sessCreated, strlen(sessCreated));
        openAiTransport.simulateIncoming(sessUpdated, strlen(sessUpdated));
        vk.update();

        TEST_ASSERT(vk.isReady(), "Scenario 19 : VoiceKit pret sur OpenAI apres bascule");
        TEST_ASSERT(readyCount == 2 && profileAppliedCount == 1,
                    "Scenario 19 : callbacks onReady et onProfileApplied preserves apres changement de fournisseur");
        TEST_ASSERT(vk.isWakeWordEnabled(),
                    "Scenario 19 : Wake Word preserve apres changement de fournisseur");

        // Scénario 20a : Cycle complet Push-To-Talk (listen -> chunks -> stopListening -> transcription finale)
        TEST_ASSERT(vk.listen(), "Scenario 20 : PTT listen() demarre sans regression apres changement de fournisseur");
        TEST_ASSERT(vk.isListening(), "Scenario 20 : isListening() true");
        std::vector<int16_t> micChunk(AUDIO_CHUNK_SAMPLES, 700);
        vk.getEngine().getStreamer().pushSimulatedChunk(micChunk.data(), micChunk.size());
        vk.update();
        TEST_ASSERT(vk.stopListening(), "Scenario 20 : PTT stopListening() reussi");
        TEST_ASSERT(vk.isTranscribing(), "Scenario 20 : en attente de transcription finale");

        const char* sttCompleted =
            "{\"type\":\"conversation.item.input_audio_transcription.completed\","
            "\"transcript\":\"Good morning from London\"}";
        openAiTransport.simulateIncoming(sttCompleted, strlen(sttCompleted));
        vk.update();

        TEST_ASSERT(transcriptionCount == 1 && lastTranscribed == "Good morning from London",
                    "Scenario 20 : transcription PTT recue via le callback conserve");
        TEST_ASSERT(vk.isReady() && !vk.isListening() && !vk.isTranscribing(),
                    "Scenario 20 : retour propre a READY et coherence de l'etat audio");

        // Scénario 20b : Non-régression LocalToolRegistry + IntegratedVoicePipeline après changement de profil
        ToolRegistry tools;
        bool toolExecuted = false;
        tools.registerTool("domotique.lumiere", "Allume la lumiere", [&]() {
            toolExecuted = true;
            return ToolResult::success("Lumiere allumee.");
        });

        IntegratedVoicePipeline pipeline;
        VoicePipelineConfig pcfg;
        pcfg.speakToolResult = false;
        TEST_ASSERT(pipeline.begin(vk.getEngine(), tools, pcfg),
                    "Scenario 20b : IntegratedVoicePipeline compatible avec le moteur apres profil v0.6.6");
        pipeline.getDispatcher().registerMapping("domotique.lumiere", "allume la lumiere");
        pipeline.feedSimulatedTranscription("allume la lumiere", true);
        TEST_ASSERT(toolExecuted && pipeline.getState() == PipelineState::READY,
                    "Scenario 20b : outil local execute sans regression et retour pipeline READY");

        vk.getEngine().getClient().setTransport(nullptr);
        vk.getEngine().getOpenAIClient().setTransport(nullptr);
    }

    if (s_testsFailed == failuresBefore150) {
        std::cout << "\n[TEST SUITE 150] SUCCES INTEGRAL DES 20 SCENARIOS UNIVERSAL VOICE PROFILES (v0.6.6)" << std::endl;
    }
}
