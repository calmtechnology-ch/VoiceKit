#pragma once

#include <iostream>
#include <string>
#include <vector>
#include "VoxCoreVoice.h"
#include "native_mock/MockLiveTransport.h"

// =============================================================================
// VoiceKit v0.6.5.2 — Suites de validation des corrections d'exemples
// [TEST SUITE 149] MinimalSTT (Correction A) & IntegratedVoiceDemo (Correction B)
// =============================================================================

// Émulation fidèle des états de MinimalSTT
enum class TestMinimalSttState {
    INITIALIZING,
    READY,
    LISTENING,
    TRANSCRIBING,
    ERROR
};

// Simulation du contrôleur tactile et d'état de MinimalSTT
struct MinimalSttTestContext {
    VoxCoreVoice voice;
    TestMinimalSttState uiState = TestMinimalSttState::INITIALIZING;
    String interimText = "";
    String finalText = "";
    String errorMessage = "";
    bool touchActive = false;
    bool captureHappened = false;

    void setDemoState(TestMinimalSttState st) {
        uiState = st;
    }

    void setupCallbacks() {
        voice.onReady([this]() {
            if (voice.getState() != VoiceState::ERROR_STATE) {
                setDemoState(TestMinimalSttState::READY);
            }
        });

        voice.onListeningStarted([this]() {
            captureHappened = true;
            setDemoState(TestMinimalSttState::LISTENING);
        });

        voice.onListeningFinished([this]() {
            if (uiState != TestMinimalSttState::READY && uiState != TestMinimalSttState::ERROR) {
                setDemoState(TestMinimalSttState::TRANSCRIBING);
            }
        });

        voice.onTranscription([this](const String& text, bool isFinal) {
            if (!captureHappened) return;
            if (isFinal) {
                finalText = text;
                interimText = "";
                captureHappened = false;
                if (voice.getState() != VoiceState::ERROR_STATE && voice.isReady()) {
                    setDemoState(TestMinimalSttState::READY);
                } else if (voice.getState() == VoiceState::ERROR_STATE) {
                    errorMessage = voice.getLastError();
                    setDemoState(TestMinimalSttState::ERROR);
                }
            } else {
                interimText = text;
            }
        });

        voice.onError([this](const String& err) {
            errorMessage = err;
            touchActive = false;
            captureHappened = false;
            setDemoState(TestMinimalSttState::ERROR);
        });
    }

    void simulateTouchPress() {
        if (uiState == TestMinimalSttState::READY && voice.isReady() && !voice.isListening()) {
            interimText = "";
            captureHappened = false;
            bool ok = voice.startListening();
            if (ok) {
                touchActive = true;
                setDemoState(TestMinimalSttState::LISTENING);
            } else {
                touchActive = false;
                captureHappened = false;
                errorMessage = voice.getLastError();
                if (errorMessage.length() == 0) errorMessage = "Demarrage ecoute refuse";
                setDemoState(TestMinimalSttState::ERROR);
            }
        }
    }

    void simulateTouchRelease() {
        if (touchActive) {
            touchActive = false;
            if (voice.isListening()) {
                bool stopOk = voice.stopListening();
                if (!stopOk) {
                    touchActive = false;
                    captureHappened = false;
                    errorMessage = voice.getLastError();
                    if (errorMessage.length() == 0) errorMessage = "Echec arret capture micro";
                    setDemoState(TestMinimalSttState::ERROR);
                } else {
                    if (voice.isTranscribing() && uiState != TestMinimalSttState::READY && uiState != TestMinimalSttState::ERROR) {
                        setDemoState(TestMinimalSttState::TRANSCRIBING);
                    } else if (uiState != TestMinimalSttState::READY && uiState != TestMinimalSttState::ERROR) {
                        if (voice.getState() != VoiceState::ERROR_STATE && voice.isReady()) {
                            setDemoState(TestMinimalSttState::READY);
                        } else if (voice.getState() == VoiceState::ERROR_STATE) {
                            errorMessage = voice.getLastError();
                            setDemoState(TestMinimalSttState::ERROR);
                        }
                    }
                }
            } else {
                touchActive = false;
                captureHappened = false;
                if (uiState != TestMinimalSttState::ERROR && voice.getState() != VoiceState::ERROR_STATE && voice.isReady()) {
                    setDemoState(TestMinimalSttState::READY);
                } else if (voice.getState() == VoiceState::ERROR_STATE && uiState != TestMinimalSttState::ERROR) {
                    errorMessage = voice.getLastError();
                    if (errorMessage.length() == 0) errorMessage = "Moteur en erreur";
                    setDemoState(TestMinimalSttState::ERROR);
                }
            }
        }
    }

    void simulateTouchResetOnError() {
        if (uiState == TestMinimalSttState::ERROR) {
            touchActive = false;
            captureHappened = false;
            if (voice.getState() == VoiceState::ERROR_STATE) {
                errorMessage = voice.getLastError();
                if (errorMessage.length() == 0) errorMessage = "Moteur toujours en erreur";
                setDemoState(TestMinimalSttState::ERROR);
            } else if (voice.isReady()) {
                errorMessage = "";
                setDemoState(TestMinimalSttState::READY);
            } else {
                errorMessage = "";
                setDemoState(TestMinimalSttState::INITIALIZING);
            }
        }
    }
};

// Fonctions de validation d'identifiants identiques à IntegratedVoiceDemo
struct DemoConfigResult {
    bool isValid = false;
    std::string errorMessage = "";
    VoiceProviderType provider = VoiceProviderType::GEMINI;
    const char* providerName = "Google Gemini Live";
};

inline bool testIsPlaceholderOrEmpty(const char* val) {
    if (val == nullptr || val[0] == '\0') return true;
    static const char* const s_placeholders[] = {
        "VOTRE_CLE_API_GEMINI",
        "VOTRE_CLE_API_OPENAI",
        "VOTRE_CLE_ICI",
        "VOTRE_SSID_WIFI",
        "VOTRE_MOT_DE_PASSE_WIFI",
        "VOTRE_MOT_DE_PASSE",
        "YOUR_API_KEY",
        "YOUR_WIFI_SSID",
        "YOUR_WIFI_PASSWORD"
    };
    for (const char* placeholder : s_placeholders) {
        if (strcmp(val, placeholder) == 0) return true;
    }
    return false;
}

inline DemoConfigResult testValidateCredentials(
    bool secretsFilePresent,
    bool macroConflict,
    bool useGemini,
    bool useOpenAi,
    const char* ssid,
    const char* password,
    const char* geminiKey,
    const char* openAiKey)
{
    DemoConfigResult res;

    // 1. secrets.h absent
    if (!secretsFilePresent) {
        res.isValid = false;
        res.errorMessage = "Fichier secrets.h absent.\nCopiez secrets.example.h vers secrets.h.";
        return res;
    }

    // 2. Conflit explicite de sélection
    if (macroConflict) {
        res.isValid = false;
        res.errorMessage = "Conflit dans secrets.h : USE_GEMINI et USE_OPENAI sont définis simultanément.";
        return res;
    }

    // 3. Fournisseur sélectionné
    if (!useGemini && !useOpenAi) {
        res.isValid = false;
        res.errorMessage = "Aucun fournisseur sélectionné dans secrets.h (définissez USE_GEMINI ou USE_OPENAI).";
        return res;
    }

    if (useOpenAi) {
        res.provider = VoiceProviderType::OPENAI;
        res.providerName = "OpenAI Realtime";
    } else {
        res.provider = VoiceProviderType::GEMINI;
        res.providerName = "Google Gemini Live";
    }

    // 4. Validation séparée du SSID Wi-Fi
    if (testIsPlaceholderOrEmpty(ssid)) {
        res.isValid = false;
        res.errorMessage = "SSID Wi-Fi non configuré dans secrets.h.";
        return res;
    }

    // 5. Validation séparée du mot de passe Wi-Fi
    if (testIsPlaceholderOrEmpty(password)) {
        res.isValid = false;
        res.errorMessage = "Mot de passe Wi-Fi non configuré dans secrets.h.";
        return res;
    }

    // 6. Validation séparée de la clé du fournisseur sélectionné (sans test trompeur de préfixe)
    if (res.provider == VoiceProviderType::GEMINI) {
        if (testIsPlaceholderOrEmpty(geminiKey)) {
            res.isValid = false;
            res.errorMessage = "Clé API Gemini Live manquante pour le fournisseur sélectionné.";
            return res;
        }
    } else {
        if (testIsPlaceholderOrEmpty(openAiKey)) {
            res.isValid = false;
            res.errorMessage = "Clé API OpenAI Realtime manquante pour le fournisseur sélectionné.";
            return res;
        }
    }

    res.isValid = true;
    return res;
}

inline void run_examples_minimal_corrective_v0652_tests() {
    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  [TEST SUITE 149] VoiceKit v0.6.5.2 — Examples Corrective  " << std::endl;
    std::cout << "===========================================================" << std::endl;

    // -------------------------------------------------------------------------
    // CORRECTION A : MinimalSTT
    // -------------------------------------------------------------------------
    std::cout << "\n--- TEST A1 : Capture normale MinimalSTT ---" << std::endl;
    {
        MinimalSttTestContext ctx;
        ctx.setupCallbacks();

        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyFakeKeyNormal";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

        MockLiveTransport mockTransport;
        ctx.voice.begin(cfg);
        prepareMockVoiceReady(ctx.voice, mockTransport);
        TEST_ASSERT(ctx.voice.isReady(), "A1.1 voice est READY");
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::READY, "A1.2 uiState est READY");

        // Appui DICTER
        ctx.simulateTouchPress();
        TEST_ASSERT(ctx.touchActive == true, "A1.3 touchActive=true apres appui");
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::LISTENING, "A1.4 uiState=LISTENING");
        TEST_ASSERT(ctx.voice.isListening(), "A1.5 voice.isListening()=true");

        // Relâchement DICTER
        ctx.simulateTouchRelease();
        TEST_ASSERT(ctx.touchActive == false, "A1.6 touchActive=false apres relachement");
        TEST_ASSERT(ctx.voice.isTranscribing(), "A1.7 voice.isTranscribing()=true");
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::TRANSCRIBING, "A1.8 uiState=TRANSCRIBING");

        // Réception transcription finale
        ctx.voice.handleIncomingTranscription("Bonjour le monde", true);
        TEST_ASSERT(ctx.finalText == "Bonjour le monde", "A1.9 Transcription finale reçue");
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::READY, "A1.10 uiState retourne a READY");
        TEST_ASSERT(ctx.captureHappened == false, "A1.11 captureHappened reinitialise");

        ctx.voice.getClient().setTransport(nullptr);
    }

    std::cout << "\n--- TEST A2 : Relâchement rapide avant capture active ---" << std::endl;
    {
        MinimalSttTestContext ctx;
        ctx.setupCallbacks();

        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyFakeKeyQuick";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

        MockLiveTransport mockTransport;
        ctx.voice.begin(cfg);
        prepareMockVoiceReady(ctx.voice, mockTransport);

        // État READY
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::READY, "A2.1 Initial READY");

        // Simulation appui rapide puis relâchement alors que l'écoute n'est pas ou plus en cours
        ctx.touchActive = true;
        ctx.simulateTouchRelease();
        TEST_ASSERT(ctx.touchActive == false, "A2.2 touchActive reinitialise");
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::READY, "A2.3 uiState reste READY");

        ctx.voice.getClient().setTransport(nullptr);
    }

    std::cout << "\n--- TEST A3 : Échec d'arrêt matériel du microphone ---" << std::endl;
    {
        class FailingAudioInputMock : public IAudioInput {
        public:
            bool running = false;
            bool fail = false;
            bool begin() override { running = true; return true; }
            void end() override { running = false; }
            void stop() override { running = false; }
            const char* getLastError() const override { return "Mock fail"; }
            bool isRunning() const override { return running; }
            int isRecording() const override { return fail ? 1 : 0; }
            bool record(int16_t*, size_t, uint32_t, bool) override { return true; }
        };

        FailingAudioInputMock failingInput;
        MinimalSttTestContext ctx;
        ctx.setupCallbacks();

        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyFakeKeyMicFail";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        cfg.audioInput = &failingInput;

        MockLiveTransport mockTransport;
        ctx.voice.begin(cfg);
        prepareMockVoiceReady(ctx.voice, mockTransport);

        ctx.simulateTouchPress();
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::LISTENING, "A3.1 En ecoute");

        // Forcer échec d'arrêt du micro
        failingInput.fail = true;

        ctx.simulateTouchRelease();
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::ERROR, "A3.2 uiState passe a ERROR suite echec stopListening");
        TEST_ASSERT(ctx.uiState != TestMinimalSttState::TRANSCRIBING, "A3.3 uiState n'est PAS ecrase par TRANSCRIBING");
        TEST_ASSERT(ctx.touchActive == false, "A3.4 touchActive=false apres echec");
        TEST_ASSERT(ctx.captureHappened == false, "A3.5 captureHappened=false");
        TEST_ASSERT(ctx.errorMessage.length() > 0, "A3.6 errorMessage non vide");

        failingInput.fail = false;
        HardwareManager::getInstance().releaseMicCaptureReservation();
        HardwareManager::getInstance().setAudioInput(nullptr);
        ctx.voice.getClient().setTransport(nullptr);
    }

    std::cout << "\n--- TEST A4 : Échec d'envoi de activityEnd (réseau) ---" << std::endl;
    {
        class FailingActivityEndTransportMock : public MockLiveTransport {
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

        MinimalSttTestContext ctx;
        ctx.setupCallbacks();

        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyFakeKeyActFail";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

        FailingActivityEndTransportMock failTransport;
        ctx.voice.begin(cfg);
        prepareMockVoiceReady(ctx.voice, failTransport);

        ctx.simulateTouchPress();
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::LISTENING, "A4.1 En ecoute");

        failTransport.failActivityEnd = true;
        ctx.simulateTouchRelease();
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::ERROR, "A4.2 uiState passe a ERROR suite echec activityEnd");
        TEST_ASSERT(ctx.uiState != TestMinimalSttState::TRANSCRIBING, "A4.3 uiState n'est PAS TRANSCRIBING");
        TEST_ASSERT(ctx.touchActive == false, "A4.4 touchActive=false");
        TEST_ASSERT(ctx.captureHappened == false, "A4.5 captureHappened=false");
        TEST_ASSERT(ctx.errorMessage.find("activityEnd") != std::string::npos ||
                    ctx.errorMessage.find("Echec") != std::string::npos, "A4.6 Message d'erreur explicite");

        ctx.voice.getClient().setTransport(nullptr);
    }

    std::cout << "\n--- TEST A5 : Double relâchement tactile ---" << std::endl;
    {
        MinimalSttTestContext ctx;
        ctx.setupCallbacks();

        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyFakeKeyDouble";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

        MockLiveTransport mockTransport;
        ctx.voice.begin(cfg);
        prepareMockVoiceReady(ctx.voice, mockTransport);

        ctx.simulateTouchPress();
        TEST_ASSERT(ctx.touchActive == true, "A5.1 touchActive=true");

        // Premier relâchement
        ctx.simulateTouchRelease();
        TEST_ASSERT(ctx.touchActive == false, "A5.2 touchActive=false");
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::TRANSCRIBING, "A5.3 uiState=TRANSCRIBING");

        // Deuxième relâchement (rebond / double événement)
        ctx.simulateTouchRelease();
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::TRANSCRIBING, "A5.4 uiState inchange par 2e relachement");

        ctx.voice.getClient().setTransport(nullptr);
    }

    std::cout << "\n--- TEST A6 : Nouvelle dictée après récupération ---" << std::endl;
    {
        class FailingAudioInputRecover : public IAudioInput {
        public:
            bool running = false;
            bool fail = false;
            bool begin() override { running = true; return true; }
            void end() override { running = false; }
            void stop() override { running = false; }
            const char* getLastError() const override { return "Mock fail"; }
            bool isRunning() const override { return running; }
            int isRecording() const override { return fail ? 1 : 0; }
            bool record(int16_t*, size_t, uint32_t, bool) override { return true; }
        };

        FailingAudioInputRecover failingInput;
        MinimalSttTestContext ctx;
        ctx.setupCallbacks();

        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyFakeKeyRecover";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        cfg.audioInput = &failingInput;

        MockLiveTransport mockTransport;
        ctx.voice.begin(cfg);
        prepareMockVoiceReady(ctx.voice, mockTransport);

        // 1. Déclenchement d'une erreur
        ctx.simulateTouchPress();
        failingInput.fail = true;
        ctx.simulateTouchRelease();
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::ERROR, "A6.1 Etat ERROR suite echec");
        failingInput.fail = false;

        // 2. Tant que le moteur est en erreur, simulateTouchResetOnError maintient ERROR (Règle 5)
        ctx.simulateTouchResetOnError();
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::ERROR, "A6.2 Maintien ERROR tant que moteur non retabli");

        // 3. Rétablissement du moteur via stop() et release reservation
        HardwareManager::getInstance().releaseMicCaptureReservation();
        ctx.voice.stop();
        TEST_ASSERT(ctx.voice.isReady(), "A6.3 Voice est retabli et ready");

        // 4. Toucher de réinitialisation après rétablissement
        ctx.simulateTouchResetOnError();
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::READY, "A6.4 uiState retabli a READY");
        TEST_ASSERT(ctx.errorMessage.isEmpty(), "A6.5 errorMessage vide");

        // 5. Nouvelle dictée
        ctx.simulateTouchPress();
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::LISTENING, "A6.6 Nouvelle dictee demarree");
        ctx.simulateTouchRelease();
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::TRANSCRIBING, "A6.7 Relachement normal");

        HardwareManager::getInstance().releaseMicCaptureReservation();
        HardwareManager::getInstance().setAudioInput(nullptr);
        ctx.voice.getClient().setTransport(nullptr);
    }

    std::cout << "\n--- TEST A7 : Réception immédiate de transcription finale pendant stopListening ---" << std::endl;
    {
        MinimalSttTestContext ctx;
        ctx.setupCallbacks();

        VoiceKitConfig cfg;
        cfg.apiKey = "AIzaSyFakeKeyImmed";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

        MockLiveTransport mockTransport;
        ctx.voice.begin(cfg);
        prepareMockVoiceReady(ctx.voice, mockTransport);

        ctx.simulateTouchPress();
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::LISTENING, "A7.1 En ecoute");

        // Simulation réception transcription finale émise lors du callback de fin
        ctx.voice.onListeningFinished([&ctx]() {
            ctx.voice.handleIncomingTranscription("Reponse immediate", true);
        });

        ctx.simulateTouchRelease();
        TEST_ASSERT(ctx.finalText == "Reponse immediate", "A7.2 Transcription recue");
        TEST_ASSERT(ctx.uiState == TestMinimalSttState::READY, "A7.3 uiState n'est PAS ecrase par TRANSCRIBING");

        ctx.voice.getClient().setTransport(nullptr);
    }

    // -------------------------------------------------------------------------
    // CORRECTION B : IntegratedVoiceDemo Credentials Validation
    // -------------------------------------------------------------------------
    std::cout << "\n--- TEST B1 : secrets.h absent ---" << std::endl;
    {
        DemoConfigResult res = testValidateCredentials(
            false,  // secretsFilePresent = false
            false,
            true,
            false,
            "MonSSID",
            "MonPass",
            "CleValide123",
            ""
        );
        TEST_ASSERT(!res.isValid, "B1.1 Rejete si secrets.h absent");
        TEST_ASSERT(res.errorMessage.find("secrets.h absent") != std::string::npos, "B1.2 Message explicite fichier absent");
    }

    std::cout << "\n--- TEST B2 : secrets.h avec les valeurs du modèle (placeholders) ---" << std::endl;
    {
        const char* placeholders[] = {
            "VOTRE_CLE_API_GEMINI",
            "VOTRE_CLE_API_OPENAI",
            "VOTRE_CLE_ICI"
        };
        for (const char* ph : placeholders) {
            DemoConfigResult resG = testValidateCredentials(true, false, true, false, "MonSSID", "MonPass", ph, "");
            TEST_ASSERT(!resG.isValid, "B2.1 Rejet cle Gemini placeholder: " + std::string(ph));

            DemoConfigResult resO = testValidateCredentials(true, false, false, true, "MonSSID", "MonPass", "", ph);
            TEST_ASSERT(!resO.isValid, "B2.2 Rejet cle OpenAI placeholder: " + std::string(ph));
        }

        // Placeholders SSID / Password
        DemoConfigResult resSSID = testValidateCredentials(true, false, true, false, "VOTRE_SSID_WIFI", "MonPass", "CleValide", "");
        TEST_ASSERT(!resSSID.isValid, "B2.3 Rejet SSID placeholder");

        DemoConfigResult resPass = testValidateCredentials(true, false, true, false, "MonSSID", "VOTRE_MOT_DE_PASSE_WIFI", "CleValide", "");
        TEST_ASSERT(!resPass.isValid, "B2.4 Rejet Pass placeholder");
    }

    std::cout << "\n--- TEST B3 : Clé Gemini valide en apparence et fournisseur Gemini sélectionné ---" << std::endl;
    {
        // Sans vérification de préfixe arbitraire (Règle 7)
        DemoConfigResult res = testValidateCredentials(
            true, false, true, false,
            "Livebox-1234", "secretwifi",
            "cle_gemini_reelle_sans_prefixe_force_xyz",
            ""
        );
        TEST_ASSERT(res.isValid, "B3.1 Configuration Gemini valide");
        TEST_ASSERT(res.provider == VoiceProviderType::GEMINI, "B3.2 Fournisseur Gemini retenu");
        TEST_ASSERT(strcmp(res.providerName, "Google Gemini Live") == 0, "B3.3 Nom Gemini conforme");
    }

    std::cout << "\n--- TEST B4 : Clé OpenAI valide en apparence et fournisseur OpenAI sélectionné ---" << std::endl;
    {
        // Sans vérification de préfixe arbitraire (Règle 7)
        DemoConfigResult res = testValidateCredentials(
            true, false, false, true,
            "Freebox-5678", "secretwifi2",
            "",
            "cle_openai_sans_prefixe_force_abc"
        );
        TEST_ASSERT(res.isValid, "B4.1 Configuration OpenAI valide");
        TEST_ASSERT(res.provider == VoiceProviderType::OPENAI, "B4.2 Fournisseur OpenAI retenu");
        TEST_ASSERT(strcmp(res.providerName, "OpenAI Realtime") == 0, "B4.3 Nom OpenAI conforme");
    }

    std::cout << "\n--- TEST B5 : Fournisseur sélectionné sans clé (non-bascule silencieuse) ---" << std::endl;
    {
        // Gemini sélectionné sans clé Gemini, alors qu'une clé OpenAI est présente
        DemoConfigResult resG = testValidateCredentials(
            true, false, true, false,
            "MonSSID", "MonPass",
            "", // Pas de clé Gemini
            "CleOpenAIPresenteMaisNonSelectionnee"
        );
        TEST_ASSERT(!resG.isValid, "B5.1 Rejet si Gemini sans cle");
        TEST_ASSERT(resG.errorMessage.find("Gemini") != std::string::npos, "B5.2 Erreur explicite cle Gemini manquante");
        TEST_ASSERT(resG.provider == VoiceProviderType::GEMINI, "B5.3 Pas de bascule silencieuse vers OpenAI");

        // OpenAI sélectionné sans clé OpenAI, alors qu'une clé Gemini est présente
        DemoConfigResult resO = testValidateCredentials(
            true, false, false, true,
            "MonSSID", "MonPass",
            "CleGeminiPresenteMaisNonSelectionnee",
            "" // Pas de clé OpenAI
        );
        TEST_ASSERT(!resO.isValid, "B5.4 Rejet si OpenAI sans cle");
        TEST_ASSERT(resO.errorMessage.find("OpenAI") != std::string::npos, "B5.5 Erreur explicite cle OpenAI manquante");
        TEST_ASSERT(resO.provider == VoiceProviderType::OPENAI, "B5.6 Pas de bascule silencieuse vers Gemini");
    }

    std::cout << "\n--- TEST B6 : Deux macros USE_GEMINI et USE_OPENAI définies (conflit) ---" << std::endl;
    {
        DemoConfigResult res = testValidateCredentials(
            true,
            true, // macroConflict = true
            true,
            true,
            "MonSSID", "MonPass",
            "CleGeminiValide", "CleOpenAiValide"
        );
        TEST_ASSERT(!res.isValid, "B6.1 Rejet en cas de conflit des deux macros");
        TEST_ASSERT(res.errorMessage.find("Conflit") != std::string::npos, "B6.2 Message explicite de conflit");
    }

    std::cout << "\n--- TEST B7 : Wi-Fi non configuré (SSID ou mot de passe) ---" << std::endl;
    {
        // SSID vide
        DemoConfigResult resNoSsid = testValidateCredentials(
            true, false, true, false,
            "", "MonPass",
            "CleGeminiValide", ""
        );
        TEST_ASSERT(!resNoSsid.isValid, "B7.1 Rejet SSID vide");
        TEST_ASSERT(resNoSsid.errorMessage.find("SSID") != std::string::npos, "B7.2 Message explicite SSID");

        // Password vide
        DemoConfigResult resNoPass = testValidateCredentials(
            true, false, true, false,
            "MonSSID", "",
            "CleGeminiValide", ""
        );
        TEST_ASSERT(!resNoPass.isValid, "B7.3 Rejet mot de passe vide");
        TEST_ASSERT(resNoPass.errorMessage.find("passe") != std::string::npos, "B7.4 Message explicite mot de passe");
    }

    std::cout << "\n--- TEST B8 : Aucune divulgation de clé API dans les messages ---" << std::endl;
    {
        const char* secretKey = "SECRET_SUPER_CONFIDENTIAL_KEY_999";
        DemoConfigResult res = testValidateCredentials(
            true, false, true, false,
            "", "MonPass", // SSID invalide
            secretKey, ""
        );
        TEST_ASSERT(!res.isValid, "B8.1 Invalide");
        TEST_ASSERT(res.errorMessage.find(secretKey) == std::string::npos, "B8.2 La cle secrete n'apparait JAMAIS dans errorMessage");
    }

    std::cout << "\n[TEST SUITE 149] SUCCES INTEGRAL DES TESTS CORRECTIFS EXEMPLES" << std::endl;
}
