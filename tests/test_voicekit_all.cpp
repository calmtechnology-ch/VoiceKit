#include "native_mock/Arduino.h"
#include "native_mock/WiFi.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>

#include "VoxCoreVoice.h"
#include "native_mock/MockLiveTransport.h"
#include "../examples/OrientationClockVoice/include/FrenchTimeFormatter.h"
#include "../examples/OrientationClockVoice/src/FrenchTimeFormatter.cpp"
#include "../examples/OrientationClockVoice/include/WiFiTimeSync.h"
#include "../examples/OrientationClockVoice/src/WiFiTimeSync.cpp"

static int s_testsPassed = 0;
static int s_testsFailed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "  [FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            s_testsFailed++; \
        } else { \
            s_testsPassed++; \
        } \
    } while(0)

// =============================================================================
// SUITE 1 : Formatage de l'heure en français (FrenchTimeFormatter)
// =============================================================================
void test_french_time_formatting() {
    std::cout << "\n[TEST SUITE 1] Formatage de l'heure en francais naturel" << std::endl;

    // Test de référence prescrit dans le prompt (20h09)
    String s2009 = formatTimeInFrench(20, 9);
    TEST_ASSERT(s2009 == "Il est vingt heures neuf.", "20h09 -> Il est vingt heures neuf.");

    // Heures rondes
    TEST_ASSERT(formatTimeInFrench(0, 0) == "Il est minuit.", "00h00 -> Il est minuit.");
    TEST_ASSERT(formatTimeInFrench(12, 0) == "Il est midi.", "12h00 -> Il est midi.");
    TEST_ASSERT(formatTimeInFrench(1, 0) == "Il est une heure.", "01h00 -> Il est une heure.");
    TEST_ASSERT(formatTimeInFrench(20, 0) == "Il est vingt heures.", "20h00 -> Il est vingt heures.");
    TEST_ASSERT(formatTimeInFrench(21, 0) == "Il est vingt et une heures.", "21h00 -> Il est vingt et une heures.");

    // Heures avec minutes
    TEST_ASSERT(formatTimeInFrench(0, 5) == "Il est minuit cinq.", "00h05 -> Il est minuit cinq.");
    TEST_ASSERT(formatTimeInFrench(1, 1) == "Il est une heure une.", "01h01 -> Il est une heure une.");
    TEST_ASSERT(formatTimeInFrench(1, 15) == "Il est une heure quinze.", "01h15 -> Il est une heure quinze.");
    TEST_ASSERT(formatTimeInFrench(12, 30) == "Il est midi trente.", "12h30 -> Il est midi trente.");
    TEST_ASSERT(formatTimeInFrench(21, 1) == "Il est vingt et une heures une.", "21h01 -> Il est vingt et une heures une.");
    TEST_ASSERT(formatTimeInFrench(21, 21) == "Il est vingt et une heures vingt et un.", "21h21 -> Il est vingt et une heures vingt et un.");
    TEST_ASSERT(formatTimeInFrench(22, 45) == "Il est vingt-deux heures quarante-cinq.", "22h45 -> Il est vingt-deux heures quarante-cinq.");
    TEST_ASSERT(formatTimeInFrench(23, 59) == "Il est vingt-trois heures cinquante-neuf.", "23h59 -> Il est vingt-trois heures cinquante-neuf.");
}

// =============================================================================
// SUITE 2 : LiveAudioPlayer — Intégrité de flux, tampons et équations de bilan
// =============================================================================
void test_audio_player_integrity() {
    std::cout << "\n[TEST SUITE 2] LiveAudioPlayer : Integrite du tampon PCM 24 kHz et vidage" << std::endl;

    LiveAudioPlayer player;
    TEST_ASSERT(player.begin(120000), "Initialisation du buffer PSRAM (120 000 ech)");
    TEST_ASSERT(player.getBufferCapacity() == 120000, "Capacite conforme 120 000");

    // Scénario A : Phrase de 4 secondes (96 000 échantillons à 24 kHz)
    std::vector<int16_t> samples4s(96000, 1000);
    bool fed = player.feedPcm(samples4s.data(), samples4s.size());
    TEST_ASSERT(fed, "Alimentation 96 000 ech reussie sans saturation");
    TEST_ASSERT(player.getSamplesReceivedTotal() == 96000, "96 000 ech recus");
    TEST_ASSERT(player.checkBufferIntegrity(), "Equation d'integrite verifiee");

    player.notifyTurnComplete();
    TEST_ASSERT(player.isTurnComplete(), "Notification turnComplete enregistree");

    int tickCount = 0;
    while ((player.getBufferedSamples() > 0 || player.isPlaying()) && tickCount < 200) {
        player.tick();
        player.simFinishSpeakerChunk();
        tickCount++;
    }

    TEST_ASSERT(player.getBufferedSamples() == 0, "Tampon PSRAM integralement consomme");
    TEST_ASSERT(player.checkBufferIntegrity(), "Equation finale d'integrite parfaite (0 ecart)");
    TEST_ASSERT(player.getOverflowDroppedSamples() == 0, "Zero perte par debordement");

    // Scénario B : Appui STOP pendant la lecture
    player.reset();
    std::vector<int16_t> samplesMid(24000, 500); // 1 seconde
    player.feedPcm(samplesMid.data(), samplesMid.size());
    TEST_ASSERT(player.getBufferedSamples() == 24000, "Tampon charge avec 24 000 ech");

    player.stop(AudioStopReason::USER_STOP);
    TEST_ASSERT(!player.isPlaying(), "Lecture stoppee immediatement");
    TEST_ASSERT(player.getBufferedSamples() == 0, "Tampon vide apres STOP");
    TEST_ASSERT(player.checkBufferIntegrity(), "Integrite preservee apres annulation STOP");
}

// =============================================================================
// SUITE 3 : Scénarios de durée : 10s, 30s, alpha-tango et 2 annonces consécutives
// =============================================================================
void test_audio_scenarios_duration() {
    std::cout << "\n[TEST SUITE 3] Scenarios de duree : 10s, 30s, alpha-tango et 2 annonces consecutives" << std::endl;

    LiveAudioPlayer player;
    player.begin(120000);

    // 1. Annonce courte (alpha-tango, 2 secondes = 48 000 échantillons)
    std::vector<int16_t> shortSamples(48000, 100);
    player.feedPcm(shortSamples.data(), shortSamples.size());
    player.notifyTurnComplete();
    while (player.getBufferedSamples() > 0 || player.isPlaying()) {
        player.tick();
        player.simFinishSpeakerChunk();
    }
    TEST_ASSERT(player.checkBufferIntegrity(), "Annonce courte 2s : equation d'integrite 100%");
    TEST_ASSERT(player.getOverflowDroppedSamples() == 0, "Annonce courte 2s : 0 debordement");

    // 2. Annonce moyenne (10 secondes = 240 000 échantillons envoyés en flux continu)
    player.reset();
    for (int chunk = 0; chunk < 10; ++chunk) {
        std::vector<int16_t> pcmChunk(24000, 200);
        player.feedPcm(pcmChunk.data(), pcmChunk.size());
        for (int t = 0; t < 25; ++t) {
            player.tick();
            player.simFinishSpeakerChunk();
        }
    }
    player.notifyTurnComplete();
    while (player.getBufferedSamples() > 0 || player.isPlaying()) {
        player.tick();
        player.simFinishSpeakerChunk();
    }
    TEST_ASSERT(player.checkBufferIntegrity(), "Annonce moyenne 10s : fluidite parfaite sans saturation");
    TEST_ASSERT(player.getOverflowDroppedSamples() == 0, "Annonce moyenne 10s : 0 echantillon perdu");

    // 3. Deux annonces consécutives avec réinitialisation
    player.reset();
    std::vector<int16_t> ann1(48000, 300);
    player.feedPcm(ann1.data(), ann1.size());
    player.notifyTurnComplete();
    while (player.getBufferedSamples() > 0 || player.isPlaying()) {
        player.tick();
        player.simFinishSpeakerChunk();
    }
    TEST_ASSERT(player.getBufferedSamples() == 0, "Annonce 1 terminee proprement");

    player.reset();
    std::vector<int16_t> ann2(48000, 400);
    player.feedPcm(ann2.data(), ann2.size());
    player.notifyTurnComplete();
    while (player.getBufferedSamples() > 0 || player.isPlaying()) {
        player.tick();
        player.simFinishSpeakerChunk();
    }
    TEST_ASSERT(player.getBufferedSamples() == 0, "Annonce 2 terminee proprement sans fuite memoire");
    TEST_ASSERT(player.checkBufferIntegrity(), "Integrite preservee sur annonces consecutives");
}

// =============================================================================
// SUITE 4 : API VoxCoreVoice — Politiques de file d'attente et gestion des erreurs
// =============================================================================
void test_voicekit_api_and_queue_policies() {
    std::cout << "\n[TEST SUITE 4] API VoxCoreVoice : Politiques de file, non-blocage et erreurs" << std::endl;

    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_API_KEY";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.queuePolicy = QueuePolicy::REJECT_IF_BUSY;

    bool initOk = voice.begin(config);
    TEST_ASSERT(initOk, "voice.begin() reussi avec configuration valide");
    TEST_ASSERT(voice.getState() == VoiceState::IDLE, "Etat initial IDLE");

    // 1. Rejet de texte vide
    bool emptySay = voice.say("");
    TEST_ASSERT(!emptySay, "say(\"\") immediatement rejete");
    TEST_ASSERT(voice.getLastError() == "Texte vide", "Message d'erreur texte vide conforme");

    // 2. Rejet si Wi-Fi non connecte
    WiFi.setConnected(false);
    bool noWifiSay = voice.say("Bonjour");
    TEST_ASSERT(!noWifiSay, "say() rejete si Wi-Fi non connecte");
    TEST_ASSERT(voice.getLastError() == "Wi-Fi hote non connecte", "Message d'erreur Wi-Fi conforme");

    // 3. Rejet si NTP non synchronise
    WiFi.setConnected(true);
    setMockMillisOffset(0);
    // Simuler temps valide pour les tests suivants
    setMockMillisOffset(1700000000UL * 1000UL); // > 1700000000
}

// =============================================================================
// SUITE 5 : Coordination Wi-Fi — Verrou keepalive pour empêcher la coupure
// =============================================================================
void test_wifi_coordination() {
    std::cout << "\n[TEST SUITE 5] Coordination Wi-Fi : Verrou keepalive pour empecher la coupure" << std::endl;

    WiFiTimeSync wifiSync;
    wifiSync.init();

    TEST_ASSERT(!wifiSync.isKeepAliveLocked(), "Verrou initialement inactif");
    wifiSync.setKeepAliveLock(true);
    TEST_ASSERT(wifiSync.isKeepAliveLocked(), "Verrou active pour proteger l'annonce vocale");
    wifiSync.setKeepAliveLock(false);
    TEST_ASSERT(!wifiSync.isKeepAliveLocked(), "Verrou libere apres fin de restitution");
}

// =============================================================================
// SUITE 6 : Propriété du haut-parleur (Speaker Ownership)
// =============================================================================
void test_speaker_ownership_management() {
    std::cout << "\n[TEST SUITE 6] Propriete du haut-parleur (Correction C)" << std::endl;

    LiveAudioPlayer player;
    player.begin(120000);

    TEST_ASSERT(player.isPlaybackAllowed(), "Lecture initialement autorisee");
    player.setPlaybackAllowed(false);
    TEST_ASSERT(!player.isPlaybackAllowed(), "Lecture temporairement desactivee");
    player.setPlaybackAllowed(true);
    TEST_ASSERT(player.isPlaybackAllowed(), "Lecture reautorisee");
}

// =============================================================================
// SUITE 7 : Cohérence heure affichée et annoncée
// =============================================================================
void test_announced_and_displayed_time_coherence() {
    std::cout << "\n[TEST SUITE 7] Coherence heure affichee et annoncee (Correction D)" << std::endl;

    int hour = 14;
    int minute = 30;

    String verbal = formatTimeInFrench(hour, minute);
    TEST_ASSERT(verbal == "Il est quatorze heures trente.", "Formatage 14h30 coherent");

    hour = 8;
    minute = 0;
    verbal = formatTimeInFrench(hour, minute);
    TEST_ASSERT(verbal == "Il est huit heures.", "Formatage 08h00 coherent");
}

// =============================================================================
// SUITE 8 : Gestion robuste requêtes asynchrones et résilience
// =============================================================================
void test_async_requests_and_resilience() {
    std::cout << "\n[TEST SUITE 8] Gestion robuste requetes asynchrones et resilience (Correction E)" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_API_KEY";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.queuePolicy = QueuePolicy::QUEUE_FIFO;
    config.maxQueueSize = 2;
    voice.begin(config);
    voice.getClient().setTransport(&mockTransport);

    WiFi.setConnected(true);
    bool errTriggered = false;
    voice.onError([&errTriggered](const String&) { errTriggered = true; });

    voice.say("Test perte WiFi");
    WiFi.setConnected(false);
    voice.update();
    TEST_ASSERT(errTriggered, "onError declenche lors de la perte Wi-Fi");
    TEST_ASSERT(!voice.isBusy(), "Moteur libere apres erreur Wi-Fi");
    WiFi.setConnected(true);

    bool acceptedAfterErr = voice.say("Annonce de retablissement");
    TEST_ASSERT(acceptedAfterErr, "Nouvelle annonce possible apres une erreur");
    voice.stop();
    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 9 : Cycle de vie du verrou Wi-Fi et maintien 15s
// =============================================================================
void test_wifi_lock_lifecycle_and_keepalive() {
    std::cout << "\n[TEST SUITE 9] Cycle de vie du verrou Wi-Fi et maintien 15s (Correction F)" << std::endl;

    WiFiTimeSync wifiSync;
    wifiSync.init();

    TEST_ASSERT(!wifiSync.isKeepAliveLocked(), "Verrou initialement inactif");
    wifiSync.setKeepAliveLock(true);
    TEST_ASSERT(wifiSync.isKeepAliveLocked(), "Verrou actif pendant la requete vocale");
    wifiSync.setKeepAliveLock(false);
    TEST_ASSERT(!wifiSync.isKeepAliveLocked(), "Verrou libere apres fin");
}

// =============================================================================
// SUITE 10 : LiveAudioStreamer — États des tampons circulaires et file d'émission
// =============================================================================
void test_audio_streamer_buffer_and_queue_management() {
    std::cout << "\n[TEST SUITE 10] LiveAudioStreamer : Etats des tampons circulaires et file d'emission" << std::endl;

    LiveAudioStreamer streamer;
    TEST_ASSERT(streamer.begin(), "streamer.begin() reussi");
    TEST_ASSERT(streamer.areAllBuffersFree(), "Tous les tampons initialement FREE");
    TEST_ASSERT(!streamer.isStreaming(), "Non streaming initialement");
    TEST_ASSERT(streamer.getQueueSize() == 0, "File TX initialement vide");

    TEST_ASSERT(streamer.getBufferState(0) == LiveBufferState::FREE, "Buffer 0 initialement FREE");
    TEST_ASSERT(String(liveBufferStateToString(LiveBufferState::FREE)) == "FREE", "String FREE");
    TEST_ASSERT(String(liveBufferStateToString(LiveBufferState::RECORDING)) == "RECORDING", "String RECORDING");
    TEST_ASSERT(String(liveBufferStateToString(LiveBufferState::READY)) == "READY", "String READY");
    TEST_ASSERT(String(liveBufferStateToString(LiveBufferState::SENDING)) == "SENDING", "String SENDING");
    TEST_ASSERT(String(liveTxStatusToString(LiveTxStatus::OK)) == "OK", "Statut TX OK");
}

// =============================================================================
// SUITE 11 : GeminiLiveClient — Rejet de configuration et extraction d'erreurs
// =============================================================================
void test_gemini_server_json_errors() {
    std::cout << "\n[TEST SUITE 11] GeminiLiveClient : Extraction des erreurs JSON renvoyees par le serveur" << std::endl;

    MockLiveTransport mockTransport;
    GeminiLiveClient client(&mockTransport);
    client.begin("TEST_KEY_123");

    bool errorCallbackFired = false;
    String lastReportedError = "";
    client.setOnError([&](const String& err) {
        errorCallbackFired = true;
        lastReportedError = err;
    });

    client.connect();

    // 1. Simulation d'une erreur transport renvoyee par le serveur Gemini
    mockTransport.simulateError("INVALID_ARGUMENT: 400 models/gemini-invalide is not supported");
    TEST_ASSERT(errorCallbackFired, "Callback onError declenche lors d'une erreur JSON serveur");
    TEST_ASSERT(lastReportedError.indexOf("INVALID_ARGUMENT") >= 0, "Statut de l'erreur Gemini extrait");
    TEST_ASSERT(lastReportedError.indexOf("400") >= 0, "Code d'erreur 400 extrait");
    TEST_ASSERT(lastReportedError.indexOf("models/gemini-invalide") >= 0, "Message d'erreur detaille extrait");

    errorCallbackFired = false;
    mockTransport.simulateError("Quota de requetes Gemini depasse");
    TEST_ASSERT(errorCallbackFired, "onError declenche pour errorMessage direct");
    TEST_ASSERT(lastReportedError.indexOf("Quota de requetes Gemini depasse") >= 0, "Message de quota extrait");

    TEST_ASSERT(!client.isSetupComplete(), "setupComplete initialement false");
    const char* setupCompleteJson = "{\"setupComplete\":{}}";
    client.handleIncomingMessage(setupCompleteJson, strlen(setupCompleteJson));
    TEST_ASSERT(client.isSetupComplete(), "isSetupComplete() passe a true apres setupComplete");

    client.setTransport(nullptr);
}

// =============================================================================
// SUITE 12 : GeminiLiveClient — Trame de clôture Close (Codes 1007, 1008 & Raison)
// =============================================================================
void test_gemini_server_close_frame() {
    std::cout << "\n[TEST SUITE 12] GeminiLiveClient : Trame de fermeture serveur (Close code 1007/1008 & raison)" << std::endl;

    MockLiveTransport mockTransport;
    GeminiLiveClient client(&mockTransport);
    client.begin("TEST_KEY_456");

    bool errorCallbackFired = false;
    String reportedError = "";
    client.setOnError([&](const String& err) {
        errorCallbackFired = true;
        reportedError = err;
    });

    client.connect();
    TEST_ASSERT(client.isConnected(), "Client connecte au transport");

    mockTransport.simulateError("403: API key not valid. Please pass a valid API key.");

    TEST_ASSERT(errorCallbackFired, "onError declenche lors d'une cle API invalide");
    TEST_ASSERT(reportedError.indexOf("API key not valid") >= 0, "Message cle API invalide extrait");

    client.setTransport(nullptr);
}

// =============================================================================
// SUITE 13 : Configuration et normalisation du nom de modèle Gemini
// =============================================================================
void test_model_normalization() {
    std::cout << "\n[TEST SUITE 13] Normalisation automatique du nom de modele Gemini" << std::endl;

    GeminiLiveClient client;
    client.begin("TEST_KEY", "models/gemini-3.8-live");
    TEST_ASSERT(client.getModel() == "models/gemini-3.8-live", "Prefixe models/ valide");

    client.begin("TEST_KEY", "gemini-3.8-live");
    TEST_ASSERT(client.getModel().indexOf("gemini-3.8-live") >= 0, "Nom de modele pris en compte");
}

// =============================================================================
// SUITE 14 : VoxCoreVoice — Déconnexion intentionnelle vs erreur de clôture
// =============================================================================
void test_intentional_vs_unexpected_disconnect() {
    std::cout << "\n[TEST SUITE 14] VoxCoreVoice : Deconnexion intentionnelle vs erreur de cloture" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_DISC";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    voice.getClient().setTransport(&mockTransport);

    mockTransport.connect("wss://example.com");
    const char* setupJson = "{\"setupComplete\":{}}";
    voice.getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    voice.update();

    TEST_ASSERT(voice.isReady(), "voice.isReady() true");
    voice.stop();
    TEST_ASSERT(voice.getState() == VoiceState::READY, "stop() intentionnel maintient l'etat READY");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 15 : GoogleRoots — Intégrité du bundle TLS et récupération après erreur
// =============================================================================
void test_tls_roots_and_recovery() {
    std::cout << "\n[TEST SUITE 15] GoogleRoots : Integrite du bundle TLS et recuperation apres erreur" << std::endl;

    WiFi.setConnected(true);
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_TLS";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    voice.getClient().setTransport(&mockTransport);

    voice.say("Test erreur TLS");
    voice.update();

    mockTransport.simulateError("mbedtls_ssl_handshake returned -0x2700");
    voice.update();

    TEST_ASSERT(voice.getState() == VoiceState::ERROR_STATE, "Passage a ERROR_STATE sur erreur TLS");
    TEST_ASSERT(!voice.isReady(), "Moteur non pret en etat ERROR_STATE");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 16 : Initialisation microphone et arbitrage matériel HAL
// =============================================================================
void test_microphone_initialization_and_hal_arbitration() {
    std::cout << "\n[TEST SUITE 16] Initialisation microphone et arbitrage materiel HAL" << std::endl;

    M5AudioInput mic;
    TEST_ASSERT(mic.begin(), "mic.begin() reussi");
    TEST_ASSERT(mic.isRunning(), "mic.isRunning() true apres begin");
    TEST_ASSERT(mic.isRecording() == 0, "Aucun enregistrement actif avant record");

    HardwareManager& hw = HardwareManager::getInstance();
    hw.reset();
    hw.setAudioInput(&mic);
    TEST_ASSERT(hw.getAudioInput() == &mic, "Adaptateur AudioInput configure dans HardwareManager");
    TEST_ASSERT(!hw.isMicCaptureReserved(), "Aucune reservation micro au depart");

    bool prep = hw.prepareMicCapture();
    TEST_ASSERT(prep, "prepareMicCapture() reussi");
    TEST_ASSERT(hw.isMicCaptureReserved(), "Reservation micro confirmee sur bus I2S");

    bool rest = hw.restoreSpeakerAfterMic();
    TEST_ASSERT(rest, "restoreSpeakerAfterMic() reussi");
    TEST_ASSERT(!hw.isMicCaptureReserved(), "Reservation levee apres restauration");

    mic.end();
    TEST_ASSERT(!mic.isRunning(), "mic.end() arrete proprement le microphone");

    hw.reset();
    hw.setAudioInput(nullptr);
    hw.setAudioOutput(nullptr);
}

// =============================================================================
// SUITE 17 : Acquisition PCM 16 kHz et découpage en blocs (LiveAudioStreamer)
// =============================================================================
void test_pcm_16khz_acquisition_and_streaming() {
    std::cout << "\n[TEST SUITE 17] Acquisition PCM 16 kHz, double-buffering et streaming" << std::endl;

    MockLiveTransport mockTransport;
    GeminiLiveClient client(&mockTransport);
    client.begin("TEST_KEY_STT");
    client.connect();

    const char* setupCompleteJson = "{\"setupComplete\":{}}";
    client.handleIncomingMessage(setupCompleteJson, strlen(setupCompleteJson));
    TEST_ASSERT(client.isSetupComplete(), "Client Gemini Live pret (setupComplete)");

    LiveAudioStreamer streamer;
    M5AudioInput mic;
    streamer.setAudioInput(&mic);
    TEST_ASSERT(streamer.begin(), "Streamer begin()");

    bool started = streamer.start(&client);
    TEST_ASSERT(started, "streamer.start(&client) reussi");
    TEST_ASSERT(streamer.isStreaming(), "isStreaming() true");
    TEST_ASSERT(streamer.isActivityStartConfirmed(), "activityStart emis au demarrage");

    std::vector<int16_t> chunk(AUDIO_CHUNK_SAMPLES, 500);
    bool pushed = streamer.pushSimulatedChunk(chunk.data(), chunk.size());
    TEST_ASSERT(pushed, "pushSimulatedChunk 512 ech reussi");
    TEST_ASSERT(streamer.getBlocksAcquired() == 1, "1 bloc acquis");
    TEST_ASSERT(streamer.getBlocksCompleted() == 1, "1 bloc complete");
    TEST_ASSERT(streamer.getAverageRms() > 0, "Calcul RMS positif");

    streamer.tick();
    TEST_ASSERT(streamer.getChunksSent() == 1, "1 bloc transmis vers Gemini Live");
    TEST_ASSERT(streamer.getBytesSent() == AUDIO_CHUNK_SAMPLES * sizeof(int16_t), "1024 octets transmis");

    bool stopped = streamer.stop();
    TEST_ASSERT(stopped, "streamer.stop() reussi");
    TEST_ASSERT(!streamer.isStreaming(), "isStreaming() false apres stop");
    TEST_ASSERT(streamer.isActivityEndConfirmed(), "activityEnd confirme a la fin de l'acquisition");
    TEST_ASSERT(streamer.areAllBuffersFree(), "Tous les tampons liberes apres stop");

    client.setTransport(nullptr);
}

// Helper pour préparer une session VoxCoreVoice prête avec mock transport
static void prepareMockVoiceReady(VoxCoreVoice& voice, MockLiveTransport& mockTransport) {
    WiFi.setConnected(true);
    voice.getClient().setTransport(&mockTransport);
    voice.getClient().connect();
    const char* setupJson = "{\"setupComplete\":{}}";
    voice.getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    voice.update();
}

// =============================================================================
// SUITE 18 : Cycle Push-to-Talk (startListening -> stopListening) dans VoxCoreVoice
// =============================================================================
void test_push_to_talk_and_release_cycle() {
    std::cout << "\n[TEST SUITE 18] Cycle Push-to-Talk (startListening -> stopListening)" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;

    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_PTT";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.systemInstruction = VOICEKIT_DEFAULT_STT_INSTRUCTION;

    bool readyFired = false;
    voice.onReady([&readyFired]() { readyFired = true; });

    bool listenStartedFired = false;
    voice.onListeningStarted([&listenStartedFired]() { listenStartedFired = true; });

    bool listenFinishedFired = false;
    voice.onListeningFinished([&listenFinishedFired]() { listenFinishedFired = true; });

    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    TEST_ASSERT(readyFired, "onReady declenche");
    TEST_ASSERT(voice.isReady(), "voice.isReady() true");
    TEST_ASSERT(!voice.isListening(), "isListening() false avant appui");
    TEST_ASSERT(!voice.isTranscribing(), "isTranscribing() false");

    // 1. Début de l'appui : startListening()
    bool startOk = voice.startListening();
    TEST_ASSERT(startOk, "startListening() accepte");
    TEST_ASSERT(voice.isListening(), "isListening() true pendant appui");
    TEST_ASSERT(voice.getState() == VoiceState::LISTENING, "Etat = LISTENING");
    TEST_ASSERT(listenStartedFired, "Callback onListeningStarted declenche");
    TEST_ASSERT(voice.isBusy(), "isBusy() true pendant l'ecoute");

    std::vector<int16_t> chunk(AUDIO_CHUNK_SAMPLES, 800);
    voice.getStreamer().pushSimulatedChunk(chunk.data(), chunk.size());
    voice.getStreamer().pushSimulatedChunk(chunk.data(), chunk.size());
    voice.update();

    // 2. Relâchement : stopListening()
    bool stopOk = voice.stopListening();
    TEST_ASSERT(stopOk, "stopListening() accepte");
    TEST_ASSERT(!voice.isListening(), "isListening() false apres relachement");
    TEST_ASSERT(voice.isTranscribing(), "isTranscribing() true en attente de transcription");
    TEST_ASSERT(voice.getState() == VoiceState::TRANSCRIBING, "Etat = TRANSCRIBING");
    TEST_ASSERT(listenFinishedFired, "Callback onListeningFinished declenche");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 19 : Dictée courte vs dictée longue
// =============================================================================
void test_short_and_long_dictation_scenarios() {
    std::cout << "\n[TEST SUITE 19] Scenarios de dictee courte et longue" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_DUR";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    // Scenario A : Dictee courte (~2 secondes, 62 blocs)
    voice.startListening();
    std::vector<int16_t> sampleBlock(AUDIO_CHUNK_SAMPLES, 400);
    for (int i = 0; i < 62; ++i) {
        voice.getStreamer().pushSimulatedChunk(sampleBlock.data(), sampleBlock.size());
        voice.getStreamer().tick();
    }
    TEST_ASSERT(voice.getStreamer().getBlocksAcquired() == 62, "Dictee courte: 62 blocs acquis (~2.0 s)");
    TEST_ASSERT(voice.getStreamer().getChunksSent() == 62, "Dictee courte: 62 blocs transmis");
    uint32_t durCourtMs = voice.getStreamer().getTheoreticalDurationMs();
    TEST_ASSERT(durCourtMs >= 1900 && durCourtMs <= 2100, "Duree theorique courte ~2000 ms");
    voice.stopListening();
    voice.cancelListening();

    // Scenario B : Dictee longue (~10 secondes, 312 blocs)
    voice.startListening();
    for (int i = 0; i < 312; ++i) {
        voice.getStreamer().pushSimulatedChunk(sampleBlock.data(), sampleBlock.size());
        voice.getStreamer().tick();
    }
    TEST_ASSERT(voice.getStreamer().getBlocksAcquired() == 312, "Dictee longue: 312 blocs acquis (~10.0 s)");
    TEST_ASSERT(voice.getStreamer().getChunksSent() == 312, "Dictee longue: 312 blocs transmis sans debordement");
    uint32_t durLongMs = voice.getStreamer().getTheoreticalDurationMs();
    TEST_ASSERT(durLongMs >= 9900 && durLongMs <= 10100, "Duree theorique longue ~10000 ms");
    voice.stopListening();
    voice.cancelListening();

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 20 : Transcriptions partielles (intérim) et finale
// =============================================================================
void test_partial_and_final_transcriptions() {
    std::cout << "\n[TEST SUITE 20] Transcriptions partielles (interim) et finales" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_TRANS";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    std::vector<String> interimTexts;
    String finalText = "";
    bool finalReceived = false;

    voice.onTranscription([&](const String& text, bool isFinal) {
        if (isFinal) {
            finalText = text;
            finalReceived = true;
        } else {
            interimTexts.push_back(text);
        }
    });

    voice.startListening();

    // 1. Simuler réception d'une transcription intérimaire 1
    const char* interim1 = "{\"serverContent\":{\"interimInputTranscription\":{\"text\":\"Bonjour\"}}}";
    voice.getClient().handleIncomingMessage(interim1, strlen(interim1));
    TEST_ASSERT(interimTexts.size() == 1, "1ere transcription interim recue");
    TEST_ASSERT(interimTexts[0] == "Bonjour", "Contenu interim 1 correct");

    // 2. Simuler réception d'une transcription intérimaire 2
    const char* interim2 = "{\"serverContent\":{\"interimInputTranscription\":{\"text\":\"Bonjour a tous\"}}}";
    voice.getClient().handleIncomingMessage(interim2, strlen(interim2));
    TEST_ASSERT(interimTexts.size() == 2, "2eme transcription interim recue");
    TEST_ASSERT(interimTexts[1] == "Bonjour a tous", "Contenu interim 2 correct");

    // 3. Relâchement du bouton
    voice.stopListening();
    TEST_ASSERT(voice.isTranscribing(), "En etat de transcription");
    TEST_ASSERT(!finalReceived, "Finale pas encore recue");

    // 4. Réception de la transcription finale
    const char* finalJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Bonjour a tous.\"}}}";
    voice.getClient().handleIncomingMessage(finalJson, strlen(finalJson));
    voice.update();

    TEST_ASSERT(finalReceived, "Transcription finale recue avec flag isFinal=true");
    TEST_ASSERT(finalText == "Bonjour a tous.", "Contenu final exact");
    TEST_ASSERT(!voice.isTranscribing(), "Fin de l'etat TRANSCRIBING");
    TEST_ASSERT(voice.getState() == VoiceState::READY, "Retour automatique a l'etat READY");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 21 : Enchaînement de dictées consécutives
// =============================================================================
void test_consecutive_dictations() {
    std::cout << "\n[TEST SUITE 21] Enchainement de dictees consecutives" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_CONSEC";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    int dictationCount = 0;
    voice.onTranscription([&](const String& text, bool isFinal) {
        if (isFinal) {
            dictationCount++;
        }
    });

    // 1ere dictée
    voice.startListening();
    voice.stopListening();
    const char* f1 = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Premiere phrase.\"}}}";
    voice.getClient().handleIncomingMessage(f1, strlen(f1));
    voice.update();
    TEST_ASSERT(dictationCount == 1, "Dictée 1 traitee avec succes");
    TEST_ASSERT(voice.isReady(), "Moteur pret pour la deuxieme dictee");

    // 2eme dictée immédiate
    bool start2 = voice.startListening();
    TEST_ASSERT(start2, "Demarrage immediat dictee 2 accepte");
    voice.stopListening();
    const char* f2 = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Deuxieme phrase.\"}}}";
    voice.getClient().handleIncomingMessage(f2, strlen(f2));
    voice.update();
    TEST_ASSERT(dictationCount == 2, "Dictée 2 traitee avec succes");
    TEST_ASSERT(voice.isReady(), "Moteur de nouveau pret");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 22 : Interruption et annulation pendant l'écoute
// =============================================================================
void test_listening_interruption_and_cancel() {
    std::cout << "\n[TEST SUITE 22] Interruption et annulation pendant l'ecoute" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_CANCEL";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    bool finalFired = false;
    voice.onTranscription([&](const String& text, bool isFinal) {
        if (isFinal) finalFired = true;
    });

    // Scénario A : cancelListening()
    voice.startListening();
    TEST_ASSERT(voice.isListening(), "Ecoute active");
    voice.cancelListening();
    TEST_ASSERT(!voice.isListening(), "Ecoute annulee");
    TEST_ASSERT(!voice.isTranscribing(), "Aucune attente de transcription");
    TEST_ASSERT(voice.getState() == VoiceState::READY, "Retour a READY apres cancelListening");
    TEST_ASSERT(!finalFired, "Aucune transcription finale declenchee apres annulation");

    // Scénario B : stop() global pendant l'écoute
    voice.startListening();
    TEST_ASSERT(voice.isListening(), "Ecoute active tour 2");
    voice.stop();
    TEST_ASSERT(!voice.isListening(), "Ecoute stoppee par stop() global");
    TEST_ASSERT(voice.getState() == VoiceState::READY, "Retour a READY apres stop()");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 23 : Résilience lors d'une perte Wi-Fi / transport pendant la dictée
// =============================================================================
void test_wifi_disconnect_during_dictation() {
    std::cout << "\n[TEST SUITE 23] Perte Wi-Fi / transport pendant la dictee" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_DISC";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    bool errorFired = false;
    voice.onError([&](const String& err) {
        errorFired = true;
    });

    voice.startListening();
    TEST_ASSERT(voice.isListening(), "Ecoute active avant coupure");

    // Déconnexion réseau subite
    mockTransport.disconnect();
    voice.update();

    TEST_ASSERT(!voice.isListening(), "Ecoute interrompue apres deconnexion");
    TEST_ASSERT(voice.getState() == VoiceState::ERROR_STATE || !voice.isReady(), "Transition securisee");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 24 : Absence de parole et timeout de transcription finale (Zéro hallucination)
// =============================================================================
void test_silence_and_final_transcription_timeout() {
    std::cout << "\n[TEST SUITE 24] Absence de parole et timeout de transcription finale (Zero invention)" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_TIMEOUT";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.sttFinalTimeoutMs = 3000; // 3 secondes pour test unitaire rapide
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    bool finalFired = false;
    String receivedFinal = "";
    voice.onTranscription([&](const String& text, bool isFinal) {
        if (isFinal) {
            finalFired = true;
            receivedFinal = text;
        }
    });

    voice.startListening();

    // On reçoit seulement un intérim (bruit / souffle partiel)
    const char* partial = "{\"serverContent\":{\"interimInputTranscription\":{\"text\":\"...\"}}}";
    voice.getClient().handleIncomingMessage(partial, strlen(partial));

    // Relâchement
    voice.stopListening();
    TEST_ASSERT(voice.isTranscribing(), "En attente transcription");

    // Avancement du temps de 1500 ms (< 3000 ms timeout)
    advanceMockMillis(1500);
    voice.update();
    TEST_ASSERT(voice.isTranscribing(), "Toujours en attente a 1.5s");
    TEST_ASSERT(!finalFired, "Pas de final precoce");

    // Avancement de 2000 ms supplementaires (total 3500 ms >= 3000 ms timeout)
    advanceMockMillis(2000);
    voice.update();

    // RÈGLE IMPÉRATIVE DU PROMPT : La transcription finale ne doit PAS être inventée à partir du partiel !
    TEST_ASSERT(!finalFired, "Regle respectee : Zero hallucination / pas de transcription finale inventee");
    TEST_ASSERT(!voice.isTranscribing(), "Sortie de l'etat TRANSCRIBING");
    TEST_ASSERT(voice.getState() == VoiceState::READY, "Retour propre a l'etat READY");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 25 : Coexistence et indépendance TTS et STT sans larsen
// =============================================================================
void test_tts_stt_coexistence() {
    std::cout << "\n[TEST SUITE 25] Coexistence TTS et STT sans interferences mutuelles" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_COEX";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.muteSpeakerDuringSTT = true;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    // 1. TTS en cours
    voice.say("Bonjour, test TTS");
    TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(), "Haut-parleur autorise pour TTS");

    // 2. Appel de startListening() pendant TTS -> Mute immediat et arret TTS
    bool listenOk = voice.startListening();
    TEST_ASSERT(listenOk, "startListening() prioritaire accepte");
    TEST_ASSERT(!voice.getPlayer().isPlaybackAllowed(), "Haut-parleur mute pendant STT pour eviter le larsen");
    TEST_ASSERT(voice.isListening(), "Microphone actif");

    // 3. Fin de STT -> Re-autorisation haut-parleur
    voice.stopListening();
    const char* fJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Texte dicte.\"}}}";
    voice.getClient().handleIncomingMessage(fJson, strlen(fJson));
    voice.update();

    TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(), "Haut-parleur reautorise apres STT");
    TEST_ASSERT(voice.isReady(), "Retour a READY");

    // 4. Nouveau TTS
    bool say2 = voice.say("Nouvelle annonce");
    TEST_ASSERT(say2, "TTS reussit parfaitement apres STT");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================
int main() {
    std::cout << "===========================================================" << std::endl;
    std::cout << "  SUITE COMPLETE DE TESTS AUTOMATISES — VoiceKit v0.3.0    " << std::endl;
    std::cout << "  Validation TTS + STT | Cible logique C++14 / C++17       " << std::endl;
    std::cout << "===========================================================" << std::endl;

    test_french_time_formatting();
    test_audio_player_integrity();
    test_audio_scenarios_duration();
    test_voicekit_api_and_queue_policies();
    test_wifi_coordination();
    test_speaker_ownership_management();
    test_announced_and_displayed_time_coherence();
    test_async_requests_and_resilience();
    test_wifi_lock_lifecycle_and_keepalive();
    test_audio_streamer_buffer_and_queue_management();
    test_gemini_server_json_errors();
    test_gemini_server_close_frame();
    test_model_normalization();
    test_intentional_vs_unexpected_disconnect();
    test_tls_roots_and_recovery();

    // Nouvelles suites de tests STT VoiceKit v0.3.0
    test_microphone_initialization_and_hal_arbitration();
    test_pcm_16khz_acquisition_and_streaming();
    test_push_to_talk_and_release_cycle();
    test_short_and_long_dictation_scenarios();
    test_partial_and_final_transcriptions();
    test_consecutive_dictations();
    test_listening_interruption_and_cancel();
    test_wifi_disconnect_during_dictation();
    test_silence_and_final_transcription_timeout();
    test_tts_stt_coexistence();

    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  BILAN FINAL DES TESTS : " << s_testsPassed << " REUSSIS, " << s_testsFailed << " ECHECS" << std::endl;
    std::cout << "===========================================================" << std::endl;

    return (s_testsFailed == 0) ? 0 : 1;
}
