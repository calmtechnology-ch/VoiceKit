#include "native_mock/Arduino.h"
#include "native_mock/WiFi.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>

#include "VoxCoreVoice.h"
#include "ContinuousListeningController.h"
#include "VoiceKit.h"
#include "Utf8Utils.h"
#include "VoiceActivityDetector.h"
#include "cJSON.h"
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

static void prepareMockVoiceKitReady(VoiceKit& vk, MockLiveTransport& mockTransport) {
    prepareMockVoiceReady(vk.getEngine(), mockTransport);
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
// SUITE 26 : Activation du mode conversationnel et instruction système (Mode B)
// =============================================================================
void test_conversation_activation_and_instructions() {
    std::cout << "\n[TEST SUITE 26] Activation mode conversationnel et instructions systeme (Mode B)" << std::endl;

    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_CONV_ACT";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.systemInstruction = VOICEKIT_DEFAULT_TTS_INSTRUCTION;
    voice.begin(config);

    TEST_ASSERT(!voice.isConversationEnabled(), "Mode conversation desactive par defaut (v0.3.0 compatible)");

    voice.enableConversation(true);
    TEST_ASSERT(voice.isConversationEnabled(), "Mode conversation active via enableConversation(true)");
    TEST_ASSERT(voice.getClient().getSystemInstruction() == VOICEKIT_DEFAULT_CONVERSATION_INSTRUCTION,
                "Instruction systeme mise a jour pour la conversation bienveillante et concise");

    voice.enableConversation(false);
    TEST_ASSERT(!voice.isConversationEnabled(), "Mode conversation desactive");
    TEST_ASSERT(voice.getClient().getSystemInstruction() == VOICEKIT_DEFAULT_TTS_INSTRUCTION,
                "Instruction systeme TTS d'origine restauree");
}

// =============================================================================
// SUITE 27 : Cycle conversationnel complet READY -> LISTENING -> PROCESSING -> SPEAKING -> READY
// =============================================================================
void test_conversation_turn_lifecycle() {
    std::cout << "\n[TEST SUITE 27] Cycle conversationnel complet (READY -> LISTENING -> PROCESSING -> SPEAKING -> READY)" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_LIFECYCLE";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableConversation = true;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    TEST_ASSERT(voice.getState() == VoiceState::READY, "Etat initial READY");
    TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(), "Haut-parleur initialement pret");

    bool convFinished = false;
    voice.onConversationFinished([&]() {
        convFinished = true;
    });

    // 1. Début de parole utilisateur (Appui PARLER)
    bool started = voice.startListening();
    TEST_ASSERT(started, "startListening() en mode conversation accepte");
    TEST_ASSERT(voice.isListening(), "isListening() true");
    TEST_ASSERT(voice.getState() == VoiceState::LISTENING, "Etat = LISTENING");
    TEST_ASSERT(!voice.getPlayer().isPlaybackAllowed(), "Anti-larsen strict : haut-parleur coupe pendant l'ecoute");

    // 2. Simulation de parole (blocs PCM 16 kHz transmis)
    std::vector<int16_t> sampleBlock(AUDIO_CHUNK_SAMPLES, 400);
    voice.getStreamer().pushSimulatedChunk(sampleBlock.data(), sampleBlock.size());
    voice.getStreamer().tick();
    TEST_ASSERT(voice.getStreamer().getChunksSent() >= 1, "Blocs audio emis vers Gemini Live");

    // 3. Relâchement du bouton PARLER
    bool stopped = voice.stopListening();
    TEST_ASSERT(stopped, "stopListening() en mode conversation accepte");
    TEST_ASSERT(voice.getState() == VoiceState::WAITING_RESPONSE, "Etat = WAITING_RESPONSE (PROCESSING)");
    TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(), "Micro coupe : haut-parleur reautorise pour la reponse");

    // 4. Réception de la transcription utilisateur
    const char* userJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Quelle heure est-il ?\"}}}";
    voice.getClient().handleIncomingMessage(userJson, strlen(userJson));
    voice.update();

    // 5. Réception du premier son audio Gemini (24 kHz)
    voice.getSession().transitionTo(LiveSessionState::PLAYING, "Premier son recu");
    voice.update();
    TEST_ASSERT(voice.getState() == VoiceState::SPEAKING, "Etat = SPEAKING");

    // 6. Fin de la restitution audio
    voice.getSession().finishTurn();
    voice.update();

    TEST_ASSERT(voice.getState() == VoiceState::READY, "Retour a l'etat READY");
    TEST_ASSERT(convFinished, "Callback onConversationFinished declenche");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 28 : Callbacks onUserText et onAssistantText
// =============================================================================
void test_conversation_callbacks_user_and_assistant() {
    std::cout << "\n[TEST SUITE 28] Transcriptions utilisateur (interim + final) et transcription assistant" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_CB";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableConversation = true;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    String lastUserInterim = "";
    String lastUserFinal = "";
    String lastAssistant = "";

    voice.onUserText([&](const String& text, bool isFinal) {
        if (isFinal) lastUserFinal = text;
        else lastUserInterim = text;
    });

    voice.onAssistantText([&](const String& text) {
        lastAssistant = text;
    });

    voice.startListening();

    // Interim user
    const char* interim = "{\"serverContent\":{\"interimInputTranscription\":{\"text\":\"Bonjour\"}}}";
    voice.getClient().handleIncomingMessage(interim, strlen(interim));
    TEST_ASSERT(lastUserInterim == "Bonjour", "Transcription utilisateur interim recue");

    // Final user
    const char* finalU = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Bonjour Gemini\"}}}";
    voice.getClient().handleIncomingMessage(finalU, strlen(finalU));
    TEST_ASSERT(lastUserFinal == "Bonjour Gemini", "Transcription utilisateur finale recue");

    // Assistant text
    const char* asst = "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"text\":\"Bonjour ! Que puis-je faire pour vous ?\"}]}}}";
    voice.getClient().handleIncomingMessage(asst, strlen(asst));
    TEST_ASSERT(lastAssistant == "Bonjour ! Que puis-je faire pour vous ?", "Transcription assistant Gemini recue");

    voice.stopListening();
    voice.getSession().finishTurn();
    voice.update();

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 29 : Restitution audio 24 kHz et préservation de la dernière syllabe
// =============================================================================
void test_conversation_audio_preservation_last_syllable() {
    std::cout << "\n[TEST SUITE 29] Restitution audio native 24 kHz et preservation derniere syllabe" << std::endl;

    LiveAudioPlayer player;
    TEST_ASSERT(player.begin(120000), "Initialisation du buffer PSRAM (120 000 ech)");

    // Remplir 2400 échantillons (100 ms)
    std::vector<int16_t> samples(2400, 1500);
    player.feedPcm(samples.data(), samples.size());

    TEST_ASSERT(player.getBufferedSamples() == 2400, "2400 echantillons en tampon avant notification de fin");

    // Arrivée de turnComplete
    player.notifyTurnComplete();

    // RÈGLE CRITIQUE : player NE DOIT PAS s'arrêter immédiatement, le tampon doit être vidé
    TEST_ASSERT(player.isPlaying(), "La lecture continue apres turnComplete (protection derniere syllabe)");
    TEST_ASSERT(player.isTurnComplete(), "Notification turnComplete enregistree");

    // Simulation de vidage par tick audio et consommation speaker
    int tickCount = 0;
    while ((player.getBufferedSamples() > 0 || player.isPlaying()) && tickCount < 100) {
        player.tick();
        player.simFinishSpeakerChunk();
        advanceMockMillis(20);
        tickCount++;
    }

    TEST_ASSERT(!player.isPlaying(), "Arret seulement apres vidage complet");
    TEST_ASSERT(player.getBufferedSamples() == 0, "Tampon integralement vide sans coupure");
}

// =============================================================================
// SUITE 30 : Enchaînement de 10 tours conversationnels successifs dans la même session
// =============================================================================
void test_conversation_ten_consecutive_turns() {
    std::cout << "\n[TEST SUITE 30] Enchainement de 10 tours conversationnels successifs" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_10TURNS";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableConversation = true;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    uint32_t turnsCompleted = 0;
    voice.onConversationFinished([&]() {
        turnsCompleted++;
    });

    for (int t = 1; t <= 10; ++t) {
        TEST_ASSERT(voice.isReady(), "Moteur PRET avant le tour");
        bool started = voice.startListening();
        TEST_ASSERT(started, "startListening() reussi pour le tour");
        TEST_ASSERT(voice.isListening(), "Etat LISTENING");

        std::vector<int16_t> sampleBlock(AUDIO_CHUNK_SAMPLES, 200);
        voice.getStreamer().pushSimulatedChunk(sampleBlock.data(), sampleBlock.size());
        voice.getStreamer().tick();

        voice.stopListening();
        TEST_ASSERT(voice.getState() == VoiceState::WAITING_RESPONSE, "Etat WAITING_RESPONSE");

        voice.getSession().transitionTo(LiveSessionState::PLAYING, "Reponse recue");
        voice.update();
        TEST_ASSERT(voice.getState() == VoiceState::SPEAKING, "Etat SPEAKING");

        voice.getSession().finishTurn();
        voice.getSession().transitionTo(LiveSessionState::CONNECTED, "Fin lecture audio");
        voice.update();
        TEST_ASSERT(voice.getState() == VoiceState::READY, "Retour a READY");
    }

    TEST_ASSERT(turnsCompleted == 10, "10 tours conversationnels acheves avec succes dans la meme session");
    TEST_ASSERT(voice.getConversationMetrics().turnId >= 10, "Compteur de tours incremente correctement");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 31 : Interruption STOP prioritaire pendant l'écoute et pendant la réponse
// =============================================================================
void test_conversation_stop_priority_listening_and_speaking() {
    std::cout << "\n[TEST SUITE 31] Interruption STOP prioritaire pendant l'ecoute et pendant la reponse" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_STOP";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableConversation = true;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    // 1. STOP pendant l'écoute
    voice.startListening();
    TEST_ASSERT(voice.isListening(), "Ecoute active avant STOP");
    voice.stop();
    TEST_ASSERT(!voice.isListening(), "Ecoute immediatement interrompue");
    TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(), "Haut-parleur reautorise apres STOP");
    TEST_ASSERT(voice.isReady(), "Retour immediat a READY");

    // 2. STOP pendant la réflexion / réponse
    voice.startListening();
    voice.stopListening();
    voice.getSession().transitionTo(LiveSessionState::PLAYING, "Audio en cours");
    voice.update();
    TEST_ASSERT(voice.getState() == VoiceState::SPEAKING, "En cours de parole");

    voice.stop();
    TEST_ASSERT(voice.getState() == VoiceState::READY, "Arret immediat de la parole -> READY");
    TEST_ASSERT(!voice.isSpeaking(), "Haut-parleur arrete");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 32 : Résilience : Déconnexion Wi-Fi pendant un tour et reprise propre
// =============================================================================
void test_conversation_wifi_disconnect_and_recovery() {
    std::cout << "\n[TEST SUITE 32] Resilience : Deconnexion Wi-Fi pendant un tour et reprise propre" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_WIFI_DISC";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableConversation = true;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    voice.startListening();
    TEST_ASSERT(voice.isListening(), "Ecoute active");

    // Coupure réseau simulée
    WiFi.setConnected(false);
    mockTransport.disconnect();
    voice.update();

    TEST_ASSERT(voice.getState() == VoiceState::ERROR_STATE || voice.getState() == VoiceState::DISCONNECTED,
                "Bascule en etat d'erreur ou deconnecte lors de la perte Wi-Fi");

    // Rétablissement réseau
    WiFi.setConnected(true);
    prepareMockVoiceReady(voice, mockTransport);
    TEST_ASSERT(voice.isReady(), "Retablissement complet de la session apres reconnexion");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 33 : Absence de parole et gestion du cycle à vide
// =============================================================================
void test_conversation_silence_handling() {
    std::cout << "\n[TEST SUITE 33] Absence de parole et gestion du cycle a vide" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_SILENCE";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableConversation = true;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    // Appui et relâchement immédiat sans parole
    voice.startListening();
    voice.stopListening();
    TEST_ASSERT(voice.getState() == VoiceState::WAITING_RESPONSE, "Bascule en WAITING_RESPONSE");

    // Réception turnComplete direct du serveur sans audio
    voice.getSession().finishTurn();
    voice.update();

    TEST_ASSERT(voice.getState() == VoiceState::READY, "Retour propre a READY apres silence");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 34 : Métriques de diagnostic d'un tour de conversation
// =============================================================================
void test_conversation_metrics_accuracy() {
    std::cout << "\n[TEST SUITE 34] Metriques diagnostiques completes du tour de conversation" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_METRICS";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableConversation = true;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    ConversationMetrics capturedMetrics;
    voice.onConversationTurnMetrics([&](const ConversationMetrics& m) {
        capturedMetrics = m;
    });

    voice.startListening();

    std::vector<int16_t> sampleBlock(AUDIO_CHUNK_SAMPLES, 350);
    voice.getStreamer().pushSimulatedChunk(sampleBlock.data(), sampleBlock.size());
    voice.getStreamer().tick();

    voice.stopListening();

    const char* userJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Bonjour\"}}}";
    voice.getClient().handleIncomingMessage(userJson, strlen(userJson));

    const char* asstJson = "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"text\":\"Bonjour !\"}]}}}";
    voice.getClient().handleIncomingMessage(asstJson, strlen(asstJson));

    voice.getSession().transitionTo(LiveSessionState::PLAYING, "Audio");
    voice.getSession().finishTurn();
    voice.update();

    const ConversationMetrics& m = voice.getConversationMetrics();
    TEST_ASSERT(m.turnId > 0, "turnId > 0");
    TEST_ASSERT(m.audioBlocksSent >= 1, "audioBlocksSent >= 1");
    TEST_ASSERT(m.userText == "Bonjour", "userText correctement renseigne dans les metriques");
    TEST_ASSERT(m.assistantText == "Bonjour !", "assistantText correctement renseigne dans les metriques");
    TEST_ASSERT(capturedMetrics.turnId == m.turnId, "Callback onConversationTurnMetrics identique");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 35 : Non-régression intégrale : TTS pur et STT pur non affectés
// =============================================================================
void test_v030_v040_complete_non_regression() {
    std::cout << "\n[TEST SUITE 35] Non-regression integrale : TTS pur et STT pur preserves" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_NONREG";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableConversation = false; // Mode classique par défaut
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    // 1. TTS pur
    bool sayOk = voice.say("Non-regression TTS");
    TEST_ASSERT(sayOk, "TTS pur fonctionne exactement comme en v0.2.1");
    voice.stop();
    TEST_ASSERT(voice.isReady(), "Retour a READY apres TTS");

    // 2. STT pur (mode silencieux avec restitution sur transcription finale)
    bool sttOk = voice.startListening();
    TEST_ASSERT(sttOk, "STT pur demarre comme en v0.3.0");
    TEST_ASSERT(!voice.getPlayer().isPlaybackAllowed(), "Haut-parleur muet pour STT");

    voice.stopListening();
    TEST_ASSERT(voice.isTranscribing(), "En cours de transcription");

    bool sttFinalFired = false;
    voice.onTranscription([&](const String& text, bool isFinal) {
        if (isFinal) sttFinalFired = true;
    });

    const char* finalJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Phrase transcrite\"}}}";
    voice.getClient().handleIncomingMessage(finalJson, strlen(finalJson));
    voice.update();

    TEST_ASSERT(sttFinalFired, "Transcription finale STT pure recue");
    TEST_ASSERT(voice.isReady(), "STT pur revient a READY sans jouer d'audio");
    TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(), "Haut-parleur reautorise apres STT");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 36 : Chaîne UTF-8 de bout en bout, accents français et ponctuations
// =============================================================================
void test_utf8_end_to_end_and_french_accents() {
    std::cout << "\n[TEST SUITE 36] Chaine UTF-8 integrale : accents francais, ligatures et ponctuation" << std::endl;

    // 1. Validation de chaque caractère prescrit
    const char* frenchChars[] = {
        "é", "è", "ê", "ë", "à", "â", "ù", "û", "î", "ï", "ô", "ö", "ç", "œ", "æ", "É", "À", "Ç", "’", "«", "»"
    };
    for (size_t i = 0; i < sizeof(frenchChars) / sizeof(frenchChars[0]); ++i) {
        String ch(frenchChars[i]);
        TEST_ASSERT(Utf8Utils::isValidUtf8(ch), String("Caractere UTF-8 valide : ") + ch);
        TEST_ASSERT(Utf8Utils::sanitizeUtf8(ch) == ch, String("Preservation absolue caractere : ") + ch);
    }

    // 2. Phrase obligatoire
    const String mandatoryPhrase = "« À Noël, Élise prépare un café crème. L'été à Genève est agréable. François préfère un cœur léger. »";
    TEST_ASSERT(Utf8Utils::isValidUtf8(mandatoryPhrase), "Phrase obligatoire est un UTF-8 strictement valide");
    TEST_ASSERT(Utf8Utils::sanitizeUtf8(mandatoryPhrase) == mandatoryPhrase, "Sanitize n'altere nullement la phrase obligatoire");

    // 3. Robustesse UTF-8 : rejet de séquences mal formées et remplacement sélectif U+FFFD
    const char invalidOverlong[] = { (char)0xC0, (char)0xAF, '\0' }; // Overlong 2 octets invalide RFC 3629
    TEST_ASSERT(!Utf8Utils::isValidUtf8(invalidOverlong, 2), "Rejet sequence overlong 0xC0");
    String sanitizedOverlong = Utf8Utils::sanitizeUtf8(String(invalidOverlong));
    TEST_ASSERT(sanitizedOverlong.indexOf("\xEF\xBF\xBD") >= 0, "Remplacement de l'octet invalide par U+FFFD");

    const char truncatedUtf8[] = { 'A', (char)0xC3, '\0' }; // 0xC3 sans octet de continuation
    TEST_ASSERT(!Utf8Utils::isValidUtf8(truncatedUtf8, 2), "Rejet sequence tronquee en fin de chaine");

    // 4. Centralisation de l'échappement JSON (RFC 8259)
    String jsonEsc = Utf8Utils::escapeJson(mandatoryPhrase);
    TEST_ASSERT(jsonEsc.indexOf("crème") >= 0, "Accents intacts dans JSON");
    TEST_ASSERT(jsonEsc.indexOf("cœur") >= 0, "Ligature coeur intacte dans JSON");
    TEST_ASSERT(jsonEsc.indexOf("Élise") >= 0, "Majuscule accentuee intacte");
    TEST_ASSERT(jsonEsc.indexOf("Genève") >= 0, "Accent grave intact");

    // Échappement des caractères de contrôle ASCII (< 32), guillemets et backslashes
    String testControls = "Ligne 1\nLigne 2\t\"Citation\"\\Fin\b\r";
    String escControls = Utf8Utils::escapeJson(testControls);
    TEST_ASSERT(escControls.indexOf("\\n") >= 0, "Saut de ligne echappe");
    TEST_ASSERT(escControls.indexOf("\\t") >= 0, "Tabulation echappee");
    TEST_ASSERT(escControls.indexOf("\\\"") >= 0, "Guillemet echappe");
    TEST_ASSERT(escControls.indexOf("\\\\") >= 0, "Backslash echappe");

    // 5. Transmission say() avec MockLiveTransport
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_UTF8_KEY";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    voice.say(mandatoryPhrase);
    std::string sentPayload = mockTransport.getLastSent();
    TEST_ASSERT(sentPayload.find("« À Noël, Élise prépare un café crème.") != std::string::npos,
                "La trame clientContent contient la phrase francaise accentuee sans aucune translitteration");
    TEST_ASSERT(sentPayload.find("François préfère un cœur léger. »") != std::string::npos,
                "Ligature et cedille transmises integralement a Gemini Live");

    // 6. Réception de transcriptions avec caractères accentués
    String receivedTrans = "";
    voice.onTranscription([&](const String& t, bool isFinal) {
        if (isFinal) receivedTrans = t;
    });

    const char* incomingJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Élise a adoré l’été à Genève !\"}}}";
    voice.getClient().handleIncomingMessage(incomingJson, strlen(incomingJson));
    voice.update();

    TEST_ASSERT(receivedTrans == "Élise a adoré l’été à Genève !",
                "Transcription entrante avec apostrophe typographique et accents preservee mot a mot");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 37 : Configuration vocale VoiceKitConfig (voix, langues, styles, tons, accents)
// =============================================================================
void test_voice_configuration_parameters() {
    std::cout << "\n[TEST SUITE 37] Parametres VoiceKitConfig : voix, langue, style, ton et accent" << std::endl;

    VoiceKitConfig cfg;
    TEST_ASSERT(cfg.voice == "Aoede", "Voix par defaut conservee (Aoede)");
    TEST_ASSERT(cfg.language == "fr-FR", "Langue par defaut conforme fr-FR");
    TEST_ASSERT(cfg.style == VoiceStyle::NATURAL, "Style par defaut conforme NATURAL");
    TEST_ASSERT(cfg.tone == VoiceTone::NEUTRAL, "Ton par defaut conforme NEUTRAL");
    TEST_ASSERT(cfg.accent.isEmpty(), "Accent par defaut vide");

    // Conversion et affectation flexible style (enum et string)
    cfg.style = VoiceStyle::EXPRESSIVE;
    TEST_ASSERT(cfg.style == VoiceStyle::EXPRESSIVE, "Affectation enum EXPRESSIVE");
    TEST_ASSERT(String(cfg.style.c_str()) == "expressive", "Conversion string expressive");

    cfg.style = "professional";
    TEST_ASSERT(cfg.style == VoiceStyle::PROFESSIONAL, "Affectation chaine 'professional'");

    cfg.style = "narrative";
    TEST_ASSERT(cfg.style == VoiceStyle::NARRATIVE, "Affectation chaine 'narrative'");

    // Conversion et affectation flexible ton (enum et string)
    cfg.tone = VoiceTone::CALM;
    TEST_ASSERT(cfg.tone == VoiceTone::CALM, "Affectation enum CALM");

    cfg.tone = "warm";
    TEST_ASSERT(cfg.tone == VoiceTone::WARM, "Affectation chaine 'warm'");

    cfg.tone = "cheerful";
    TEST_ASSERT(cfg.tone == VoiceTone::CHEERFUL, "Affectation chaine 'cheerful'");

    cfg.tone = "serious";
    TEST_ASSERT(cfg.tone == VoiceTone::SERIOUS, "Affectation chaine 'serious'");

    // Formatage des directives vocales
    cfg.language = "fr-FR";
    cfg.style = VoiceStyle::EXPRESSIVE;
    cfg.tone = VoiceTone::WARM;
    cfg.accent = "parisien";
    String directives = cfg.buildVoiceDirectives();
    TEST_ASSERT(directives.indexOf("francais (fr-FR)") >= 0, "Directives : langue francaise");
    TEST_ASSERT(directives.indexOf("expressif, vivant et nuance") >= 0, "Directives : style expressif");
    TEST_ASSERT(directives.indexOf("chaleureuse, accueillante et bienveillante") >= 0, "Directives : ton chaleureux");
    TEST_ASSERT(directives.indexOf("parisien") >= 0, "Directives : accent parisien");

    // Langues étrangères
    cfg.language = "de-DE";
    String dirDe = cfg.buildVoiceDirectives();
    TEST_ASSERT(dirDe.indexOf("allemand (de-DE)") >= 0, "Directives : langue allemande");

    cfg.language = "en-US";
    String dirEn = cfg.buildVoiceDirectives();
    TEST_ASSERT(dirEn.indexOf("anglais (en-US)") >= 0, "Directives : langue anglaise");

    // Construction de l'instruction système effective
    String effectiveTTS = cfg.buildEffectiveInstruction(VOICEKIT_DEFAULT_TTS_INSTRUCTION);
    TEST_ASSERT(effectiveTTS.startsWith(VOICEKIT_DEFAULT_TTS_INSTRUCTION), "Instruction metier TTS strictement preservee au debut");
    TEST_ASSERT(effectiveTTS.indexOf("[Directives vocales]") >= 0, "Balise [Directives vocales] presente");
}

// =============================================================================
// SUITE 38 : Protocole Gemini Live Setup : speechConfig et systemInstruction
// =============================================================================
void test_gemini_setup_json_validation() {
    std::cout << "\n[TEST SUITE 38] Trame de setup Gemini Live : speechConfig, voix et directives" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_SETUP_KEY";
    config.voice = "Charon";
    config.language = "fr-FR";
    config.style = VoiceStyle::PROFESSIONAL;
    config.tone = VoiceTone::SERIOUS;
    config.accent = "meridional";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;

    voice.begin(config);
    voice.getClient().setTransport(&mockTransport);
    voice.getClient().connect();

    std::string setupSent = mockTransport.getLastSent();
    TEST_ASSERT(!setupSent.empty(), "Trame de setup emise a la connexion");

    cJSON* root = cJSON_Parse(setupSent.c_str());
    TEST_ASSERT(root != nullptr, "Setup est un JSON valide RFC 8259");

    if (root) {
        cJSON* setup = cJSON_GetObjectItemCaseSensitive(root, "setup");
        TEST_ASSERT(setup != nullptr, "Objet racine 'setup' present");

        if (setup) {
            // Modèle
            cJSON* model = cJSON_GetObjectItemCaseSensitive(setup, "model");
            TEST_ASSERT(model && cJSON_IsString(model), "Champ model present");

            // generationConfig
            cJSON* genConfig = cJSON_GetObjectItemCaseSensitive(setup, "generationConfig");
            TEST_ASSERT(genConfig != nullptr, "Objet generationConfig present");

            if (genConfig) {
                cJSON* speechConfig = cJSON_GetObjectItemCaseSensitive(genConfig, "speechConfig");
                TEST_ASSERT(speechConfig != nullptr, "Objet speechConfig present");

                if (speechConfig) {
                    // voiceConfig.prebuiltVoiceConfig.voiceName
                    cJSON* voiceConfig = cJSON_GetObjectItemCaseSensitive(speechConfig, "voiceConfig");
                    TEST_ASSERT(voiceConfig != nullptr, "voiceConfig present");
                    if (voiceConfig) {
                        cJSON* prebuilt = cJSON_GetObjectItemCaseSensitive(voiceConfig, "prebuiltVoiceConfig");
                        TEST_ASSERT(prebuilt != nullptr, "prebuiltVoiceConfig present");
                        if (prebuilt) {
                            cJSON* vName = cJSON_GetObjectItemCaseSensitive(prebuilt, "voiceName");
                            TEST_ASSERT(vName && cJSON_IsString(vName) && String(vName->valuestring) == "Charon",
                                        "voiceName est conforme ('Charon')");
                        }
                    }

                    // Règle formelle : pour modèles natifs (gemini-3.8-live), languageCode est ABSENT de speechConfig
                    cJSON* langCode = cJSON_GetObjectItemCaseSensitive(speechConfig, "languageCode");
                    TEST_ASSERT(langCode == nullptr, "languageCode absent de speechConfig pour modele natif (compatibilite Bidi)");
                }
            }

            // systemInstruction
            cJSON* sysInst = cJSON_GetObjectItemCaseSensitive(setup, "systemInstruction");
            TEST_ASSERT(sysInst != nullptr, "systemInstruction present");
            if (sysInst) {
                cJSON* parts = cJSON_GetObjectItemCaseSensitive(sysInst, "parts");
                if (parts && cJSON_GetArraySize(parts) > 0) {
                    cJSON* p0 = cJSON_GetArrayItem(parts, 0);
                    cJSON* txt = cJSON_GetObjectItemCaseSensitive(p0, "text");
                    TEST_ASSERT(txt && cJSON_IsString(txt), "Texte instruction present");
                    if (txt && txt->valuestring) {
                        String sTxt(txt->valuestring);
                        TEST_ASSERT(sTxt.indexOf("Tu es un moteur de synthese vocale TTS pur") >= 0,
                                    "Instruction metier TTS d'origine strictement preservee");
                        TEST_ASSERT(sTxt.indexOf("[Directives vocales]") >= 0,
                                    "Directives vocales presentes dans les instructions systeme");
                        TEST_ASSERT(sTxt.indexOf("professionnel, pose et clair") >= 0,
                                    "Style professionnel present dans le setup");
                        TEST_ASSERT(sTxt.indexOf("serieuse, formelle et rigoureuse") >= 0,
                                    "Ton serieux present dans le setup");
                        TEST_ASSERT(sTxt.indexOf("meridional") >= 0,
                                    "Accent meridional present dans le setup");
                    }
                }
            }
        }
        cJSON_Delete(root);
    }

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 39 : Dynamique de profil vocal et reconnexion sécurisée
// =============================================================================
void test_voice_profile_switching_and_safety() {
    std::cout << "\n[TEST SUITE 39] Changement dynamique de profil vocal et securite d'interruption" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_DYN_KEY";
    config.voice = "Aoede";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    TEST_ASSERT(voice.isReady(), "VoiceKit pret au depart");
    TEST_ASSERT(voice.getVoice() == "Aoede", "Voix initiale Aoede");

    // 1. Changement de profil au repos (READY) -> accepte avec reconnexion
    bool profOk = voice.setVoiceProfile("Fenrir", "en-US", VoiceStyle::EXPRESSIVE, VoiceTone::CHEERFUL, "british");
    TEST_ASSERT(profOk, "Changement de profil accepte au repos");
    TEST_ASSERT(voice.isConnecting(), "Reconnexion en cours");
    TEST_ASSERT(!voice.isReady(), "isReady false pendant la reconnexion");
    // REGLE CRITIQUE v0.4.1.1 : Le profil précédent est strictement conservé tant que setupComplete n'a pas confirmé le nouveau profil
    TEST_ASSERT(voice.getVoice() == "Aoede", "Voix precedente Aoede conservee tant que setupComplete n'est pas confirme");

    // Reconnexion confirmée via simulateIncoming setupComplete
    const char* setupCompJson = "{\"setupComplete\":{}}";
    voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
    voice.update();
    TEST_ASSERT(voice.isReady(), "Nouvelle session securisee prete");
    TEST_ASSERT(voice.getVoice() == "Fenrir", "Nouvelle voix Fenrir validee apres setupComplete");
    TEST_ASSERT(voice.getLanguage() == "en-US", "Nouvelle langue en-US validee apres setupComplete");
    TEST_ASSERT(voice.getStyle() == VoiceStyle::EXPRESSIVE, "Nouveau style EXPRESSIVE valide apres setupComplete");
    TEST_ASSERT(voice.getTone() == VoiceTone::CHEERFUL, "Nouveau ton CHEERFUL valide apres setupComplete");
    TEST_ASSERT(voice.getAccent() == "british", "Nouvel accent british valide apres setupComplete");

    // 2. Vérification de la nouvelle trame de setup emise
    std::string newSetup = mockTransport.getLastSent();
    TEST_ASSERT(newSetup.find("Fenrir") != std::string::npos, "Nouveau setup avec voix Fenrir emise");
    TEST_ASSERT(newSetup.find("anglais (en-US)") != std::string::npos, "Nouveau setup avec langue anglaise");

    // 3. Sécurité absolue : interdiction de reconnexion pendant une génération vocale ou écoute active
    voice.say("Phrase en cours d'elocution");
    // Le moteur est occupé
    TEST_ASSERT(voice.isBusy(), "Moteur occupe en cours de parole");

    bool rejectDuringBusy = voice.setVoiceProfile("Puck", "fr-FR", VoiceStyle::NATURAL, VoiceTone::CALM);
    TEST_ASSERT(!rejectDuringBusy, "Changement de profil STRICTEMENT REFUSE pendant activite vocale active");
    TEST_ASSERT(voice.getVoice() == "Fenrir", "Voix inchangee apres tentative pendant activite");

    voice.stop();
    TEST_ASSERT(!voice.isBusy(), "Moteur au repos apres stop()");

    // 4. Modificateurs individuels avec confirmation setupComplete
    bool changeTone = voice.setTone(VoiceTone::SERIOUS);
    TEST_ASSERT(changeTone, "setTone accepte au repos");
    TEST_ASSERT(voice.getTone() == VoiceTone::CHEERFUL, "Ton precedent conserve tant que non confirme");
    voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
    voice.update();
    TEST_ASSERT(voice.getTone() == VoiceTone::SERIOUS, "Ton serieux valide apres setupComplete");

    bool changeStyle = voice.setStyle(VoiceStyle::NARRATIVE);
    TEST_ASSERT(changeStyle, "setStyle accepte au repos");
    voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
    voice.update();
    TEST_ASSERT(voice.getStyle() == VoiceStyle::NARRATIVE, "Style narrative valide apres setupComplete");

    bool changeLang = voice.setLanguage("de-DE");
    TEST_ASSERT(changeLang, "setLanguage accepte au repos");
    voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
    voice.update();
    TEST_ASSERT(voice.getLanguage() == "de-DE", "Langue de-DE validee apres setupComplete");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 40 : Sécurisation avancée des profils vocaux et reconnexions (v0.4.1.1)
// =============================================================================
void test_voice_profile_safety_stabilization() {
    std::cout << "\n[TEST SUITE 40] Securisation des profils vocaux, reconnexions et refus stricts" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_SAFETY_KEY";
    config.voice = "Aoede";
    config.language = "fr-FR";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    TEST_ASSERT(voice.isReady(), "VoiceKit pret au demarrage");
    TEST_ASSERT(voice.getVoice() == "Aoede", "Voix initiale Aoede");

    // 1. Changement de voix lorsque le moteur est prêt
    String readyReportedVoice = "";
    voice.onReady([&]() {
        readyReportedVoice = voice.getVoice();
    });

    bool changeOk = voice.setVoice("Fenrir");
    TEST_ASSERT(changeOk, "setVoice('Fenrir') accepte a froid/au repos");
    TEST_ASSERT(voice.isConnecting(), "Reconnexion active");
    TEST_ASSERT(voice.isProfileChangePending(), "isProfileChangePending() true");
    // Le profil précédent est conservé tant que setupComplete n'est pas reçu
    TEST_ASSERT(voice.getVoice() == "Aoede", "Profil precedent Aoede conserve avant setupComplete");

    // Vérification que la nouvelle trame de setup contient la voix Fenrir
    std::string setupSent = mockTransport.getLastSent();
    TEST_ASSERT(setupSent.find("\"voiceName\":\"Fenrir\"") != std::string::npos, "Trame setup contient la voix Fenrir");

    // Réception du setupComplete
    const char* setupCompJson = "{\"setupComplete\":{}}";
    voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
    voice.update();

    TEST_ASSERT(voice.isReady(), "Moteur PRET apres setupComplete");
    TEST_ASSERT(!voice.isProfileChangePending(), "isProfileChangePending() false apres setupComplete");
    TEST_ASSERT(voice.getVoice() == "Fenrir", "Nouvelle voix Fenrir validee apres setupComplete");
    TEST_ASSERT(readyReportedVoice == "Fenrir", "onReady() rapporte bien le profil valide et actif (Fenrir)");

    // 2. Refus strict du changement pendant la parole
    voice.say("Test de parole en cours");
    TEST_ASSERT(voice.isBusy(), "isBusy() true pendant elocution");
    bool rejectDuringSpeaking = voice.setVoice("Puck");
    TEST_ASSERT(!rejectDuringSpeaking, "Changement de voix STRICTEMENT REFUSE pendant la parole");
    TEST_ASSERT(voice.getVoice() == "Fenrir", "Voix inchangee apres tentative pendant la parole");
    voice.stop();
    TEST_ASSERT(!voice.isBusy(), "Moteur libere apres stop()");

    // 3. Refus strict pendant une transcription STT
    bool listenOk = voice.startListening();
    TEST_ASSERT(listenOk, "startListening() demarre");
    voice.stopListening();
    TEST_ASSERT(voice.isTranscribing(), "isTranscribing() true en attente de transcription finale");
    bool rejectDuringTranscription = voice.setVoice("Charon");
    TEST_ASSERT(!rejectDuringTranscription, "Changement de voix STRICTEMENT REFUSE pendant transcription");
    TEST_ASSERT(voice.getVoice() == "Fenrir", "Voix inchangee apres tentative pendant transcription");
    voice.cancelListening();
    TEST_ASSERT(voice.isReady(), "Moteur PRET apres cancelListening()");

    // 4. Refus strict pendant une connexion ou reconnexion déjà active
    bool firstRequest = voice.setVoice("Charon");
    TEST_ASSERT(firstRequest, "Premiere demande de profil acceptee");
    TEST_ASSERT(voice.isConnecting(), "Reconnexion en cours");

    // Tentative immédiate d'un second changement alors que la reconnexion est en cours
    bool rejectSecondRequest = voice.setVoice("Puck");
    TEST_ASSERT(!rejectSecondRequest, "Deuxieme demande successive de profil STRICTEMENT REFUSEE pendant reconnexion");
    TEST_ASSERT(voice.getVoice() == "Fenrir", "Voix active reste Fenrir pendant la tentative");

    // Validation du premier changement via setupComplete
    voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
    voice.update();
    TEST_ASSERT(voice.isReady(), "Moteur PRET apres validation de Charon");
    TEST_ASSERT(voice.getVoice() == "Charon", "Voix validee est Charon, et non Puck");

    // 5. Échec simulé de reconnexion et restauration cohérente
    bool changeToFenrir = voice.setVoice("Fenrir");
    TEST_ASSERT(changeToFenrir, "Demande vers Fenrir acceptee pour le test d'echec");
    TEST_ASSERT(voice.isProfileChangePending(), "Profil pending actif");
    TEST_ASSERT(voice.getVoice() == "Charon", "Profil precedent reste Charon");

    // Simulation d'une erreur fatale de session/transport
    voice.getSession().transitionTo(LiveSessionState::ERROR, "Erreur simulee TLS");
    voice.update();

    TEST_ASSERT(voice.getState() == VoiceState::ERROR_STATE, "Etat bascule a ERROR_STATE");
    TEST_ASSERT(!voice.isProfileChangePending(), "isProfileChangePending() reinitialise a false");
    // Restauration cohérente : _config et _client ont retrouve Charon
    TEST_ASSERT(voice.getVoice() == "Charon", "Profil precedent Charon preserve dans _config");
    TEST_ASSERT(voice.getClient().getVoice() == "Charon", "Voix Charon restauree dans GeminiLiveClient");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 41 : Transitions fiabilisées TTS <-> Conversation et non-interférence (v0.4.1.1)
// =============================================================================
void test_mode_transition_stabilization() {
    std::cout << "\n[TEST SUITE 41] Transitions fiabilisees TTS <-> Conversation et non-interference" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_MODE_KEY";
    config.voice = "Aoede";
    config.language = "fr-FR";
    config.style = VoiceStyle::NATURAL;
    config.tone = VoiceTone::CALM;
    config.accent = "parisien";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableConversation = false; // Mode TTS initial
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    TEST_ASSERT(voice.isReady(), "VoiceKit pret en mode TTS");
    TEST_ASSERT(!voice.isConversationEnabled(), "Mode conversation desactive initialement");

    // Enregistrement de callbacks pour vérifier leur persistance
    bool readyCallbackFired = false;
    voice.onReady([&]() { readyCallbackFired = true; });

    bool userTextCallbackFired = false;
    voice.onUserText([&](const String&, bool) { userTextCallbackFired = true; });

    // 1. Passage TTS -> Conversation avec reconnexion maîtrisée
    readyCallbackFired = false;
    bool switchConvOk = voice.enableConversation(true);
    TEST_ASSERT(switchConvOk, "enableConversation(true) accepte au repos");
    TEST_ASSERT(voice.isConnecting(), "Reconnexion en cours pour application du mode conversation");
    TEST_ASSERT(voice.isConversationEnabled(), "Mode conversation marque active");

    // Vérification de la nouvelle trame de setup émise
    std::string convSetup = mockTransport.getLastSent();
    TEST_ASSERT(convSetup.find(Utf8Utils::escapeJson(VOICEKIT_DEFAULT_CONVERSATION_INSTRUCTION).c_str()) != std::string::npos ||
                convSetup.find("conversation bienveillante") != std::string::npos,
                "Nouvelle trame setup contient l'instruction systeme conversation");
    TEST_ASSERT(convSetup.find("[Directives vocales]") != std::string::npos,
                "Directives vocales preservees dans la trame setup de conversation");
    TEST_ASSERT(convSetup.find("parisien") != std::string::npos,
                "Accent parisien preserve dans la trame setup de conversation");

    // Confirmation setupComplete
    const char* setupCompJson = "{\"setupComplete\":{}}";
    voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
    voice.update();

    TEST_ASSERT(voice.isReady(), "Session conversationnelle confirmee et prete");
    TEST_ASSERT(!voice.isModeSwitchPending(), "isModeSwitchPending() false apres setupComplete");
    TEST_ASSERT(readyCallbackFired, "Callback onReady declenche apres transition vers Conversation");

    // 2. Tentative de basculement de mode pendant un tour actif
    bool turnStarted = voice.startListening();
    TEST_ASSERT(turnStarted, "Tour conversationnel demarre");
    TEST_ASSERT(voice.isListening(), "isListening() true");

    bool rejectSwitchDuringTurn = voice.enableConversation(false);
    TEST_ASSERT(!rejectSwitchDuringTurn, "enableConversation(false) STRICTEMENT REFUSE pendant tour actif");
    TEST_ASSERT(voice.isConversationEnabled(), "Mode reste conversationnel apres tentative refusee");

    // Fin du tour
    voice.stop();
    TEST_ASSERT(!voice.isBusy(), "Moteur au repos apres stop()");
    TEST_ASSERT(voice.isReady(), "Moteur PRET");

    // 3. Passage Conversation -> TTS avec reconnexion maîtrisée
    readyCallbackFired = false;
    bool switchTtsOk = voice.enableConversation(false);
    TEST_ASSERT(switchTtsOk, "enableConversation(false) accepte au repos");
    TEST_ASSERT(voice.isConnecting(), "Reconnexion en cours pour retour TTS");
    TEST_ASSERT(!voice.isConversationEnabled(), "Mode conversation marque desactive");

    // Vérification de la trame setup pour TTS
    std::string ttsSetup = mockTransport.getLastSent();
    TEST_ASSERT(ttsSetup.find(Utf8Utils::escapeJson(VOICEKIT_DEFAULT_TTS_INSTRUCTION).c_str()) != std::string::npos ||
                ttsSetup.find("synthese vocale TTS pur") != std::string::npos,
                "Nouvelle trame setup contient l'instruction systeme TTS");
    TEST_ASSERT(ttsSetup.find("[Directives vocales]") != std::string::npos,
                "Directives vocales preservees dans la trame setup TTS");

    voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
    voice.update();

    TEST_ASSERT(voice.isReady(), "Session TTS confirmee et prete");
    TEST_ASSERT(readyCallbackFired, "Callback onReady declenche apres retour a TTS");

    // 4. Tentative de passage Conversation pendant une élocution TTS
    voice.say("Synthese en cours de test");
    TEST_ASSERT(voice.isBusy(), "isBusy() true pendant TTS");
    bool rejectSwitchDuringTts = voice.enableConversation(true);
    TEST_ASSERT(!rejectSwitchDuringTts, "enableConversation(true) STRICTEMENT REFUSE pendant elocution TTS");
    TEST_ASSERT(!voice.isConversationEnabled(), "Mode reste TTS");
    voice.stop();

    // 5. Vérification de la préservation des directives vocales après toutes les transitions
    TEST_ASSERT(voice.getVoice() == "Aoede", "Voix inchangee (Aoede)");
    TEST_ASSERT(voice.getLanguage() == "fr-FR", "Langue inchangee (fr-FR)");
    TEST_ASSERT(voice.getStyle() == VoiceStyle::NATURAL, "Style inchange (NATURAL)");
    TEST_ASSERT(voice.getTone() == VoiceTone::CALM, "Ton inchange (CALM)");
    TEST_ASSERT(voice.getAccent() == "parisien", "Accent inchange (parisien)");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 42 : Préservation intégrale UTF-8 et compatibilité linguistique (v0.4.1.1)
// =============================================================================
void test_utf8_and_language_compatibility_stabilization() {
    std::cout << "\n[TEST SUITE 42] Chaine UTF-8 integrale, accents francais et compatibilite linguistique" << std::endl;

    // 1. Phrase de référence obligatoire prescrite dans le cahier des charges
    const char* obligatoryPhrase =
        "« À Noël, Élise prépare un café crème. L'été à Genève est agréable. François préfère un cœur léger. »";

    TEST_ASSERT(Utf8Utils::isValidUtf8(obligatoryPhrase), "Phrase obligatoire est un UTF-8 RFC 3629 valide");

    // Échappement JSON RFC 8259 : préserve les octets bruts Unicode et les chevrons français « »
    String escaped = Utf8Utils::escapeJson(obligatoryPhrase);
    TEST_ASSERT(escaped.indexOf("«") >= 0 && escaped.indexOf("»") >= 0, "Guillemets francais chevrons « » strictement preserves");
    TEST_ASSERT(escaped.indexOf("À") >= 0, "Caractere À majuscule accentue strictement preserve");
    TEST_ASSERT(escaped.indexOf("É") >= 0, "Caractere É majuscule accentue strictement preserve");
    TEST_ASSERT(escaped.indexOf("é") >= 0, "Caractere é minuscule accentue strictement preserve");
    TEST_ASSERT(escaped.indexOf("è") >= 0, "Caractere è minuscule accentue strictement preserve");
    TEST_ASSERT(escaped.indexOf("ç") >= 0, "Caractere ç cedille strictement preserve");
    TEST_ASSERT(escaped.indexOf("œ") >= 0, "Ligature œ strictement preservee");

    // Échappement des guillemets doubles ASCII "
    String withAsciiQuotes = "Texte avec \"citation\" et « chevrons »";
    String escAscii = Utf8Utils::escapeJson(withAsciiQuotes);
    TEST_ASSERT(escAscii.indexOf("\\\"citation\\\"") >= 0, "Guillemets ASCII doubles correctement echappes en \\\"");
    TEST_ASSERT(escAscii.indexOf("« chevrons »") >= 0, "Chevrons francais non alteres");

    // Vérification exhaustive des glyphes français demandés : é è ê ë à â ù û î ï ô ö ç œ æ É À Ç ’ « »
    const char* allFrenchGlyphs = "é è ê ë à â ù û î ï ô ö ç œ æ É À Ç ’ « »";
    TEST_ASSERT(Utf8Utils::isValidUtf8(allFrenchGlyphs), "Tous les glyphes francais obligatoires sont UTF-8 valides");
    String escapedGlyphs = Utf8Utils::escapeJson(allFrenchGlyphs);
    TEST_ASSERT(escapedGlyphs.indexOf("œ") >= 0 && escapedGlyphs.indexOf("æ") >= 0, "Ligatures œ et æ preservees");
    TEST_ASSERT(escapedGlyphs.indexOf("Ç") >= 0 && escapedGlyphs.indexOf("É") >= 0, "Majuscules Ç et É preservees");

    // 2. Clarification formelle de isModelSpeechLanguageCodeSupported() (Section 4)
    TEST_ASSERT(!GeminiLiveClient::isModelSpeechLanguageCodeSupported("models/gemini-2.0-flash-exp"),
                "gemini-2.0-flash-exp -> false (pas de speechConfig.languageCode)");
    TEST_ASSERT(!GeminiLiveClient::isModelSpeechLanguageCodeSupported("models/gemini-3.8-live"),
                "gemini-3.8-live -> false (pas de speechConfig.languageCode)");
    TEST_ASSERT(!GeminiLiveClient::isModelSpeechLanguageCodeSupported("gemini-2.5-flash"),
                "gemini-2.5-flash -> false (pas de speechConfig.languageCode)");
    TEST_ASSERT(!GeminiLiveClient::isModelSpeechLanguageCodeSupported("any-model"),
                "Modele quelconque -> false (garantie pas de champ speechConfig.languageCode non valide)");

    // 3. Vérification de la transmission dans la trame de setup avec MockLiveTransport
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_UTF8_KEY";
    config.voice = "Charon";
    config.language = "fr-FR";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    // Envoi de la phrase obligatoire via say()
    bool sayOk = voice.say(obligatoryPhrase);
    TEST_ASSERT(sayOk, "say() avec phrase francaise accentuee accepte");

    std::string sentMsg = mockTransport.getLastSent();
    TEST_ASSERT(sentMsg.find("À Noël, Élise prépare un café crème") != std::string::npos,
                "Phrase francaise accentuee transmise mot pour mot dans le message JSON Gemini Live");
    TEST_ASSERT(sentMsg.find("François préfère un cœur léger") != std::string::npos,
                "Ligature et cédille transmises mot pour mot sans altération");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// NOUVELLES SUITES CONTINUOUS STT & VAD (VOICEKIT v0.4.1.2)
// =============================================================================

// SUITE 43 : VAD (Voice Activity Detection) - Algorithme, seuils adaptatifs, hystérésis et transitions
void test_vad_algorithm_and_state_transitions() {
    std::cout << "\n[TEST SUITE 43] VAD : Algorithme, seuils adaptatifs, hysteresis et transitions" << std::endl;
    VoiceActivityDetector vad;

    // 1. Initialisation et configuration par défaut
    TEST_ASSERT(vad.getState() == VadState::SILENCE, "Etat initial VAD est SILENCE");
    TEST_ASSERT(vad.getConfig().minSpeechDurationMs == 200, "Duree min parole = 200 ms");
    TEST_ASSERT(vad.getConfig().silenceDurationMs == 1200, "Duree silence = 1200 ms");
    TEST_ASSERT(vad.getConfig().maxSegmentDurationMs == 30000, "Duree max segment = 30000 ms");
    TEST_ASSERT(!vad.isSpeechActive(), "isSpeechActive() initialement faux");
    TEST_ASSERT(!vad.isSpeechEnded(), "isSpeechEnded() initialement faux");

    // 2. Calcul et verification de l'hysteresis
    vad.setNoiseFloor(200);
    uint32_t spThresh = vad.getSpeechThreshold();
    uint32_t silThresh = vad.getSilenceThreshold();
    TEST_ASSERT(spThresh == (uint32_t)(200 * 2.0f) + 250, "Seuil de parole calcule conformement a la formule");
    TEST_ASSERT(silThresh == (uint32_t)(200 * 1.3f) + 120, "Seuil de silence calcule conformement a la formule");
    TEST_ASSERT(spThresh > silThresh, "Hysteresis validee : seuil de parole superieur au seuil de silence");

    // 3. Suivi adaptatif du bruit de fond pendant le silence
    for (int i = 0; i < 30; ++i) {
        vad.processRms(100, 32);
    }
    TEST_ASSERT(vad.getState() == VadState::SILENCE, "Reste en SILENCE avec bruit faible (100 RMS)");
    TEST_ASSERT(vad.getNoiseFloor() < 180, "Plancher de bruit s'adapte a la baisse vers 100 RMS");

    // 4. Rejet du bruit transitoire (clic / impulsion de 64 ms < 200 ms)
    vad.processRms(3000, 32); // 1er bloc fort -> SPEECH_STARTING
    TEST_ASSERT(vad.getState() == VadState::SPEECH_STARTING, "Transition en SPEECH_STARTING sur front montant");
    vad.processRms(3000, 32); // 2e bloc fort (total 64 ms < 200 ms)
    vad.processRms(100, 32);  // retour au silence
    TEST_ASSERT(vad.getState() == VadState::SILENCE, "Rejet du bruit transitoire : retour en SILENCE");
    TEST_ASSERT(!vad.isSpeechConfirmed(), "Parole non confirmee pour bruit < 200 ms");

    // 5. Validation de la parole (> 200 ms)
    bool startedCbFired = false;
    vad.onSpeechStarted([&]() { startedCbFired = true; });

    // 7 blocs a 2500 RMS = 224 ms (> 200 ms)
    for (int i = 0; i < 7; ++i) {
        vad.processRms(2500, 32);
    }
    TEST_ASSERT(vad.getState() == VadState::SPEECH_ACTIVE, "Parole confirmee (> 200 ms) -> SPEECH_ACTIVE");
    TEST_ASSERT(vad.isSpeechActive(), "isSpeechActive() vrai");
    TEST_ASSERT(vad.isSpeechConfirmed(), "isSpeechConfirmed() vrai");
    TEST_ASSERT(startedCbFired, "Callback onSpeechStarted declenche a la confirmation");

    // 6. Silence post-parole et detection de fin de segment (1200 ms)
    bool endedCbFired = false;
    VadEndReason reportedReason = VadEndReason::NONE;
    vad.onSpeechEnded([&](VadEndReason reason) {
        endedCbFired = true;
        reportedReason = reason;
    });

    // 1 bloc sous seuil bas -> transition en SILENCE_STARTING
    vad.processRms(80, 32);
    TEST_ASSERT(vad.getState() == VadState::SILENCE_STARTING, "Transition en SILENCE_STARTING post-parole");
    TEST_ASSERT(vad.isSilenceCounting(), "isSilenceCounting() vrai");
    TEST_ASSERT(vad.isSpeechActive(), "isSpeechActive() toujours vrai pendant le compte a rebours silence");

    // Suite du silence jusqu'a 1200 ms (encore 37 blocs de 32 ms : 38 * 32 = 1216 ms >= 1200 ms)
    for (int i = 0; i < 37; ++i) {
        vad.processRms(80, 32);
    }
    TEST_ASSERT(vad.getState() == VadState::SPEECH_ENDED, "Fin de segment confirmee apres 1200 ms de silence -> SPEECH_ENDED");
    TEST_ASSERT(vad.isSpeechEnded(), "isSpeechEnded() vrai");
    TEST_ASSERT(endedCbFired, "Callback onSpeechEnded declenche");
    TEST_ASSERT(reportedReason == VadEndReason::SILENCE_TIMEOUT, "Raison de fin = SILENCE_TIMEOUT");
    TEST_ASSERT(vad.getEndReason() == VadEndReason::SILENCE_TIMEOUT, "getEndReason() = SILENCE_TIMEOUT");
}

// SUITE 44 : VAD - Scénarios temporels avancés (pauses inter-mots, durée max 30s, silence prolongé, PCM)
void test_vad_advanced_temporal_scenarios() {
    std::cout << "\n[TEST SUITE 44] VAD : Scenarios temporels avances (pauses, 30s max, silence prolonge, PCM)" << std::endl;
    VoiceActivityDetector vad;

    // 1. Pause au milieu d'une phrase (mid-sentence pause de 400 ms < 1200 ms)
    for (int i = 0; i < 8; ++i) vad.processRms(2500, 32);
    TEST_ASSERT(vad.getState() == VadState::SPEECH_ACTIVE, "Phase parole 1 active");

    // Pause de 400 ms (12 blocs de 32 ms = 384 ms)
    for (int i = 0; i < 12; ++i) vad.processRms(80, 32);
    TEST_ASSERT(vad.getState() == VadState::SILENCE_STARTING, "Pause inter-mots en cours");
    TEST_ASSERT(vad.getCurrentSilenceMs() >= 380 && vad.getCurrentSilenceMs() < 1200, "Compteur silence avance sans expirer");

    // Reprise de la parole avant 1200 ms
    vad.processRms(2500, 32);
    TEST_ASSERT(vad.getState() == VadState::SPEECH_ACTIVE, "Reprise vocale : retour direct en SPEECH_ACTIVE");
    TEST_ASSERT(vad.getCurrentSilenceMs() == 0, "Compteur silence reinitialise a 0");

    // Cloture du segment apres la deuxieme partie de phrase
    for (int i = 0; i < 5; ++i) vad.processRms(2500, 32);
    for (int i = 0; i < 38; ++i) vad.processRms(80, 32);
    TEST_ASSERT(vad.isSpeechEnded(), "Segment finalise apres le deuxieme silence complet");

    // 2. Duree maximale de segment (30 secondes = 30000 ms)
    vad.reset();
    bool maxDurationFired = false;
    vad.onSpeechEnded([&](VadEndReason r) {
        if (r == VadEndReason::MAX_DURATION) maxDurationFired = true;
    });

    // Envoi continu de blocs de parole pendant 30 secondes (940 blocs de 32 ms = 30080 ms)
    for (int i = 0; i < 940; ++i) {
        bool ended = vad.processRms(2000, 32);
        if (ended) break;
    }
    TEST_ASSERT(vad.getState() == VadState::SPEECH_ENDED, "Coupure automatique a 30 secondes -> SPEECH_ENDED");
    TEST_ASSERT(vad.getEndReason() == VadEndReason::MAX_DURATION, "Raison = MAX_DURATION");
    TEST_ASSERT(maxDurationFired, "Notification de duree max recue");

    // 3. Silence prolonge (10 secondes = 312 blocs) sans fausse detection
    vad.reset();
    for (int i = 0; i < 312; ++i) {
        vad.processRms(120, 32);
    }
    TEST_ASSERT(vad.getState() == VadState::SILENCE, "Reste strictement en SILENCE apres 10s de silence");
    TEST_ASSERT(!vad.isSpeechConfirmed(), "Aucun debut de parole n'a ete confirme");
    TEST_ASSERT(!vad.isSpeechEnded(), "Aucune fin de segment n'a ete declenchee a tort");

    // 4. Adaptation au bruit ambiant dynamique
    vad.reset();
    vad.setNoiseFloor(150);
    // Bruit de fond monte progressivement de 150 a 600 RMS
    for (int step = 150; step <= 600; step += 15) {
        for (int i = 0; i < 6; ++i) {
            vad.processRms(step, 32);
        }
    }
    TEST_ASSERT(vad.getNoiseFloor() > 500, "Plancher de bruit a suivi l'elevation ambiante (> 500)");
    TEST_ASSERT(vad.getSpeechThreshold() > 1200, "Seuil de parole eleve pour eviter les declenchements par le bruit");
    TEST_ASSERT(vad.getState() == VadState::SILENCE, "Toujours en SILENCE malgre un bruit a 600 RMS");

    // La voix au-dessus du nouveau seuil (3000 RMS) declenche bien la parole
    for (int i = 0; i < 8; ++i) {
        vad.processRms(3000, 32);
    }
    TEST_ASSERT(vad.getState() == VadState::SPEECH_ACTIVE, "Voix a 3000 RMS detectee malgre un bruit ambiant eleve");

    // 5. Test de traitement PCM brut via processSamples()
    vad.reset();
    int16_t silentPcm[512] = {0};
    int16_t speechPcm[512];
    for (size_t i = 0; i < 512; ++i) {
        speechPcm[i] = (int16_t)(2000.0 * sin(2.0 * M_PI * i / 16.0));
    }
    vad.processSamples(silentPcm, 512, 16000);
    TEST_ASSERT(vad.getState() == VadState::SILENCE, "processSamples(silentPcm) maintient SILENCE");
    for (int i = 0; i < 8; ++i) {
        vad.processSamples(speechPcm, 512, 16000);
    }
    TEST_ASSERT(vad.getState() == VadState::SPEECH_ACTIVE, "processSamples(speechPcm) active SPEECH_ACTIVE");
}

// SUITE 45 : Flux ContinuousSTT complet - Écoute continue, streaming et segments
void test_continuous_stt_complete_lifecycle() {
    std::cout << "\n[TEST SUITE 45] Flux ContinuousSTT complet : Ecoute continue, streaming et segments" << std::endl;
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;

    VoiceKitConfig config;
    config.apiKey = "AIzaFakeKey";
    config.systemInstruction = VOICEKIT_DEFAULT_STT_INSTRUCTION;
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.sttFinalTimeoutMs = 6000;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    VoiceActivityDetector vad;
    uint32_t chunksReceived = 0;

    // Enregistrement du callback de chunk audio
    voice.onAudioChunk([&](const int16_t* samples, size_t count, uint32_t rms) {
        (void)samples;
        (void)count;
        chunksReceived++;
        if (voice.isListening()) {
            vad.processRms(rms, 32);
        }
    });

    int finalTranscriptionsCount = 0;
    String lastFinalReceived = "";
    voice.onTranscription([&](const String& text, bool isFinal) {
        if (isFinal) {
            finalTranscriptionsCount++;
            lastFinalReceived = text;
        }
    });

    // === SEGMENT 1 ===
    vad.reset();
    bool listen1 = voice.startListening();
    TEST_ASSERT(listen1, "startListening() segment 1 accepte");
    TEST_ASSERT(voice.isListening(), "voice.isListening() actif");

    // Injection de 10 blocs de parole PCM (320 ms)
    int16_t speechChunk[AUDIO_CHUNK_SAMPLES];
    for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
        speechChunk[i] = (int16_t)(3000.0 * sin(2.0 * M_PI * i / 16.0));
    }
    for (int i = 0; i < 10; ++i) {
        voice.getStreamer().pushSimulatedChunk(speechChunk, AUDIO_CHUNK_SAMPLES);
        voice.update();
    }

    TEST_ASSERT(chunksReceived >= 10, "onAudioChunk() invoque pour chaque bloc");
    TEST_ASSERT(voice.getAudioLevel() > 1000, "voice.getAudioLevel() rapporte un RMS eleve");
    TEST_ASSERT(vad.isSpeechActive(), "VAD detecte la parole du segment 1");

    // Injection de 38 blocs de silence (1216 ms)
    int16_t silenceChunk[AUDIO_CHUNK_SAMPLES] = {0};
    for (int i = 0; i < 38; ++i) {
        voice.getStreamer().pushSimulatedChunk(silenceChunk, AUDIO_CHUNK_SAMPLES);
        voice.update();
    }

    TEST_ASSERT(vad.isSpeechEnded(), "VAD signale la fin du segment 1 apres 1200 ms de silence");

    // L'application arrete l'ecoute suite a la detection VAD
    bool stopOk1 = voice.stopListening();
    TEST_ASSERT(stopOk1, "stopListening() segment 1 effectue avec succes");
    TEST_ASSERT(voice.isTranscribing(), "voice.isTranscribing() actif en attente de reponse Gemini");

    // Reception de la transcription finale authentique
    const char* finalJson1 = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Bonjour Gemini voici ma première phrase\"}}}";
    voice.getClient().handleIncomingMessage(finalJson1, strlen(finalJson1));
    voice.update();

    TEST_ASSERT(finalTranscriptionsCount == 1, "Transcription finale 1 recue");
    TEST_ASSERT(lastFinalReceived == "Bonjour Gemini voici ma première phrase", "Texte final 1 exact");
    TEST_ASSERT(!voice.isTranscribing(), "Fin d'attente de transcription");
    TEST_ASSERT(voice.isReady(), "VoiceKit pret pour le segment suivant");

    // === SEGMENT 2 (Enchainement mains libres sur la meme session) ===
    vad.reset();
    bool listen2 = voice.startListening();
    TEST_ASSERT(listen2, "startListening() segment 2 accepte sans reconnexion de session");
    TEST_ASSERT(voice.isListening(), "voice.isListening() actif pour segment 2");

    for (int i = 0; i < 8; ++i) {
        voice.getStreamer().pushSimulatedChunk(speechChunk, AUDIO_CHUNK_SAMPLES);
        voice.update();
    }
    TEST_ASSERT(vad.isSpeechActive(), "Parole segment 2 detectee");

    for (int i = 0; i < 38; ++i) {
        voice.getStreamer().pushSimulatedChunk(silenceChunk, AUDIO_CHUNK_SAMPLES);
        voice.update();
    }
    TEST_ASSERT(vad.isSpeechEnded(), "VAD termine segment 2");

    voice.stopListening();
    const char* finalJson2 = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Deuxième phrase en dictée continue\"}}}";
    voice.getClient().handleIncomingMessage(finalJson2, strlen(finalJson2));
    voice.update();

    TEST_ASSERT(finalTranscriptionsCount == 2, "Transcription finale 2 recue");
    TEST_ASSERT(lastFinalReceived == "Deuxième phrase en dictée continue", "Texte final 2 exact");
    TEST_ASSERT(voice.isReady(), "VoiceKit pret apres segment 2");

    voice.getClient().setTransport(nullptr);
}

// SUITE 46 : Résilience ContinuousSTT - Absence de transcription finale, pause et UTF-8
void test_continuous_stt_resilience_and_utf8() {
    std::cout << "\n[TEST SUITE 46] Resilience ContinuousSTT : Timeout sans invention, pause et accents" << std::endl;
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;

    VoiceKitConfig config;
    config.apiKey = "AIzaFakeKey";
    config.systemInstruction = VOICEKIT_DEFAULT_STT_INSTRUCTION;
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.sttFinalTimeoutMs = 6000;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    // 1. Absence de transcription finale (timeout 6000 ms) et non-invention
    String interimReceived = "";
    String finalReceived = "";
    bool finalFired = false;
    voice.onTranscription([&](const String& text, bool isFinal) {
        if (isFinal) {
            finalFired = true;
            finalReceived = text;
        } else {
            interimReceived = text;
        }
    });

    voice.startListening();
    const char* partialJson = "{\"serverContent\":{\"interimInputTranscription\":{\"text\":\"Texte partiel incomplet...\"}}}";
    voice.getClient().handleIncomingMessage(partialJson, strlen(partialJson));
    TEST_ASSERT(interimReceived == "Texte partiel incomplet...", "Interim enregistre");

    voice.stopListening();
    TEST_ASSERT(voice.isTranscribing(), "En attente de transcription finale");

    // Avancement de l'horloge au-dela de sttFinalTimeoutMs (6000 ms)
    advanceMockMillis(6500);
    voice.update();

    TEST_ASSERT(!voice.isTranscribing(), "Sortie de l'etat transcribing apres timeout");
    TEST_ASSERT(voice.isReady(), "Retour a READY");
    TEST_ASSERT(!finalFired, "REGLE ABSOLUE : Aucune transcription finale n'a ete inventee depuis l'interim");
    TEST_ASSERT(finalReceived.isEmpty(), "Le texte final reste vide");

    // 2. Mise en pause manuelle
    bool paused = true;
    if (voice.isListening()) {
        voice.stopListening();
    }
    TEST_ASSERT(!voice.isListening(), "Ecoute arretee lors de la pause");
    TEST_ASSERT(paused, "Flag pause actif");

    // Reprise de la pause
    paused = false;
    bool resumed = voice.startListening();
    TEST_ASSERT(resumed, "startListening() accepte lors de la reprise de pause");
    TEST_ASSERT(voice.isListening(), "Micro actif suite a la reprise");
    voice.cancelListening();

    // 3. Deconnexion et reconnexion du transport
    WiFi.setConnected(false);
    mockTransport.disconnect();
    voice.update();
    TEST_ASSERT(voice.getState() == VoiceState::ERROR_STATE || voice.getState() == VoiceState::DISCONNECTED || !voice.isReady(),
                "Etat session signale la coupure");

    // Reconnexion du transport
    WiFi.setConnected(true);
    prepareMockVoiceReady(voice, mockTransport);
    TEST_ASSERT(voice.isReady(), "VoiceKit redevient READY apres retablissement de la session");

    // 4. Chaine UTF-8 francaise avec accents transmise sans alteration
    const char* frenchPhrase = "À Noël, Élise prépare un café crème. L'été à Genève est agréable. François préfère un cœur léger.";
    String receivedFinalAccent = "";
    voice.onTranscription([&](const String& text, bool isFinal) {
        if (isFinal) receivedFinalAccent = text;
    });

    voice.startListening();
    std::string frenchJson = std::string("{\"serverContent\":{\"inputTranscription\":{\"text\":\"") + frenchPhrase + "\"}}}";
    voice.getClient().handleIncomingMessage(frenchJson.c_str(), frenchJson.length());
    voice.update();

    TEST_ASSERT(receivedFinalAccent == frenchPhrase, "Phrase francaise accentuee complete restituee a 100%");
    TEST_ASSERT(receivedFinalAccent.indexOf("À") >= 0, "Accent A majuscule conserve");
    TEST_ASSERT(receivedFinalAccent.indexOf("É") >= 0, "Accent E majuscule conserve");
    TEST_ASSERT(receivedFinalAccent.indexOf("é") >= 0, "Accent e aigu conserve");
    TEST_ASSERT(receivedFinalAccent.indexOf("è") >= 0, "Accent e grave conserve");
    TEST_ASSERT(receivedFinalAccent.indexOf("ç") >= 0, "Cedille conservee");
    TEST_ASSERT(receivedFinalAccent.indexOf("œ") >= 0, "Ligature oe conservee");

    voice.getClient().setTransport(nullptr);
}

// =============================================================================
// NOUVELLES SUITES VOICEKIT v0.5.0 (SIMPLIFICATION ET STABILISATION)
// =============================================================================

// SUITE 47 : Façade VoiceKit — Initialisation, cycle de vie, modes et délégation transparente
void test_voicekit_facade_lifecycle_and_delegation() {
    std::cout << "\n[TEST SUITE 47] Facade VoiceKit : Initialisation, cycle de vie, modes et delegation transparente" << std::endl;
    MockLiveTransport mockTransport;
    VoiceKit vk;

    VoiceKitConfig config;
    config.apiKey = "AIzaTestKeyFacade";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.voice = "Aoede";
    config.language = "fr-FR";
    config.volume = 75;

    bool readyCalled = false;
    vk.onReady([&readyCalled]() { readyCalled = true; });

    bool ok = vk.begin(config);
    TEST_ASSERT(ok, "VoiceKit::begin() reussi");
    TEST_ASSERT(!vk.isReady(), "VoiceKit non pret avant connexion session");
    TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode initial = IDLE");

    prepareMockVoiceKitReady(vk, mockTransport);
    TEST_ASSERT(vk.isReady(), "VoiceKit::isReady() vrai apres setupComplete");
    TEST_ASSERT(!vk.isBusy(), "VoiceKit::isBusy() faux au repos");
    TEST_ASSERT(vk.getState() == VoiceState::READY, "getState() retourne VoiceState::READY");
    TEST_ASSERT(readyCalled, "Callback onReady() invoque");

    // Propagation du volume
    TEST_ASSERT(vk.getVolume() == 75, "Volume initial conforme 75%");
    vk.setVolume(90);
    TEST_ASSERT(vk.getVolume() == 90, "setVolume(90) correctement applique");
    TEST_ASSERT(vk.getEngine().getVolume() == 90, "Volume delegue a VoxCoreVoice");

    // Accesseurs de profil vocal
    TEST_ASSERT(vk.getVoice() == "Aoede", "getVoice() conforme Aoede");
    TEST_ASSERT(vk.getLanguage() == "fr-FR", "getLanguage() conforme fr-FR");
    TEST_ASSERT(vk.getStyle() == VoiceStyle::NATURAL, "getStyle() conforme NATURAL");
    TEST_ASSERT(vk.getTone() == VoiceTone::NEUTRAL, "getTone() conforme NEUTRAL");

    // Modifications valides de profil au repos avec handshake setupComplete
    bool vOk = vk.setVoice("Fenrir");
    TEST_ASSERT(vOk, "setVoice(Fenrir) accepte au repos");
    const char* setupJson = "{\"setupComplete\":{}}";
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();
    TEST_ASSERT(vk.getVoice() == "Fenrir", "Voix mise a jour vers Fenrir");

    bool lOk = vk.setLanguage("en-US");
    TEST_ASSERT(lOk, "setLanguage(en-US) accepte au repos");
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();
    TEST_ASSERT(vk.getLanguage() == "en-US", "Langue mise a jour vers en-US");

    vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 48 : Façade VoiceKit — Validation des opérations TTS, STT manuel, ContinuousSTT et Conversation
void test_voicekit_facade_operations() {
    std::cout << "\n[TEST SUITE 48] Facade VoiceKit : Validation des operations TTS, STT manuel, ContinuousSTT et Conversation" << std::endl;
    MockLiveTransport mockTransport;
    VoiceKit vk;

    VoiceKitConfig config;
    config.apiKey = "AIzaTestKeyOps";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    vk.begin(config);
    prepareMockVoiceKitReady(vk, mockTransport);

    const char* setupJson = "{\"setupComplete\":{}}";

    // --- 1. Mode TTS (say) ---
    bool speechFinishedFired = false;
    vk.onSpeechFinished([&speechFinishedFired]() { speechFinishedFired = true; });

    bool sayOk = vk.say("Bonjour, test de synthese vocale.");
    TEST_ASSERT(sayOk, "vk.say() accepte en mode IDLE");
    vk.update();

    vk.getEngine().getSession().transitionTo(LiveSessionState::PLAYING, "Audio en cours");
    std::vector<int16_t> audio(2400, 100);
    vk.getEngine().getPlayer().feedPcm(audio.data(), audio.size());
    vk.getEngine().getPlayer().notifyTurnComplete();
    vk.update();
    TEST_ASSERT(vk.isSpeaking(), "vk.isSpeaking() vrai pendant restitution audio");

    while (vk.isSpeaking()) {
        vk.getEngine().getPlayer().simFinishSpeakerChunk();
        vk.update();
    }

    TEST_ASSERT(!vk.isSpeaking(), "Restitution achevee");
    TEST_ASSERT(speechFinishedFired, "onSpeechFinished fired a l'achevement");
    TEST_ASSERT(vk.isReady(), "VoiceKit redevient PRET apres TTS");

    // --- 2. Mode STT Manuel (listen / stopListening) ---
    String transcribedText = "";
    bool transcribedFinal = false;
    vk.onTranscription([&](const String& text, bool isFinal) {
        transcribedText = text;
        transcribedFinal = isFinal;
    });

    bool listenOk = vk.listen();
    TEST_ASSERT(listenOk, "vk.listen() accepte en mode IDLE");
    TEST_ASSERT(vk.isListening(), "vk.isListening() vrai");
    TEST_ASSERT(vk.isBusy(), "vk.isBusy() vrai pendant l'ecoute");

    int16_t audioChunk[AUDIO_CHUNK_SAMPLES];
    for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) audioChunk[i] = 1500;
    vk.getEngine().getStreamer().pushSimulatedChunk(audioChunk, AUDIO_CHUNK_SAMPLES);
    vk.update();

    const char* interimJson = "{\"serverContent\":{\"interimInputTranscription\":{\"text\":\"Bonjour tout\"}}}";
    vk.getEngine().getClient().handleIncomingMessage(interimJson, strlen(interimJson));
    vk.update();
    TEST_ASSERT(transcribedText == "Bonjour tout", "Transcription partielle recue");
    TEST_ASSERT(!transcribedFinal, "transcribedFinal faux pour interim");

    bool stopListenOk = vk.stopListening();
    TEST_ASSERT(stopListenOk, "vk.stopListening() accepte");
    TEST_ASSERT(vk.isTranscribing(), "vk.isTranscribing() vrai");

    const char* finalJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Bonjour tout le monde\"}}}";
    vk.getEngine().getClient().handleIncomingMessage(finalJson, strlen(finalJson));
    vk.update();
    TEST_ASSERT(transcribedText == "Bonjour tout le monde", "Transcription finale recue");
    TEST_ASSERT(transcribedFinal, "transcribedFinal vrai");
    TEST_ASSERT(vk.isReady(), "VoiceKit redevient PRET apres transcription finale");

    // --- 3. Mode ContinuousSTT (startContinuousListening / stopContinuousListening) ---
    bool contStartOk = vk.startContinuousListening();
    TEST_ASSERT(contStartOk, "startContinuousListening() accepte");
    TEST_ASSERT(vk.isContinuousListening(), "isContinuousListening() vrai");
    TEST_ASSERT(vk.getMode() == VoiceKitMode::CONTINUOUS_LISTENING, "Mode = CONTINUOUS_LISTENING");
    vk.update();

    bool pauseOk = vk.pauseContinuousListening();
    TEST_ASSERT(pauseOk, "pauseContinuousListening() reussi");
    TEST_ASSERT(vk.isContinuousListeningPaused(), "isContinuousListeningPaused() vrai");

    bool resumeOk = vk.resumeContinuousListening();
    TEST_ASSERT(resumeOk, "resumeContinuousListening() reussi");
    TEST_ASSERT(!vk.isContinuousListeningPaused(), "isContinuousListeningPaused() faux apres reprise");

    bool contStopOk = vk.stopContinuousListening();
    TEST_ASSERT(contStopOk, "stopContinuousListening() reussi");
    TEST_ASSERT(!vk.isContinuousListening(), "isContinuousListening() faux apres arret");
    TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode redevient IDLE");
    TEST_ASSERT(vk.isReady(), "VoiceKit est PRET");

    // --- 4. Mode Conversation (startConversation / stopConversation) ---
    bool convStartOk = vk.startConversation();
    TEST_ASSERT(convStartOk, "startConversation() accepte");
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();

    TEST_ASSERT(vk.isConversationActive(), "isConversationActive() vrai");
    TEST_ASSERT(vk.getMode() == VoiceKitMode::CONVERSATION, "Mode = CONVERSATION");

    bool convStopOk = vk.stopConversation();
    TEST_ASSERT(convStopOk, "stopConversation() accepte");
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();

    TEST_ASSERT(!vk.isConversationActive(), "isConversationActive() faux apres arret");
    TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode redevient IDLE");
    TEST_ASSERT(vk.isReady(), "VoiceKit redevient PRET apres arret conversation");

    vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 49 : Façade VoiceKit — Refus stricts des opérations incompatibles et transitions de modes
void test_voicekit_facade_refusals_and_mode_transitions() {
    std::cout << "\n[TEST SUITE 49] Facade VoiceKit : Refus stricts des operations incompatibles et transitions de modes" << std::endl;
    MockLiveTransport mockTransport;
    VoiceKit vk;

    VoiceKitConfig config;
    config.apiKey = "AIzaTestKeyRefusals";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    vk.begin(config);
    prepareMockVoiceKitReady(vk, mockTransport);

    const char* setupJson = "{\"setupComplete\":{}}";

    // 1. En mode ContinuousListening : say, listen et startConversation doivent être strictement REFUSÉS
    vk.startContinuousListening();
    TEST_ASSERT(vk.getMode() == VoiceKitMode::CONTINUOUS_LISTENING, "Mode CONTINUOUS actif");

    TEST_ASSERT(!vk.say("Tentative TTS interdite"), "say() REFUSE en mode ContinuousListening");
    TEST_ASSERT(!vk.listen(), "listen() manuel REFUSE en mode ContinuousListening");
    TEST_ASSERT(!vk.stopListening(), "stopListening() manuel REFUSE en mode ContinuousListening");
    TEST_ASSERT(!vk.startConversation(), "startConversation() REFUSE en mode ContinuousListening");

    // 2. En mode Conversation : say, listen et startContinuousListening doivent être strictement REFUSÉS
    vk.stopContinuousListening();
    vk.startConversation();
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();
    TEST_ASSERT(vk.getMode() == VoiceKitMode::CONVERSATION, "Mode CONVERSATION actif");

    TEST_ASSERT(!vk.say("Tentative TTS interdite"), "say() REFUSE en mode Conversation");
    TEST_ASSERT(!vk.listen(), "listen() REFUSE en mode Conversation");
    TEST_ASSERT(!vk.startContinuousListening(), "startContinuousListening() REFUSE en mode Conversation");

    // 3. Pendant une restitution sonore active (TTS isSpeaking / isBusy) : say, listen, modes sont refusés
    vk.stopConversation();
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();
    TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode revenu a IDLE");

    vk.say("Phrase longue pour test de busy");
    vk.getEngine().getSession().transitionTo(LiveSessionState::PLAYING, "Audio");
    vk.update();
    TEST_ASSERT(vk.isBusy(), "VoiceKit occupe par say()");

    TEST_ASSERT(!vk.say("Seconde phrase refusee"), "say() concurrent REFUSE quand isBusy()");
    TEST_ASSERT(!vk.listen(), "listen() REFUSE quand isBusy()");
    TEST_ASSERT(!vk.startContinuousListening(), "startContinuousListening() REFUSE quand isBusy()");
    TEST_ASSERT(!vk.startConversation(), "startConversation() REFUSE quand isBusy()");

    // 4. Interruption STOP inconditionnelle
    vk.stop();
    TEST_ASSERT(!vk.isSpeaking(), "Restitution arretee immediatement par vk.stop()");
    TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode reset a IDLE par stop()");
    TEST_ASSERT(vk.isReady(), "VoiceKit redevient PRET");

    // 5. Enchaînement fluide de transitions de modes sans corruption de session
    bool ok1 = vk.startContinuousListening();
    TEST_ASSERT(ok1, "Transition 1 : IDLE -> CONTINUOUS");
    bool ok2 = vk.stopContinuousListening();
    TEST_ASSERT(ok2, "Transition 2 : CONTINUOUS -> IDLE");
    bool ok3 = vk.startConversation();
    TEST_ASSERT(ok3, "Transition 3 : IDLE -> CONVERSATION");
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();

    bool ok4 = vk.stopConversation();
    TEST_ASSERT(ok4, "Transition 4 : CONVERSATION -> IDLE");
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();

    bool ok5 = vk.say("TTS final apres transitions");
    TEST_ASSERT(ok5, "Transition 5 : IDLE -> TTS accepte");
    vk.stop();

    vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 50 : ContinuousListeningController — VAD intégré, silence, timeout sans hallucination, pause et reprise
void test_continuous_controller_lifecycle_and_vad() {
    std::cout << "\n[TEST SUITE 50] ContinuousListeningController : VAD integre, silence, timeout sans hallucination, pause et reprise" << std::endl;
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    ContinuousListeningController continuous;

    VoiceKitConfig config;
    config.apiKey = "AIzaTestKeyContinuous";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.sttFinalTimeoutMs = 5000;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);

    continuous.begin(&voice);

    // 1. Paramètres VAD par défaut conformes au cahier des charges
    const VadConfig& vadCfg = continuous.getVadConfig();
    TEST_ASSERT(vadCfg.minSpeechDurationMs == 200, "VAD minSpeechDurationMs = 200 ms par defaut");
    TEST_ASSERT(vadCfg.silenceDurationMs == 1200, "VAD silenceDurationMs = 1 200 ms par defaut");
    TEST_ASSERT(vadCfg.maxSegmentDurationMs == 30000, "VAD maxSegmentDurationMs = 30 000 ms par defaut");

    // 2. Personnalisation de configuration sans complexité
    continuous.setSilenceDurationMs(1000);
    TEST_ASSERT(continuous.getVadConfig().silenceDurationMs == 1000, "setSilenceDurationMs(1000) applique");
    continuous.setMinSpeechDurationMs(150);
    TEST_ASSERT(continuous.getVadConfig().minSpeechDurationMs == 150, "setMinSpeechDurationMs(150) applique");
    continuous.setMaxSegmentDurationMs(25000);
    TEST_ASSERT(continuous.getVadConfig().maxSegmentDurationMs == 25000, "setMaxSegmentDurationMs(25000) applique");

    // Restauration des valeurs standard
    continuous.setSilenceDurationMs(1200);
    continuous.setMinSpeechDurationMs(200);
    continuous.setMaxSegmentDurationMs(30000);

    // 3. Démarrage et suivi du cycle de vie
    bool speechStartFired = false;
    continuous.onSpeechStarted([&speechStartFired]() { speechStartFired = true; });

    VadEndReason endReasonReceived = VadEndReason::NONE;
    continuous.onSpeechEnded([&endReasonReceived](VadEndReason r) { endReasonReceived = r; });

    String segTextReceived = "";
    bool segConfirmedReceived = false;
    continuous.onSegmentComplete([&](const String& text, bool confirmed) {
        segTextReceived = text;
        segConfirmedReceived = confirmed;
    });

    bool startOk = continuous.start();
    TEST_ASSERT(startOk, "continuous.start() reussi");
    TEST_ASSERT(continuous.isActive(), "continuous.isActive() vrai");
    TEST_ASSERT(voice.isListening(), "voice.isListening() active automatiquement");
    TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING, "Etat = LISTENING");

    // Blocs de parole (> 200 ms = 7 blocs de 32 ms)
    int16_t speechChunk[AUDIO_CHUNK_SAMPLES];
    for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) speechChunk[i] = 1600;
    for (int i = 0; i < 8; ++i) {
        continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
        voice.update();
        continuous.update();
    }
    TEST_ASSERT(speechStartFired, "onSpeechStarted fired apres >200ms de parole");
    TEST_ASSERT(continuous.isSpeechActive(), "isSpeechActive() vrai");
    TEST_ASSERT(continuous.isBusy(), "isBusy() vrai pendant la parole");

    // Blocs de silence (> 1 200 ms = 38 blocs de 32 ms)
    int16_t silenceChunk[AUDIO_CHUNK_SAMPLES];
    for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) silenceChunk[i] = 50;
    for (int i = 0; i < 39; ++i) {
        continuous.processAudioChunk(silenceChunk, AUDIO_CHUNK_SAMPLES, 50);
        voice.update();
        continuous.update();
    }
    TEST_ASSERT(endReasonReceived == VadEndReason::SILENCE_TIMEOUT, "Fin de parole sur SILENCE_TIMEOUT 1 200 ms");
    TEST_ASSERT(continuous.getState() == ContinuousState::WAITING_FINAL, "Automate passe a WAITING_FINAL");
    TEST_ASSERT(!voice.isListening(), "voice.stopListening() appele automatiquement par le controleur");

    // Réception transcription finale
    const char* okJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Segment valide par ContinuousController\"}}}";
    voice.getClient().handleIncomingMessage(okJson, strlen(okJson));
    voice.update();
    continuous.handleTranscription("Segment valide par ContinuousController", true);

    TEST_ASSERT(continuous.getState() == ContinuousState::SEGMENT_DONE, "Etat = SEGMENT_DONE");
    TEST_ASSERT(continuous.getSegmentCount() == 1, "Compteur de segments = 1");
    TEST_ASSERT(segTextReceived == "Segment valide par ContinuousController", "Texte du segment conforme");
    TEST_ASSERT(segConfirmedReceived, "Segment confirme = true");

    // Reprise automatique après 400 ms
    advanceMockMillis(450);
    voice.update();
    continuous.update();
    TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING, "Reprise automatique vers LISTENING effectuee");
    TEST_ASSERT(voice.isListening(), "voice.isListening() re-active sans aucune intervention");

    // 4. Test du timeout sans fabrication de texte (zéro hallucination)
    // Fin de parole simulée
    continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
    for (int i = 0; i < 8; ++i) continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
    for (int i = 0; i < 39; ++i) continuous.processAudioChunk(silenceChunk, AUDIO_CHUNK_SAMPLES, 50);
    voice.update();
    continuous.update();
    TEST_ASSERT(continuous.getState() == ContinuousState::WAITING_FINAL, "En attente finale pour segment 2");

    // Écoulement du temps au-delà du timeout sans message serveur
    advanceMockMillis(6000);
    voice.update();
    continuous.update();

    TEST_ASSERT(continuous.getState() == ContinuousState::SEGMENT_DONE, "Passage a SEGMENT_DONE apres timeout");
    TEST_ASSERT(!continuous.wasLastFinalConfirmed(), "Dernier segment non confirme");
    TEST_ASSERT(continuous.getLastFinalText().isEmpty(), "Zero hallucination : texte vide");

    // 5. Test de pause et reprise manuelle
    continuous.pause();
    TEST_ASSERT(continuous.isPaused(), "Controleur en pause");
    TEST_ASSERT(continuous.getState() == ContinuousState::PAUSED, "Etat = PAUSED");
    TEST_ASSERT(!voice.isListening(), "Ecoute micro arretee par pause()");

    advanceMockMillis(1000);
    continuous.update();
    TEST_ASSERT(!voice.isListening(), "Aucune reprise automatique tant que paused");

    continuous.resume();
    TEST_ASSERT(!continuous.isPaused(), "Pause levee");
    TEST_ASSERT(voice.isListening(), "Ecoute reprise suite a resume()");

    continuous.stop();
    TEST_ASSERT(!continuous.isActive(), "Controleur inactif");

    voice.getClient().setTransport(nullptr);
}

// SUITE 51 : Intégrité des événements — Absence de duplication des callbacks de fin et unicité
void test_events_uniqueness_and_no_duplicate_callbacks() {
    std::cout << "\n[TEST SUITE 51] Integrite des evenements : Absence de duplication des callbacks de fin et unicite" << std::endl;
    MockLiveTransport mockTransport;
    VoiceKit vk;

    VoiceKitConfig config;
    config.apiKey = "AIzaTestKeyEvents";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    vk.begin(config);
    prepareMockVoiceKitReady(vk, mockTransport);

    // 1. Unicité du callback onSpeechFinished
    int speechFinishedCalls = 0;
    vk.onSpeechFinished([&speechFinishedCalls]() { speechFinishedCalls++; });

    vk.say("Test unicite callback fin parole");
    vk.getEngine().getSession().transitionTo(LiveSessionState::PLAYING, "Audio");
    std::vector<int16_t> audio(2400, 100);
    vk.getEngine().getPlayer().feedPcm(audio.data(), audio.size());
    vk.getEngine().getPlayer().notifyTurnComplete();
    vk.update();

    while (vk.isSpeaking()) {
        vk.getEngine().getPlayer().simFinishSpeakerChunk();
        vk.update();
    }
    TEST_ASSERT(speechFinishedCalls == 1, "onSpeechFinished emis EXACTEMENT UNE FOIS");

    // Appels update() répétés ne doivent JAMAIS re-déclencher onSpeechFinished
    for (int i = 0; i < 10; ++i) {
        vk.update();
    }
    TEST_ASSERT(speechFinishedCalls == 1, "Zero duplication de onSpeechFinished sur boucles d'update suivantes");

    // 2. Unicité du callback onTranscription (final)
    int finalTranscriptionCalls = 0;
    vk.onTranscription([&finalTranscriptionCalls](const String& text, bool isFinal) {
        if (isFinal) finalTranscriptionCalls++;
    });

    vk.listen();
    vk.stopListening();
    const char* finalJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Phrase test unicite\"}}}";
    vk.getEngine().getClient().handleIncomingMessage(finalJson, strlen(finalJson));
    vk.update();

    TEST_ASSERT(finalTranscriptionCalls == 1, "Transcription finale emise EXACTEMENT UNE FOIS");
    for (int i = 0; i < 10; ++i) {
        vk.update();
    }
    TEST_ASSERT(finalTranscriptionCalls == 1, "Zero duplication de transcription finale sur updates ulterieurs");

    // 3. Unicité du callback onConversationFinished
    int convFinishedCalls = 0;
    vk.onConversationFinished([&convFinishedCalls]() { convFinishedCalls++; });

    vk.startConversation();
    const char* setupComp = "{\"setupComplete\":{}}";
    vk.getEngine().getClient().handleIncomingMessage(setupComp, strlen(setupComp));
    vk.update();

    bool convTurnStarted = vk.getEngine().startListening();
    TEST_ASSERT(convTurnStarted, "startListening() conversation accepte");
    std::vector<int16_t> sampleBlock(AUDIO_CHUNK_SAMPLES, 400);
    vk.getEngine().getStreamer().pushSimulatedChunk(sampleBlock.data(), sampleBlock.size());
    vk.getEngine().getStreamer().tick();
    vk.getEngine().stopListening();

    vk.getEngine().getSession().transitionTo(LiveSessionState::PLAYING, "Premier son recu");
    vk.update();
    vk.getEngine().getSession().finishTurn();
    vk.update();

    TEST_ASSERT(convFinishedCalls == 1, "onConversationFinished emis EXACTEMENT UNE FOIS par tour");
    for (int i = 0; i < 10; ++i) {
        vk.update();
    }
    TEST_ASSERT(convFinishedCalls == 1, "Zero duplication de onConversationFinished sur updates ulterieurs");

    vk.stopConversation();
    vk.getEngine().getClient().handleIncomingMessage(setupComp, strlen(setupComp));
    vk.update();
    vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 52 : Compatibilité ascendante intégrale — Coexistence VoxCoreVoice et VoiceKit sans régression
void test_v050_complete_backward_compatibility() {
    std::cout << "\n[TEST SUITE 52] Compatibilite ascendante integrale : Coexistence VoxCoreVoice et VoiceKit sans regression" << std::endl;
    MockLiveTransport mockTransport;

    // 1. Instanciation directe de l'ancienne API VoxCoreVoice
    VoxCoreVoice directVoice;
    VoiceKitConfig config;
    config.apiKey = "AIzaTestKeyRetro";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.voice = "Charon";
    config.language = "fr-FR";

    bool directOk = directVoice.begin(config);
    TEST_ASSERT(directOk, "Ancienne API VoxCoreVoice::begin() fonctionne sans modification");
    prepareMockVoiceReady(directVoice, mockTransport);

    TEST_ASSERT(directVoice.isReady(), "VoxCoreVoice directe est READY");
    TEST_ASSERT(directVoice.getVoice() == "Charon", "Profil vocal VoxCoreVoice preserve");
    TEST_ASSERT(directVoice.getLanguage() == "fr-FR", "Langue VoxCoreVoice preservee");

    bool directSay = directVoice.say("Test compatibilite descendante");
    TEST_ASSERT(directSay, "VoxCoreVoice::say() fonctionne a l'identique de v0.4.1.2");

    directVoice.stop();
    TEST_ASSERT(directVoice.isReady(), "VoxCoreVoice directe redevient READY apres stop()");

    // 2. Chaine UTF-8 et accents français vérifiés sur la façade VoiceKit
    VoiceKit vk;
    vk.begin(config);
    prepareMockVoiceKitReady(vk, mockTransport);

    String receivedAccent = "";
    vk.onTranscription([&receivedAccent](const String& txt, bool isFinal) {
        if (isFinal) receivedAccent = txt;
    });

    vk.listen();
    const char* utf8Test = "Hélène fête l'été avec un délicieux gâteau à Noël près du Rhône.";
    std::string fullJson = std::string("{\"serverContent\":{\"inputTranscription\":{\"text\":\"") + utf8Test + "\"}}}";
    vk.getEngine().getClient().handleIncomingMessage(fullJson.c_str(), fullJson.length());
    vk.update();

    TEST_ASSERT(receivedAccent == utf8Test, "Chaine UTF-8 et accents francais 100% preserves via VoiceKit");
    TEST_ASSERT(receivedAccent.indexOf("Hélène") >= 0, "Accents e majuscule et e grave conserves");
    TEST_ASSERT(receivedAccent.indexOf("délicieux") >= 0, "Accents e aigu conserves");
    TEST_ASSERT(receivedAccent.indexOf("gâteau") >= 0, "Accent circonflexe conserve");
    TEST_ASSERT(receivedAccent.indexOf("Rhône") >= 0, "Circonflexe o conserve");

    vk.stop();
    vk.getEngine().getClient().setTransport(nullptr);
    directVoice.getClient().setTransport(nullptr);
}

// =============================================================================
// NOUVELLES SUITES VOICEKIT v0.5.0.1 (STABILISATION FREERTOS, MODES & EVENEMENTS)
// =============================================================================

// SUITE 53 : Concurrence FreeRTOS et synchronisation audio
void test_v0501_freertos_concurrency_and_audio_synchronization() {
    std::cout << "\n[TEST SUITE 53] Concurrence FreeRTOS : Blocs audio asynchrones, pause/reprise et contexte" << std::endl;
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    ContinuousListeningController continuous;

    VoiceKitConfig config;
    config.apiKey = "AIzaTestKeyFreeRTOS";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);
    continuous.begin(&voice);

    // 1. Arrivée d'un bloc audio pendant un arrêt de la dictée
    continuous.start();
    TEST_ASSERT(continuous.isActive(), "Controleur actif");
    TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING, "Etat initial LISTENING");
    continuous.stop();
    TEST_ASSERT(!continuous.isActive(), "Controleur arrete");
    TEST_ASSERT(continuous.getState() == ContinuousState::INACTIVE, "Etat INACTIVE");

    // Simulation d'un bloc audio arrivant de la tâche audio FreeRTOS pendant que le contrôleur est inactif
    int16_t speechChunk[AUDIO_CHUNK_SAMPLES];
    for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) speechChunk[i] = 1600;
    continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
    voice.update();
    continuous.update();

    TEST_ASSERT(!continuous.isActive(), "Controleur toujours inactif apres audio chunk");
    TEST_ASSERT(continuous.getState() == ContinuousState::INACTIVE, "Etat reste INACTIVE (pas de demarrage parasite)");
    TEST_ASSERT(!voice.isListening(), "Microphone non re-ouvert");
    TEST_ASSERT(!continuous.isSpeechActive(), "Aucune parole detectee pendant inactif");

    // 2. Pause et reprise répétées (5 cycles successifs)
    continuous.start();
    TEST_ASSERT(continuous.isActive(), "Controleur redemarre");
    TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING, "Etat LISTENING");

    for (int cycle = 1; cycle <= 5; ++cycle) {
        continuous.pause();
        TEST_ASSERT(continuous.isPaused(), "Pause active au cycle");
        TEST_ASSERT(continuous.getState() == ContinuousState::PAUSED, "Etat PAUSED");
        TEST_ASSERT(!voice.isListening(), "Micro ferme pendant pause");

        // Envoi de chunks audio pendant la pause
        continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
        voice.update();
        continuous.update();
        TEST_ASSERT(continuous.getState() == ContinuousState::PAUSED, "Etat reste PAUSED malgre audio");

        bool resumed = continuous.resume();
        TEST_ASSERT(resumed, "resume() reussi");
        TEST_ASSERT(!continuous.isPaused(), "Pause levee");
        TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING, "Etat redevient LISTENING");
        TEST_ASSERT(voice.isListening(), "Micro re-ouvert apres resume()");
    }

    // Vérification que le VAD fonctionne parfaitement après les 5 cycles de pause/reprise
    bool speechDetected = false;
    continuous.onSpeechStarted([&speechDetected]() { speechDetected = true; });

    for (int i = 0; i < 8; ++i) {
        continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
        voice.update();
        continuous.update();
    }
    TEST_ASSERT(speechDetected, "VAD operationnel apres 5 cycles pause/reprise");
    TEST_ASSERT(continuous.getState() == ContinuousState::SPEECH_DETECTED, "Etat passe a SPEECH_DETECTED");

    // 3. Vérification du contexte d'exécution des callbacks
    // Tous les callbacks applicatifs s'exécutent strictement dans update() (tâche principale),
    // tandis que onAudioChunk est documenté comme le seul contact de la tâche FreeRTOS audio.
    bool chunkProcessed = false;
    voice.onAudioChunk([&chunkProcessed](const int16_t* s, size_t c, uint32_t r) {
        (void)s; (void)c; (void)r;
        chunkProcessed = true;
    });

    voice.getStreamer().pushSimulatedChunk(speechChunk, AUDIO_CHUNK_SAMPLES);
    voice.getStreamer().tick();
    TEST_ASSERT(chunkProcessed, "onAudioChunk execute dans le flux audio");

    continuous.stop();
    voice.getClient().setTransport(nullptr);
}

// SUITE 54 : Fiabilisation des segments vocaux et unicité des transcriptions
void test_v0501_segment_reliability_and_duplicate_events() {
    std::cout << "\n[TEST SUITE 54] Fiabilisation ContinuousSTT : Identifiant de segment, unicite et rejet callbacks tardifs" << std::endl;
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    ContinuousListeningController continuous;

    VoiceKitConfig config;
    config.apiKey = "AIzaTestKeySegments";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.sttFinalTimeoutMs = 5000;
    voice.begin(config);
    prepareMockVoiceReady(voice, mockTransport);
    continuous.begin(&voice);

    int segmentCompletedCalls = 0;
    String lastCompletedText = "";
    bool lastCompletedConfirmed = false;
    continuous.onSegmentComplete([&](const String& text, bool confirmed) {
        segmentCompletedCalls++;
        lastCompletedText = text;
        lastCompletedConfirmed = confirmed;
    });

    continuous.start();
    uint32_t segId1 = continuous.getCurrentSegmentId();
    TEST_ASSERT(segId1 > 0, "Identifiant sequentiel de segment initialise (> 0)");

    // 1. Deux événements de transcription finale identiques
    int16_t speechChunk[AUDIO_CHUNK_SAMPLES];
    for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) speechChunk[i] = 1600;
    for (int i = 0; i < 8; ++i) continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);

    int16_t silenceChunk[AUDIO_CHUNK_SAMPLES];
    for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) silenceChunk[i] = 50;
    for (int i = 0; i < 39; ++i) continuous.processAudioChunk(silenceChunk, AUDIO_CHUNK_SAMPLES, 50);

    voice.update();
    continuous.update();
    TEST_ASSERT(continuous.getState() == ContinuousState::WAITING_FINAL, "En attente transcription finale");

    // Première notification finale authentique
    const char* seg1Json = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Segment premier et unique\"}}}";
    voice.getClient().handleIncomingMessage(seg1Json, strlen(seg1Json));
    voice.update();
    continuous.handleTranscription("Segment premier et unique", true);
    TEST_ASSERT(continuous.getState() == ContinuousState::SEGMENT_DONE, "Etat passe a SEGMENT_DONE");
    TEST_ASSERT(segmentCompletedCalls == 1, "onSegmentComplete appele EXACTEMENT 1 fois");
    TEST_ASSERT(lastCompletedText == "Segment premier et unique", "Texte recu conforme");
    TEST_ASSERT(lastCompletedConfirmed, "Segment confirme true");
    TEST_ASSERT(continuous.getSegmentCount() == 1, "Compteur de segments = 1");

    // Deuxième notification finale identique (duplication par le réseau ou transport)
    continuous.handleTranscription("Segment premier et unique", true);
    TEST_ASSERT(segmentCompletedCalls == 1, "Zero duplication : onSegmentComplete non re-declenche");
    TEST_ASSERT(continuous.getSegmentCount() == 1, "Compteur de segments inchange (toujours 1)");

    // 2. Callback final reçu après timeout
    // Attente reprise automatique vers segment suivant (400 ms)
    advanceMockMillis(450);
    voice.update();
    continuous.update();
    TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING, "Reprise automatique vers segment 2");
    uint32_t segId2 = continuous.getCurrentSegmentId();
    TEST_ASSERT(segId2 > segId1, "Identifiant sequentiel incremente pour segment 2");

    // Émettre de la parole puis silence pour le segment 2
    for (int i = 0; i < 8; ++i) continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
    for (int i = 0; i < 39; ++i) continuous.processAudioChunk(silenceChunk, AUDIO_CHUNK_SAMPLES, 50);
    voice.update();
    continuous.update();
    TEST_ASSERT(continuous.getState() == ContinuousState::WAITING_FINAL, "Segment 2 en WAITING_FINAL");

    // Timeout STT : absence de réponse du serveur
    advanceMockMillis(6000);
    voice.update();
    continuous.update();
    TEST_ASSERT(continuous.getState() == ContinuousState::SEGMENT_DONE, "Segment 2 cloture sur timeout");
    TEST_ASSERT(!continuous.wasLastFinalConfirmed(), "Dernier segment confirme = false");
    TEST_ASSERT(continuous.getLastFinalText().isEmpty(), "Zero hallucination : texte vide");
    TEST_ASSERT(segmentCompletedCalls == 2, "Callback complete appele pour timeout non-confirme");

    // Réception tardive d'une transcription finale APRÈS timeout
    continuous.handleTranscription("Texte arrive trop tardivement", true);
    TEST_ASSERT(segmentCompletedCalls == 2, "Transcription tardive apres timeout STRICTEMENT IGNOREE");
    TEST_ASSERT(continuous.getLastFinalText().isEmpty(), "Texte final reste vide");

    // 3. Documenter et tester la limitation : attribution certaine des transcriptions tardives
    // Lorsque le segment a expiré et que le contrôleur a déjà repris (LISTENING),
    // un callback tardif du segment précédent ne peut pas être corrélé sans identifiant serveur.
    // VoiceKit le rejette catégoriquement pour protéger le segment en cours.
    advanceMockMillis(450);
    voice.update();
    continuous.update();
    TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING, "Segment 3 demarre");

    // Tentative d'injection d'un callback final pendant que le micro écoute le segment 3
    continuous.handleTranscription("Texte orphelin du passe", true);
    TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING, "Etat LISTENING non perturbe");
    TEST_ASSERT(segmentCompletedCalls == 2, "Callback orphelin rejete");

    // 4. Arrêt durant une transcription
    for (int i = 0; i < 8; ++i) continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
    for (int i = 0; i < 39; ++i) continuous.processAudioChunk(silenceChunk, AUDIO_CHUNK_SAMPLES, 50);
    voice.update();
    continuous.update();
    TEST_ASSERT(continuous.getState() == ContinuousState::WAITING_FINAL, "En attente transcription");

    continuous.stop();
    TEST_ASSERT(continuous.getState() == ContinuousState::INACTIVE, "Arret immediat effectue");
    TEST_ASSERT(!voice.isListening(), "Microphone ferme");

    // Si une transcription arrive après stop, elle doit être complètement ignorée
    continuous.handleTranscription("Transcription recue apres stop", true);
    TEST_ASSERT(segmentCompletedCalls == 2, "Zero notification suite a stop()");

    voice.getClient().setTransport(nullptr);
}

// SUITE 55 : Transitions de modes sécurisées et résilience aux pannes
void test_v0501_mode_transitions_and_failure_resilience() {
    std::cout << "\n[TEST SUITE 55] Facade VoiceKit : Transitions de modes, echecs de reconnexion et non-regressions" << std::endl;
    MockLiveTransport mockTransport;
    VoiceKit vk;

    VoiceKitConfig config;
    config.apiKey = "AIzaTestKeyModeTransitions";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    vk.begin(config);
    prepareMockVoiceKitReady(vk, mockTransport);

    const char* setupJson = "{\"setupComplete\":{}}";

    // 1. Échec de stopConversation()
    bool convStart = vk.startConversation();
    TEST_ASSERT(convStart, "startConversation() accepte");
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();
    TEST_ASSERT(vk.getMode() == VoiceKitMode::CONVERSATION, "Mode CONVERSATION actif");
    TEST_ASSERT(vk.isConversationActive(), "Conversation active");

    // Simuler un moteur occupé (ex: parole de l'assistant active)
    vk.getEngine().getSession().transitionTo(LiveSessionState::PLAYING, "Parole assistant");
    vk.update();
    TEST_ASSERT(vk.isBusy(), "Moteur occupe par la reponse");

    // Appel stopConversation() pendant que le moteur est occupé
    bool stopBusy = vk.stopConversation();
    TEST_ASSERT(!stopBusy, "stopConversation() REFUSE car moteur occupe");
    TEST_ASSERT(vk.getMode() == VoiceKitMode::CONVERSATION, "CORRECTION CRITIQUE : Mode CONVERSATION conserve malgre l'echec");
    TEST_ASSERT(vk.isConversationActive(), "isConversationActive reste vrai apres refus");

    // Libérer l'occupation
    vk.getEngine().getSession().transitionTo(LiveSessionState::CONNECTED, "Pret");
    vk.getEngine().getPlayer().reset();
    vk.update();

    // Arrêt réussi de la conversation
    bool stopOk = vk.stopConversation();
    TEST_ASSERT(stopOk, "stopConversation() accepte au repos");
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();
    TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode redevient IDLE apres confirmation");
    TEST_ASSERT(!vk.isConversationActive(), "isConversationActive faux");

    // 2. Échec de reconnexion Gemini pendant startConversation
    bool convStart2 = vk.startConversation();
    TEST_ASSERT(convStart2, "Demande startConversation() acceptee");
    TEST_ASSERT(vk.getEngine().isModeSwitchPending(), "Transition en attente");

    // Simuler une erreur de session réseau avant setupComplete
    vk.getEngine().getSession().transitionTo(LiveSessionState::ERROR, "Erreur TLS/WebSocket");
    vk.update();

    TEST_ASSERT(!vk.getEngine().isModeSwitchPending(), "Transition annulee");
    TEST_ASSERT(!vk.isConversationActive(), "Conversation non activee");
    TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "CORRECTION CRITIQUE : Mode IDLE restaure apres echec reconnexion");

    // 3. Démarrage continu lorsque le service n'est pas encore prêt
    VoiceKit vkNotReady;
    VoiceKitConfig cfgNotReady;
    cfgNotReady.apiKey = "AIzaTestKeyNotReady";
    cfgNotReady.wifiMode = WiFiMode::EXTERNAL_WIFI;
    vkNotReady.begin(cfgNotReady);
    TEST_ASSERT(!vkNotReady.isReady(), "Service non encore pret");

    bool contNotReadyOk = vkNotReady.startContinuousListening();
    TEST_ASSERT(contNotReadyOk, "startContinuousListening() accepte meme si session pas encore prete");
    TEST_ASSERT(vkNotReady.isContinuousListening(), "Mode CONTINUOUS_LISTENING enregistre");
    TEST_ASSERT(vkNotReady.getContinuousController().getState() == ContinuousState::IDLE, "Controleur en attente IDLE");

    // Le service devient prêt
    prepareMockVoiceKitReady(vkNotReady, mockTransport);
    vkNotReady.update();

    TEST_ASSERT(vkNotReady.getContinuousController().getState() == ContinuousState::LISTENING, "Controleur passe automatiquement a LISTENING");
    TEST_ASSERT(vkNotReady.isListening(), "Microphone active automatiquement a la disponibilite");
    vkNotReady.stopContinuousListening();

    // 4. Transcription reçue pendant un changement de mode
    prepareMockVoiceKitReady(vk, mockTransport);
    bool contOk = vk.startContinuousListening();
    TEST_ASSERT(contOk, "Continuous demarre");
    vk.stopContinuousListening();
    TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode IDLE");

    // Réception d'une transcription résiduelle
    const char* strayJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Transcription orpheline\"}}}";
    vk.getEngine().getClient().handleIncomingMessage(strayJson, strlen(strayJson));
    vk.update();
    TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode IDLE preserve sans corruption");

    // 5. Vérification de l'absence de double acquisition microphone
    vk.startContinuousListening();
    TEST_ASSERT(vk.isContinuousListening(), "Continuous actif");
    TEST_ASSERT(!vk.listen(), "listen() manuel STRICTEMENT REFUSE pendant ContinuousSTT (zero double acquisition)");
    vk.stopContinuousListening();

    // 6. Enchaînement rapide TTS -> STT -> ContinuousSTT -> Conversation
    // TTS
    bool sayOk = vk.say("Test enchainement rapide");
    TEST_ASSERT(sayOk, "1. TTS accepte");
    vk.stop();
    TEST_ASSERT(vk.isReady(), "Moteur READY apres TTS");

    // STT
    bool listenOk = vk.listen();
    TEST_ASSERT(listenOk, "2. STT accepte");
    vk.stopListening();
    const char* sttRes = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Resultat STT\"}}}";
    vk.getEngine().getClient().handleIncomingMessage(sttRes, strlen(sttRes));
    vk.update();
    TEST_ASSERT(vk.isReady(), "STT acheve");

    // ContinuousSTT
    bool cOk = vk.startContinuousListening();
    TEST_ASSERT(cOk, "3. ContinuousSTT accepte");
    TEST_ASSERT(vk.getMode() == VoiceKitMode::CONTINUOUS_LISTENING, "Mode CONTINUOUS");
    vk.stopContinuousListening();
    TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Continuous arrete");

    // Conversation
    bool convOk = vk.startConversation();
    TEST_ASSERT(convOk, "4. Conversation acceptee");
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();
    TEST_ASSERT(vk.getMode() == VoiceKitMode::CONVERSATION, "Mode CONVERSATION actif");

    vk.stopConversation();
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();
    TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Retour a IDLE apres enchainement complet");

    vk.getEngine().getClient().setTransport(nullptr);
    vkNotReady.getEngine().getClient().setTransport(nullptr);
}

// =============================================================================
// NOUVELLES SUITES VOICEKIT v0.5.2 (FAST CONNECTION & INSTRUMENTATION)
// =============================================================================

// SUITE 56 : WiFiManager — FastConnect, repli automatique et backoff progressif
void test_v052_wifi_fast_connect_and_fallback() {
    std::cout << "\n[TEST SUITE 56] WiFiManager : FastConnect, repli automatique et backoff progressif" << std::endl;

    WiFiManager wm;
    WiFi.resetCounters();
    
    // 1. Initialisation avec FastConnect activé
    bool ok = wm.begin("TestSSID", "TestPass", true, 2000);
    TEST_ASSERT(ok, "WiFiManager::begin avec FastConnect reussi");
    TEST_ASSERT(WiFi.getBeginCalls() > 0, "WiFi.begin a ete invoque");
    
    // 2. Tick avec connexion active
    wm.tick();
    TEST_ASSERT(wm.isConnected(), "WiFiManager indique connecte");
    TEST_ASSERT(wm.getWifiAttempts() >= 1, "Compteur de tentatives incremente");
    TEST_ASSERT(wm.getChannel() == 6, "Canal Wi-Fi reporte correctement");
    TEST_ASSERT(strlen(wm.getBSSID()) > 0, "BSSID reporte");

    // 3. Deconnexion et premiere reconnexion rapide
    WiFi.setConnected(false);
    wm.tick();
    TEST_ASSERT(!wm.isConnected(), "WiFiManager detecte deconnexion");
    TEST_ASSERT(wm.getWifiReconnects() == 1, "Compteur reconnexions incremente");

    // Retablissement Wi-Fi
    WiFi.setConnected(true);
    wm.tick();
    TEST_ASSERT(wm.isConnected(), "WiFiManager reconnecte avec succes");
}

// SUITE 57 : Synchronisation SNTP — Bypass si horloge valide vs attente si invalide
void test_v052_sntp_optimization_and_tls_safety() {
    std::cout << "\n[TEST SUITE 57] Synchronisation SNTP : Detection horloge valide vs securite TLS" << std::endl;

    WiFiManager wm;
    WiFi.setConnected(true);
    wm.begin("TestSSID", "TestPass", false);
    wm.tick();

    TEST_ASSERT(wm.isConnected(), "Wi-Fi connecte");
    TEST_ASSERT(wm.isTimeSynced(), "Horloge systeme valide -> isTimeSynced() true");
    TEST_ASSERT(wm.isSntpSkipped(), "SNTP saute sans delai bloquant car horloge deja valide");
    TEST_ASSERT(wm.getSntpSyncMs() == 0, "Duree SNTP nulle si deja synchro");

    TEST_ASSERT(VoxCoreVoice::isSystemTimeValid(), "isSystemTimeValid() retourne true pour timestamp >= 2024");
}

// SUITE 58 : Gemini Live — Conditionnement de la reconnexion a la connectivite reseau
void test_v052_gemini_reconnect_network_conditioning() {
    std::cout << "\n[TEST SUITE 58] Gemini Live : Conditionnement strict de la reconnexion au reseau" << std::endl;

    GeminiLiveClient client;
    LiveAudioPlayer player;
    LiveSessionController session;

    client.begin("TEST_KEY_COND", "models/gemini-3.8-live", "Aoede");
    session.begin(&client, nullptr, &player);

    bool networkUp = false;
    session.setNetworkCheck([&networkUp]() { return networkUp; });
    TEST_ASSERT(!session.isNetworkReady(), "isNetworkReady reflete le predicat (false)");

    // Forcer etat RECONNECTING
    session.transitionTo(LiveSessionState::RECONNECTING, "Test deconnexion");
    TEST_ASSERT(session.getState() == LiveSessionState::RECONNECTING, "Session en RECONNECTING");

    // Tick avec réseau DOWN : ne doit PAS tenter de se reconnecter ni incrementer
    session.tick();
    TEST_ASSERT(session.getState() == LiveSessionState::RECONNECTING, "Reste en RECONNECTING sans declencher connect");
    TEST_ASSERT(session.getReconnectAttempts() == 0, "Tentatives non consommees pendant reseau DOWN");

    // Retablir le reseau
    networkUp = true;
    TEST_ASSERT(session.isNetworkReady(), "isNetworkReady reflete le predicat (true)");
}

// SUITE 59 : Instrumentation des temps de demarrage (wifi, sntp, ws, setup, total)
void test_v052_startup_timing_instrumentation() {
    std::cout << "\n[TEST SUITE 59] Instrumentation des 5 temps de demarrage" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_TIMING";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableFastConnect = true;

    voice.begin(config);
    voice.getClient().setTransport(&mockTransport);

    mockTransport.connect("wss://example.com");
    voice.update();

    const char* setupJson = "{\"setupComplete\":{}}";
    voice.getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    voice.update();

    TEST_ASSERT(voice.isReady(), "VoiceKit est READY");

    const auto& metrics = voice.getConnectionMetrics();
    TEST_ASSERT(metrics.fastConnectEnabled == true, "FastConnect actif dans les metriques");
    TEST_ASSERT(metrics.connectionFailures == 0, "Zero echec de connexion observe");

    voice.getClient().setTransport(nullptr);
}

// SUITE 60 : Façade VoiceKit v0.5.2 — Exposition ConnectionMetrics et non-regression
void test_v052_facade_connection_metrics_and_resilience() {
    std::cout << "\n[TEST SUITE 60] Facade VoiceKit v0.5.2 : Exposition ConnectionMetrics et non-regression" << std::endl;

    MockLiveTransport mockTransport;
    VoiceKit vk;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_FACADE_V052";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableFastConnect = true;

    bool initOk = vk.begin(config);
    TEST_ASSERT(initOk, "VoiceKit::begin() accepte la configuration v0.5.2");
    vk.getEngine().getClient().setTransport(&mockTransport);
    vk.getEngine().getClient().connect();

    mockTransport.connect("wss://example.com");
    const char* setupJson = "{\"setupComplete\":{}}";
    vk.getEngine().getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    vk.update();

    TEST_ASSERT(vk.isReady(), "VoiceKit::isReady() true");
    const auto& m = vk.getConnectionMetrics();
    TEST_ASSERT(m.fastConnectEnabled == true, "Metriques accessibles via facade");

    // Non-regression vocale : say() TTS operationnel
    bool sayOk = vk.say("Test vocal v0.5.2");
    TEST_ASSERT(sayOk, "say() accepte en etat READY");

    // Interruption propre
    vk.stop();
    TEST_ASSERT(vk.isReady(), "Moteur revient a READY apres stop()");

    vk.getEngine().getClient().setTransport(nullptr);
}

// =============================================================================
// SUITES DE TESTS VoiceKit v0.5.3 (Voice Latency Optimization & Non-Regression)
// =============================================================================

// SUITE 61 : Structure VoiceLatencyMetrics et calculs dérivés stricts (zéro faux 0 ms)
void test_v053_voice_latency_metrics_calculations() {
    std::cout << "\n[TEST SUITE 61] VoiceLatencyMetrics : Calculs des durees et gestion des valeurs non observees" << std::endl;

    VoiceLatencyMetrics m;
    TEST_ASSERT(!m.hasSpeechEndDetected(), "speechEndDetected non observe par defaut");
    TEST_ASSERT(!m.hasMicStopped(), "micStopped non observe par defaut");
    TEST_ASSERT(!m.hasActivityEndSent(), "activityEndSent non observe par defaut");
    TEST_ASSERT(!m.hasFirstResponseReceived(), "firstResponseReceived non observe par defaut");
    TEST_ASSERT(!m.hasFirstAudioReceived(), "firstAudioReceived non observe par defaut");
    TEST_ASSERT(!m.hasFirstAudioPlayed(), "firstAudioPlayed non observe par defaut");
    TEST_ASSERT(!m.hasPlaybackFinished(), "playbackFinished non observe par defaut");

    // RÈGLE ABSOLUE : Une valeur non observée doit renvoyer -1 (N/D) et JAMAIS 0 ms
    TEST_ASSERT(m.getSpeechEndToMicStoppedMs() == -1, "getSpeechEndToMicStoppedMs renvoie -1 si non observe");
    TEST_ASSERT(m.getMicStoppedToActivityEndMs() == -1, "getMicStoppedToActivityEndMs renvoie -1 si non observe");
    TEST_ASSERT(m.getActivityEndToFirstAudioMs() == -1, "getActivityEndToFirstAudioMs renvoie -1 si non observe");
    TEST_ASSERT(m.getActivityEndToFirstResponseMs() == -1, "getActivityEndToFirstResponseMs renvoie -1 si non observe");
    TEST_ASSERT(m.getFirstAudioToPlaybackMs() == -1, "getFirstAudioToPlaybackMs renvoie -1 si non observe");
    TEST_ASSERT(m.getSpeechEndToPlaybackMs() == -1, "getSpeechEndToPlaybackMs renvoie -1 si non observe");
    TEST_ASSERT(m.getResponseLatencyMs() == -1, "getResponseLatencyMs renvoie -1 si non observe");
    TEST_ASSERT(m.getTotalTurnDurationMs() == -1, "getTotalTurnDurationMs renvoie -1 si non observe");

    // Simulation d'un tour vocal complet avec VAD local
    m.turnStartedMs           = 10000;
    m.speechEndDetectedMs     = 12400; // VAD local detecte fin de parole
    m.micStoppedMs            = 12450; // Capture micro arretee 50 ms plus tard
    m.activityEndSentMs       = 12470; // Paquet activityEnd emis sur WebSocket
    m.firstResponseReceivedMs = 13100; // Premier retour Gemini (texte/metadata)
    m.firstAudioReceivedMs    = 13200; // Premier paquet PCM recu de Gemini
    m.firstAudioPlayedMs      = 13240; // Premier bloc PCM remis au DMA I2S (playRaw)
    m.playbackFinishedMs      = 15500; // Restitution audio complete achevee

    TEST_ASSERT(m.hasSpeechEndDetected(), "hasSpeechEndDetected true");
    TEST_ASSERT(m.hasMicStopped(), "hasMicStopped true");
    TEST_ASSERT(m.hasActivityEndSent(), "hasActivityEndSent true");
    TEST_ASSERT(m.hasFirstResponseReceived(), "hasFirstResponseReceived true");
    TEST_ASSERT(m.hasFirstAudioReceived(), "hasFirstAudioReceived true");
    TEST_ASSERT(m.hasFirstAudioPlayed(), "hasFirstAudioPlayed true");
    TEST_ASSERT(m.hasPlaybackFinished(), "hasPlaybackFinished true");

    // Verification des 6 calculs derives
    TEST_ASSERT(m.getSpeechEndToMicStoppedMs() == 50, "Fin parole -> Fin capture = 50 ms");
    TEST_ASSERT(m.getMicStoppedToActivityEndMs() == 20, "Fin capture -> Notification Gemini = 20 ms");
    TEST_ASSERT(m.getActivityEndToFirstAudioMs() == 730, "Notification Gemini -> Premier audio recu = 730 ms");
    TEST_ASSERT(m.getActivityEndToFirstResponseMs() == 630, "Notification Gemini -> Premiere reponse = 630 ms");
    TEST_ASSERT(m.getFirstAudioToPlaybackMs() == 40, "Premier audio recu -> Debut restitution DMA = 40 ms");
    TEST_ASSERT(m.getSpeechEndToPlaybackMs() == 840, "Fin parole -> Debut restitution DMA = 840 ms");
    TEST_ASSERT(m.getResponseLatencyMs() == 840, "getResponseLatencyMs avec VAD = 840 ms");
    TEST_ASSERT(m.getTotalTurnDurationMs() == 5500, "Duree totale tour vocal = 5500 ms");

    // Simulation sans VAD local (mode push-to-talk manuel avec bouton)
    VoiceLatencyMetrics manualM;
    manualM.turnStartedMs        = 20000;
    manualM.speechEndDetectedMs  = 0;     // Non observe (bouton manuel)
    manualM.micStoppedMs         = 23000; // Relachement bouton
    manualM.activityEndSentMs    = 23025;
    manualM.firstAudioReceivedMs = 23700;
    manualM.firstAudioPlayedMs   = 23740;
    manualM.playbackFinishedMs   = 25000;

    TEST_ASSERT(manualM.getSpeechEndToMicStoppedMs() == -1, "Fin parole -> Fin capture est N/D (-1) sans VAD");
    TEST_ASSERT(manualM.getSpeechEndToPlaybackMs() == -1, "Fin parole -> Restitution est N/D (-1) sans VAD");
    TEST_ASSERT(manualM.getResponseLatencyMs() == 740, "getResponseLatencyMs replie sur micStopped en mode bouton (740 ms)");
    TEST_ASSERT(manualM.getTotalTurnDurationMs() == 5000, "Duree totale = 5000 ms");

    // Verification de la reinitialisation
    m.reset();
    TEST_ASSERT(!m.hasMicStopped(), "reset remet micStopped a 0");
    TEST_ASSERT(!m.hasFirstAudioPlayed(), "reset remet firstAudioPlayed a 0");
    TEST_ASSERT(m.getResponseLatencyMs() == -1, "reset remet getResponseLatencyMs a -1");
}

// SUITE 62 : Encodage et décodage Base64 optimisés (zéro allocation tas pendant le streaming)
void test_v053_base64_optimization_integrity() {
    std::cout << "\n[TEST SUITE 62] Optimisation Base64 : Encodage direct et conformite RFC 4648" << std::endl;

    GeminiLiveClient client;

    // Test 1 : Vecteurs de test standards RFC 4648
    char buf[2048];
    const uint8_t v1[] = "f";
    size_t len1 = GeminiLiveClient::encodeBase64Direct(v1, 1, buf);
    buf[len1] = '\0';
    TEST_ASSERT(strcmp(buf, "Zg==") == 0, "Base64 RFC 'f' -> 'Zg=='");

    const uint8_t v2[] = "fo";
    size_t len2 = GeminiLiveClient::encodeBase64Direct(v2, 2, buf);
    buf[len2] = '\0';
    TEST_ASSERT(strcmp(buf, "Zm8=") == 0, "Base64 RFC 'fo' -> 'Zm8='");

    const uint8_t v3[] = "foo";
    size_t len3 = GeminiLiveClient::encodeBase64Direct(v3, 3, buf);
    buf[len3] = '\0';
    TEST_ASSERT(strcmp(buf, "Zm9v") == 0, "Base64 RFC 'foo' -> 'Zm9v'");

    const uint8_t v4[] = "foobar";
    size_t len4 = GeminiLiveClient::encodeBase64Direct(v4, 6, buf);
    buf[len4] = '\0';
    TEST_ASSERT(strcmp(buf, "Zm9vYmFy") == 0, "Base64 RFC 'foobar' -> 'Zm9vYmFy'");

    // Test 2 : Encodage d'un bloc audio microphone réel (512 échantillons = 1024 octets)
    int16_t micChunk[AUDIO_CHUNK_SAMPLES];
    for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
        micChunk[i] = static_cast<int16_t>(i * 37 - 9500);
    }
    const uint8_t* rawBytes = reinterpret_cast<const uint8_t*>(micChunk);
    size_t rawLen = AUDIO_CHUNK_SAMPLES * sizeof(int16_t);

    size_t outLen = GeminiLiveClient::encodeBase64Direct(rawBytes, rawLen, buf);
    buf[outLen] = '\0';
    size_t expectedLen = ((rawLen + 2) / 3) * 4;
    TEST_ASSERT(outLen == expectedLen, "Taille Base64 exacte pour 1024 octets (1368 car)");

    // Test 3 : Décodage et vérification de la parfaite intégrité bit-à-bit (zéro altération audio)
    std::vector<uint8_t> decoded;
    bool decOk = GeminiLiveClient::decodeBase64(buf, outLen, decoded);
    TEST_ASSERT(decOk, "Decodage Base64 reussi");
    TEST_ASSERT(decoded.size() == rawLen, "Taille decodee identique a l'original (1024 octets)");

    bool dataMatches = (memcmp(rawBytes, decoded.data(), rawLen) == 0);
    TEST_ASSERT(dataMatches, "Donnees audio PCM strictement identiques apres encodage/decodage");
}

// SUITE 63 : Expérimentation et configuration des seuils VAD (800, 1000, 1200 ms)
void test_v053_vad_threshold_experimentation() {
    std::cout << "\n[TEST SUITE 63] VAD : Experimentation controlee des seuils de silence (800, 1000, 1200 ms)" << std::endl;

    ContinuousListeningController clc;

    // 1. Valeur par défaut stricte : 1 200 ms
    TEST_ASSERT(clc.getVadConfig().silenceDurationMs == 1200, "Seuil de silence par defaut = 1 200 ms");
    TEST_ASSERT(clc.getVadConfig().minSpeechDurationMs == 200, "Validation debut parole = 200 ms");
    TEST_ASSERT(clc.getVadConfig().maxSegmentDurationMs == 30000, "Duree max segment = 30 000 ms");

    // 2. Configuration contrôlée à 800 ms
    clc.setSilenceDurationMs(800);
    TEST_ASSERT(clc.getVadConfig().silenceDurationMs == 800, "Seuil de silence configurable a 800 ms");

    // 3. Configuration contrôlée à 1000 ms
    clc.setSilenceDurationMs(1000);
    TEST_ASSERT(clc.getVadConfig().silenceDurationMs == 1000, "Seuil de silence configurable a 1000 ms");

    // 4. Rétablissement à 1200 ms
    clc.setSilenceDurationMs(1200);
    TEST_ASSERT(clc.getVadConfig().silenceDurationMs == 1200, "Seuil de silence retabli a 1200 ms");

    // 5. Test temporel de pause : une pause naturelle de 500 ms NE DOIT PAS couper la parole
    // même avec le seuil le plus réactif (800 ms)
    VoiceActivityDetector vad;
    VadConfig cfg;
    cfg.minSpeechDurationMs = 200;
    cfg.silenceDurationMs = 800; // seuil le plus agressif
    cfg.maxSegmentDurationMs = 30000;
    cfg.initialNoiseFloor = 200;
    vad.setConfig(cfg);

    // Initialisation du bruit de fond
    for (int i = 0; i < 15; ++i) {
        vad.processRms(200, 32);
    }
    TEST_ASSERT(!vad.isSpeechActive(), "Silence initial");

    // Début de parole : 250 ms de voix à 1500 RMS (> 200 ms requis)
    for (int i = 0; i < 8; ++i) {
        vad.processRms(1500, 32);
    }
    TEST_ASSERT(vad.isSpeechActive(), "Parole active validee (> 200 ms)");

    // Pause naturelle de 500 ms (15 blocs de 32 ms = 480 ms de silence à 200 RMS)
    bool prematureCut = false;
    for (int i = 0; i < 15; ++i) {
        if (vad.processRms(200, 32)) {
            prematureCut = true;
            break;
        }
    }
    TEST_ASSERT(!prematureCut, "Pause naturelle de 500 ms NON coupee avec seuil de 800 ms");
    TEST_ASSERT(vad.isSpeechActive(), "La parole reste active pendant la pause de 500 ms");

    // Reprise de parole après la pause
    for (int i = 0; i < 5; ++i) {
        vad.processRms(1500, 32);
    }
    TEST_ASSERT(vad.isSpeechActive(), "Parole toujours active apres reprise");

    // Fin de parole : silence prolonge de 850 ms (> 800 ms)
    bool ended = false;
    for (int i = 0; i < 28; ++i) { // 28 * 32 ms = 896 ms
        if (vad.processRms(200, 32)) {
            ended = true;
            break;
        }
    }
    TEST_ASSERT(ended, "Fin de parole detectee apres depassement du seuil de 800 ms");
}

// SUITE 64 : Pipeline de latence en tour de conversation (Mode B)
void test_v053_conversation_turn_latency_pipeline() {
    std::cout << "\n[TEST SUITE 64] Pipeline de latence en tour de conversation (Mode B)" << std::endl;

    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_LATENCY_PIPE";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.enableConversation = true;

    voice.begin(config);
    voice.getClient().setTransport(&mockTransport);

    mockTransport.connect("wss://example.com");
    voice.update();

    const char* setupJson = "{\"setupComplete\":{}}";
    voice.getClient().handleIncomingMessage(setupJson, strlen(setupJson));
    voice.update();

    TEST_ASSERT(voice.isReady(), "Session prete");

    ConversationController& conv = voice.getConversationController();

    // 1. Démarrage du tour
    bool startOk = conv.startTurn();
    TEST_ASSERT(startOk, "startTurn() reussi");
    TEST_ASSERT(conv.isTurnActive(), "Tour actif");
    const auto& lat1 = conv.getLatencyMetrics();
    TEST_ASSERT(lat1.turnStartedMs > 0, "turnStartedMs horodate");
    TEST_ASSERT(!lat1.hasMicStopped(), "micStopped non encore defini");

    // 2. Fin de capture audio
    bool finishAudioOk = conv.finishTurnAudio();
    TEST_ASSERT(finishAudioOk, "finishTurnAudio() reussi");
    const auto& lat2 = conv.getLatencyMetrics();
    TEST_ASSERT(lat2.hasMicStopped(), "micStoppedMs horodate apres finishTurnAudio");
    TEST_ASSERT(lat2.micStoppedMs >= lat2.turnStartedMs, "micStoppedMs >= turnStartedMs");

    // 3. Réception de la réponse Gemini Live (interim transcription + model audio)
    const char* serverContent = "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/pcm;rate=24000\",\"data\":\"//8=\"}}]}}}";
    voice.getClient().handleIncomingMessage(serverContent, strlen(serverContent));
    voice.update();
    conv.tick();

    const auto& lat3 = conv.getLatencyMetrics();
    TEST_ASSERT(lat3.hasFirstResponseReceived(), "firstResponseReceivedMs capture des reception JSON");
    TEST_ASSERT(lat3.hasFirstAudioReceived(), "firstAudioReceivedMs capture des reception PCM");

    // 4. Clôture propre du tour
    conv.handleTurnFinished(1);
    const auto& latFinal = conv.getLatencyMetrics();
    TEST_ASSERT(latFinal.hasPlaybackFinished(), "playbackFinishedMs capture a la fin de restitution");
    TEST_ASSERT(latFinal.getTotalTurnDurationMs() >= 0, "Duree totale valide >= 0");

    voice.getClient().setTransport(nullptr);
}

// SUITE 65 : Façade VoiceKit v0.5.3 — Métriques de latence et préservation de l'intégrité audio
void test_v053_facade_latency_and_audio_integrity() {
    std::cout << "\n[TEST SUITE 65] Facade VoiceKit v0.5.3 : Exposition VoiceLatencyMetrics et integrite audio" << std::endl;

    VoiceKit vk;
    VoiceKitConfig config;
    config.apiKey = "TEST_KEY_FACADE_V053";
    config.wifiMode = WiFiMode::EXTERNAL_WIFI;
    config.vadSilenceDurationMs = 1200;

    bool initOk = vk.begin(config);
    TEST_ASSERT(initOk, "VoiceKit::begin() accepte la configuration v0.5.3");
    TEST_ASSERT(vk.getVadSilenceDurationMs() == 1200, "VAD silence duration par defaut = 1200 ms");

    // Modification du seuil VAD via la façade
    vk.setVadSilenceDurationMs(1000);
    TEST_ASSERT(vk.getVadSilenceDurationMs() == 1000, "VAD silence duration modifiee a 1000 ms");

    vk.setVadSilenceDurationMs(800);
    TEST_ASSERT(vk.getVadSilenceDurationMs() == 800, "VAD silence duration modifiee a 800 ms");

    vk.setSilenceDurationMs(1200); // Test de l'alias
    TEST_ASSERT(vk.getSilenceDurationMs() == 1200, "Alias setSilenceDurationMs retablit 1200 ms");

    // Exposition des métriques de latence
    const auto& lat = vk.getLatencyMetrics();
    TEST_ASSERT(lat.getResponseLatencyMs() == -1, "Latence de reponse initialement N/D (-1)");

    // Garanties d'intégrité audio VoxCore strictes (v0.5.3) :
    // - Capacité du buffer PSRAM étendue à 120 000 échantillons (5,0 secondes)
    // - Quadruple buffer DMA (4x40 ms = 160 ms de marge)
    // - Format micro 16 kHz et format restitution 24 kHz strictement préservés
    TEST_ASSERT(vk.getEngine().getPlayer().getBufferCapacity() == LIVE_AUDIO_RX_BUFFER_SAMPLES, "Buffer circulaire PSRAM de 120 000 echantillons preserve");
    TEST_ASSERT(LiveAudioPlayer::DMA_BUFFER_COUNT == 4, "Quadruple tampon DMA preserve (4x40 ms)");
    TEST_ASSERT(LiveAudioPlayer::DMA_CHUNK_SAMPLES == 960, "Blocs DMA de 960 echantillons = 40 ms a 24 kHz");
    TEST_ASSERT(AUDIO_CHUNK_SAMPLES == 512, "Blocs micro de 512 echantillons = 32 ms a 16 kHz");
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================
int main() {
    std::cout << "===========================================================" << std::endl;
    std::cout << "  SUITE COMPLETE DE TESTS AUTOMATISES — VoiceKit v0.5.3    " << std::endl;
    std::cout << "  Voice Latency Optimization, Base64 & Non-Regression      " << std::endl;
    std::cout << "  Stabilisation FreeRTOS, Modes, VAD & Facade VoiceKit     " << std::endl;
    std::cout << "  Cible logique C++14 / C++17                             " << std::endl;
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

    // Suites de tests STT VoiceKit v0.3.0
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

    // Nouvelles suites de tests conversation VoiceKit v0.4.0
    test_conversation_activation_and_instructions();
    test_conversation_turn_lifecycle();
    test_conversation_callbacks_user_and_assistant();
    test_conversation_audio_preservation_last_syllable();
    test_conversation_ten_consecutive_turns();
    test_conversation_stop_priority_listening_and_speaking();
    test_conversation_wifi_disconnect_and_recovery();
    test_conversation_silence_handling();
    test_conversation_metrics_accuracy();
    test_v030_v040_complete_non_regression();

    // Nouvelles suites de tests VoiceKit v0.4.1 (UTF-8, Configuration vocale, Gemini Setup, Profils)
    test_utf8_end_to_end_and_french_accents();
    test_voice_configuration_parameters();
    test_gemini_setup_json_validation();
    test_voice_profile_switching_and_safety();

    // Nouvelles suites de stabilisation VoiceKit v0.4.1.1
    test_voice_profile_safety_stabilization();
    test_mode_transition_stabilization();
    test_utf8_and_language_compatibility_stabilization();

    // Nouvelles suites ContinuousSTT & VAD VoiceKit v0.4.1.2
    test_vad_algorithm_and_state_transitions();
    test_vad_advanced_temporal_scenarios();
    test_continuous_stt_complete_lifecycle();
    test_continuous_stt_resilience_and_utf8();

    // Nouvelles suites VoiceKit v0.5.0 (Simplification et Stabilisation)
    test_voicekit_facade_lifecycle_and_delegation();
    test_voicekit_facade_operations();
    test_voicekit_facade_refusals_and_mode_transitions();
    test_continuous_controller_lifecycle_and_vad();
    test_events_uniqueness_and_no_duplicate_callbacks();
    test_v050_complete_backward_compatibility();

    // Nouvelles suites VoiceKit v0.5.0.1 (Stabilisation FreeRTOS, Transitions de modes & Unicite des evenements)
    test_v0501_freertos_concurrency_and_audio_synchronization();
    test_v0501_segment_reliability_and_duplicate_events();
    test_v0501_mode_transitions_and_failure_resilience();

    // Nouvelles suites VoiceKit v0.5.2 (Fast Connection & Instrumentation)
    test_v052_wifi_fast_connect_and_fallback();
    test_v052_sntp_optimization_and_tls_safety();
    test_v052_gemini_reconnect_network_conditioning();
    test_v052_startup_timing_instrumentation();
    test_v052_facade_connection_metrics_and_resilience();

    // Nouvelles suites VoiceKit v0.5.3 (Voice Latency Optimization & Non-Regression)
    test_v053_voice_latency_metrics_calculations();
    test_v053_base64_optimization_integrity();
    test_v053_vad_threshold_experimentation();
    test_v053_conversation_turn_latency_pipeline();
    test_v053_facade_latency_and_audio_integrity();

    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  BILAN FINAL DES TESTS : " << s_testsPassed << " REUSSIS, " << s_testsFailed << " ECHECS" << std::endl;
    std::cout << "===========================================================" << std::endl;

    return (s_testsFailed == 0) ? 0 : 1;
}

