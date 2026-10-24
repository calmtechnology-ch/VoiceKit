#include "native_mock/Arduino.h"
#include "native_mock/WiFi.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>

#include "VoxCoreVoice.h"
#include "Utf8Utils.h"
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
// MAIN TEST RUNNER
// =============================================================================
int main() {
    std::cout << "===========================================================" << std::endl;
    std::cout << "  SUITE COMPLETE DE TESTS AUTOMATISES — VoiceKit v0.4.1.1  " << std::endl;
    std::cout << "  Validation Stabilisation Profils + Transitions + Langues " << std::endl;
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

    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  BILAN FINAL DES TESTS : " << s_testsPassed << " REUSSIS, " << s_testsFailed << " ECHECS" << std::endl;
    std::cout << "===========================================================" << std::endl;

    return (s_testsFailed == 0) ? 0 : 1;
}

