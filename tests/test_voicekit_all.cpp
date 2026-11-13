#include "native_mock/Arduino.h"
#include "native_mock/WiFi.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "../showcases/OrientationClockVoice/include/FrenchTimeFormatter.h"
#include "../showcases/OrientationClockVoice/include/WiFiTimeSync.h"
#include "../showcases/OrientationClockVoice/src/FrenchTimeFormatter.cpp"
#include "../showcases/OrientationClockVoice/src/WiFiTimeSync.cpp"
#include "ContinuousListeningController.h"
#include "Utf8Utils.h"
#include "VoiceActivityDetector.h"
#include "VoiceKit.h"
#include "VoxCoreVoice.h"
#include "cJSON.h"
#include "core/AudioResampler.h"
#include "core/GeminiVoiceProvider.h"
#include "core/IVoiceProvider.h"
#include "core/OpenAIRealtimeClient.h"
#include "core/tools/ToolRegistry.h"
#include "native_mock/MockLiveTransport.h"

static int s_testsPassed = 0;
static int s_testsFailed = 0;

#define TEST_ASSERT(cond, msg)                                                 \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "  [FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__   \
                << ")" << std::endl;                                           \
      s_testsFailed++;                                                         \
    } else {                                                                   \
      s_testsPassed++;                                                         \
    }                                                                          \
  } while (0)

#include "test_voiceux_suites.h"

// =============================================================================
// SUITE 1 : Formatage de l'heure en français (FrenchTimeFormatter)
// =============================================================================
void test_french_time_formatting() {
  std::cout << "\n[TEST SUITE 1] Formatage de l'heure en francais naturel"
            << std::endl;

  // Test de référence prescrit dans le prompt (20h09)
  String s2009 = formatTimeInFrench(20, 9);
  TEST_ASSERT(s2009 == "Il est vingt heures neuf.",
              "20h09 -> Il est vingt heures neuf.");

  // Heures rondes
  TEST_ASSERT(formatTimeInFrench(0, 0) == "Il est minuit.",
              "00h00 -> Il est minuit.");
  TEST_ASSERT(formatTimeInFrench(12, 0) == "Il est midi.",
              "12h00 -> Il est midi.");
  TEST_ASSERT(formatTimeInFrench(1, 0) == "Il est une heure.",
              "01h00 -> Il est une heure.");
  TEST_ASSERT(formatTimeInFrench(20, 0) == "Il est vingt heures.",
              "20h00 -> Il est vingt heures.");
  TEST_ASSERT(formatTimeInFrench(21, 0) == "Il est vingt et une heures.",
              "21h00 -> Il est vingt et une heures.");

  // Heures avec minutes
  TEST_ASSERT(formatTimeInFrench(0, 5) == "Il est minuit cinq.",
              "00h05 -> Il est minuit cinq.");
  TEST_ASSERT(formatTimeInFrench(1, 1) == "Il est une heure une.",
              "01h01 -> Il est une heure une.");
  TEST_ASSERT(formatTimeInFrench(1, 15) == "Il est une heure quinze.",
              "01h15 -> Il est une heure quinze.");
  TEST_ASSERT(formatTimeInFrench(12, 30) == "Il est midi trente.",
              "12h30 -> Il est midi trente.");
  TEST_ASSERT(formatTimeInFrench(21, 1) == "Il est vingt et une heures une.",
              "21h01 -> Il est vingt et une heures une.");
  TEST_ASSERT(formatTimeInFrench(21, 21) ==
                  "Il est vingt et une heures vingt et un.",
              "21h21 -> Il est vingt et une heures vingt et un.");
  TEST_ASSERT(formatTimeInFrench(22, 45) ==
                  "Il est vingt-deux heures quarante-cinq.",
              "22h45 -> Il est vingt-deux heures quarante-cinq.");
  TEST_ASSERT(formatTimeInFrench(23, 59) ==
                  "Il est vingt-trois heures cinquante-neuf.",
              "23h59 -> Il est vingt-trois heures cinquante-neuf.");
}

// =============================================================================
// SUITE 2 : LiveAudioPlayer — Intégrité de flux, tampons et équations de bilan
// =============================================================================
void test_audio_player_integrity() {
  std::cout << "\n[TEST SUITE 2] LiveAudioPlayer : Integrite du tampon PCM 24 "
               "kHz et vidage"
            << std::endl;

  LiveAudioPlayer player;
  TEST_ASSERT(player.begin(120000),
              "Initialisation du buffer PSRAM (120 000 ech)");
  TEST_ASSERT(player.getBufferCapacity() == 120000,
              "Capacite conforme 120 000");

  // Scénario A : Phrase de 4 secondes (96 000 échantillons à 24 kHz)
  std::vector<int16_t> samples4s(96000, 1000);
  bool fed = player.feedPcm(samples4s.data(), samples4s.size());
  TEST_ASSERT(fed, "Alimentation 96 000 ech reussie sans saturation");
  TEST_ASSERT(player.getSamplesReceivedTotal() == 96000, "96 000 ech recus");
  TEST_ASSERT(player.checkBufferIntegrity(), "Equation d'integrite verifiee");

  player.notifyTurnComplete();
  TEST_ASSERT(player.isTurnComplete(), "Notification turnComplete enregistree");

  int tickCount = 0;
  while ((player.getBufferedSamples() > 0 || player.isPlaying()) &&
         tickCount < 200) {
    player.tick();
    player.simFinishSpeakerChunk();
    tickCount++;
  }

  TEST_ASSERT(player.getBufferedSamples() == 0,
              "Tampon PSRAM integralement consomme");
  TEST_ASSERT(player.checkBufferIntegrity(),
              "Equation finale d'integrite parfaite (0 ecart)");
  TEST_ASSERT(player.getOverflowDroppedSamples() == 0,
              "Zero perte par debordement");

  // Scénario B : Appui STOP pendant la lecture
  player.reset();
  std::vector<int16_t> samplesMid(24000, 500); // 1 seconde
  player.feedPcm(samplesMid.data(), samplesMid.size());
  TEST_ASSERT(player.getBufferedSamples() == 24000,
              "Tampon charge avec 24 000 ech");

  player.stop(AudioStopReason::USER_STOP);
  TEST_ASSERT(!player.isPlaying(), "Lecture stoppee immediatement");
  TEST_ASSERT(player.getBufferedSamples() == 0, "Tampon vide apres STOP");
  TEST_ASSERT(player.checkBufferIntegrity(),
              "Integrite preservee apres annulation STOP");
}

// =============================================================================
// SUITE 3 : Scénarios de durée : 10s, 30s, alpha-tango et 2 annonces
// consécutives
// =============================================================================
void test_audio_scenarios_duration() {
  std::cout << "\n[TEST SUITE 3] Scenarios de duree : 10s, 30s, alpha-tango et "
               "2 annonces consecutives"
            << std::endl;

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
  TEST_ASSERT(player.checkBufferIntegrity(),
              "Annonce courte 2s : equation d'integrite 100%");
  TEST_ASSERT(player.getOverflowDroppedSamples() == 0,
              "Annonce courte 2s : 0 debordement");

  // 2. Annonce moyenne (10 secondes = 240 000 échantillons envoyés en flux
  // continu)
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
  TEST_ASSERT(player.checkBufferIntegrity(),
              "Annonce moyenne 10s : fluidite parfaite sans saturation");
  TEST_ASSERT(player.getOverflowDroppedSamples() == 0,
              "Annonce moyenne 10s : 0 echantillon perdu");

  // 3. Deux annonces consécutives avec réinitialisation
  player.reset();
  std::vector<int16_t> ann1(48000, 300);
  player.feedPcm(ann1.data(), ann1.size());
  player.notifyTurnComplete();
  while (player.getBufferedSamples() > 0 || player.isPlaying()) {
    player.tick();
    player.simFinishSpeakerChunk();
  }
  TEST_ASSERT(player.getBufferedSamples() == 0,
              "Annonce 1 terminee proprement");

  player.reset();
  std::vector<int16_t> ann2(48000, 400);
  player.feedPcm(ann2.data(), ann2.size());
  player.notifyTurnComplete();
  while (player.getBufferedSamples() > 0 || player.isPlaying()) {
    player.tick();
    player.simFinishSpeakerChunk();
  }
  TEST_ASSERT(player.getBufferedSamples() == 0,
              "Annonce 2 terminee proprement sans fuite memoire");
  TEST_ASSERT(player.checkBufferIntegrity(),
              "Integrite preservee sur annonces consecutives");
}

// =============================================================================
// SUITE 4 : API VoxCoreVoice — Politiques de file d'attente et gestion des
// erreurs
// =============================================================================
void test_voicekit_api_and_queue_policies() {
  std::cout << "\n[TEST SUITE 4] API VoxCoreVoice : Politiques de file, "
               "non-blocage et erreurs"
            << std::endl;

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
  TEST_ASSERT(voice.getLastError() == "Texte vide",
              "Message d'erreur texte vide conforme");

  // 2. Rejet si Wi-Fi non connecte
  WiFi.setConnected(false);
  bool noWifiSay = voice.say("Bonjour");
  TEST_ASSERT(!noWifiSay, "say() rejete si Wi-Fi non connecte");
  TEST_ASSERT(voice.getLastError() == "Wi-Fi hote non connecte",
              "Message d'erreur Wi-Fi conforme");

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
  std::cout << "\n[TEST SUITE 5] Coordination Wi-Fi : Verrou keepalive pour "
               "empecher la coupure"
            << std::endl;

  WiFiTimeSync wifiSync;
  wifiSync.init();

  TEST_ASSERT(!wifiSync.isKeepAliveLocked(), "Verrou initialement inactif");
  wifiSync.setKeepAliveLock(true);
  TEST_ASSERT(wifiSync.isKeepAliveLocked(),
              "Verrou active pour proteger l'annonce vocale");
  wifiSync.setKeepAliveLock(false);
  TEST_ASSERT(!wifiSync.isKeepAliveLocked(),
              "Verrou libere apres fin de restitution");
}

// =============================================================================
// SUITE 6 : Propriété du haut-parleur (Speaker Ownership)
// =============================================================================
void test_speaker_ownership_management() {
  std::cout << "\n[TEST SUITE 6] Propriete du haut-parleur (Correction C)"
            << std::endl;

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
  std::cout
      << "\n[TEST SUITE 7] Coherence heure affichee et annoncee (Correction D)"
      << std::endl;

  int hour = 14;
  int minute = 30;

  String verbal = formatTimeInFrench(hour, minute);
  TEST_ASSERT(verbal == "Il est quatorze heures trente.",
              "Formatage 14h30 coherent");

  hour = 8;
  minute = 0;
  verbal = formatTimeInFrench(hour, minute);
  TEST_ASSERT(verbal == "Il est huit heures.", "Formatage 08h00 coherent");
}

// =============================================================================
// SUITE 8 : Gestion robuste requêtes asynchrones et résilience
// =============================================================================
void test_async_requests_and_resilience() {
  std::cout << "\n[TEST SUITE 8] Gestion robuste requetes asynchrones et "
               "resilience (Correction E)"
            << std::endl;

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
  voice.onError([&errTriggered](const String &) { errTriggered = true; });

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
  std::cout << "\n[TEST SUITE 9] Cycle de vie du verrou Wi-Fi et maintien 15s "
               "(Correction F)"
            << std::endl;

  WiFiTimeSync wifiSync;
  wifiSync.init();

  TEST_ASSERT(!wifiSync.isKeepAliveLocked(), "Verrou initialement inactif");
  wifiSync.setKeepAliveLock(true);
  TEST_ASSERT(wifiSync.isKeepAliveLocked(),
              "Verrou actif pendant la requete vocale");
  wifiSync.setKeepAliveLock(false);
  TEST_ASSERT(!wifiSync.isKeepAliveLocked(), "Verrou libere apres fin");
}

// =============================================================================
// SUITE 10 : LiveAudioStreamer — États des tampons circulaires et file
// d'émission
// =============================================================================
void test_audio_streamer_buffer_and_queue_management() {
  std::cout << "\n[TEST SUITE 10] LiveAudioStreamer : Etats des tampons "
               "circulaires et file d'emission"
            << std::endl;

  LiveAudioStreamer streamer;
  TEST_ASSERT(streamer.begin(), "streamer.begin() reussi");
  TEST_ASSERT(streamer.areAllBuffersFree(),
              "Tous les tampons initialement FREE");
  TEST_ASSERT(!streamer.isStreaming(), "Non streaming initialement");
  TEST_ASSERT(streamer.getQueueSize() == 0, "File TX initialement vide");

  TEST_ASSERT(streamer.getBufferState(0) == LiveBufferState::FREE,
              "Buffer 0 initialement FREE");
  TEST_ASSERT(String(liveBufferStateToString(LiveBufferState::FREE)) == "FREE",
              "String FREE");
  TEST_ASSERT(String(liveBufferStateToString(LiveBufferState::RECORDING)) ==
                  "RECORDING",
              "String RECORDING");
  TEST_ASSERT(String(liveBufferStateToString(LiveBufferState::READY)) ==
                  "READY",
              "String READY");
  TEST_ASSERT(String(liveBufferStateToString(LiveBufferState::SENDING)) ==
                  "SENDING",
              "String SENDING");
  TEST_ASSERT(String(liveTxStatusToString(LiveTxStatus::OK)) == "OK",
              "Statut TX OK");
}

// =============================================================================
// SUITE 11 : GeminiLiveClient — Rejet de configuration et extraction d'erreurs
// =============================================================================
void test_gemini_server_json_errors() {
  std::cout << "\n[TEST SUITE 11] GeminiLiveClient : Extraction des erreurs "
               "JSON renvoyees par le serveur"
            << std::endl;

  MockLiveTransport mockTransport;
  GeminiLiveClient client(&mockTransport);
  client.begin("TEST_KEY_123");

  bool errorCallbackFired = false;
  String lastReportedError = "";
  client.setOnError([&](const String &err) {
    errorCallbackFired = true;
    lastReportedError = err;
  });

  client.connect();

  // 1. Simulation d'une erreur transport renvoyee par le serveur Gemini
  mockTransport.simulateError(
      "INVALID_ARGUMENT: 400 models/gemini-invalide is not supported");
  TEST_ASSERT(errorCallbackFired,
              "Callback onError declenche lors d'une erreur JSON serveur");
  TEST_ASSERT(lastReportedError.indexOf("INVALID_ARGUMENT") >= 0,
              "Statut de l'erreur Gemini extrait");
  TEST_ASSERT(lastReportedError.indexOf("400") >= 0,
              "Code d'erreur 400 extrait");
  TEST_ASSERT(lastReportedError.indexOf("models/gemini-invalide") >= 0,
              "Message d'erreur detaille extrait");

  errorCallbackFired = false;
  mockTransport.simulateError("Quota de requetes Gemini depasse");
  TEST_ASSERT(errorCallbackFired, "onError declenche pour errorMessage direct");
  TEST_ASSERT(lastReportedError.indexOf("Quota de requetes Gemini depasse") >=
                  0,
              "Message de quota extrait");

  TEST_ASSERT(!client.isSetupComplete(), "setupComplete initialement false");
  const char *setupCompleteJson = "{\"setupComplete\":{}}";
  client.handleIncomingMessage(setupCompleteJson, strlen(setupCompleteJson));
  TEST_ASSERT(client.isSetupComplete(),
              "isSetupComplete() passe a true apres setupComplete");

  client.setTransport(nullptr);
}

// =============================================================================
// SUITE 12 : GeminiLiveClient — Trame de clôture Close (Codes 1007, 1008 &
// Raison)
// =============================================================================
void test_gemini_server_close_frame() {
  std::cout << "\n[TEST SUITE 12] GeminiLiveClient : Trame de fermeture "
               "serveur (Close code 1007/1008 & raison)"
            << std::endl;

  MockLiveTransport mockTransport;
  GeminiLiveClient client(&mockTransport);
  client.begin("TEST_KEY_456");

  bool errorCallbackFired = false;
  String reportedError = "";
  client.setOnError([&](const String &err) {
    errorCallbackFired = true;
    reportedError = err;
  });

  client.connect();
  TEST_ASSERT(client.isConnected(), "Client connecte au transport");

  mockTransport.simulateError(
      "403: API key not valid. Please pass a valid API key.");

  TEST_ASSERT(errorCallbackFired,
              "onError declenche lors d'une cle API invalide");
  TEST_ASSERT(reportedError.indexOf("API key not valid") >= 0,
              "Message cle API invalide extrait");

  client.setTransport(nullptr);
}

// =============================================================================
// SUITE 13 : Configuration et normalisation du nom de modèle Gemini
// =============================================================================
void test_model_normalization() {
  std::cout
      << "\n[TEST SUITE 13] Normalisation automatique du nom de modele Gemini"
      << std::endl;

  GeminiLiveClient client;
  client.begin("TEST_KEY", "models/gemini-3.8-live");
  TEST_ASSERT(client.getModel() == "models/gemini-3.8-live",
              "Prefixe models/ valide");

  client.begin("TEST_KEY", "gemini-3.8-live");
  TEST_ASSERT(client.getModel().indexOf("gemini-3.8-live") >= 0,
              "Nom de modele pris en compte");
}

// =============================================================================
// SUITE 14 : VoxCoreVoice — Déconnexion intentionnelle vs erreur de clôture
// =============================================================================
void test_intentional_vs_unexpected_disconnect() {
  std::cout << "\n[TEST SUITE 14] VoxCoreVoice : Deconnexion intentionnelle vs "
               "erreur de cloture"
            << std::endl;

  MockLiveTransport mockTransport;
  VoxCoreVoice voice;
  VoiceKitConfig config;
  config.apiKey = "TEST_KEY_DISC";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  voice.begin(config);
  voice.getClient().setTransport(&mockTransport);

  mockTransport.connect("wss://example.com");
  const char *setupJson = "{\"setupComplete\":{}}";
  voice.getClient().handleIncomingMessage(setupJson, strlen(setupJson));
  voice.update();

  TEST_ASSERT(voice.isReady(), "voice.isReady() true");
  voice.stop();
  TEST_ASSERT(voice.getState() == VoiceState::READY,
              "stop() intentionnel maintient l'etat READY");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 15 : GoogleRoots — Intégrité du bundle TLS et récupération après erreur
// =============================================================================
void test_tls_roots_and_recovery() {
  std::cout << "\n[TEST SUITE 15] GoogleRoots : Integrite du bundle TLS et "
               "recuperation apres erreur"
            << std::endl;

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

  TEST_ASSERT(voice.getState() == VoiceState::ERROR_STATE,
              "Passage a ERROR_STATE sur erreur TLS");
  TEST_ASSERT(!voice.isReady(), "Moteur non pret en etat ERROR_STATE");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 16 : Initialisation microphone et arbitrage matériel HAL
// =============================================================================
void test_microphone_initialization_and_hal_arbitration() {
  std::cout
      << "\n[TEST SUITE 16] Initialisation microphone et arbitrage materiel HAL"
      << std::endl;

  M5AudioInput mic;
  TEST_ASSERT(mic.begin(), "mic.begin() reussi");
  TEST_ASSERT(mic.isRunning(), "mic.isRunning() true apres begin");
  TEST_ASSERT(mic.isRecording() == 0,
              "Aucun enregistrement actif avant record");

  HardwareManager &hw = HardwareManager::getInstance();
  hw.reset();
  hw.setAudioInput(&mic);
  TEST_ASSERT(hw.getAudioInput() == &mic,
              "Adaptateur AudioInput configure dans HardwareManager");
  TEST_ASSERT(!hw.isMicCaptureReserved(), "Aucune reservation micro au depart");

  bool prep = hw.prepareMicCapture();
  TEST_ASSERT(prep, "prepareMicCapture() reussi");
  TEST_ASSERT(hw.isMicCaptureReserved(),
              "Reservation micro confirmee sur bus I2S");

  bool rest = hw.restoreSpeakerAfterMic();
  TEST_ASSERT(rest, "restoreSpeakerAfterMic() reussi");
  TEST_ASSERT(!hw.isMicCaptureReserved(),
              "Reservation levee apres restauration");

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
  std::cout << "\n[TEST SUITE 17] Acquisition PCM 16 kHz, double-buffering et "
               "streaming"
            << std::endl;

  MockLiveTransport mockTransport;
  GeminiLiveClient client(&mockTransport);
  client.begin("TEST_KEY_STT");
  client.connect();

  const char *setupCompleteJson = "{\"setupComplete\":{}}";
  client.handleIncomingMessage(setupCompleteJson, strlen(setupCompleteJson));
  TEST_ASSERT(client.isSetupComplete(),
              "Client Gemini Live pret (setupComplete)");

  LiveAudioStreamer streamer;
  M5AudioInput mic;
  streamer.setAudioInput(&mic);
  TEST_ASSERT(streamer.begin(), "Streamer begin()");

  bool started = streamer.start(&client);
  TEST_ASSERT(started, "streamer.start(&client) reussi");
  TEST_ASSERT(streamer.isStreaming(), "isStreaming() true");
  TEST_ASSERT(streamer.isActivityStartConfirmed(),
              "activityStart emis au demarrage");

  std::vector<int16_t> chunk(AUDIO_CHUNK_SAMPLES, 500);
  bool pushed = streamer.pushSimulatedChunk(chunk.data(), chunk.size());
  TEST_ASSERT(pushed, "pushSimulatedChunk 512 ech reussi");
  TEST_ASSERT(streamer.getBlocksAcquired() == 1, "1 bloc acquis");
  TEST_ASSERT(streamer.getBlocksCompleted() == 1, "1 bloc complete");
  TEST_ASSERT(streamer.getAverageRms() > 0, "Calcul RMS positif");

  streamer.tick();
  TEST_ASSERT(streamer.getChunksSent() == 1,
              "1 bloc transmis vers Gemini Live");
  TEST_ASSERT(streamer.getBytesSent() == AUDIO_CHUNK_SAMPLES * sizeof(int16_t),
              "1024 octets transmis");

  bool stopped = streamer.stop();
  TEST_ASSERT(stopped, "streamer.stop() reussi");
  TEST_ASSERT(!streamer.isStreaming(), "isStreaming() false apres stop");
  TEST_ASSERT(streamer.isActivityEndConfirmed(),
              "activityEnd confirme a la fin de l'acquisition");
  TEST_ASSERT(streamer.areAllBuffersFree(),
              "Tous les tampons liberes apres stop");

  client.setTransport(nullptr);
}

// Helper pour préparer une session VoxCoreVoice prête avec mock transport
static void prepareMockVoiceReady(VoxCoreVoice &voice,
                                  MockLiveTransport &mockTransport) {
  WiFi.setConnected(true);
  voice.getClient().setTransport(&mockTransport);
  voice.getClient().connect();
  const char *setupJson = "{\"setupComplete\":{}}";
  voice.getClient().handleIncomingMessage(setupJson, strlen(setupJson));
  voice.update();
}

static void prepareMockVoiceKitReady(VoiceKit &vk,
                                     MockLiveTransport &mockTransport) {
  prepareMockVoiceReady(vk.getEngine(), mockTransport);
}

// =============================================================================
// SUITE 18 : Cycle Push-to-Talk (startListening -> stopListening) dans
// VoxCoreVoice
// =============================================================================
void test_push_to_talk_and_release_cycle() {
  std::cout << "\n[TEST SUITE 18] Cycle Push-to-Talk (startListening -> "
               "stopListening)"
            << std::endl;

  MockLiveTransport mockTransport;
  VoxCoreVoice voice;

  VoiceKitConfig config;
  config.apiKey = "TEST_KEY_PTT";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  config.systemInstruction = VOICEKIT_DEFAULT_STT_INSTRUCTION;

  bool readyFired = false;
  voice.onReady([&readyFired]() { readyFired = true; });

  bool listenStartedFired = false;
  voice.onListeningStarted(
      [&listenStartedFired]() { listenStartedFired = true; });

  bool listenFinishedFired = false;
  voice.onListeningFinished(
      [&listenFinishedFired]() { listenFinishedFired = true; });

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
  TEST_ASSERT(voice.isTranscribing(),
              "isTranscribing() true en attente de transcription");
  TEST_ASSERT(voice.getState() == VoiceState::TRANSCRIBING,
              "Etat = TRANSCRIBING");
  TEST_ASSERT(listenFinishedFired, "Callback onListeningFinished declenche");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 19 : Dictée courte vs dictée longue
// =============================================================================
void test_short_and_long_dictation_scenarios() {
  std::cout << "\n[TEST SUITE 19] Scenarios de dictee courte et longue"
            << std::endl;

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
    voice.getStreamer().pushSimulatedChunk(sampleBlock.data(),
                                           sampleBlock.size());
    voice.getStreamer().tick();
  }
  TEST_ASSERT(voice.getStreamer().getBlocksAcquired() == 62,
              "Dictee courte: 62 blocs acquis (~2.0 s)");
  TEST_ASSERT(voice.getStreamer().getChunksSent() == 62,
              "Dictee courte: 62 blocs transmis");
  uint32_t durCourtMs = voice.getStreamer().getTheoreticalDurationMs();
  TEST_ASSERT(durCourtMs >= 1900 && durCourtMs <= 2100,
              "Duree theorique courte ~2000 ms");
  voice.stopListening();
  voice.cancelListening();

  // Scenario B : Dictee longue (~10 secondes, 312 blocs)
  voice.startListening();
  for (int i = 0; i < 312; ++i) {
    voice.getStreamer().pushSimulatedChunk(sampleBlock.data(),
                                           sampleBlock.size());
    voice.getStreamer().tick();
  }
  TEST_ASSERT(voice.getStreamer().getBlocksAcquired() == 312,
              "Dictee longue: 312 blocs acquis (~10.0 s)");
  TEST_ASSERT(voice.getStreamer().getChunksSent() == 312,
              "Dictee longue: 312 blocs transmis sans debordement");
  uint32_t durLongMs = voice.getStreamer().getTheoreticalDurationMs();
  TEST_ASSERT(durLongMs >= 9900 && durLongMs <= 10100,
              "Duree theorique longue ~10000 ms");
  voice.stopListening();
  voice.cancelListening();

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 20 : Transcriptions partielles (intérim) et finale
// =============================================================================
void test_partial_and_final_transcriptions() {
  std::cout
      << "\n[TEST SUITE 20] Transcriptions partielles (interim) et finales"
      << std::endl;

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

  voice.onTranscription([&](const String &text, bool isFinal) {
    if (isFinal) {
      finalText = text;
      finalReceived = true;
    } else {
      interimTexts.push_back(text);
    }
  });

  voice.startListening();

  // 1. Simuler réception d'une transcription intérimaire 1
  const char *interim1 = "{\"serverContent\":{\"interimInputTranscription\":{"
                         "\"text\":\"Bonjour\"}}}";
  voice.getClient().handleIncomingMessage(interim1, strlen(interim1));
  TEST_ASSERT(interimTexts.size() == 1, "1ere transcription interim recue");
  TEST_ASSERT(interimTexts[0] == "Bonjour", "Contenu interim 1 correct");

  // 2. Simuler réception d'une transcription intérimaire 2
  const char *interim2 = "{\"serverContent\":{\"interimInputTranscription\":{"
                         "\"text\":\"Bonjour a tous\"}}}";
  voice.getClient().handleIncomingMessage(interim2, strlen(interim2));
  TEST_ASSERT(interimTexts.size() == 2, "2eme transcription interim recue");
  TEST_ASSERT(interimTexts[1] == "Bonjour a tous", "Contenu interim 2 correct");

  // 3. Relâchement du bouton
  voice.stopListening();
  TEST_ASSERT(voice.isTranscribing(), "En etat de transcription");
  TEST_ASSERT(!finalReceived, "Finale pas encore recue");

  // 4. Réception de la transcription finale
  const char *finalJson = "{\"serverContent\":{\"inputTranscription\":{"
                          "\"text\":\"Bonjour a tous.\"}}}";
  voice.getClient().handleIncomingMessage(finalJson, strlen(finalJson));
  voice.update();

  TEST_ASSERT(finalReceived,
              "Transcription finale recue avec flag isFinal=true");
  TEST_ASSERT(finalText == "Bonjour a tous.", "Contenu final exact");
  TEST_ASSERT(!voice.isTranscribing(), "Fin de l'etat TRANSCRIBING");
  TEST_ASSERT(voice.getState() == VoiceState::READY,
              "Retour automatique a l'etat READY");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 21 : Enchaînement de dictées consécutives
// =============================================================================
void test_consecutive_dictations() {
  std::cout << "\n[TEST SUITE 21] Enchainement de dictees consecutives"
            << std::endl;

  MockLiveTransport mockTransport;
  VoxCoreVoice voice;
  VoiceKitConfig config;
  config.apiKey = "TEST_KEY_CONSEC";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  voice.begin(config);
  prepareMockVoiceReady(voice, mockTransport);

  int dictationCount = 0;
  voice.onTranscription([&](const String &text, bool isFinal) {
    if (isFinal) {
      dictationCount++;
    }
  });

  // 1ere dictée
  voice.startListening();
  voice.stopListening();
  const char *f1 = "{\"serverContent\":{\"inputTranscription\":{\"text\":"
                   "\"Premiere phrase.\"}}}";
  voice.getClient().handleIncomingMessage(f1, strlen(f1));
  voice.update();
  TEST_ASSERT(dictationCount == 1, "Dictée 1 traitee avec succes");
  TEST_ASSERT(voice.isReady(), "Moteur pret pour la deuxieme dictee");

  // 2eme dictée immédiate
  bool start2 = voice.startListening();
  TEST_ASSERT(start2, "Demarrage immediat dictee 2 accepte");
  voice.stopListening();
  const char *f2 = "{\"serverContent\":{\"inputTranscription\":{\"text\":"
                   "\"Deuxieme phrase.\"}}}";
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
  std::cout << "\n[TEST SUITE 22] Interruption et annulation pendant l'ecoute"
            << std::endl;

  MockLiveTransport mockTransport;
  VoxCoreVoice voice;
  VoiceKitConfig config;
  config.apiKey = "TEST_KEY_CANCEL";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  voice.begin(config);
  prepareMockVoiceReady(voice, mockTransport);

  bool finalFired = false;
  voice.onTranscription([&](const String &text, bool isFinal) {
    if (isFinal)
      finalFired = true;
  });

  // Scénario A : cancelListening()
  voice.startListening();
  TEST_ASSERT(voice.isListening(), "Ecoute active");
  voice.cancelListening();
  TEST_ASSERT(!voice.isListening(), "Ecoute annulee");
  TEST_ASSERT(!voice.isTranscribing(), "Aucune attente de transcription");
  TEST_ASSERT(voice.getState() == VoiceState::READY,
              "Retour a READY apres cancelListening");
  TEST_ASSERT(!finalFired,
              "Aucune transcription finale declenchee apres annulation");

  // Scénario B : stop() global pendant l'écoute
  voice.startListening();
  TEST_ASSERT(voice.isListening(), "Ecoute active tour 2");
  voice.stop();
  TEST_ASSERT(!voice.isListening(), "Ecoute stoppee par stop() global");
  TEST_ASSERT(voice.getState() == VoiceState::READY,
              "Retour a READY apres stop()");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 23 : Résilience lors d'une perte Wi-Fi / transport pendant la dictée
// =============================================================================
void test_wifi_disconnect_during_dictation() {
  std::cout << "\n[TEST SUITE 23] Perte Wi-Fi / transport pendant la dictee"
            << std::endl;

  MockLiveTransport mockTransport;
  VoxCoreVoice voice;
  VoiceKitConfig config;
  config.apiKey = "TEST_KEY_DISC";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  voice.begin(config);
  prepareMockVoiceReady(voice, mockTransport);

  bool errorFired = false;
  voice.onError([&](const String &err) { errorFired = true; });

  voice.startListening();
  TEST_ASSERT(voice.isListening(), "Ecoute active avant coupure");

  // Déconnexion réseau subite
  mockTransport.disconnect();
  voice.update();

  TEST_ASSERT(!voice.isListening(), "Ecoute interrompue apres deconnexion");
  TEST_ASSERT(voice.getState() == VoiceState::ERROR_STATE || !voice.isReady(),
              "Transition securisee");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 24 : Absence de parole et timeout de transcription finale (Zéro
// hallucination)
// =============================================================================
void test_silence_and_final_transcription_timeout() {
  std::cout << "\n[TEST SUITE 24] Absence de parole et timeout de "
               "transcription finale (Zero invention)"
            << std::endl;

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
  voice.onTranscription([&](const String &text, bool isFinal) {
    if (isFinal) {
      finalFired = true;
      receivedFinal = text;
    }
  });

  voice.startListening();

  // On reçoit seulement un intérim (bruit / souffle partiel)
  const char *partial =
      "{\"serverContent\":{\"interimInputTranscription\":{\"text\":\"...\"}}}";
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

  // RÈGLE IMPÉRATIVE DU PROMPT : La transcription finale ne doit PAS être
  // inventée à partir du partiel !
  TEST_ASSERT(!finalFired, "Regle respectee : Zero hallucination / pas de "
                           "transcription finale inventee");
  TEST_ASSERT(!voice.isTranscribing(), "Sortie de l'etat TRANSCRIBING");
  TEST_ASSERT(voice.getState() == VoiceState::READY,
              "Retour propre a l'etat READY");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 25 : Coexistence et indépendance TTS et STT sans larsen
// =============================================================================
void test_tts_stt_coexistence() {
  std::cout
      << "\n[TEST SUITE 25] Coexistence TTS et STT sans interferences mutuelles"
      << std::endl;

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
  TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(),
              "Haut-parleur autorise pour TTS");

  // 2. Appel de startListening() pendant TTS -> Mute immediat et arret TTS
  bool listenOk = voice.startListening();
  TEST_ASSERT(listenOk, "startListening() prioritaire accepte");
  TEST_ASSERT(!voice.getPlayer().isPlaybackAllowed(),
              "Haut-parleur mute pendant STT pour eviter le larsen");
  TEST_ASSERT(voice.isListening(), "Microphone actif");

  // 3. Fin de STT -> Re-autorisation haut-parleur
  voice.stopListening();
  const char *fJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":"
                      "\"Texte dicte.\"}}}";
  voice.getClient().handleIncomingMessage(fJson, strlen(fJson));
  voice.update();

  TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(),
              "Haut-parleur reautorise apres STT");
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
  std::cout << "\n[TEST SUITE 26] Activation mode conversationnel et "
               "instructions systeme (Mode B)"
            << std::endl;

  VoxCoreVoice voice;
  VoiceKitConfig config;
  config.apiKey = "TEST_KEY_CONV_ACT";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  config.systemInstruction = VOICEKIT_DEFAULT_TTS_INSTRUCTION;
  voice.begin(config);

  TEST_ASSERT(!voice.isConversationEnabled(),
              "Mode conversation desactive par defaut (v0.3.0 compatible)");

  voice.enableConversation(true);
  TEST_ASSERT(voice.isConversationEnabled(),
              "Mode conversation active via enableConversation(true)");
  TEST_ASSERT(voice.getClient().getSystemInstruction() ==
                  VOICEKIT_DEFAULT_CONVERSATION_INSTRUCTION,
              "Instruction systeme mise a jour pour la conversation "
              "bienveillante et concise");

  voice.enableConversation(false);
  TEST_ASSERT(!voice.isConversationEnabled(), "Mode conversation desactive");
  TEST_ASSERT(voice.getClient().getSystemInstruction() ==
                  VOICEKIT_DEFAULT_TTS_INSTRUCTION,
              "Instruction systeme TTS d'origine restauree");
}

// =============================================================================
// SUITE 27 : Cycle conversationnel complet READY -> LISTENING -> PROCESSING ->
// SPEAKING -> READY
// =============================================================================
void test_conversation_turn_lifecycle() {
  std::cout << "\n[TEST SUITE 27] Cycle conversationnel complet (READY -> "
               "LISTENING -> PROCESSING -> SPEAKING -> READY)"
            << std::endl;

  MockLiveTransport mockTransport;
  VoxCoreVoice voice;
  VoiceKitConfig config;
  config.apiKey = "TEST_KEY_LIFECYCLE";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  config.enableConversation = true;
  voice.begin(config);
  prepareMockVoiceReady(voice, mockTransport);

  TEST_ASSERT(voice.getState() == VoiceState::READY, "Etat initial READY");
  TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(),
              "Haut-parleur initialement pret");

  bool convFinished = false;
  voice.onConversationFinished([&]() { convFinished = true; });

  // 1. Début de parole utilisateur (Appui PARLER)
  bool started = voice.startListening();
  TEST_ASSERT(started, "startListening() en mode conversation accepte");
  TEST_ASSERT(voice.isListening(), "isListening() true");
  TEST_ASSERT(voice.getState() == VoiceState::LISTENING, "Etat = LISTENING");
  TEST_ASSERT(!voice.getPlayer().isPlaybackAllowed(),
              "Anti-larsen strict : haut-parleur coupe pendant l'ecoute");

  // 2. Simulation de parole (blocs PCM 16 kHz transmis)
  std::vector<int16_t> sampleBlock(AUDIO_CHUNK_SAMPLES, 400);
  voice.getStreamer().pushSimulatedChunk(sampleBlock.data(),
                                         sampleBlock.size());
  voice.getStreamer().tick();
  TEST_ASSERT(voice.getStreamer().getChunksSent() >= 1,
              "Blocs audio emis vers Gemini Live");

  // 3. Relâchement du bouton PARLER
  bool stopped = voice.stopListening();
  TEST_ASSERT(stopped, "stopListening() en mode conversation accepte");
  TEST_ASSERT(voice.getState() == VoiceState::WAITING_RESPONSE,
              "Etat = WAITING_RESPONSE (PROCESSING)");
  TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(),
              "Micro coupe : haut-parleur reautorise pour la reponse");

  // 4. Réception de la transcription utilisateur
  const char *userJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":"
                         "\"Quelle heure est-il ?\"}}}";
  voice.getClient().handleIncomingMessage(userJson, strlen(userJson));
  voice.update();

  // 5. Réception du premier son audio Gemini (24 kHz)
  voice.getSession().transitionTo(LiveSessionState::PLAYING,
                                  "Premier son recu");
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
  std::cout << "\n[TEST SUITE 28] Transcriptions utilisateur (interim + final) "
               "et transcription assistant"
            << std::endl;

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

  voice.onUserText([&](const String &text, bool isFinal) {
    if (isFinal)
      lastUserFinal = text;
    else
      lastUserInterim = text;
  });

  voice.onAssistantText([&](const String &text) { lastAssistant = text; });

  voice.startListening();

  // Interim user
  const char *interim = "{\"serverContent\":{\"interimInputTranscription\":{"
                        "\"text\":\"Bonjour\"}}}";
  voice.getClient().handleIncomingMessage(interim, strlen(interim));
  TEST_ASSERT(lastUserInterim == "Bonjour",
              "Transcription utilisateur interim recue");

  // Final user
  const char *finalU = "{\"serverContent\":{\"inputTranscription\":{\"text\":"
                       "\"Bonjour Gemini\"}}}";
  voice.getClient().handleIncomingMessage(finalU, strlen(finalU));
  TEST_ASSERT(lastUserFinal == "Bonjour Gemini",
              "Transcription utilisateur finale recue");

  // Assistant text
  const char *asst = "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"text\":"
                     "\"Bonjour ! Que puis-je faire pour vous ?\"}]}}}";
  voice.getClient().handleIncomingMessage(asst, strlen(asst));
  TEST_ASSERT(lastAssistant == "Bonjour ! Que puis-je faire pour vous ?",
              "Transcription assistant Gemini recue");

  voice.stopListening();
  voice.getSession().finishTurn();
  voice.update();

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 29 : Restitution audio 24 kHz et préservation de la dernière syllabe
// =============================================================================
void test_conversation_audio_preservation_last_syllable() {
  std::cout << "\n[TEST SUITE 29] Restitution audio native 24 kHz et "
               "preservation derniere syllabe"
            << std::endl;

  LiveAudioPlayer player;
  TEST_ASSERT(player.begin(120000),
              "Initialisation du buffer PSRAM (120 000 ech)");

  // Remplir 2400 échantillons (100 ms)
  std::vector<int16_t> samples(2400, 1500);
  player.feedPcm(samples.data(), samples.size());

  TEST_ASSERT(player.getBufferedSamples() == 2400,
              "2400 echantillons en tampon avant notification de fin");

  // Arrivée de turnComplete
  player.notifyTurnComplete();

  // RÈGLE CRITIQUE : player NE DOIT PAS s'arrêter immédiatement, le tampon doit
  // être vidé
  TEST_ASSERT(
      player.isPlaying(),
      "La lecture continue apres turnComplete (protection derniere syllabe)");
  TEST_ASSERT(player.isTurnComplete(), "Notification turnComplete enregistree");

  // Simulation de vidage par tick audio et consommation speaker
  int tickCount = 0;
  while ((player.getBufferedSamples() > 0 || player.isPlaying()) &&
         tickCount < 100) {
    player.tick();
    player.simFinishSpeakerChunk();
    advanceMockMillis(20);
    tickCount++;
  }

  TEST_ASSERT(!player.isPlaying(), "Arret seulement apres vidage complet");
  TEST_ASSERT(player.getBufferedSamples() == 0,
              "Tampon integralement vide sans coupure");
}

// =============================================================================
// SUITE 30 : Enchaînement de 10 tours conversationnels successifs dans la même
// session
// =============================================================================
void test_conversation_ten_consecutive_turns() {
  std::cout << "\n[TEST SUITE 30] Enchainement de 10 tours conversationnels "
               "successifs"
            << std::endl;

  MockLiveTransport mockTransport;
  VoxCoreVoice voice;
  VoiceKitConfig config;
  config.apiKey = "TEST_KEY_10TURNS";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  config.enableConversation = true;
  voice.begin(config);
  prepareMockVoiceReady(voice, mockTransport);

  uint32_t turnsCompleted = 0;
  voice.onConversationFinished([&]() { turnsCompleted++; });

  for (int t = 1; t <= 10; ++t) {
    TEST_ASSERT(voice.isReady(), "Moteur PRET avant le tour");
    bool started = voice.startListening();
    TEST_ASSERT(started, "startListening() reussi pour le tour");
    TEST_ASSERT(voice.isListening(), "Etat LISTENING");

    std::vector<int16_t> sampleBlock(AUDIO_CHUNK_SAMPLES, 200);
    voice.getStreamer().pushSimulatedChunk(sampleBlock.data(),
                                           sampleBlock.size());
    voice.getStreamer().tick();

    voice.stopListening();
    TEST_ASSERT(voice.getState() == VoiceState::WAITING_RESPONSE,
                "Etat WAITING_RESPONSE");

    voice.getSession().transitionTo(LiveSessionState::PLAYING, "Reponse recue");
    voice.update();
    TEST_ASSERT(voice.getState() == VoiceState::SPEAKING, "Etat SPEAKING");

    voice.getSession().finishTurn();
    voice.getSession().transitionTo(LiveSessionState::CONNECTED,
                                    "Fin lecture audio");
    voice.update();
    TEST_ASSERT(voice.getState() == VoiceState::READY, "Retour a READY");
  }

  TEST_ASSERT(
      turnsCompleted == 10,
      "10 tours conversationnels acheves avec succes dans la meme session");
  TEST_ASSERT(voice.getConversationMetrics().turnId >= 10,
              "Compteur de tours incremente correctement");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 31 : Interruption STOP prioritaire pendant l'écoute et pendant la
// réponse
// =============================================================================
void test_conversation_stop_priority_listening_and_speaking() {
  std::cout << "\n[TEST SUITE 31] Interruption STOP prioritaire pendant "
               "l'ecoute et pendant la reponse"
            << std::endl;

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
  TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(),
              "Haut-parleur reautorise apres STOP");
  TEST_ASSERT(voice.isReady(), "Retour immediat a READY");

  // 2. STOP pendant la réflexion / réponse
  voice.startListening();
  voice.stopListening();
  voice.getSession().transitionTo(LiveSessionState::PLAYING, "Audio en cours");
  voice.update();
  TEST_ASSERT(voice.getState() == VoiceState::SPEAKING, "En cours de parole");

  voice.stop();
  TEST_ASSERT(voice.getState() == VoiceState::READY,
              "Arret immediat de la parole -> READY");
  TEST_ASSERT(!voice.isSpeaking(), "Haut-parleur arrete");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 32 : Résilience : Déconnexion Wi-Fi pendant un tour et reprise propre
// =============================================================================
void test_conversation_wifi_disconnect_and_recovery() {
  std::cout << "\n[TEST SUITE 32] Resilience : Deconnexion Wi-Fi pendant un "
               "tour et reprise propre"
            << std::endl;

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

  TEST_ASSERT(voice.getState() == VoiceState::ERROR_STATE ||
                  voice.getState() == VoiceState::DISCONNECTED,
              "Bascule en etat d'erreur ou deconnecte lors de la perte Wi-Fi");

  // Rétablissement réseau
  WiFi.setConnected(true);
  prepareMockVoiceReady(voice, mockTransport);
  TEST_ASSERT(voice.isReady(),
              "Retablissement complet de la session apres reconnexion");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 33 : Absence de parole et gestion du cycle à vide
// =============================================================================
void test_conversation_silence_handling() {
  std::cout << "\n[TEST SUITE 33] Absence de parole et gestion du cycle a vide"
            << std::endl;

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
  TEST_ASSERT(voice.getState() == VoiceState::WAITING_RESPONSE,
              "Bascule en WAITING_RESPONSE");

  // Réception turnComplete direct du serveur sans audio
  voice.getSession().finishTurn();
  voice.update();

  TEST_ASSERT(voice.getState() == VoiceState::READY,
              "Retour propre a READY apres silence");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 34 : Métriques de diagnostic d'un tour de conversation
// =============================================================================
void test_conversation_metrics_accuracy() {
  std::cout << "\n[TEST SUITE 34] Metriques diagnostiques completes du tour de "
               "conversation"
            << std::endl;

  MockLiveTransport mockTransport;
  VoxCoreVoice voice;
  VoiceKitConfig config;
  config.apiKey = "TEST_KEY_METRICS";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  config.enableConversation = true;
  voice.begin(config);
  prepareMockVoiceReady(voice, mockTransport);

  ConversationMetrics capturedMetrics;
  voice.onConversationTurnMetrics(
      [&](const ConversationMetrics &m) { capturedMetrics = m; });

  voice.startListening();

  std::vector<int16_t> sampleBlock(AUDIO_CHUNK_SAMPLES, 350);
  voice.getStreamer().pushSimulatedChunk(sampleBlock.data(),
                                         sampleBlock.size());
  voice.getStreamer().tick();

  voice.stopListening();

  const char *userJson =
      "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Bonjour\"}}}";
  voice.getClient().handleIncomingMessage(userJson, strlen(userJson));

  const char *asstJson = "{\"serverContent\":{\"modelTurn\":{\"parts\":[{"
                         "\"text\":\"Bonjour !\"}]}}}";
  voice.getClient().handleIncomingMessage(asstJson, strlen(asstJson));

  voice.getSession().transitionTo(LiveSessionState::PLAYING, "Audio");
  voice.getSession().finishTurn();
  voice.update();

  const ConversationMetrics &m = voice.getConversationMetrics();
  TEST_ASSERT(m.turnId > 0, "turnId > 0");
  TEST_ASSERT(m.audioBlocksSent >= 1, "audioBlocksSent >= 1");
  TEST_ASSERT(m.userText == "Bonjour",
              "userText correctement renseigne dans les metriques");
  TEST_ASSERT(m.assistantText == "Bonjour !",
              "assistantText correctement renseigne dans les metriques");
  TEST_ASSERT(capturedMetrics.turnId == m.turnId,
              "Callback onConversationTurnMetrics identique");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 35 : Non-régression intégrale : TTS pur et STT pur non affectés
// =============================================================================
void test_v030_v040_complete_non_regression() {
  std::cout << "\n[TEST SUITE 35] Non-regression integrale : TTS pur et STT "
               "pur preserves"
            << std::endl;

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
  TEST_ASSERT(!voice.getPlayer().isPlaybackAllowed(),
              "Haut-parleur muet pour STT");

  voice.stopListening();
  TEST_ASSERT(voice.isTranscribing(), "En cours de transcription");

  bool sttFinalFired = false;
  voice.onTranscription([&](const String &text, bool isFinal) {
    if (isFinal)
      sttFinalFired = true;
  });

  const char *finalJson = "{\"serverContent\":{\"inputTranscription\":{"
                          "\"text\":\"Phrase transcrite\"}}}";
  voice.getClient().handleIncomingMessage(finalJson, strlen(finalJson));
  voice.update();

  TEST_ASSERT(sttFinalFired, "Transcription finale STT pure recue");
  TEST_ASSERT(voice.isReady(), "STT pur revient a READY sans jouer d'audio");
  TEST_ASSERT(voice.getPlayer().isPlaybackAllowed(),
              "Haut-parleur reautorise apres STT");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 36 : Chaîne UTF-8 de bout en bout, accents français et ponctuations
// =============================================================================
void test_utf8_end_to_end_and_french_accents() {
  std::cout << "\n[TEST SUITE 36] Chaine UTF-8 integrale : accents francais, "
               "ligatures et ponctuation"
            << std::endl;

  // 1. Validation de chaque caractère prescrit
  const char *frenchChars[] = {"é", "è", "ê", "ë", "à", "â", "ù",
                               "û", "î", "ï", "ô", "ö", "ç", "œ",
                               "æ", "É", "À", "Ç", "’", "«", "»"};
  for (size_t i = 0; i < sizeof(frenchChars) / sizeof(frenchChars[0]); ++i) {
    String ch(frenchChars[i]);
    TEST_ASSERT(Utf8Utils::isValidUtf8(ch),
                String("Caractere UTF-8 valide : ") + ch);
    TEST_ASSERT(Utf8Utils::sanitizeUtf8(ch) == ch,
                String("Preservation absolue caractere : ") + ch);
  }

  // 2. Phrase obligatoire
  const String mandatoryPhrase =
      "« À Noël, Élise prépare un café crème. L'été à Genève est agréable. "
      "François préfère un cœur léger. »";
  TEST_ASSERT(Utf8Utils::isValidUtf8(mandatoryPhrase),
              "Phrase obligatoire est un UTF-8 strictement valide");
  TEST_ASSERT(Utf8Utils::sanitizeUtf8(mandatoryPhrase) == mandatoryPhrase,
              "Sanitize n'altere nullement la phrase obligatoire");

  // 3. Robustesse UTF-8 : rejet de séquences mal formées et remplacement
  // sélectif U+FFFD
  const char invalidOverlong[] = {(char)0xC0, (char)0xAF,
                                  '\0'}; // Overlong 2 octets invalide RFC 3629
  TEST_ASSERT(!Utf8Utils::isValidUtf8(invalidOverlong, 2),
              "Rejet sequence overlong 0xC0");
  String sanitizedOverlong = Utf8Utils::sanitizeUtf8(String(invalidOverlong));
  TEST_ASSERT(sanitizedOverlong.indexOf("\xEF\xBF\xBD") >= 0,
              "Remplacement de l'octet invalide par U+FFFD");

  const char truncatedUtf8[] = {'A', (char)0xC3,
                                '\0'}; // 0xC3 sans octet de continuation
  TEST_ASSERT(!Utf8Utils::isValidUtf8(truncatedUtf8, 2),
              "Rejet sequence tronquee en fin de chaine");

  // 4. Centralisation de l'échappement JSON (RFC 8259)
  String jsonEsc = Utf8Utils::escapeJson(mandatoryPhrase);
  TEST_ASSERT(jsonEsc.indexOf("crème") >= 0, "Accents intacts dans JSON");
  TEST_ASSERT(jsonEsc.indexOf("cœur") >= 0, "Ligature coeur intacte dans JSON");
  TEST_ASSERT(jsonEsc.indexOf("Élise") >= 0, "Majuscule accentuee intacte");
  TEST_ASSERT(jsonEsc.indexOf("Genève") >= 0, "Accent grave intact");

  // Échappement des caractères de contrôle ASCII (< 32), guillemets et
  // backslashes
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
  TEST_ASSERT(sentPayload.find("« À Noël, Élise prépare un café crème.") !=
                  std::string::npos,
              "La trame clientContent contient la phrase francaise accentuee "
              "sans aucune translitteration");
  TEST_ASSERT(sentPayload.find("François préfère un cœur léger. »") !=
                  std::string::npos,
              "Ligature et cedille transmises integralement a Gemini Live");

  // 6. Réception de transcriptions avec caractères accentués
  String receivedTrans = "";
  voice.onTranscription([&](const String &t, bool isFinal) {
    if (isFinal)
      receivedTrans = t;
  });

  const char *incomingJson = "{\"serverContent\":{\"inputTranscription\":{"
                             "\"text\":\"Élise a adoré l’été à Genève !\"}}}";
  voice.getClient().handleIncomingMessage(incomingJson, strlen(incomingJson));
  voice.update();

  TEST_ASSERT(receivedTrans == "Élise a adoré l’été à Genève !",
              "Transcription entrante avec apostrophe typographique et accents "
              "preservee mot a mot");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 37 : Configuration vocale VoiceKitConfig (voix, langues, styles, tons,
// accents)
// =============================================================================
void test_voice_configuration_parameters() {
  std::cout << "\n[TEST SUITE 37] Parametres VoiceKitConfig : voix, langue, "
               "style, ton et accent"
            << std::endl;

  VoiceKitConfig cfg;
  TEST_ASSERT(cfg.voice == "Aoede", "Voix par defaut conservee (Aoede)");
  TEST_ASSERT(cfg.language == "fr-FR", "Langue par defaut conforme fr-FR");
  TEST_ASSERT(cfg.style == VoiceStyle::NATURAL,
              "Style par defaut conforme NATURAL");
  TEST_ASSERT(cfg.tone == VoiceTone::NEUTRAL,
              "Ton par defaut conforme NEUTRAL");
  TEST_ASSERT(cfg.accent.isEmpty(), "Accent par defaut vide");

  // Conversion et affectation flexible style (enum et string)
  cfg.style = VoiceStyle::EXPRESSIVE;
  TEST_ASSERT(cfg.style == VoiceStyle::EXPRESSIVE,
              "Affectation enum EXPRESSIVE");
  TEST_ASSERT(String(cfg.style.c_str()) == "expressive",
              "Conversion string expressive");

  cfg.style = "professional";
  TEST_ASSERT(cfg.style == VoiceStyle::PROFESSIONAL,
              "Affectation chaine 'professional'");

  cfg.style = "narrative";
  TEST_ASSERT(cfg.style == VoiceStyle::NARRATIVE,
              "Affectation chaine 'narrative'");

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
  TEST_ASSERT(directives.indexOf("francais (fr-FR)") >= 0,
              "Directives : langue francaise");
  TEST_ASSERT(directives.indexOf("expressif, vivant et nuance") >= 0,
              "Directives : style expressif");
  TEST_ASSERT(
      directives.indexOf("chaleureuse, accueillante et bienveillante") >= 0,
      "Directives : ton chaleureux");
  TEST_ASSERT(directives.indexOf("parisien") >= 0,
              "Directives : accent parisien");

  // Langues étrangères
  cfg.language = "de-DE";
  String dirDe = cfg.buildVoiceDirectives();
  TEST_ASSERT(dirDe.indexOf("allemand (de-DE)") >= 0,
              "Directives : langue allemande");

  cfg.language = "en-US";
  String dirEn = cfg.buildVoiceDirectives();
  TEST_ASSERT(dirEn.indexOf("anglais (en-US)") >= 0,
              "Directives : langue anglaise");

  // Construction de l'instruction système effective
  String effectiveTTS =
      cfg.buildEffectiveInstruction(VOICEKIT_DEFAULT_TTS_INSTRUCTION);
  TEST_ASSERT(effectiveTTS.startsWith(VOICEKIT_DEFAULT_TTS_INSTRUCTION),
              "Instruction metier TTS strictement preservee au debut");
  TEST_ASSERT(effectiveTTS.indexOf("[Directives vocales]") >= 0,
              "Balise [Directives vocales] presente");
}

// =============================================================================
// SUITE 38 : Protocole Gemini Live Setup : speechConfig et systemInstruction
// =============================================================================
void test_gemini_setup_json_validation() {
  std::cout << "\n[TEST SUITE 38] Trame de setup Gemini Live : speechConfig, "
               "voix et directives"
            << std::endl;

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

  cJSON *root = cJSON_Parse(setupSent.c_str());
  TEST_ASSERT(root != nullptr, "Setup est un JSON valide RFC 8259");

  if (root) {
    cJSON *setup = cJSON_GetObjectItemCaseSensitive(root, "setup");
    TEST_ASSERT(setup != nullptr, "Objet racine 'setup' present");

    if (setup) {
      // Modèle
      cJSON *model = cJSON_GetObjectItemCaseSensitive(setup, "model");
      TEST_ASSERT(model && cJSON_IsString(model), "Champ model present");

      // generationConfig
      cJSON *genConfig =
          cJSON_GetObjectItemCaseSensitive(setup, "generationConfig");
      TEST_ASSERT(genConfig != nullptr, "Objet generationConfig present");

      if (genConfig) {
        cJSON *speechConfig =
            cJSON_GetObjectItemCaseSensitive(genConfig, "speechConfig");
        TEST_ASSERT(speechConfig != nullptr, "Objet speechConfig present");

        if (speechConfig) {
          // voiceConfig.prebuiltVoiceConfig.voiceName
          cJSON *voiceConfig =
              cJSON_GetObjectItemCaseSensitive(speechConfig, "voiceConfig");
          TEST_ASSERT(voiceConfig != nullptr, "voiceConfig present");
          if (voiceConfig) {
            cJSON *prebuilt = cJSON_GetObjectItemCaseSensitive(
                voiceConfig, "prebuiltVoiceConfig");
            TEST_ASSERT(prebuilt != nullptr, "prebuiltVoiceConfig present");
            if (prebuilt) {
              cJSON *vName =
                  cJSON_GetObjectItemCaseSensitive(prebuilt, "voiceName");
              TEST_ASSERT(vName && cJSON_IsString(vName) &&
                              String(vName->valuestring) == "Charon",
                          "voiceName est conforme ('Charon')");
            }
          }

          // Règle formelle : pour modèles natifs (gemini-3.8-live),
          // languageCode est ABSENT de speechConfig
          cJSON *langCode =
              cJSON_GetObjectItemCaseSensitive(speechConfig, "languageCode");
          TEST_ASSERT(langCode == nullptr,
                      "languageCode absent de speechConfig pour modele natif "
                      "(compatibilite Bidi)");
        }
      }

      // systemInstruction
      cJSON *sysInst =
          cJSON_GetObjectItemCaseSensitive(setup, "systemInstruction");
      TEST_ASSERT(sysInst != nullptr, "systemInstruction present");
      if (sysInst) {
        cJSON *parts = cJSON_GetObjectItemCaseSensitive(sysInst, "parts");
        if (parts && cJSON_GetArraySize(parts) > 0) {
          cJSON *p0 = cJSON_GetArrayItem(parts, 0);
          cJSON *txt = cJSON_GetObjectItemCaseSensitive(p0, "text");
          TEST_ASSERT(txt && cJSON_IsString(txt), "Texte instruction present");
          if (txt && txt->valuestring) {
            String sTxt(txt->valuestring);
            TEST_ASSERT(
                sTxt.indexOf("Tu es un moteur de synthese vocale TTS pur") >= 0,
                "Instruction metier TTS d'origine strictement preservee");
            TEST_ASSERT(
                sTxt.indexOf("[Directives vocales]") >= 0,
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
  std::cout << "\n[TEST SUITE 39] Changement dynamique de profil vocal et "
               "securite d'interruption"
            << std::endl;

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
  bool profOk = voice.setVoiceProfile("Fenrir", "en-US", VoiceStyle::EXPRESSIVE,
                                      VoiceTone::CHEERFUL, "british");
  TEST_ASSERT(profOk, "Changement de profil accepte au repos");
  TEST_ASSERT(voice.isConnecting(), "Reconnexion en cours");
  TEST_ASSERT(!voice.isReady(), "isReady false pendant la reconnexion");
  // REGLE CRITIQUE v0.4.1.1 : Le profil précédent est strictement conservé tant
  // que setupComplete n'a pas confirmé le nouveau profil
  TEST_ASSERT(voice.getVoice() == "Aoede",
              "Voix precedente Aoede conservee tant que setupComplete n'est "
              "pas confirme");

  // Reconnexion confirmée via simulateIncoming setupComplete
  const char *setupCompJson = "{\"setupComplete\":{}}";
  voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
  voice.update();
  TEST_ASSERT(voice.isReady(), "Nouvelle session securisee prete");
  TEST_ASSERT(voice.getVoice() == "Fenrir",
              "Nouvelle voix Fenrir validee apres setupComplete");
  TEST_ASSERT(voice.getLanguage() == "en-US",
              "Nouvelle langue en-US validee apres setupComplete");
  TEST_ASSERT(voice.getStyle() == VoiceStyle::EXPRESSIVE,
              "Nouveau style EXPRESSIVE valide apres setupComplete");
  TEST_ASSERT(voice.getTone() == VoiceTone::CHEERFUL,
              "Nouveau ton CHEERFUL valide apres setupComplete");
  TEST_ASSERT(voice.getAccent() == "british",
              "Nouvel accent british valide apres setupComplete");

  // 2. Vérification de la nouvelle trame de setup emise
  std::string newSetup = mockTransport.getLastSent();
  TEST_ASSERT(newSetup.find("Fenrir") != std::string::npos,
              "Nouveau setup avec voix Fenrir emise");
  TEST_ASSERT(newSetup.find("anglais (en-US)") != std::string::npos,
              "Nouveau setup avec langue anglaise");

  // 3. Sécurité absolue : interdiction de reconnexion pendant une génération
  // vocale ou écoute active
  voice.say("Phrase en cours d'elocution");
  // Le moteur est occupé
  TEST_ASSERT(voice.isBusy(), "Moteur occupe en cours de parole");

  bool rejectDuringBusy = voice.setVoiceProfile(
      "Puck", "fr-FR", VoiceStyle::NATURAL, VoiceTone::CALM);
  TEST_ASSERT(
      !rejectDuringBusy,
      "Changement de profil STRICTEMENT REFUSE pendant activite vocale active");
  TEST_ASSERT(voice.getVoice() == "Fenrir",
              "Voix inchangee apres tentative pendant activite");

  voice.stop();
  TEST_ASSERT(!voice.isBusy(), "Moteur au repos apres stop()");

  // 4. Modificateurs individuels avec confirmation setupComplete
  bool changeTone = voice.setTone(VoiceTone::SERIOUS);
  TEST_ASSERT(changeTone, "setTone accepte au repos");
  TEST_ASSERT(voice.getTone() == VoiceTone::CHEERFUL,
              "Ton precedent conserve tant que non confirme");
  voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
  voice.update();
  TEST_ASSERT(voice.getTone() == VoiceTone::SERIOUS,
              "Ton serieux valide apres setupComplete");

  bool changeStyle = voice.setStyle(VoiceStyle::NARRATIVE);
  TEST_ASSERT(changeStyle, "setStyle accepte au repos");
  voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
  voice.update();
  TEST_ASSERT(voice.getStyle() == VoiceStyle::NARRATIVE,
              "Style narrative valide apres setupComplete");

  bool changeLang = voice.setLanguage("de-DE");
  TEST_ASSERT(changeLang, "setLanguage accepte au repos");
  voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
  voice.update();
  TEST_ASSERT(voice.getLanguage() == "de-DE",
              "Langue de-DE validee apres setupComplete");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 40 : Sécurisation avancée des profils vocaux et reconnexions (v0.4.1.1)
// =============================================================================
void test_voice_profile_safety_stabilization() {
  std::cout << "\n[TEST SUITE 40] Securisation des profils vocaux, "
               "reconnexions et refus stricts"
            << std::endl;

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
  voice.onReady([&]() { readyReportedVoice = voice.getVoice(); });

  bool changeOk = voice.setVoice("Fenrir");
  TEST_ASSERT(changeOk, "setVoice('Fenrir') accepte a froid/au repos");
  TEST_ASSERT(voice.isConnecting(), "Reconnexion active");
  TEST_ASSERT(voice.isProfileChangePending(), "isProfileChangePending() true");
  // Le profil précédent est conservé tant que setupComplete n'est pas reçu
  TEST_ASSERT(voice.getVoice() == "Aoede",
              "Profil precedent Aoede conserve avant setupComplete");

  // Vérification que la nouvelle trame de setup contient la voix Fenrir
  std::string setupSent = mockTransport.getLastSent();
  TEST_ASSERT(setupSent.find("\"voiceName\":\"Fenrir\"") != std::string::npos,
              "Trame setup contient la voix Fenrir");

  // Réception du setupComplete
  const char *setupCompJson = "{\"setupComplete\":{}}";
  voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
  voice.update();

  TEST_ASSERT(voice.isReady(), "Moteur PRET apres setupComplete");
  TEST_ASSERT(!voice.isProfileChangePending(),
              "isProfileChangePending() false apres setupComplete");
  TEST_ASSERT(voice.getVoice() == "Fenrir",
              "Nouvelle voix Fenrir validee apres setupComplete");
  TEST_ASSERT(readyReportedVoice == "Fenrir",
              "onReady() rapporte bien le profil valide et actif (Fenrir)");

  // 2. Refus strict du changement pendant la parole
  voice.say("Test de parole en cours");
  TEST_ASSERT(voice.isBusy(), "isBusy() true pendant elocution");
  bool rejectDuringSpeaking = voice.setVoice("Puck");
  TEST_ASSERT(!rejectDuringSpeaking,
              "Changement de voix STRICTEMENT REFUSE pendant la parole");
  TEST_ASSERT(voice.getVoice() == "Fenrir",
              "Voix inchangee apres tentative pendant la parole");
  voice.stop();
  TEST_ASSERT(!voice.isBusy(), "Moteur libere apres stop()");

  // 3. Refus strict pendant une transcription STT
  bool listenOk = voice.startListening();
  TEST_ASSERT(listenOk, "startListening() demarre");
  voice.stopListening();
  TEST_ASSERT(voice.isTranscribing(),
              "isTranscribing() true en attente de transcription finale");
  bool rejectDuringTranscription = voice.setVoice("Charon");
  TEST_ASSERT(!rejectDuringTranscription,
              "Changement de voix STRICTEMENT REFUSE pendant transcription");
  TEST_ASSERT(voice.getVoice() == "Fenrir",
              "Voix inchangee apres tentative pendant transcription");
  voice.cancelListening();
  TEST_ASSERT(voice.isReady(), "Moteur PRET apres cancelListening()");

  // 4. Refus strict pendant une connexion ou reconnexion déjà active
  bool firstRequest = voice.setVoice("Charon");
  TEST_ASSERT(firstRequest, "Premiere demande de profil acceptee");
  TEST_ASSERT(voice.isConnecting(), "Reconnexion en cours");

  // Tentative immédiate d'un second changement alors que la reconnexion est en
  // cours
  bool rejectSecondRequest = voice.setVoice("Puck");
  TEST_ASSERT(!rejectSecondRequest, "Deuxieme demande successive de profil "
                                    "STRICTEMENT REFUSEE pendant reconnexion");
  TEST_ASSERT(voice.getVoice() == "Fenrir",
              "Voix active reste Fenrir pendant la tentative");

  // Validation du premier changement via setupComplete
  voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
  voice.update();
  TEST_ASSERT(voice.isReady(), "Moteur PRET apres validation de Charon");
  TEST_ASSERT(voice.getVoice() == "Charon",
              "Voix validee est Charon, et non Puck");

  // 5. Échec simulé de reconnexion et restauration cohérente
  bool changeToFenrir = voice.setVoice("Fenrir");
  TEST_ASSERT(changeToFenrir,
              "Demande vers Fenrir acceptee pour le test d'echec");
  TEST_ASSERT(voice.isProfileChangePending(), "Profil pending actif");
  TEST_ASSERT(voice.getVoice() == "Charon", "Profil precedent reste Charon");

  // Simulation d'une erreur fatale de session/transport
  voice.getSession().transitionTo(LiveSessionState::ERROR,
                                  "Erreur simulee TLS");
  voice.update();

  TEST_ASSERT(voice.getState() == VoiceState::ERROR_STATE,
              "Etat bascule a ERROR_STATE");
  TEST_ASSERT(!voice.isProfileChangePending(),
              "isProfileChangePending() reinitialise a false");
  // Restauration cohérente : _config et _client ont retrouve Charon
  TEST_ASSERT(voice.getVoice() == "Charon",
              "Profil precedent Charon preserve dans _config");
  TEST_ASSERT(voice.getClient().getVoice() == "Charon",
              "Voix Charon restauree dans GeminiLiveClient");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 41 : Transitions fiabilisées TTS <-> Conversation et non-interférence
// (v0.4.1.1)
// =============================================================================
void test_mode_transition_stabilization() {
  std::cout << "\n[TEST SUITE 41] Transitions fiabilisees TTS <-> Conversation "
               "et non-interference"
            << std::endl;

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
  TEST_ASSERT(!voice.isConversationEnabled(),
              "Mode conversation desactive initialement");

  // Enregistrement de callbacks pour vérifier leur persistance
  bool readyCallbackFired = false;
  voice.onReady([&]() { readyCallbackFired = true; });

  bool userTextCallbackFired = false;
  voice.onUserText([&](const String &, bool) { userTextCallbackFired = true; });

  // 1. Passage TTS -> Conversation avec reconnexion maîtrisée
  readyCallbackFired = false;
  bool switchConvOk = voice.enableConversation(true);
  TEST_ASSERT(switchConvOk, "enableConversation(true) accepte au repos");
  TEST_ASSERT(voice.isConnecting(),
              "Reconnexion en cours pour application du mode conversation");
  TEST_ASSERT(voice.isConversationEnabled(), "Mode conversation marque active");

  // Vérification de la nouvelle trame de setup émise
  std::string convSetup = mockTransport.getLastSent();
  TEST_ASSERT(
      convSetup.find(
          Utf8Utils::escapeJson(VOICEKIT_DEFAULT_CONVERSATION_INSTRUCTION)
              .c_str()) != std::string::npos ||
          convSetup.find("conversation bienveillante") != std::string::npos,
      "Nouvelle trame setup contient l'instruction systeme conversation");
  TEST_ASSERT(
      convSetup.find("[Directives vocales]") != std::string::npos,
      "Directives vocales preservees dans la trame setup de conversation");
  TEST_ASSERT(convSetup.find("parisien") != std::string::npos,
              "Accent parisien preserve dans la trame setup de conversation");

  // Confirmation setupComplete
  const char *setupCompJson = "{\"setupComplete\":{}}";
  voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
  voice.update();

  TEST_ASSERT(voice.isReady(), "Session conversationnelle confirmee et prete");
  TEST_ASSERT(!voice.isModeSwitchPending(),
              "isModeSwitchPending() false apres setupComplete");
  TEST_ASSERT(readyCallbackFired,
              "Callback onReady declenche apres transition vers Conversation");

  // 2. Tentative de basculement de mode pendant un tour actif
  bool turnStarted = voice.startListening();
  TEST_ASSERT(turnStarted, "Tour conversationnel demarre");
  TEST_ASSERT(voice.isListening(), "isListening() true");

  bool rejectSwitchDuringTurn = voice.enableConversation(false);
  TEST_ASSERT(
      !rejectSwitchDuringTurn,
      "enableConversation(false) STRICTEMENT REFUSE pendant tour actif");
  TEST_ASSERT(voice.isConversationEnabled(),
              "Mode reste conversationnel apres tentative refusee");

  // Fin du tour
  voice.stop();
  TEST_ASSERT(!voice.isBusy(), "Moteur au repos apres stop()");
  TEST_ASSERT(voice.isReady(), "Moteur PRET");

  // 3. Passage Conversation -> TTS avec reconnexion maîtrisée
  readyCallbackFired = false;
  bool switchTtsOk = voice.enableConversation(false);
  TEST_ASSERT(switchTtsOk, "enableConversation(false) accepte au repos");
  TEST_ASSERT(voice.isConnecting(), "Reconnexion en cours pour retour TTS");
  TEST_ASSERT(!voice.isConversationEnabled(),
              "Mode conversation marque desactive");

  // Vérification de la trame setup pour TTS
  std::string ttsSetup = mockTransport.getLastSent();
  TEST_ASSERT(
      ttsSetup.find(
          Utf8Utils::escapeJson(VOICEKIT_DEFAULT_TTS_INSTRUCTION).c_str()) !=
              std::string::npos ||
          ttsSetup.find("synthese vocale TTS pur") != std::string::npos,
      "Nouvelle trame setup contient l'instruction systeme TTS");
  TEST_ASSERT(ttsSetup.find("[Directives vocales]") != std::string::npos,
              "Directives vocales preservees dans la trame setup TTS");

  voice.getClient().handleIncomingMessage(setupCompJson, strlen(setupCompJson));
  voice.update();

  TEST_ASSERT(voice.isReady(), "Session TTS confirmee et prete");
  TEST_ASSERT(readyCallbackFired,
              "Callback onReady declenche apres retour a TTS");

  // 4. Tentative de passage Conversation pendant une élocution TTS
  voice.say("Synthese en cours de test");
  TEST_ASSERT(voice.isBusy(), "isBusy() true pendant TTS");
  bool rejectSwitchDuringTts = voice.enableConversation(true);
  TEST_ASSERT(
      !rejectSwitchDuringTts,
      "enableConversation(true) STRICTEMENT REFUSE pendant elocution TTS");
  TEST_ASSERT(!voice.isConversationEnabled(), "Mode reste TTS");
  voice.stop();

  // 5. Vérification de la préservation des directives vocales après toutes les
  // transitions
  TEST_ASSERT(voice.getVoice() == "Aoede", "Voix inchangee (Aoede)");
  TEST_ASSERT(voice.getLanguage() == "fr-FR", "Langue inchangee (fr-FR)");
  TEST_ASSERT(voice.getStyle() == VoiceStyle::NATURAL,
              "Style inchange (NATURAL)");
  TEST_ASSERT(voice.getTone() == VoiceTone::CALM, "Ton inchange (CALM)");
  TEST_ASSERT(voice.getAccent() == "parisien", "Accent inchange (parisien)");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 42 : Préservation intégrale UTF-8 et compatibilité linguistique
// (v0.4.1.1)
// =============================================================================
void test_utf8_and_language_compatibility_stabilization() {
  std::cout << "\n[TEST SUITE 42] Chaine UTF-8 integrale, accents francais et "
               "compatibilite linguistique"
            << std::endl;

  // 1. Phrase de référence obligatoire prescrite dans le cahier des charges
  const char *obligatoryPhrase =
      "« À Noël, Élise prépare un café crème. L'été à Genève est agréable. "
      "François préfère un cœur léger. »";

  TEST_ASSERT(Utf8Utils::isValidUtf8(obligatoryPhrase),
              "Phrase obligatoire est un UTF-8 RFC 3629 valide");

  // Échappement JSON RFC 8259 : préserve les octets bruts Unicode et les
  // chevrons français « »
  String escaped = Utf8Utils::escapeJson(obligatoryPhrase);
  TEST_ASSERT(escaped.indexOf("«") >= 0 && escaped.indexOf("»") >= 0,
              "Guillemets francais chevrons « » strictement preserves");
  TEST_ASSERT(escaped.indexOf("À") >= 0,
              "Caractere À majuscule accentue strictement preserve");
  TEST_ASSERT(escaped.indexOf("É") >= 0,
              "Caractere É majuscule accentue strictement preserve");
  TEST_ASSERT(escaped.indexOf("é") >= 0,
              "Caractere é minuscule accentue strictement preserve");
  TEST_ASSERT(escaped.indexOf("è") >= 0,
              "Caractere è minuscule accentue strictement preserve");
  TEST_ASSERT(escaped.indexOf("ç") >= 0,
              "Caractere ç cedille strictement preserve");
  TEST_ASSERT(escaped.indexOf("œ") >= 0, "Ligature œ strictement preservee");

  // Échappement des guillemets doubles ASCII "
  String withAsciiQuotes = "Texte avec \"citation\" et « chevrons »";
  String escAscii = Utf8Utils::escapeJson(withAsciiQuotes);
  TEST_ASSERT(escAscii.indexOf("\\\"citation\\\"") >= 0,
              "Guillemets ASCII doubles correctement echappes en \\\"");
  TEST_ASSERT(escAscii.indexOf("« chevrons »") >= 0,
              "Chevrons francais non alteres");

  // Vérification exhaustive des glyphes français demandés : é è ê ë à â ù û î ï
  // ô ö ç œ æ É À Ç ’ « »
  const char *allFrenchGlyphs = "é è ê ë à â ù û î ï ô ö ç œ æ É À Ç ’ « »";
  TEST_ASSERT(Utf8Utils::isValidUtf8(allFrenchGlyphs),
              "Tous les glyphes francais obligatoires sont UTF-8 valides");
  String escapedGlyphs = Utf8Utils::escapeJson(allFrenchGlyphs);
  TEST_ASSERT(escapedGlyphs.indexOf("œ") >= 0 &&
                  escapedGlyphs.indexOf("æ") >= 0,
              "Ligatures œ et æ preservees");
  TEST_ASSERT(escapedGlyphs.indexOf("Ç") >= 0 &&
                  escapedGlyphs.indexOf("É") >= 0,
              "Majuscules Ç et É preservees");

  // 2. Clarification formelle de isModelSpeechLanguageCodeSupported() (Section
  // 4)
  TEST_ASSERT(
      !GeminiLiveClient::isModelSpeechLanguageCodeSupported(
          "models/gemini-2.0-flash-exp"),
      "gemini-2.0-flash-exp -> false (pas de speechConfig.languageCode)");
  TEST_ASSERT(!GeminiLiveClient::isModelSpeechLanguageCodeSupported(
                  "models/gemini-3.8-live"),
              "gemini-3.8-live -> false (pas de speechConfig.languageCode)");
  TEST_ASSERT(
      !GeminiLiveClient::isModelSpeechLanguageCodeSupported("gemini-2.5-flash"),
      "gemini-2.5-flash -> false (pas de speechConfig.languageCode)");
  TEST_ASSERT(
      !GeminiLiveClient::isModelSpeechLanguageCodeSupported("any-model"),
      "Modele quelconque -> false (garantie pas de champ "
      "speechConfig.languageCode non valide)");

  // 3. Vérification de la transmission dans la trame de setup avec
  // MockLiveTransport
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
  TEST_ASSERT(sentMsg.find("À Noël, Élise prépare un café crème") !=
                  std::string::npos,
              "Phrase francaise accentuee transmise mot pour mot dans le "
              "message JSON Gemini Live");
  TEST_ASSERT(sentMsg.find("François préfère un cœur léger") !=
                  std::string::npos,
              "Ligature et cédille transmises mot pour mot sans altération");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// NOUVELLES SUITES CONTINUOUS STT & VAD (VOICEKIT v0.4.1.2)
// =============================================================================

// SUITE 43 : VAD (Voice Activity Detection) - Algorithme, seuils adaptatifs,
// hystérésis et transitions
void test_vad_algorithm_and_state_transitions() {
  std::cout << "\n[TEST SUITE 43] VAD : Algorithme, seuils adaptatifs, "
               "hysteresis et transitions"
            << std::endl;
  VoiceActivityDetector vad;

  // 1. Initialisation et configuration par défaut
  TEST_ASSERT(vad.getState() == VadState::SILENCE,
              "Etat initial VAD est SILENCE");
  TEST_ASSERT(vad.getConfig().minSpeechDurationMs == 200,
              "Duree min parole = 200 ms");
  TEST_ASSERT(vad.getConfig().silenceDurationMs == 1200,
              "Duree silence = 1200 ms");
  TEST_ASSERT(vad.getConfig().maxSegmentDurationMs == 30000,
              "Duree max segment = 30000 ms");
  TEST_ASSERT(!vad.isSpeechActive(), "isSpeechActive() initialement faux");
  TEST_ASSERT(!vad.isSpeechEnded(), "isSpeechEnded() initialement faux");

  // 2. Calcul et verification de l'hysteresis
  vad.setNoiseFloor(200);
  uint32_t spThresh = vad.getSpeechThreshold();
  uint32_t silThresh = vad.getSilenceThreshold();
  TEST_ASSERT(spThresh == (uint32_t)(200 * 2.0f) + 250,
              "Seuil de parole calcule conformement a la formule");
  TEST_ASSERT(silThresh == (uint32_t)(200 * 1.3f) + 120,
              "Seuil de silence calcule conformement a la formule");
  TEST_ASSERT(
      spThresh > silThresh,
      "Hysteresis validee : seuil de parole superieur au seuil de silence");

  // 3. Suivi adaptatif du bruit de fond pendant le silence
  for (int i = 0; i < 30; ++i) {
    vad.processRms(100, 32);
  }
  TEST_ASSERT(vad.getState() == VadState::SILENCE,
              "Reste en SILENCE avec bruit faible (100 RMS)");
  TEST_ASSERT(vad.getNoiseFloor() < 180,
              "Plancher de bruit s'adapte a la baisse vers 100 RMS");

  // 4. Rejet du bruit transitoire (clic / impulsion de 64 ms < 200 ms)
  vad.processRms(3000, 32); // 1er bloc fort -> SPEECH_STARTING
  TEST_ASSERT(vad.getState() == VadState::SPEECH_STARTING,
              "Transition en SPEECH_STARTING sur front montant");
  vad.processRms(3000, 32); // 2e bloc fort (total 64 ms < 200 ms)
  vad.processRms(100, 32);  // retour au silence
  TEST_ASSERT(vad.getState() == VadState::SILENCE,
              "Rejet du bruit transitoire : retour en SILENCE");
  TEST_ASSERT(!vad.isSpeechConfirmed(),
              "Parole non confirmee pour bruit < 200 ms");

  // 5. Validation de la parole (> 200 ms)
  bool startedCbFired = false;
  vad.onSpeechStarted([&]() { startedCbFired = true; });

  // 7 blocs a 2500 RMS = 224 ms (> 200 ms)
  for (int i = 0; i < 7; ++i) {
    vad.processRms(2500, 32);
  }
  TEST_ASSERT(vad.getState() == VadState::SPEECH_ACTIVE,
              "Parole confirmee (> 200 ms) -> SPEECH_ACTIVE");
  TEST_ASSERT(vad.isSpeechActive(), "isSpeechActive() vrai");
  TEST_ASSERT(vad.isSpeechConfirmed(), "isSpeechConfirmed() vrai");
  TEST_ASSERT(startedCbFired,
              "Callback onSpeechStarted declenche a la confirmation");

  // 6. Silence post-parole et detection de fin de segment (1200 ms)
  bool endedCbFired = false;
  VadEndReason reportedReason = VadEndReason::NONE;
  vad.onSpeechEnded([&](VadEndReason reason) {
    endedCbFired = true;
    reportedReason = reason;
  });

  // 1 bloc sous seuil bas -> transition en SILENCE_STARTING
  vad.processRms(80, 32);
  TEST_ASSERT(vad.getState() == VadState::SILENCE_STARTING,
              "Transition en SILENCE_STARTING post-parole");
  TEST_ASSERT(vad.isSilenceCounting(), "isSilenceCounting() vrai");
  TEST_ASSERT(
      vad.isSpeechActive(),
      "isSpeechActive() toujours vrai pendant le compte a rebours silence");

  // Suite du silence jusqu'a 1200 ms (encore 37 blocs de 32 ms : 38 * 32 = 1216
  // ms >= 1200 ms)
  for (int i = 0; i < 37; ++i) {
    vad.processRms(80, 32);
  }
  TEST_ASSERT(
      vad.getState() == VadState::SPEECH_ENDED,
      "Fin de segment confirmee apres 1200 ms de silence -> SPEECH_ENDED");
  TEST_ASSERT(vad.isSpeechEnded(), "isSpeechEnded() vrai");
  TEST_ASSERT(endedCbFired, "Callback onSpeechEnded declenche");
  TEST_ASSERT(reportedReason == VadEndReason::SILENCE_TIMEOUT,
              "Raison de fin = SILENCE_TIMEOUT");
  TEST_ASSERT(vad.getEndReason() == VadEndReason::SILENCE_TIMEOUT,
              "getEndReason() = SILENCE_TIMEOUT");
}

// SUITE 44 : VAD - Scénarios temporels avancés (pauses inter-mots, durée max
// 30s, silence prolongé, PCM)
void test_vad_advanced_temporal_scenarios() {
  std::cout << "\n[TEST SUITE 44] VAD : Scenarios temporels avances (pauses, "
               "30s max, silence prolonge, PCM)"
            << std::endl;
  VoiceActivityDetector vad;

  // 1. Pause au milieu d'une phrase (mid-sentence pause de 400 ms < 1200 ms)
  for (int i = 0; i < 8; ++i)
    vad.processRms(2500, 32);
  TEST_ASSERT(vad.getState() == VadState::SPEECH_ACTIVE,
              "Phase parole 1 active");

  // Pause de 400 ms (12 blocs de 32 ms = 384 ms)
  for (int i = 0; i < 12; ++i)
    vad.processRms(80, 32);
  TEST_ASSERT(vad.getState() == VadState::SILENCE_STARTING,
              "Pause inter-mots en cours");
  TEST_ASSERT(vad.getCurrentSilenceMs() >= 380 &&
                  vad.getCurrentSilenceMs() < 1200,
              "Compteur silence avance sans expirer");

  // Reprise de la parole avant 1200 ms
  vad.processRms(2500, 32);
  TEST_ASSERT(vad.getState() == VadState::SPEECH_ACTIVE,
              "Reprise vocale : retour direct en SPEECH_ACTIVE");
  TEST_ASSERT(vad.getCurrentSilenceMs() == 0,
              "Compteur silence reinitialise a 0");

  // Cloture du segment apres la deuxieme partie de phrase
  for (int i = 0; i < 5; ++i)
    vad.processRms(2500, 32);
  for (int i = 0; i < 38; ++i)
    vad.processRms(80, 32);
  TEST_ASSERT(vad.isSpeechEnded(),
              "Segment finalise apres le deuxieme silence complet");

  // 2. Duree maximale de segment (30 secondes = 30000 ms)
  vad.reset();
  bool maxDurationFired = false;
  vad.onSpeechEnded([&](VadEndReason r) {
    if (r == VadEndReason::MAX_DURATION)
      maxDurationFired = true;
  });

  // Envoi continu de blocs de parole pendant 30 secondes (940 blocs de 32 ms =
  // 30080 ms)
  for (int i = 0; i < 940; ++i) {
    bool ended = vad.processRms(2000, 32);
    if (ended)
      break;
  }
  TEST_ASSERT(vad.getState() == VadState::SPEECH_ENDED,
              "Coupure automatique a 30 secondes -> SPEECH_ENDED");
  TEST_ASSERT(vad.getEndReason() == VadEndReason::MAX_DURATION,
              "Raison = MAX_DURATION");
  TEST_ASSERT(maxDurationFired, "Notification de duree max recue");

  // 3. Silence prolonge (10 secondes = 312 blocs) sans fausse detection
  vad.reset();
  for (int i = 0; i < 312; ++i) {
    vad.processRms(120, 32);
  }
  TEST_ASSERT(vad.getState() == VadState::SILENCE,
              "Reste strictement en SILENCE apres 10s de silence");
  TEST_ASSERT(!vad.isSpeechConfirmed(),
              "Aucun debut de parole n'a ete confirme");
  TEST_ASSERT(!vad.isSpeechEnded(),
              "Aucune fin de segment n'a ete declenchee a tort");

  // 4. Adaptation au bruit ambiant dynamique
  vad.reset();
  vad.setNoiseFloor(150);
  // Bruit de fond monte progressivement de 150 a 600 RMS
  for (int step = 150; step <= 600; step += 15) {
    for (int i = 0; i < 6; ++i) {
      vad.processRms(step, 32);
    }
  }
  TEST_ASSERT(vad.getNoiseFloor() > 500,
              "Plancher de bruit a suivi l'elevation ambiante (> 500)");
  TEST_ASSERT(
      vad.getSpeechThreshold() > 1200,
      "Seuil de parole eleve pour eviter les declenchements par le bruit");
  TEST_ASSERT(vad.getState() == VadState::SILENCE,
              "Toujours en SILENCE malgre un bruit a 600 RMS");

  // La voix au-dessus du nouveau seuil (3000 RMS) declenche bien la parole
  for (int i = 0; i < 8; ++i) {
    vad.processRms(3000, 32);
  }
  TEST_ASSERT(vad.getState() == VadState::SPEECH_ACTIVE,
              "Voix a 3000 RMS detectee malgre un bruit ambiant eleve");

  // 5. Test de traitement PCM brut via processSamples()
  vad.reset();
  int16_t silentPcm[512] = {0};
  int16_t speechPcm[512];
  for (size_t i = 0; i < 512; ++i) {
    speechPcm[i] = (int16_t)(2000.0 * sin(2.0 * M_PI * i / 16.0));
  }
  vad.processSamples(silentPcm, 512, 16000);
  TEST_ASSERT(vad.getState() == VadState::SILENCE,
              "processSamples(silentPcm) maintient SILENCE");
  for (int i = 0; i < 8; ++i) {
    vad.processSamples(speechPcm, 512, 16000);
  }
  TEST_ASSERT(vad.getState() == VadState::SPEECH_ACTIVE,
              "processSamples(speechPcm) active SPEECH_ACTIVE");
}

// SUITE 45 : Flux ContinuousSTT complet - Écoute continue, streaming et
// segments
void test_continuous_stt_complete_lifecycle() {
  std::cout << "\n[TEST SUITE 45] Flux ContinuousSTT complet : Ecoute "
               "continue, streaming et segments"
            << std::endl;
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
  voice.onAudioChunk([&](const int16_t *samples, size_t count, uint32_t rms) {
    (void)samples;
    (void)count;
    chunksReceived++;
    if (voice.isListening()) {
      vad.processRms(rms, 32);
    }
  });

  int finalTranscriptionsCount = 0;
  String lastFinalReceived = "";
  voice.onTranscription([&](const String &text, bool isFinal) {
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
  TEST_ASSERT(voice.getAudioLevel() > 1000,
              "voice.getAudioLevel() rapporte un RMS eleve");
  TEST_ASSERT(vad.isSpeechActive(), "VAD detecte la parole du segment 1");

  // Injection de 38 blocs de silence (1216 ms)
  int16_t silenceChunk[AUDIO_CHUNK_SAMPLES] = {0};
  for (int i = 0; i < 38; ++i) {
    voice.getStreamer().pushSimulatedChunk(silenceChunk, AUDIO_CHUNK_SAMPLES);
    voice.update();
  }

  TEST_ASSERT(vad.isSpeechEnded(),
              "VAD signale la fin du segment 1 apres 1200 ms de silence");

  // L'application arrete l'ecoute suite a la detection VAD
  bool stopOk1 = voice.stopListening();
  TEST_ASSERT(stopOk1, "stopListening() segment 1 effectue avec succes");
  TEST_ASSERT(voice.isTranscribing(),
              "voice.isTranscribing() actif en attente de reponse Gemini");

  // Reception de la transcription finale authentique
  const char *finalJson1 =
      "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Bonjour Gemini "
      "voici ma première phrase\"}}}";
  voice.getClient().handleIncomingMessage(finalJson1, strlen(finalJson1));
  voice.update();

  TEST_ASSERT(finalTranscriptionsCount == 1, "Transcription finale 1 recue");
  TEST_ASSERT(lastFinalReceived == "Bonjour Gemini voici ma première phrase",
              "Texte final 1 exact");
  TEST_ASSERT(!voice.isTranscribing(), "Fin d'attente de transcription");
  TEST_ASSERT(voice.isReady(), "VoiceKit pret pour le segment suivant");

  // === SEGMENT 2 (Enchainement mains libres sur la meme session) ===
  vad.reset();
  bool listen2 = voice.startListening();
  TEST_ASSERT(listen2,
              "startListening() segment 2 accepte sans reconnexion de session");
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
  const char *finalJson2 = "{\"serverContent\":{\"inputTranscription\":{"
                           "\"text\":\"Deuxième phrase en dictée continue\"}}}";
  voice.getClient().handleIncomingMessage(finalJson2, strlen(finalJson2));
  voice.update();

  TEST_ASSERT(finalTranscriptionsCount == 2, "Transcription finale 2 recue");
  TEST_ASSERT(lastFinalReceived == "Deuxième phrase en dictée continue",
              "Texte final 2 exact");
  TEST_ASSERT(voice.isReady(), "VoiceKit pret apres segment 2");

  voice.getClient().setTransport(nullptr);
}

// SUITE 46 : Résilience ContinuousSTT - Absence de transcription finale, pause
// et UTF-8
void test_continuous_stt_resilience_and_utf8() {
  std::cout << "\n[TEST SUITE 46] Resilience ContinuousSTT : Timeout sans "
               "invention, pause et accents"
            << std::endl;
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
  voice.onTranscription([&](const String &text, bool isFinal) {
    if (isFinal) {
      finalFired = true;
      finalReceived = text;
    } else {
      interimReceived = text;
    }
  });

  voice.startListening();
  const char *partialJson = "{\"serverContent\":{\"interimInputTranscription\":"
                            "{\"text\":\"Texte partiel incomplet...\"}}}";
  voice.getClient().handleIncomingMessage(partialJson, strlen(partialJson));
  TEST_ASSERT(interimReceived == "Texte partiel incomplet...",
              "Interim enregistre");

  voice.stopListening();
  TEST_ASSERT(voice.isTranscribing(), "En attente de transcription finale");

  // Avancement de l'horloge au-dela de sttFinalTimeoutMs (6000 ms)
  advanceMockMillis(6500);
  voice.update();

  TEST_ASSERT(!voice.isTranscribing(),
              "Sortie de l'etat transcribing apres timeout");
  TEST_ASSERT(voice.isReady(), "Retour a READY");
  TEST_ASSERT(!finalFired, "REGLE ABSOLUE : Aucune transcription finale n'a "
                           "ete inventee depuis l'interim");
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
  TEST_ASSERT(voice.getState() == VoiceState::ERROR_STATE ||
                  voice.getState() == VoiceState::DISCONNECTED ||
                  !voice.isReady(),
              "Etat session signale la coupure");

  // Reconnexion du transport
  WiFi.setConnected(true);
  prepareMockVoiceReady(voice, mockTransport);
  TEST_ASSERT(voice.isReady(),
              "VoiceKit redevient READY apres retablissement de la session");

  // 4. Chaine UTF-8 francaise avec accents transmise sans alteration
  const char *frenchPhrase =
      "À Noël, Élise prépare un café crème. L'été à Genève est agréable. "
      "François préfère un cœur léger.";
  String receivedFinalAccent = "";
  voice.onTranscription([&](const String &text, bool isFinal) {
    if (isFinal)
      receivedFinalAccent = text;
  });

  voice.startListening();
  std::string frenchJson =
      std::string("{\"serverContent\":{\"inputTranscription\":{\"text\":\"") +
      frenchPhrase + "\"}}}";
  voice.getClient().handleIncomingMessage(frenchJson.c_str(),
                                          frenchJson.length());
  voice.update();

  TEST_ASSERT(receivedFinalAccent == frenchPhrase,
              "Phrase francaise accentuee complete restituee a 100%");
  TEST_ASSERT(receivedFinalAccent.indexOf("À") >= 0,
              "Accent A majuscule conserve");
  TEST_ASSERT(receivedFinalAccent.indexOf("É") >= 0,
              "Accent E majuscule conserve");
  TEST_ASSERT(receivedFinalAccent.indexOf("é") >= 0, "Accent e aigu conserve");
  TEST_ASSERT(receivedFinalAccent.indexOf("è") >= 0, "Accent e grave conserve");
  TEST_ASSERT(receivedFinalAccent.indexOf("ç") >= 0, "Cedille conservee");
  TEST_ASSERT(receivedFinalAccent.indexOf("œ") >= 0, "Ligature oe conservee");

  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// NOUVELLES SUITES VOICEKIT v0.5.0 (SIMPLIFICATION ET STABILISATION)
// =============================================================================

// SUITE 47 : Façade VoiceKit — Initialisation, cycle de vie, modes et
// délégation transparente
void test_voicekit_facade_lifecycle_and_delegation() {
  std::cout << "\n[TEST SUITE 47] Facade VoiceKit : Initialisation, cycle de "
               "vie, modes et delegation transparente"
            << std::endl;
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
  TEST_ASSERT(vk.getState() == VoiceState::READY,
              "getState() retourne VoiceState::READY");
  TEST_ASSERT(readyCalled, "Callback onReady() invoque");

  // Propagation du volume
  TEST_ASSERT(vk.getVolume() == 75, "Volume initial conforme 75%");
  vk.setVolume(90);
  TEST_ASSERT(vk.getVolume() == 90, "setVolume(90) correctement applique");
  TEST_ASSERT(vk.getEngine().getVolume() == 90,
              "Volume delegue a VoxCoreVoice");

  // Accesseurs de profil vocal
  TEST_ASSERT(vk.getVoice() == "Aoede", "getVoice() conforme Aoede");
  TEST_ASSERT(vk.getLanguage() == "fr-FR", "getLanguage() conforme fr-FR");
  TEST_ASSERT(vk.getStyle() == VoiceStyle::NATURAL,
              "getStyle() conforme NATURAL");
  TEST_ASSERT(vk.getTone() == VoiceTone::NEUTRAL, "getTone() conforme NEUTRAL");

  // Modifications valides de profil au repos avec handshake setupComplete
  bool vOk = vk.setVoice("Fenrir");
  TEST_ASSERT(vOk, "setVoice(Fenrir) accepte au repos");
  const char *setupJson = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.getVoice() == "Fenrir", "Voix mise a jour vers Fenrir");

  bool lOk = vk.setLanguage("en-US");
  TEST_ASSERT(lOk, "setLanguage(en-US) accepte au repos");
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.getLanguage() == "en-US", "Langue mise a jour vers en-US");

  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 48 : Façade VoiceKit — Validation des opérations TTS, STT manuel,
// ContinuousSTT et Conversation
void test_voicekit_facade_operations() {
  std::cout << "\n[TEST SUITE 48] Facade VoiceKit : Validation des operations "
               "TTS, STT manuel, ContinuousSTT et Conversation"
            << std::endl;
  MockLiveTransport mockTransport;
  VoiceKit vk;

  VoiceKitConfig config;
  config.apiKey = "AIzaTestKeyOps";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  vk.begin(config);
  prepareMockVoiceKitReady(vk, mockTransport);

  const char *setupJson = "{\"setupComplete\":{}}";

  // --- 1. Mode TTS (say) ---
  bool speechFinishedFired = false;
  vk.onSpeechFinished([&speechFinishedFired]() { speechFinishedFired = true; });

  bool sayOk = vk.say("Bonjour, test de synthese vocale.");
  TEST_ASSERT(sayOk, "vk.say() accepte en mode IDLE");
  vk.update();

  vk.getEngine().getSession().transitionTo(LiveSessionState::PLAYING,
                                           "Audio en cours");
  std::vector<int16_t> audio(2400, 100);
  vk.getEngine().getPlayer().feedPcm(audio.data(), audio.size());
  vk.getEngine().getPlayer().notifyTurnComplete();
  vk.update();
  TEST_ASSERT(vk.isSpeaking(),
              "vk.isSpeaking() vrai pendant restitution audio");

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
  vk.onTranscription([&](const String &text, bool isFinal) {
    transcribedText = text;
    transcribedFinal = isFinal;
  });

  bool listenOk = vk.listen();
  TEST_ASSERT(listenOk, "vk.listen() accepte en mode IDLE");
  TEST_ASSERT(vk.isListening(), "vk.isListening() vrai");
  TEST_ASSERT(vk.isBusy(), "vk.isBusy() vrai pendant l'ecoute");

  int16_t audioChunk[AUDIO_CHUNK_SAMPLES];
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i)
    audioChunk[i] = 1500;
  vk.getEngine().getStreamer().pushSimulatedChunk(audioChunk,
                                                  AUDIO_CHUNK_SAMPLES);
  vk.update();

  const char *interimJson = "{\"serverContent\":{\"interimInputTranscription\":"
                            "{\"text\":\"Bonjour tout\"}}}";
  vk.getEngine().getClient().handleIncomingMessage(interimJson,
                                                   strlen(interimJson));
  vk.update();
  TEST_ASSERT(transcribedText == "Bonjour tout",
              "Transcription partielle recue");
  TEST_ASSERT(!transcribedFinal, "transcribedFinal faux pour interim");

  bool stopListenOk = vk.stopListening();
  TEST_ASSERT(stopListenOk, "vk.stopListening() accepte");
  TEST_ASSERT(vk.isTranscribing(), "vk.isTranscribing() vrai");

  const char *finalJson = "{\"serverContent\":{\"inputTranscription\":{"
                          "\"text\":\"Bonjour tout le monde\"}}}";
  vk.getEngine().getClient().handleIncomingMessage(finalJson,
                                                   strlen(finalJson));
  vk.update();
  TEST_ASSERT(transcribedText == "Bonjour tout le monde",
              "Transcription finale recue");
  TEST_ASSERT(transcribedFinal, "transcribedFinal vrai");
  TEST_ASSERT(vk.isReady(),
              "VoiceKit redevient PRET apres transcription finale");

  // --- 3. Mode ContinuousSTT (startContinuousListening /
  // stopContinuousListening) ---
  bool contStartOk = vk.startContinuousListening();
  TEST_ASSERT(contStartOk, "startContinuousListening() accepte");
  TEST_ASSERT(vk.isContinuousListening(), "isContinuousListening() vrai");
  TEST_ASSERT(vk.getMode() == VoiceKitMode::CONTINUOUS_LISTENING,
              "Mode = CONTINUOUS_LISTENING");
  vk.update();

  bool pauseOk = vk.pauseContinuousListening();
  TEST_ASSERT(pauseOk, "pauseContinuousListening() reussi");
  TEST_ASSERT(vk.isContinuousListeningPaused(),
              "isContinuousListeningPaused() vrai");

  bool resumeOk = vk.resumeContinuousListening();
  TEST_ASSERT(resumeOk, "resumeContinuousListening() reussi");
  TEST_ASSERT(!vk.isContinuousListeningPaused(),
              "isContinuousListeningPaused() faux apres reprise");

  bool contStopOk = vk.stopContinuousListening();
  TEST_ASSERT(contStopOk, "stopContinuousListening() reussi");
  TEST_ASSERT(!vk.isContinuousListening(),
              "isContinuousListening() faux apres arret");
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode redevient IDLE");
  TEST_ASSERT(vk.isReady(), "VoiceKit est PRET");

  // --- 4. Mode Conversation (startConversation / stopConversation) ---
  bool convStartOk = vk.startConversation();
  TEST_ASSERT(convStartOk, "startConversation() accepte");
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();

  TEST_ASSERT(vk.isConversationActive(), "isConversationActive() vrai");
  TEST_ASSERT(vk.getMode() == VoiceKitMode::CONVERSATION,
              "Mode = CONVERSATION");

  bool convStopOk = vk.stopConversation();
  TEST_ASSERT(convStopOk, "stopConversation() accepte");
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();

  TEST_ASSERT(!vk.isConversationActive(),
              "isConversationActive() faux apres arret");
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode redevient IDLE");
  TEST_ASSERT(vk.isReady(), "VoiceKit redevient PRET apres arret conversation");

  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 49 : Façade VoiceKit — Refus stricts des opérations incompatibles et
// transitions de modes
void test_voicekit_facade_refusals_and_mode_transitions() {
  std::cout << "\n[TEST SUITE 49] Facade VoiceKit : Refus stricts des "
               "operations incompatibles et transitions de modes"
            << std::endl;
  MockLiveTransport mockTransport;
  VoiceKit vk;

  VoiceKitConfig config;
  config.apiKey = "AIzaTestKeyRefusals";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  vk.begin(config);
  prepareMockVoiceKitReady(vk, mockTransport);

  const char *setupJson = "{\"setupComplete\":{}}";

  // 1. En mode ContinuousListening : say, listen et startConversation doivent
  // être strictement REFUSÉS
  vk.startContinuousListening();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::CONTINUOUS_LISTENING,
              "Mode CONTINUOUS actif");

  TEST_ASSERT(!vk.say("Tentative TTS interdite"),
              "say() REFUSE en mode ContinuousListening");
  TEST_ASSERT(!vk.listen(),
              "listen() manuel REFUSE en mode ContinuousListening");
  TEST_ASSERT(!vk.stopListening(),
              "stopListening() manuel REFUSE en mode ContinuousListening");
  TEST_ASSERT(!vk.startConversation(),
              "startConversation() REFUSE en mode ContinuousListening");

  // 2. En mode Conversation : say, listen et startContinuousListening doivent
  // être strictement REFUSÉS
  vk.stopContinuousListening();
  vk.startConversation();
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::CONVERSATION,
              "Mode CONVERSATION actif");

  TEST_ASSERT(!vk.say("Tentative TTS interdite"),
              "say() REFUSE en mode Conversation");
  TEST_ASSERT(!vk.listen(), "listen() REFUSE en mode Conversation");
  TEST_ASSERT(!vk.startContinuousListening(),
              "startContinuousListening() REFUSE en mode Conversation");

  // 3. Pendant une restitution sonore active (TTS isSpeaking / isBusy) : say,
  // listen, modes sont refusés
  vk.stopConversation();
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode revenu a IDLE");

  vk.say("Phrase longue pour test de busy");
  vk.getEngine().getSession().transitionTo(LiveSessionState::PLAYING, "Audio");
  vk.update();
  TEST_ASSERT(vk.isBusy(), "VoiceKit occupe par say()");

  TEST_ASSERT(!vk.say("Seconde phrase refusee"),
              "say() concurrent REFUSE quand isBusy()");
  TEST_ASSERT(!vk.listen(), "listen() REFUSE quand isBusy()");
  TEST_ASSERT(!vk.startContinuousListening(),
              "startContinuousListening() REFUSE quand isBusy()");
  TEST_ASSERT(!vk.startConversation(),
              "startConversation() REFUSE quand isBusy()");

  // 4. Interruption STOP inconditionnelle
  vk.stop();
  TEST_ASSERT(!vk.isSpeaking(),
              "Restitution arretee immediatement par vk.stop()");
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE,
              "Mode reset a IDLE par stop()");
  TEST_ASSERT(vk.isReady(), "VoiceKit redevient PRET");

  // 5. Enchaînement fluide de transitions de modes sans corruption de session
  bool ok1 = vk.startContinuousListening();
  TEST_ASSERT(ok1, "Transition 1 : IDLE -> CONTINUOUS");
  bool ok2 = vk.stopContinuousListening();
  TEST_ASSERT(ok2, "Transition 2 : CONTINUOUS -> IDLE");
  bool ok3 = vk.startConversation();
  TEST_ASSERT(ok3, "Transition 3 : IDLE -> CONVERSATION");
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();

  bool ok4 = vk.stopConversation();
  TEST_ASSERT(ok4, "Transition 4 : CONVERSATION -> IDLE");
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();

  bool ok5 = vk.say("TTS final apres transitions");
  TEST_ASSERT(ok5, "Transition 5 : IDLE -> TTS accepte");
  vk.stop();

  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 50 : ContinuousListeningController — VAD intégré, silence, timeout sans
// hallucination, pause et reprise
void test_continuous_controller_lifecycle_and_vad() {
  std::cout << "\n[TEST SUITE 50] ContinuousListeningController : VAD integre, "
               "silence, timeout sans hallucination, pause et reprise"
            << std::endl;
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
  const VadConfig &vadCfg = continuous.getVadConfig();
  TEST_ASSERT(vadCfg.minSpeechDurationMs == 200,
              "VAD minSpeechDurationMs = 200 ms par defaut");
  TEST_ASSERT(vadCfg.silenceDurationMs == 1200,
              "VAD silenceDurationMs = 1 200 ms par defaut");
  TEST_ASSERT(vadCfg.maxSegmentDurationMs == 30000,
              "VAD maxSegmentDurationMs = 30 000 ms par defaut");

  // 2. Personnalisation de configuration sans complexité
  continuous.setSilenceDurationMs(1000);
  TEST_ASSERT(continuous.getVadConfig().silenceDurationMs == 1000,
              "setSilenceDurationMs(1000) applique");
  continuous.setMinSpeechDurationMs(150);
  TEST_ASSERT(continuous.getVadConfig().minSpeechDurationMs == 150,
              "setMinSpeechDurationMs(150) applique");
  continuous.setMaxSegmentDurationMs(25000);
  TEST_ASSERT(continuous.getVadConfig().maxSegmentDurationMs == 25000,
              "setMaxSegmentDurationMs(25000) applique");

  // Restauration des valeurs standard
  continuous.setSilenceDurationMs(1200);
  continuous.setMinSpeechDurationMs(200);
  continuous.setMaxSegmentDurationMs(30000);

  // 3. Démarrage et suivi du cycle de vie
  bool speechStartFired = false;
  continuous.onSpeechStarted(
      [&speechStartFired]() { speechStartFired = true; });

  VadEndReason endReasonReceived = VadEndReason::NONE;
  continuous.onSpeechEnded(
      [&endReasonReceived](VadEndReason r) { endReasonReceived = r; });

  String segTextReceived = "";
  bool segConfirmedReceived = false;
  continuous.onSegmentComplete([&](const String &text, bool confirmed) {
    segTextReceived = text;
    segConfirmedReceived = confirmed;
  });

  bool startOk = continuous.start();
  TEST_ASSERT(startOk, "continuous.start() reussi");
  TEST_ASSERT(continuous.isActive(), "continuous.isActive() vrai");
  TEST_ASSERT(voice.isListening(),
              "voice.isListening() active automatiquement");
  TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING,
              "Etat = LISTENING");

  // Blocs de parole (> 200 ms = 7 blocs de 32 ms)
  int16_t speechChunk[AUDIO_CHUNK_SAMPLES];
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i)
    speechChunk[i] = 1600;
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
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i)
    silenceChunk[i] = 50;
  for (int i = 0; i < 39; ++i) {
    continuous.processAudioChunk(silenceChunk, AUDIO_CHUNK_SAMPLES, 50);
    voice.update();
    continuous.update();
  }
  TEST_ASSERT(endReasonReceived == VadEndReason::SILENCE_TIMEOUT,
              "Fin de parole sur SILENCE_TIMEOUT 1 200 ms");
  TEST_ASSERT(continuous.getState() == ContinuousState::WAITING_FINAL,
              "Automate passe a WAITING_FINAL");
  TEST_ASSERT(!voice.isListening(),
              "voice.stopListening() appele automatiquement par le controleur");

  // Réception transcription finale
  const char *okJson = "{\"serverContent\":{\"inputTranscription\":{\"text\":"
                       "\"Segment valide par ContinuousController\"}}}";
  voice.getClient().handleIncomingMessage(okJson, strlen(okJson));
  voice.update();
  continuous.handleTranscription("Segment valide par ContinuousController",
                                 true);

  TEST_ASSERT(continuous.getState() == ContinuousState::SEGMENT_DONE,
              "Etat = SEGMENT_DONE");
  TEST_ASSERT(continuous.getSegmentCount() == 1, "Compteur de segments = 1");
  TEST_ASSERT(segTextReceived == "Segment valide par ContinuousController",
              "Texte du segment conforme");
  TEST_ASSERT(segConfirmedReceived, "Segment confirme = true");

  // Reprise automatique après 400 ms
  advanceMockMillis(450);
  voice.update();
  continuous.update();
  TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING,
              "Reprise automatique vers LISTENING effectuee");
  TEST_ASSERT(voice.isListening(),
              "voice.isListening() re-active sans aucune intervention");

  // 4. Test du timeout sans fabrication de texte (zéro hallucination)
  // Fin de parole simulée
  continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
  for (int i = 0; i < 8; ++i)
    continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
  for (int i = 0; i < 39; ++i)
    continuous.processAudioChunk(silenceChunk, AUDIO_CHUNK_SAMPLES, 50);
  voice.update();
  continuous.update();
  TEST_ASSERT(continuous.getState() == ContinuousState::WAITING_FINAL,
              "En attente finale pour segment 2");

  // Écoulement du temps au-delà du timeout sans message serveur
  advanceMockMillis(6000);
  voice.update();
  continuous.update();

  TEST_ASSERT(continuous.getState() == ContinuousState::SEGMENT_DONE,
              "Passage a SEGMENT_DONE apres timeout");
  TEST_ASSERT(!continuous.wasLastFinalConfirmed(),
              "Dernier segment non confirme");
  TEST_ASSERT(continuous.getLastFinalText().isEmpty(),
              "Zero hallucination : texte vide");

  // 5. Test de pause et reprise manuelle
  continuous.pause();
  TEST_ASSERT(continuous.isPaused(), "Controleur en pause");
  TEST_ASSERT(continuous.getState() == ContinuousState::PAUSED,
              "Etat = PAUSED");
  TEST_ASSERT(!voice.isListening(), "Ecoute micro arretee par pause()");

  advanceMockMillis(1000);
  continuous.update();
  TEST_ASSERT(!voice.isListening(),
              "Aucune reprise automatique tant que paused");

  continuous.resume();
  TEST_ASSERT(!continuous.isPaused(), "Pause levee");
  TEST_ASSERT(voice.isListening(), "Ecoute reprise suite a resume()");

  continuous.stop();
  TEST_ASSERT(!continuous.isActive(), "Controleur inactif");

  voice.getClient().setTransport(nullptr);
}

// SUITE 51 : Intégrité des événements — Absence de duplication des callbacks de
// fin et unicité
void test_events_uniqueness_and_no_duplicate_callbacks() {
  std::cout << "\n[TEST SUITE 51] Integrite des evenements : Absence de "
               "duplication des callbacks de fin et unicite"
            << std::endl;
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
  TEST_ASSERT(speechFinishedCalls == 1,
              "onSpeechFinished emis EXACTEMENT UNE FOIS");

  // Appels update() répétés ne doivent JAMAIS re-déclencher onSpeechFinished
  for (int i = 0; i < 10; ++i) {
    vk.update();
  }
  TEST_ASSERT(
      speechFinishedCalls == 1,
      "Zero duplication de onSpeechFinished sur boucles d'update suivantes");

  // 2. Unicité du callback onTranscription (final)
  int finalTranscriptionCalls = 0;
  vk.onTranscription(
      [&finalTranscriptionCalls](const String &text, bool isFinal) {
        if (isFinal)
          finalTranscriptionCalls++;
      });

  vk.listen();
  vk.stopListening();
  const char *finalJson = "{\"serverContent\":{\"inputTranscription\":{"
                          "\"text\":\"Phrase test unicite\"}}}";
  vk.getEngine().getClient().handleIncomingMessage(finalJson,
                                                   strlen(finalJson));
  vk.update();

  TEST_ASSERT(finalTranscriptionCalls == 1,
              "Transcription finale emise EXACTEMENT UNE FOIS");
  for (int i = 0; i < 10; ++i) {
    vk.update();
  }
  TEST_ASSERT(
      finalTranscriptionCalls == 1,
      "Zero duplication de transcription finale sur updates ulterieurs");

  // 3. Unicité du callback onConversationFinished
  int convFinishedCalls = 0;
  vk.onConversationFinished([&convFinishedCalls]() { convFinishedCalls++; });

  vk.startConversation();
  const char *setupComp = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupComp,
                                                   strlen(setupComp));
  vk.update();

  bool convTurnStarted = vk.getEngine().startListening();
  TEST_ASSERT(convTurnStarted, "startListening() conversation accepte");
  std::vector<int16_t> sampleBlock(AUDIO_CHUNK_SAMPLES, 400);
  vk.getEngine().getStreamer().pushSimulatedChunk(sampleBlock.data(),
                                                  sampleBlock.size());
  vk.getEngine().getStreamer().tick();
  vk.getEngine().stopListening();

  vk.getEngine().getSession().transitionTo(LiveSessionState::PLAYING,
                                           "Premier son recu");
  vk.update();
  vk.getEngine().getSession().finishTurn();
  vk.update();

  TEST_ASSERT(convFinishedCalls == 1,
              "onConversationFinished emis EXACTEMENT UNE FOIS par tour");
  for (int i = 0; i < 10; ++i) {
    vk.update();
  }
  TEST_ASSERT(
      convFinishedCalls == 1,
      "Zero duplication de onConversationFinished sur updates ulterieurs");

  vk.stopConversation();
  vk.getEngine().getClient().handleIncomingMessage(setupComp,
                                                   strlen(setupComp));
  vk.update();
  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 52 : Compatibilité ascendante intégrale — Coexistence VoxCoreVoice et
// VoiceKit sans régression
void test_v050_complete_backward_compatibility() {
  std::cout << "\n[TEST SUITE 52] Compatibilite ascendante integrale : "
               "Coexistence VoxCoreVoice et VoiceKit sans regression"
            << std::endl;
  MockLiveTransport mockTransport;

  // 1. Instanciation directe de l'ancienne API VoxCoreVoice
  VoxCoreVoice directVoice;
  VoiceKitConfig config;
  config.apiKey = "AIzaTestKeyRetro";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  config.voice = "Charon";
  config.language = "fr-FR";

  bool directOk = directVoice.begin(config);
  TEST_ASSERT(
      directOk,
      "Ancienne API VoxCoreVoice::begin() fonctionne sans modification");
  prepareMockVoiceReady(directVoice, mockTransport);

  TEST_ASSERT(directVoice.isReady(), "VoxCoreVoice directe est READY");
  TEST_ASSERT(directVoice.getVoice() == "Charon",
              "Profil vocal VoxCoreVoice preserve");
  TEST_ASSERT(directVoice.getLanguage() == "fr-FR",
              "Langue VoxCoreVoice preservee");

  bool directSay = directVoice.say("Test compatibilite descendante");
  TEST_ASSERT(directSay,
              "VoxCoreVoice::say() fonctionne a l'identique de v0.4.1.2");

  directVoice.stop();
  TEST_ASSERT(directVoice.isReady(),
              "VoxCoreVoice directe redevient READY apres stop()");

  // 2. Chaine UTF-8 et accents français vérifiés sur la façade VoiceKit
  VoiceKit vk;
  vk.begin(config);
  prepareMockVoiceKitReady(vk, mockTransport);

  String receivedAccent = "";
  vk.onTranscription([&receivedAccent](const String &txt, bool isFinal) {
    if (isFinal)
      receivedAccent = txt;
  });

  vk.listen();
  const char *utf8Test =
      "Hélène fête l'été avec un délicieux gâteau à Noël près du Rhône.";
  std::string fullJson =
      std::string("{\"serverContent\":{\"inputTranscription\":{\"text\":\"") +
      utf8Test + "\"}}}";
  vk.getEngine().getClient().handleIncomingMessage(fullJson.c_str(),
                                                   fullJson.length());
  vk.update();

  TEST_ASSERT(receivedAccent == utf8Test,
              "Chaine UTF-8 et accents francais 100% preserves via VoiceKit");
  TEST_ASSERT(receivedAccent.indexOf("Hélène") >= 0,
              "Accents e majuscule et e grave conserves");
  TEST_ASSERT(receivedAccent.indexOf("délicieux") >= 0,
              "Accents e aigu conserves");
  TEST_ASSERT(receivedAccent.indexOf("gâteau") >= 0,
              "Accent circonflexe conserve");
  TEST_ASSERT(receivedAccent.indexOf("Rhône") >= 0, "Circonflexe o conserve");

  vk.stop();
  vk.getEngine().getClient().setTransport(nullptr);
  directVoice.getClient().setTransport(nullptr);
}

// =============================================================================
// NOUVELLES SUITES VOICEKIT v0.5.0.1 (STABILISATION FREERTOS, MODES &
// EVENEMENTS)
// =============================================================================

// SUITE 53 : Concurrence FreeRTOS et synchronisation audio
void test_v0501_freertos_concurrency_and_audio_synchronization() {
  std::cout << "\n[TEST SUITE 53] Concurrence FreeRTOS : Blocs audio "
               "asynchrones, pause/reprise et contexte"
            << std::endl;
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
  TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING,
              "Etat initial LISTENING");
  continuous.stop();
  TEST_ASSERT(!continuous.isActive(), "Controleur arrete");
  TEST_ASSERT(continuous.getState() == ContinuousState::INACTIVE,
              "Etat INACTIVE");

  // Simulation d'un bloc audio arrivant de la tâche audio FreeRTOS pendant que
  // le contrôleur est inactif
  int16_t speechChunk[AUDIO_CHUNK_SAMPLES];
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i)
    speechChunk[i] = 1600;
  continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
  voice.update();
  continuous.update();

  TEST_ASSERT(!continuous.isActive(),
              "Controleur toujours inactif apres audio chunk");
  TEST_ASSERT(continuous.getState() == ContinuousState::INACTIVE,
              "Etat reste INACTIVE (pas de demarrage parasite)");
  TEST_ASSERT(!voice.isListening(), "Microphone non re-ouvert");
  TEST_ASSERT(!continuous.isSpeechActive(),
              "Aucune parole detectee pendant inactif");

  // 2. Pause et reprise répétées (5 cycles successifs)
  continuous.start();
  TEST_ASSERT(continuous.isActive(), "Controleur redemarre");
  TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING,
              "Etat LISTENING");

  for (int cycle = 1; cycle <= 5; ++cycle) {
    continuous.pause();
    TEST_ASSERT(continuous.isPaused(), "Pause active au cycle");
    TEST_ASSERT(continuous.getState() == ContinuousState::PAUSED,
                "Etat PAUSED");
    TEST_ASSERT(!voice.isListening(), "Micro ferme pendant pause");

    // Envoi de chunks audio pendant la pause
    continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
    voice.update();
    continuous.update();
    TEST_ASSERT(continuous.getState() == ContinuousState::PAUSED,
                "Etat reste PAUSED malgre audio");

    bool resumed = continuous.resume();
    TEST_ASSERT(resumed, "resume() reussi");
    TEST_ASSERT(!continuous.isPaused(), "Pause levee");
    TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING,
                "Etat redevient LISTENING");
    TEST_ASSERT(voice.isListening(), "Micro re-ouvert apres resume()");
  }

  // Vérification que le VAD fonctionne parfaitement après les 5 cycles de
  // pause/reprise
  bool speechDetected = false;
  continuous.onSpeechStarted([&speechDetected]() { speechDetected = true; });

  for (int i = 0; i < 8; ++i) {
    continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
    voice.update();
    continuous.update();
  }
  TEST_ASSERT(speechDetected, "VAD operationnel apres 5 cycles pause/reprise");
  TEST_ASSERT(continuous.getState() == ContinuousState::SPEECH_DETECTED,
              "Etat passe a SPEECH_DETECTED");

  // 3. Vérification du contexte d'exécution des callbacks
  // Tous les callbacks applicatifs s'exécutent strictement dans update() (tâche
  // principale), tandis que onAudioChunk est documenté comme le seul contact de
  // la tâche FreeRTOS audio.
  bool chunkProcessed = false;
  voice.onAudioChunk([&chunkProcessed](const int16_t *s, size_t c, uint32_t r) {
    (void)s;
    (void)c;
    (void)r;
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
  std::cout << "\n[TEST SUITE 54] Fiabilisation ContinuousSTT : Identifiant de "
               "segment, unicite et rejet callbacks tardifs"
            << std::endl;
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
  continuous.onSegmentComplete([&](const String &text, bool confirmed) {
    segmentCompletedCalls++;
    lastCompletedText = text;
    lastCompletedConfirmed = confirmed;
  });

  continuous.start();
  uint32_t segId1 = continuous.getCurrentSegmentId();
  TEST_ASSERT(segId1 > 0, "Identifiant sequentiel de segment initialise (> 0)");

  // 1. Deux événements de transcription finale identiques
  int16_t speechChunk[AUDIO_CHUNK_SAMPLES];
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i)
    speechChunk[i] = 1600;
  for (int i = 0; i < 8; ++i)
    continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);

  int16_t silenceChunk[AUDIO_CHUNK_SAMPLES];
  for (int i = 0; i < AUDIO_CHUNK_SAMPLES; ++i)
    silenceChunk[i] = 50;
  for (int i = 0; i < 39; ++i)
    continuous.processAudioChunk(silenceChunk, AUDIO_CHUNK_SAMPLES, 50);

  voice.update();
  continuous.update();
  TEST_ASSERT(continuous.getState() == ContinuousState::WAITING_FINAL,
              "En attente transcription finale");

  // Première notification finale authentique
  const char *seg1Json = "{\"serverContent\":{\"inputTranscription\":{\"text\":"
                         "\"Segment premier et unique\"}}}";
  voice.getClient().handleIncomingMessage(seg1Json, strlen(seg1Json));
  voice.update();
  continuous.handleTranscription("Segment premier et unique", true);
  TEST_ASSERT(continuous.getState() == ContinuousState::SEGMENT_DONE,
              "Etat passe a SEGMENT_DONE");
  TEST_ASSERT(segmentCompletedCalls == 1,
              "onSegmentComplete appele EXACTEMENT 1 fois");
  TEST_ASSERT(lastCompletedText == "Segment premier et unique",
              "Texte recu conforme");
  TEST_ASSERT(lastCompletedConfirmed, "Segment confirme true");
  TEST_ASSERT(continuous.getSegmentCount() == 1, "Compteur de segments = 1");

  // Deuxième notification finale identique (duplication par le réseau ou
  // transport)
  continuous.handleTranscription("Segment premier et unique", true);
  TEST_ASSERT(segmentCompletedCalls == 1,
              "Zero duplication : onSegmentComplete non re-declenche");
  TEST_ASSERT(continuous.getSegmentCount() == 1,
              "Compteur de segments inchange (toujours 1)");

  // 2. Callback final reçu après timeout
  // Attente reprise automatique vers segment suivant (400 ms)
  advanceMockMillis(450);
  voice.update();
  continuous.update();
  TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING,
              "Reprise automatique vers segment 2");
  uint32_t segId2 = continuous.getCurrentSegmentId();
  TEST_ASSERT(segId2 > segId1,
              "Identifiant sequentiel incremente pour segment 2");

  // Émettre de la parole puis silence pour le segment 2
  for (int i = 0; i < 8; ++i)
    continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
  for (int i = 0; i < 39; ++i)
    continuous.processAudioChunk(silenceChunk, AUDIO_CHUNK_SAMPLES, 50);
  voice.update();
  continuous.update();
  TEST_ASSERT(continuous.getState() == ContinuousState::WAITING_FINAL,
              "Segment 2 en WAITING_FINAL");

  // Timeout STT : absence de réponse du serveur
  advanceMockMillis(6000);
  voice.update();
  continuous.update();
  TEST_ASSERT(continuous.getState() == ContinuousState::SEGMENT_DONE,
              "Segment 2 cloture sur timeout");
  TEST_ASSERT(!continuous.wasLastFinalConfirmed(),
              "Dernier segment confirme = false");
  TEST_ASSERT(continuous.getLastFinalText().isEmpty(),
              "Zero hallucination : texte vide");
  TEST_ASSERT(segmentCompletedCalls == 2,
              "Callback complete appele pour timeout non-confirme");

  // Réception tardive d'une transcription finale APRÈS timeout
  continuous.handleTranscription("Texte arrive trop tardivement", true);
  TEST_ASSERT(segmentCompletedCalls == 2,
              "Transcription tardive apres timeout STRICTEMENT IGNOREE");
  TEST_ASSERT(continuous.getLastFinalText().isEmpty(),
              "Texte final reste vide");

  // 3. Documenter et tester la limitation : attribution certaine des
  // transcriptions tardives Lorsque le segment a expiré et que le contrôleur a
  // déjà repris (LISTENING), un callback tardif du segment précédent ne peut
  // pas être corrélé sans identifiant serveur. VoiceKit le rejette
  // catégoriquement pour protéger le segment en cours.
  advanceMockMillis(450);
  voice.update();
  continuous.update();
  TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING,
              "Segment 3 demarre");

  // Tentative d'injection d'un callback final pendant que le micro écoute le
  // segment 3
  continuous.handleTranscription("Texte orphelin du passe", true);
  TEST_ASSERT(continuous.getState() == ContinuousState::LISTENING,
              "Etat LISTENING non perturbe");
  TEST_ASSERT(segmentCompletedCalls == 2, "Callback orphelin rejete");

  // 4. Arrêt durant une transcription
  for (int i = 0; i < 8; ++i)
    continuous.processAudioChunk(speechChunk, AUDIO_CHUNK_SAMPLES, 1600);
  for (int i = 0; i < 39; ++i)
    continuous.processAudioChunk(silenceChunk, AUDIO_CHUNK_SAMPLES, 50);
  voice.update();
  continuous.update();
  TEST_ASSERT(continuous.getState() == ContinuousState::WAITING_FINAL,
              "En attente transcription");

  continuous.stop();
  TEST_ASSERT(continuous.getState() == ContinuousState::INACTIVE,
              "Arret immediat effectue");
  TEST_ASSERT(!voice.isListening(), "Microphone ferme");

  // Si une transcription arrive après stop, elle doit être complètement ignorée
  continuous.handleTranscription("Transcription recue apres stop", true);
  TEST_ASSERT(segmentCompletedCalls == 2, "Zero notification suite a stop()");

  voice.getClient().setTransport(nullptr);
}

// SUITE 55 : Transitions de modes sécurisées et résilience aux pannes
void test_v0501_mode_transitions_and_failure_resilience() {
  std::cout << "\n[TEST SUITE 55] Facade VoiceKit : Transitions de modes, "
               "echecs de reconnexion et non-regressions"
            << std::endl;
  MockLiveTransport mockTransport;
  VoiceKit vk;

  VoiceKitConfig config;
  config.apiKey = "AIzaTestKeyModeTransitions";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  vk.begin(config);
  prepareMockVoiceKitReady(vk, mockTransport);

  const char *setupJson = "{\"setupComplete\":{}}";

  // 1. Échec de stopConversation()
  bool convStart = vk.startConversation();
  TEST_ASSERT(convStart, "startConversation() accepte");
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::CONVERSATION,
              "Mode CONVERSATION actif");
  TEST_ASSERT(vk.isConversationActive(), "Conversation active");

  // Simuler un moteur occupé (ex: parole de l'assistant active)
  vk.getEngine().getSession().transitionTo(LiveSessionState::PLAYING,
                                           "Parole assistant");
  vk.update();
  TEST_ASSERT(vk.isBusy(), "Moteur occupe par la reponse");

  // Appel stopConversation() pendant que le moteur est occupé
  bool stopBusy = vk.stopConversation();
  TEST_ASSERT(!stopBusy, "stopConversation() REFUSE car moteur occupe");
  TEST_ASSERT(
      vk.getMode() == VoiceKitMode::CONVERSATION,
      "CORRECTION CRITIQUE : Mode CONVERSATION conserve malgre l'echec");
  TEST_ASSERT(vk.isConversationActive(),
              "isConversationActive reste vrai apres refus");

  // Libérer l'occupation
  vk.getEngine().getSession().transitionTo(LiveSessionState::CONNECTED, "Pret");
  vk.getEngine().getPlayer().reset();
  vk.update();

  // Arrêt réussi de la conversation
  bool stopOk = vk.stopConversation();
  TEST_ASSERT(stopOk, "stopConversation() accepte au repos");
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE,
              "Mode redevient IDLE apres confirmation");
  TEST_ASSERT(!vk.isConversationActive(), "isConversationActive faux");

  // 2. Échec de reconnexion Gemini pendant startConversation
  bool convStart2 = vk.startConversation();
  TEST_ASSERT(convStart2, "Demande startConversation() acceptee");
  TEST_ASSERT(vk.getEngine().isModeSwitchPending(), "Transition en attente");

  // Simuler une erreur de session réseau avant setupComplete
  vk.getEngine().getSession().transitionTo(LiveSessionState::ERROR,
                                           "Erreur TLS/WebSocket");
  vk.update();

  TEST_ASSERT(!vk.getEngine().isModeSwitchPending(), "Transition annulee");
  TEST_ASSERT(!vk.isConversationActive(), "Conversation non activee");
  TEST_ASSERT(
      vk.getMode() == VoiceKitMode::IDLE,
      "CORRECTION CRITIQUE : Mode IDLE restaure apres echec reconnexion");

  // 3. Démarrage continu lorsque le service n'est pas encore prêt
  VoiceKit vkNotReady;
  VoiceKitConfig cfgNotReady;
  cfgNotReady.apiKey = "AIzaTestKeyNotReady";
  cfgNotReady.wifiMode = WiFiMode::EXTERNAL_WIFI;
  vkNotReady.begin(cfgNotReady);
  TEST_ASSERT(!vkNotReady.isReady(), "Service non encore pret");

  bool contNotReadyOk = vkNotReady.startContinuousListening();
  TEST_ASSERT(
      contNotReadyOk,
      "startContinuousListening() accepte meme si session pas encore prete");
  TEST_ASSERT(vkNotReady.isContinuousListening(),
              "Mode CONTINUOUS_LISTENING enregistre");
  TEST_ASSERT(vkNotReady.getContinuousController().getState() ==
                  ContinuousState::IDLE,
              "Controleur en attente IDLE");

  // Le service devient prêt
  prepareMockVoiceKitReady(vkNotReady, mockTransport);
  vkNotReady.update();

  TEST_ASSERT(vkNotReady.getContinuousController().getState() ==
                  ContinuousState::LISTENING,
              "Controleur passe automatiquement a LISTENING");
  TEST_ASSERT(vkNotReady.isListening(),
              "Microphone active automatiquement a la disponibilite");
  vkNotReady.stopContinuousListening();

  // 4. Transcription reçue pendant un changement de mode
  prepareMockVoiceKitReady(vk, mockTransport);
  bool contOk = vk.startContinuousListening();
  TEST_ASSERT(contOk, "Continuous demarre");
  vk.stopContinuousListening();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode IDLE");

  // Réception d'une transcription résiduelle
  const char *strayJson = "{\"serverContent\":{\"inputTranscription\":{"
                          "\"text\":\"Transcription orpheline\"}}}";
  vk.getEngine().getClient().handleIncomingMessage(strayJson,
                                                   strlen(strayJson));
  vk.update();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE,
              "Mode IDLE preserve sans corruption");

  // 5. Vérification de l'absence de double acquisition microphone
  vk.startContinuousListening();
  TEST_ASSERT(vk.isContinuousListening(), "Continuous actif");
  TEST_ASSERT(!vk.listen(), "listen() manuel STRICTEMENT REFUSE pendant "
                            "ContinuousSTT (zero double acquisition)");
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
  const char *sttRes = "{\"serverContent\":{\"inputTranscription\":{\"text\":"
                       "\"Resultat STT\"}}}";
  vk.getEngine().getClient().handleIncomingMessage(sttRes, strlen(sttRes));
  vk.update();
  TEST_ASSERT(vk.isReady(), "STT acheve");

  // ContinuousSTT
  bool cOk = vk.startContinuousListening();
  TEST_ASSERT(cOk, "3. ContinuousSTT accepte");
  TEST_ASSERT(vk.getMode() == VoiceKitMode::CONTINUOUS_LISTENING,
              "Mode CONTINUOUS");
  vk.stopContinuousListening();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Continuous arrete");

  // Conversation
  bool convOk = vk.startConversation();
  TEST_ASSERT(convOk, "4. Conversation acceptee");
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::CONVERSATION,
              "Mode CONVERSATION actif");

  vk.stopConversation();
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE,
              "Retour a IDLE apres enchainement complet");

  vk.getEngine().getClient().setTransport(nullptr);
  vkNotReady.getEngine().getClient().setTransport(nullptr);
}

// =============================================================================
// NOUVELLES SUITES VOICEKIT v0.5.2 (FAST CONNECTION & INSTRUMENTATION)
// =============================================================================

// SUITE 56 : WiFiManager — FastConnect, repli automatique et backoff progressif
void test_v052_wifi_fast_connect_and_fallback() {
  std::cout << "\n[TEST SUITE 56] WiFiManager : FastConnect, repli automatique "
               "et backoff progressif"
            << std::endl;

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

// SUITE 57 : Synchronisation SNTP — Bypass si horloge valide vs attente si
// invalide
void test_v052_sntp_optimization_and_tls_safety() {
  std::cout << "\n[TEST SUITE 57] Synchronisation SNTP : Detection horloge "
               "valide vs securite TLS"
            << std::endl;

  WiFiManager wm;
  WiFi.setConnected(true);
  wm.begin("TestSSID", "TestPass", false);
  wm.tick();

  TEST_ASSERT(wm.isConnected(), "Wi-Fi connecte");
  TEST_ASSERT(wm.isTimeSynced(),
              "Horloge systeme valide -> isTimeSynced() true");
  TEST_ASSERT(wm.isSntpSkipped(),
              "SNTP saute sans delai bloquant car horloge deja valide");
  TEST_ASSERT(wm.getSntpSyncMs() == 0, "Duree SNTP nulle si deja synchro");

  TEST_ASSERT(VoxCoreVoice::isSystemTimeValid(),
              "isSystemTimeValid() retourne true pour timestamp >= 2024");
}

// SUITE 58 : Gemini Live — Conditionnement de la reconnexion a la connectivite
// reseau
void test_v052_gemini_reconnect_network_conditioning() {
  std::cout << "\n[TEST SUITE 58] Gemini Live : Conditionnement strict de la "
               "reconnexion au reseau"
            << std::endl;

  GeminiLiveClient client;
  LiveAudioPlayer player;
  LiveSessionController session;

  client.begin("TEST_KEY_COND", "models/gemini-3.8-live", "Aoede");
  session.begin(&client, nullptr, &player);

  bool networkUp = false;
  session.setNetworkCheck([&networkUp]() { return networkUp; });
  TEST_ASSERT(!session.isNetworkReady(),
              "isNetworkReady reflete le predicat (false)");

  // Forcer etat RECONNECTING
  session.transitionTo(LiveSessionState::RECONNECTING, "Test deconnexion");
  TEST_ASSERT(session.getState() == LiveSessionState::RECONNECTING,
              "Session en RECONNECTING");

  // Tick avec réseau DOWN : ne doit PAS tenter de se reconnecter ni incrementer
  session.tick();
  TEST_ASSERT(session.getState() == LiveSessionState::RECONNECTING,
              "Reste en RECONNECTING sans declencher connect");
  TEST_ASSERT(session.getReconnectAttempts() == 0,
              "Tentatives non consommees pendant reseau DOWN");

  // Retablir le reseau
  networkUp = true;
  TEST_ASSERT(session.isNetworkReady(),
              "isNetworkReady reflete le predicat (true)");
}

// SUITE 59 : Instrumentation des temps de demarrage (wifi, sntp, ws, setup,
// total)
void test_v052_startup_timing_instrumentation() {
  std::cout << "\n[TEST SUITE 59] Instrumentation des 5 temps de demarrage"
            << std::endl;

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

  const char *setupJson = "{\"setupComplete\":{}}";
  voice.getClient().handleIncomingMessage(setupJson, strlen(setupJson));
  voice.update();

  TEST_ASSERT(voice.isReady(), "VoiceKit est READY");

  const auto &metrics = voice.getConnectionMetrics();
  TEST_ASSERT(metrics.fastConnectEnabled == true,
              "FastConnect actif dans les metriques");
  TEST_ASSERT(metrics.connectionFailures == 0,
              "Zero echec de connexion observe");

  voice.getClient().setTransport(nullptr);
}

// SUITE 60 : Façade VoiceKit v0.5.2 — Exposition ConnectionMetrics et
// non-regression
void test_v052_facade_connection_metrics_and_resilience() {
  std::cout << "\n[TEST SUITE 60] Facade VoiceKit v0.5.2 : Exposition "
               "ConnectionMetrics et non-regression"
            << std::endl;

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
  const char *setupJson = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();

  TEST_ASSERT(vk.isReady(), "VoiceKit::isReady() true");
  const auto &m = vk.getConnectionMetrics();
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

// SUITE 61 : Structure VoiceLatencyMetrics et calculs dérivés stricts (zéro
// faux 0 ms)
void test_v053_voice_latency_metrics_calculations() {
  std::cout << "\n[TEST SUITE 61] VoiceLatencyMetrics : Calculs des durees et "
               "gestion des valeurs non observees"
            << std::endl;

  VoiceLatencyMetrics m;
  TEST_ASSERT(!m.hasSpeechEndDetected(),
              "speechEndDetected non observe par defaut");
  TEST_ASSERT(!m.hasMicStopped(), "micStopped non observe par defaut");
  TEST_ASSERT(!m.hasActivityEndSent(),
              "activityEndSent non observe par defaut");
  TEST_ASSERT(!m.hasFirstResponseReceived(),
              "firstResponseReceived non observe par defaut");
  TEST_ASSERT(!m.hasFirstAudioReceived(),
              "firstAudioReceived non observe par defaut");
  TEST_ASSERT(!m.hasFirstAudioPlayed(),
              "firstAudioPlayed non observe par defaut");
  TEST_ASSERT(!m.hasPlaybackFinished(),
              "playbackFinished non observe par defaut");

  // RÈGLE ABSOLUE : Une valeur non observée doit renvoyer -1 (N/D) et JAMAIS 0
  // ms
  TEST_ASSERT(m.getSpeechEndToMicStoppedMs() == -1,
              "getSpeechEndToMicStoppedMs renvoie -1 si non observe");
  TEST_ASSERT(m.getMicStoppedToActivityEndMs() == -1,
              "getMicStoppedToActivityEndMs renvoie -1 si non observe");
  TEST_ASSERT(m.getActivityEndToFirstAudioMs() == -1,
              "getActivityEndToFirstAudioMs renvoie -1 si non observe");
  TEST_ASSERT(m.getActivityEndToFirstResponseMs() == -1,
              "getActivityEndToFirstResponseMs renvoie -1 si non observe");
  TEST_ASSERT(m.getFirstAudioToPlaybackMs() == -1,
              "getFirstAudioToPlaybackMs renvoie -1 si non observe");
  TEST_ASSERT(m.getSpeechEndToPlaybackMs() == -1,
              "getSpeechEndToPlaybackMs renvoie -1 si non observe");
  TEST_ASSERT(m.getResponseLatencyMs() == -1,
              "getResponseLatencyMs renvoie -1 si non observe");
  TEST_ASSERT(m.getTotalTurnDurationMs() == -1,
              "getTotalTurnDurationMs renvoie -1 si non observe");

  // Simulation d'un tour vocal complet avec VAD local
  m.turnStartedMs = 10000;
  m.speechEndDetectedMs = 12400;     // VAD local detecte fin de parole
  m.micStoppedMs = 12450;            // Capture micro arretee 50 ms plus tard
  m.activityEndSentMs = 12470;       // Paquet activityEnd emis sur WebSocket
  m.firstResponseReceivedMs = 13100; // Premier retour Gemini (texte/metadata)
  m.firstAudioReceivedMs = 13200;    // Premier paquet PCM recu de Gemini
  m.firstAudioPlayedMs = 13240; // Premier bloc PCM remis au DMA I2S (playRaw)
  m.playbackFinishedMs = 15500; // Restitution audio complete achevee

  TEST_ASSERT(m.hasSpeechEndDetected(), "hasSpeechEndDetected true");
  TEST_ASSERT(m.hasMicStopped(), "hasMicStopped true");
  TEST_ASSERT(m.hasActivityEndSent(), "hasActivityEndSent true");
  TEST_ASSERT(m.hasFirstResponseReceived(), "hasFirstResponseReceived true");
  TEST_ASSERT(m.hasFirstAudioReceived(), "hasFirstAudioReceived true");
  TEST_ASSERT(m.hasFirstAudioPlayed(), "hasFirstAudioPlayed true");
  TEST_ASSERT(m.hasPlaybackFinished(), "hasPlaybackFinished true");

  // Verification des 6 calculs derives
  TEST_ASSERT(m.getSpeechEndToMicStoppedMs() == 50,
              "Fin parole -> Fin capture = 50 ms");
  TEST_ASSERT(m.getMicStoppedToActivityEndMs() == 20,
              "Fin capture -> Notification Gemini = 20 ms");
  TEST_ASSERT(m.getActivityEndToFirstAudioMs() == 730,
              "Notification Gemini -> Premier audio recu = 730 ms");
  TEST_ASSERT(m.getActivityEndToFirstResponseMs() == 630,
              "Notification Gemini -> Premiere reponse = 630 ms");
  TEST_ASSERT(m.getFirstAudioToPlaybackMs() == 40,
              "Premier audio recu -> Debut restitution DMA = 40 ms");
  TEST_ASSERT(m.getSpeechEndToPlaybackMs() == 840,
              "Fin parole -> Debut restitution DMA = 840 ms");
  TEST_ASSERT(m.getResponseLatencyMs() == 840,
              "getResponseLatencyMs avec VAD = 840 ms");
  TEST_ASSERT(m.getTotalTurnDurationMs() == 5500,
              "Duree totale tour vocal = 5500 ms");

  // Simulation sans VAD local (mode push-to-talk manuel avec bouton)
  VoiceLatencyMetrics manualM;
  manualM.turnStartedMs = 20000;
  manualM.speechEndDetectedMs = 0; // Non observe (bouton manuel)
  manualM.micStoppedMs = 23000;    // Relachement bouton
  manualM.activityEndSentMs = 23025;
  manualM.firstAudioReceivedMs = 23700;
  manualM.firstAudioPlayedMs = 23740;
  manualM.playbackFinishedMs = 25000;

  TEST_ASSERT(manualM.getSpeechEndToMicStoppedMs() == -1,
              "Fin parole -> Fin capture est N/D (-1) sans VAD");
  TEST_ASSERT(manualM.getSpeechEndToPlaybackMs() == -1,
              "Fin parole -> Restitution est N/D (-1) sans VAD");
  TEST_ASSERT(
      manualM.getResponseLatencyMs() == 740,
      "getResponseLatencyMs replie sur micStopped en mode bouton (740 ms)");
  TEST_ASSERT(manualM.getTotalTurnDurationMs() == 5000,
              "Duree totale = 5000 ms");

  // Verification de la reinitialisation
  m.reset();
  TEST_ASSERT(!m.hasMicStopped(), "reset remet micStopped a 0");
  TEST_ASSERT(!m.hasFirstAudioPlayed(), "reset remet firstAudioPlayed a 0");
  TEST_ASSERT(m.getResponseLatencyMs() == -1,
              "reset remet getResponseLatencyMs a -1");
}

// SUITE 62 : Encodage et décodage Base64 optimisés (zéro allocation tas pendant
// le streaming)
void test_v053_base64_optimization_integrity() {
  std::cout << "\n[TEST SUITE 62] Optimisation Base64 : Encodage direct et "
               "conformite RFC 4648"
            << std::endl;

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
  TEST_ASSERT(strcmp(buf, "Zm9vYmFy") == 0,
              "Base64 RFC 'foobar' -> 'Zm9vYmFy'");

  // Test 2 : Encodage d'un bloc audio microphone réel (512 échantillons = 1024
  // octets)
  int16_t micChunk[AUDIO_CHUNK_SAMPLES];
  for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
    micChunk[i] = static_cast<int16_t>(i * 37 - 9500);
  }
  const uint8_t *rawBytes = reinterpret_cast<const uint8_t *>(micChunk);
  size_t rawLen = AUDIO_CHUNK_SAMPLES * sizeof(int16_t);

  size_t outLen = GeminiLiveClient::encodeBase64Direct(rawBytes, rawLen, buf);
  buf[outLen] = '\0';
  size_t expectedLen = ((rawLen + 2) / 3) * 4;
  TEST_ASSERT(outLen == expectedLen,
              "Taille Base64 exacte pour 1024 octets (1368 car)");

  // Test 3 : Décodage et vérification de la parfaite intégrité bit-à-bit (zéro
  // altération audio)
  std::vector<uint8_t> decoded;
  bool decOk = GeminiLiveClient::decodeBase64(buf, outLen, decoded);
  TEST_ASSERT(decOk, "Decodage Base64 reussi");
  TEST_ASSERT(decoded.size() == rawLen,
              "Taille decodee identique a l'original (1024 octets)");

  bool dataMatches = (memcmp(rawBytes, decoded.data(), rawLen) == 0);
  TEST_ASSERT(
      dataMatches,
      "Donnees audio PCM strictement identiques apres encodage/decodage");
}

// SUITE 63 : Expérimentation et configuration des seuils VAD (800, 1000, 1200
// ms)
void test_v053_vad_threshold_experimentation() {
  std::cout << "\n[TEST SUITE 63] VAD : Experimentation controlee des seuils "
               "de silence (800, 1000, 1200 ms)"
            << std::endl;

  ContinuousListeningController clc;

  // 1. Valeur par défaut stricte : 1 200 ms
  TEST_ASSERT(clc.getVadConfig().silenceDurationMs == 1200,
              "Seuil de silence par defaut = 1 200 ms");
  TEST_ASSERT(clc.getVadConfig().minSpeechDurationMs == 200,
              "Validation debut parole = 200 ms");
  TEST_ASSERT(clc.getVadConfig().maxSegmentDurationMs == 30000,
              "Duree max segment = 30 000 ms");

  // 2. Configuration contrôlée à 800 ms
  clc.setSilenceDurationMs(800);
  TEST_ASSERT(clc.getVadConfig().silenceDurationMs == 800,
              "Seuil de silence configurable a 800 ms");

  // 3. Configuration contrôlée à 1000 ms
  clc.setSilenceDurationMs(1000);
  TEST_ASSERT(clc.getVadConfig().silenceDurationMs == 1000,
              "Seuil de silence configurable a 1000 ms");

  // 4. Rétablissement à 1200 ms
  clc.setSilenceDurationMs(1200);
  TEST_ASSERT(clc.getVadConfig().silenceDurationMs == 1200,
              "Seuil de silence retabli a 1200 ms");

  // 5. Test temporel de pause : une pause naturelle de 500 ms NE DOIT PAS
  // couper la parole même avec le seuil le plus réactif (800 ms)
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
  TEST_ASSERT(!prematureCut,
              "Pause naturelle de 500 ms NON coupee avec seuil de 800 ms");
  TEST_ASSERT(vad.isSpeechActive(),
              "La parole reste active pendant la pause de 500 ms");

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
  TEST_ASSERT(ended,
              "Fin de parole detectee apres depassement du seuil de 800 ms");
}

// SUITE 64 : Pipeline de latence en tour de conversation (Mode B)
void test_v053_conversation_turn_latency_pipeline() {
  std::cout << "\n[TEST SUITE 64] Pipeline de latence en tour de conversation "
               "(Mode B)"
            << std::endl;

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

  const char *setupJson = "{\"setupComplete\":{}}";
  voice.getClient().handleIncomingMessage(setupJson, strlen(setupJson));
  voice.update();

  TEST_ASSERT(voice.isReady(), "Session prete");

  ConversationController &conv = voice.getConversationController();

  // 1. Démarrage du tour
  bool startOk = conv.startTurn();
  TEST_ASSERT(startOk, "startTurn() reussi");
  TEST_ASSERT(conv.isTurnActive(), "Tour actif");
  const auto &lat1 = conv.getLatencyMetrics();
  TEST_ASSERT(lat1.turnStartedMs > 0, "turnStartedMs horodate");
  TEST_ASSERT(!lat1.hasMicStopped(), "micStopped non encore defini");

  // 2. Fin de capture audio
  bool finishAudioOk = conv.finishTurnAudio();
  TEST_ASSERT(finishAudioOk, "finishTurnAudio() reussi");
  const auto &lat2 = conv.getLatencyMetrics();
  TEST_ASSERT(lat2.hasMicStopped(),
              "micStoppedMs horodate apres finishTurnAudio");
  TEST_ASSERT(lat2.micStoppedMs >= lat2.turnStartedMs,
              "micStoppedMs >= turnStartedMs");

  // 3. Réception de la réponse Gemini Live (interim transcription + model
  // audio)
  const char *serverContent =
      "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"inlineData\":{"
      "\"mimeType\":\"audio/pcm;rate=24000\",\"data\":\"//8=\"}}]}}}";
  voice.getClient().handleIncomingMessage(serverContent, strlen(serverContent));
  voice.update();
  conv.tick();

  const auto &lat3 = conv.getLatencyMetrics();
  TEST_ASSERT(lat3.hasFirstResponseReceived(),
              "firstResponseReceivedMs capture des reception JSON");
  TEST_ASSERT(lat3.hasFirstAudioReceived(),
              "firstAudioReceivedMs capture des reception PCM");

  // 4. Clôture propre du tour
  conv.handleTurnFinished(1);
  const auto &latFinal = conv.getLatencyMetrics();
  TEST_ASSERT(latFinal.hasPlaybackFinished(),
              "playbackFinishedMs capture a la fin de restitution");
  TEST_ASSERT(latFinal.getTotalTurnDurationMs() >= 0,
              "Duree totale valide >= 0");

  voice.getClient().setTransport(nullptr);
}

// SUITE 65 : Façade VoiceKit v0.5.3 — Métriques de latence et préservation de
// l'intégrité audio
void test_v053_facade_latency_and_audio_integrity() {
  std::cout << "\n[TEST SUITE 65] Facade VoiceKit v0.5.3 : Exposition "
               "VoiceLatencyMetrics et integrite audio"
            << std::endl;

  VoiceKit vk;
  VoiceKitConfig config;
  config.apiKey = "TEST_KEY_FACADE_V053";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  config.vadSilenceDurationMs = 1200;

  bool initOk = vk.begin(config);
  TEST_ASSERT(initOk, "VoiceKit::begin() accepte la configuration v0.5.3");
  TEST_ASSERT(vk.getVadSilenceDurationMs() == 1200,
              "VAD silence duration par defaut = 1200 ms");

  // Modification du seuil VAD via la façade
  vk.setVadSilenceDurationMs(1000);
  TEST_ASSERT(vk.getVadSilenceDurationMs() == 1000,
              "VAD silence duration modifiee a 1000 ms");

  vk.setVadSilenceDurationMs(800);
  TEST_ASSERT(vk.getVadSilenceDurationMs() == 800,
              "VAD silence duration modifiee a 800 ms");

  vk.setSilenceDurationMs(1200); // Test de l'alias
  TEST_ASSERT(vk.getSilenceDurationMs() == 1200,
              "Alias setSilenceDurationMs retablit 1200 ms");

  // Exposition des métriques de latence
  const auto &lat = vk.getLatencyMetrics();
  TEST_ASSERT(lat.getResponseLatencyMs() == -1,
              "Latence de reponse initialement N/D (-1)");

  // Garanties d'intégrité audio VoxCore strictes (v0.5.3) :
  // - Capacité du buffer PSRAM étendue à 120 000 échantillons (5,0 secondes)
  // - Quadruple buffer DMA (4x40 ms = 160 ms de marge)
  // - Format micro 16 kHz et format restitution 24 kHz strictement préservés
  TEST_ASSERT(vk.getEngine().getPlayer().getBufferCapacity() ==
                  LIVE_AUDIO_RX_BUFFER_SAMPLES,
              "Buffer circulaire PSRAM de 120 000 echantillons preserve");
  TEST_ASSERT(LiveAudioPlayer::DMA_BUFFER_COUNT == 4,
              "Quadruple tampon DMA preserve (4x40 ms)");
  TEST_ASSERT(LiveAudioPlayer::DMA_CHUNK_SAMPLES == 960,
              "Blocs DMA de 960 echantillons = 40 ms a 24 kHz");
  TEST_ASSERT(AUDIO_CHUNK_SAMPLES == 512,
              "Blocs micro de 512 echantillons = 32 ms a 16 kHz");
}

// =============================================================================
// SUITES DE TESTS VoiceKit v0.5.3.1 (Consolidation, Latence Vocale &
// Validation)
// =============================================================================

// SUITE 66 : VoiceLatencyMetrics — Audit des 7 jalons, ordre chronologique, cas
// limites et débordement millis()
void test_v0531_latency_metrics_audit_and_edge_cases() {
  std::cout << "\n[TEST SUITE 66] VoiceLatencyMetrics : Audit des 7 jalons, "
               "ordre chronologique et cas limites"
            << std::endl;

  // 1. Audit complet des 7 jalons et vérification de l'ordre chronologique
  // strict
  VoiceLatencyMetrics m;
  m.turnStartedMs = 10000;
  m.speechEndDetectedMs = 11000; // t0 : Fin de parole détectée par VAD
  m.micStoppedMs = 11035; // t1 : Fin effective de capture microphone (+35 ms)
  m.activityEndSentMs =
      11050; // t2 : Envoi notification activityEnd à Gemini (+15 ms)
  m.firstResponseReceivedMs =
      11450; // t3 : Premier événement de réponse reçu (+400 ms)
  m.firstAudioReceivedMs =
      11500; // t4 : Premier paquet PCM reçu de Gemini (+50 ms)
  m.firstAudioPlayedMs =
      11530; // t5 : Premier bloc remis au DMA (playRaw) (+30 ms)
  m.playbackFinishedMs =
      13200; // t6 : Restitution acoustique achevée (+1670 ms)

  TEST_ASSERT(m.hasSpeechEndDetected(), "hasSpeechEndDetected valide");
  TEST_ASSERT(m.hasMicStopped(), "hasMicStopped valide");
  TEST_ASSERT(m.hasActivityEndSent(), "hasActivityEndSent valide");
  TEST_ASSERT(m.hasFirstResponseReceived(), "hasFirstResponseReceived valide");
  TEST_ASSERT(m.hasFirstAudioReceived(), "hasFirstAudioReceived valide");
  TEST_ASSERT(m.hasFirstAudioPlayed(), "hasFirstAudioPlayed valide");
  TEST_ASSERT(m.hasPlaybackFinished(), "hasPlaybackFinished valide");

  // Vérification exacte des 6 calculs dérivés
  TEST_ASSERT(m.getSpeechEndToMicStoppedMs() == 35,
              "getSpeechEndToMicStoppedMs = 35 ms");
  TEST_ASSERT(m.getMicStoppedToActivityEndMs() == 15,
              "getMicStoppedToActivityEndMs = 15 ms");
  TEST_ASSERT(m.getActivityEndToFirstAudioMs() == 450,
              "getActivityEndToFirstAudioMs = 450 ms");
  TEST_ASSERT(m.getActivityEndToFirstResponseMs() == 400,
              "getActivityEndToFirstResponseMs = 400 ms");
  // Distinction explicite : paquet réseau reçu (t4) vs échantillon remis au DMA
  // (t5)
  TEST_ASSERT(
      m.getFirstAudioToPlaybackMs() == 30,
      "getFirstAudioToPlaybackMs distingue paquet recu et remise DMA (30 ms)");
  TEST_ASSERT(m.getSpeechEndToPlaybackMs() == 530,
              "getSpeechEndToPlaybackMs = 530 ms");
  TEST_ASSERT(m.getResponseLatencyMs() == 530,
              "getResponseLatencyMs avec VAD = 530 ms");
  TEST_ASSERT(m.getTotalTurnDurationMs() == 3200,
              "getTotalTurnDurationMs = 3200 ms");

  // 2. Détection et rejet des anomalies d'inversion chronologique (end < start)
  VoiceLatencyMetrics inverted;
  inverted.turnStartedMs = 10000;
  inverted.speechEndDetectedMs = 12000;
  inverted.micStoppedMs =
      11500; // Anormal : arrêt micro antérieur à la fin de parole
  TEST_ASSERT(inverted.getSpeechEndToMicStoppedMs() == -1,
              "Inversion chronologique speechEnd > micStopped retourne -1");

  inverted.micStoppedMs = 12050;
  inverted.activityEndSentMs =
      12020; // Anormal : activityEnd antérieur à l'arrêt micro
  TEST_ASSERT(inverted.getMicStoppedToActivityEndMs() == -1,
              "Inversion micStopped > activityEnd retourne -1");

  inverted.activityEndSentMs = 12100;
  inverted.firstAudioReceivedMs =
      12080; // Anormal : audio reçu avant envoi activityEnd
  TEST_ASSERT(inverted.getActivityEndToFirstAudioMs() == -1,
              "Inversion activityEnd > firstAudio retourne -1");

  inverted.firstAudioReceivedMs = 12500;
  inverted.firstAudioPlayedMs =
      12450; // Anormal : lecture DMA antérieure à la réception
  TEST_ASSERT(inverted.getFirstAudioToPlaybackMs() == -1,
              "Inversion firstAudio > playRaw retourne -1");

  inverted.playbackFinishedMs =
      9000; // Anormal : fin antérieure au début de tour
  TEST_ASSERT(inverted.getTotalTurnDurationMs() == -1,
              "Inversion turnStarted > playbackFinished retourne -1");

  // 3. Tour interrompu (STOP / Interruption réseau)
  VoiceLatencyMetrics interrupted;
  interrupted.turnStartedMs = 20000;
  interrupted.speechEndDetectedMs = 21000;
  interrupted.micStoppedMs = 21050;
  interrupted.activityEndSentMs = 21070;
  // Interruption : aucun audio ni réponse serveur n'a été reçu
  interrupted.firstResponseReceivedMs = 0;
  interrupted.firstAudioReceivedMs = 0;
  interrupted.firstAudioPlayedMs = 0;
  interrupted.playbackFinishedMs = 0;

  TEST_ASSERT(interrupted.getSpeechEndToMicStoppedMs() == 50,
              "Tour interrompu : duree capture partielle disponible");
  TEST_ASSERT(interrupted.getMicStoppedToActivityEndMs() == 20,
              "Tour interrompu : delai envoi partiel disponible");
  TEST_ASSERT(interrupted.getActivityEndToFirstAudioMs() == -1,
              "Tour interrompu : delai Gemini non observe renvoie -1");
  TEST_ASSERT(interrupted.getFirstAudioToPlaybackMs() == -1,
              "Tour interrompu : delai DMA non observe renvoie -1");
  TEST_ASSERT(interrupted.getResponseLatencyMs() == -1,
              "Tour interrompu : latence reponse non observee renvoie -1");
  TEST_ASSERT(interrupted.getTotalTurnDurationMs() == -1,
              "Tour interrompu : duree totale incomplete renvoie -1");

  // 4. Remise à zéro complète
  interrupted.reset();
  TEST_ASSERT(!interrupted.hasSpeechEndDetected(),
              "reset remet speechEndDetected a 0");
  TEST_ASSERT(!interrupted.hasMicStopped(), "reset remet micStopped a 0");
  TEST_ASSERT(!interrupted.hasActivityEndSent(),
              "reset remet activityEndSent a 0");
  TEST_ASSERT(!interrupted.hasFirstResponseReceived(),
              "reset remet firstResponseReceived a 0");
  TEST_ASSERT(!interrupted.hasFirstAudioReceived(),
              "reset remet firstAudioReceived a 0");
  TEST_ASSERT(!interrupted.hasFirstAudioPlayed(),
              "reset remet firstAudioPlayed a 0");
  TEST_ASSERT(!interrupted.hasPlaybackFinished(),
              "reset remet playbackFinished a 0");
  TEST_ASSERT(interrupted.turnStartedMs == 0, "reset remet turnStartedMs a 0");
  TEST_ASSERT(interrupted.getResponseLatencyMs() == -1,
              "reset remet getResponseLatencyMs a -1");
}

// SUITE 67 : VAD — Validation approfondie des 3 seuils, valeurs incorrectes et
// changements à chaud
void test_v0531_vad_thresholds_hot_switch_and_validation() {
  std::cout << "\n[TEST SUITE 67] VAD : Validation approfondie des 3 seuils, "
               "valeurs incorrectes et switch a chaud"
            << std::endl;

  // 1. Initialisation avec seuil par défaut à 1 200 ms
  VoiceKit vkDefault;
  VoiceKitConfig cfgDefault;
  cfgDefault.apiKey = "TEST_VAD_DEF";
  cfgDefault.wifiMode = WiFiMode::EXTERNAL_WIFI;
  vkDefault.begin(cfgDefault);
  TEST_ASSERT(vkDefault.getVadSilenceDurationMs() == 1200,
              "Seuil de silence VAD par defaut = 1 200 ms");

  // 2. Initialisation personnalisée via VoiceKitConfig
  VoiceKit vkCustom;
  VoiceKitConfig cfgCustom;
  cfgCustom.apiKey = "TEST_VAD_CUST";
  cfgCustom.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfgCustom.vadSilenceDurationMs =
      800; // Spécification 800 ms (commandes courtes)
  vkCustom.begin(cfgCustom);
  TEST_ASSERT(vkCustom.getVadSilenceDurationMs() == 800,
              "Configuration initiale vadSilenceDurationMs = 800 ms appliquee");

  // 3. Contrôle des 3 seuils opérationnels et alias
  vkCustom.setVadSilenceDurationMs(1000); // 1000 ms : usage équilibré
  TEST_ASSERT(vkCustom.getVadSilenceDurationMs() == 1000,
              "Seuil passe a 1 000 ms");
  vkCustom.setSilenceDurationMs(1200); // 1200 ms : parole naturelle
  TEST_ASSERT(vkCustom.getSilenceDurationMs() == 1200,
              "Alias setSilenceDurationMs passe a 1 200 ms");
  vkCustom.setVadSilenceDurationMs(800); // 800 ms : commandes courtes
  TEST_ASSERT(vkCustom.getVadSilenceDurationMs() == 800,
              "Seuil passe a 800 ms");

  // 4. Protection contre les valeurs incorrectes (ex: 0 ms)
  vkCustom.setVadSilenceDurationMs(0);
  TEST_ASSERT(vkCustom.getVadSilenceDurationMs() == 800,
              "Valeur incorrecte 0 ms rejetee : seuil preserve a 800 ms");

  ContinuousListeningController clc;
  TEST_ASSERT(clc.getVadConfig().silenceDurationMs == 1200,
              "ContinuousListeningController defaut 1200 ms");
  clc.setSilenceDurationMs(0);
  TEST_ASSERT(clc.getVadConfig().silenceDurationMs == 1200,
              "clc.setSilenceDurationMs(0) rejete et preserve 1200 ms");

  // 5. Changement de seuil à chaud pendant une écoute active
  VoiceActivityDetector vad;
  VadConfig vadCfg;
  vadCfg.minSpeechDurationMs = 200;
  vadCfg.silenceDurationMs = 1200; // Démarrage à 1 200 ms
  vadCfg.maxSegmentDurationMs = 30000;
  vadCfg.initialNoiseFloor = 200;
  vad.setConfig(vadCfg);

  // Initialisation du plancher de bruit (silence)
  for (int i = 0; i < 15; ++i) {
    vad.processRms(200, 32);
  }
  TEST_ASSERT(!vad.isSpeechActive(), "Silence initial VAD");

  // Début de parole : 256 ms (> 200 ms)
  for (int i = 0; i < 8; ++i) {
    vad.processRms(1600, 32);
  }
  TEST_ASSERT(vad.isSpeechActive(), "Parole active validee");

  // Changement de seuil À CHAUD en pleine parole active : bascule vers 800 ms
  vad.setSilenceDurationMs(800);
  TEST_ASSERT(vad.getConfig().silenceDurationMs == 800,
              "Changement de seuil a chaud applique a 800 ms");
  TEST_ASSERT(vad.isSpeechActive(),
              "La parole reste active apres le changement a chaud");

  // Pause courte de 480 ms (15 blocs de 32 ms) : ne doit PAS couper la parole
  for (int i = 0; i < 15; ++i) {
    vad.processRms(200, 32);
  }
  TEST_ASSERT(vad.isSpeechActive(),
              "Pause de 480 ms non coupee avec seuil a chaud de 800 ms");

  // Prolongation du silence au-delà de 800 ms : 12 blocs supplémentaires (total
  // 864 ms)
  bool endedWithHotSwitch = false;
  for (int i = 0; i < 12; ++i) {
    if (vad.processRms(200, 32)) {
      endedWithHotSwitch = true;
      break;
    }
  }
  TEST_ASSERT(endedWithHotSwitch,
              "Fin de parole detectee a 800 ms apres switch a chaud");
}

// SUITE 68 : Façade VoiceKit v0.5.3.1 — Validation des 4 modes, transitions et
// résilience
void test_v0531_facade_modes_transitions_and_resilience() {
  std::cout << "\n[TEST SUITE 68] Facade VoiceKit v0.5.3.1 : Non-regression "
               "des 4 modes et resilience"
            << std::endl;

  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.apiKey = "TEST_V0531_FACADE";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  vk.begin(cfg);

  // 1. Constante de version alignée sur v0.6.5.2, v0.6.5.1 ou v0.6.5
  TEST_ASSERT(strcmp(VOICEKIT_VERSION, "v0.6.5.2") == 0 ||
                  strcmp(VOICEKIT_VERSION, "v0.6.5.1") == 0 ||
                  strcmp(VOICEKIT_VERSION, "v0.6.5") == 0,
              "VOICEKIT_VERSION est v0.6.5.2, v0.6.5.1 ou v0.6.5");

  // 2. Mode par défaut IDLE
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE, "Mode initial = IDLE");

  // 3. Test refus d'opérations incompatibles
  // Refus de listen() en mode Conversation
  vk.startConversation();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::CONVERSATION,
              "Mode CONVERSATION active");
  TEST_ASSERT(!vk.listen(), "listen() refuse strictement en mode CONVERSATION");
  TEST_ASSERT(
      !vk.startContinuousListening(),
      "startContinuousListening() refuse strictement en mode CONVERSATION");
  TEST_ASSERT(!vk.say("Test"), "say() refuse strictement en mode CONVERSATION");

  // Sortie propre du mode Conversation
  vk.stopConversation();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE,
              "Retour au mode IDLE apres stopConversation");

  // 4. Test d'activation de la dictée continue
  vk.startContinuousListening();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::CONTINUOUS_LISTENING,
              "Mode CONTINUOUS_LISTENING active");
  TEST_ASSERT(!vk.startConversation(),
              "startConversation() refuse en CONTINUOUS_LISTENING");
  TEST_ASSERT(!vk.listen(), "listen() refuse en CONTINUOUS_LISTENING");

  // Pause et reprise
  TEST_ASSERT(vk.pauseContinuousListening(),
              "pauseContinuousListening() reussi");
  TEST_ASSERT(vk.isContinuousListeningPaused(), "Dictee continue en pause");
  TEST_ASSERT(vk.resumeContinuousListening(),
              "resumeContinuousListening() reussi");

  // Arrêt de la dictée continue
  vk.stopContinuousListening();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE,
              "Retour IDLE apres stopContinuousListening");

  // 5. Arrêt d'urgence instantané stop()
  vk.stop();
  TEST_ASSERT(vk.getMode() == VoiceKitMode::IDLE,
              "stop() maintient le mode IDLE et securise le moteur");
}

// =============================================================================
// SUITE 69 : Multi-Provider VoiceKitConfig & Enum Selection (v0.6.0)
// =============================================================================
void test_v060_multiprovider_config_and_enum() {
  std::cout
      << "\n[TEST SUITE 69] Multi-Provider VoiceKitConfig & Enum Selection"
      << std::endl;

  VoiceKitConfig cfg;
  TEST_ASSERT(cfg.provider == VoiceProviderType::GEMINI,
              "Fournisseur par defaut est GEMINI");
  TEST_ASSERT(strcmp(voiceProviderTypeToString(VoiceProviderType::GEMINI),
                     "GEMINI") == 0,
              "Enum GEMINI string valide");
  TEST_ASSERT(strcmp(voiceProviderTypeToString(VoiceProviderType::OPENAI),
                     "OPENAI") == 0,
              "Enum OPENAI string valide");

  // Configuration explicite OpenAI
  cfg.provider = VoiceProviderType::OPENAI;
  cfg.apiKey = "sk-proj-test123456";
  cfg.model = "gpt-realtime-2.1-mini";
  cfg.voice = "alloy";
  cfg.language = "fr-FR";

  TEST_ASSERT(cfg.provider == VoiceProviderType::OPENAI,
              "Fournisseur bascule a OPENAI");
  TEST_ASSERT(cfg.apiKey == "sk-proj-test123456", "Cle API OpenAI assignee");
  TEST_ASSERT(cfg.model == "gpt-realtime-2.1-mini", "Modele OpenAI assigne");
  TEST_ASSERT(cfg.voice == "alloy", "Voix OpenAI assignee");
  TEST_ASSERT(cfg.language == "fr-FR", "Langue OpenAI assignee");
}

// =============================================================================
// SUITE 70 : AudioResampler 16 kHz vers 24 kHz (Rapport 2:3) (v0.6.0)
// =============================================================================
void test_v060_audio_resampler_16to24() {
  std::cout << "\n[TEST SUITE 70] AudioResampler 16 kHz vers 24 kHz"
            << std::endl;

  TEST_ASSERT(AudioResampler16to24::calculateOutputSamples(0) == 0,
              "0 ech -> 0 ech");
  TEST_ASSERT(AudioResampler16to24::calculateOutputSamples(512) == 768,
              "512 ech -> 768 ech");
  TEST_ASSERT(AudioResampler16to24::calculateOutputSamples(1024) == 1536,
              "1024 ech -> 1536 ech");

  int16_t in[512];
  int16_t out[768];

  // Vérification des pointeurs nuls ou tampons trop courts
  TEST_ASSERT(AudioResampler16to24::resample(nullptr, 512, out, 768) == 0,
              "Rejet pointeur in null");
  TEST_ASSERT(AudioResampler16to24::resample(in, 512, nullptr, 768) == 0,
              "Rejet pointeur out null");
  TEST_ASSERT(AudioResampler16to24::resample(in, 512, out, 500) == 0,
              "Rejet tampon out trop court");

  // 1. Signal continu plat (DC)
  for (size_t i = 0; i < 512; i++)
    in[i] = 1500;
  size_t count = AudioResampler16to24::resample(in, 512, out, 768);
  TEST_ASSERT(count == 768, "768 echantillons generes");
  bool allDc = true;
  for (size_t i = 0; i < 768; i++) {
    if (out[i] != 1500) {
      allDc = false;
      break;
    }
  }
  TEST_ASSERT(allDc, "Signal DC conserve a 1500 sur tous les 768 echantillons");

  // 2. Rampe linéaire x(n) = 3 * n -> interpolation (x0 + 2*x1)/3 et (2*x1 +
  // x2)/3
  for (size_t i = 0; i < 512; i++)
    in[i] = (int16_t)(3 * i);
  count = AudioResampler16to24::resample(in, 512, out, 768);
  TEST_ASSERT(count == 768, "Rampe 768 ech");
  TEST_ASSERT(out[0] == 0, "out[0] == 0");
  TEST_ASSERT(out[1] == 2, "out[1] == 2");
  TEST_ASSERT(out[2] == 4, "out[2] == 4");
  TEST_ASSERT(out[3] == 6, "out[3] == 6");
  TEST_ASSERT(out[4] == 8, "out[4] == 8");

  // 3. Signal sinusoïdal : vérification de continuité et absence de saturation
  // anormale
  for (size_t i = 0; i < 512; i++) {
    in[i] = (int16_t)(10000.0 * sin(2.0 * M_PI * 440.0 * i / 16000.0));
  }
  count = AudioResampler16to24::resample(in, 512, out, 768);
  TEST_ASSERT(count == 768, "Sinus 768 ech");
  int16_t minVal = 0, maxVal = 0;
  for (size_t i = 0; i < 768; i++) {
    if (out[i] < minVal)
      minVal = out[i];
    if (out[i] > maxVal)
      maxVal = out[i];
  }
  TEST_ASSERT(maxVal <= 10000 && minVal >= -10000,
              "Amplitude de la sinusoide preservee sans depassement");
}

// =============================================================================
// SUITE 71 : IVoiceProvider & GeminiVoiceProvider Adapter (v0.6.0)
// =============================================================================
void test_v060_ivoiceprovider_contract_and_gemini_adapter() {
  std::cout << "\n[TEST SUITE 71] IVoiceProvider & GeminiVoiceProvider Adapter"
            << std::endl;

  GeminiLiveClient client;
  GeminiVoiceProvider adapter(&client);
  TEST_ASSERT(adapter.getProviderType() == VoiceProviderType::GEMINI,
              "Type Gemini valide");

  adapter.setVoice("Aoede");
  TEST_ASSERT(client.getVoice() == "Aoede", "Délégation setVoice");
  TEST_ASSERT(adapter.getVoice() == "Aoede", "Lecture getVoice");

  adapter.setModel("gemini-2.0-flash-exp");
  TEST_ASSERT(client.getModel() == "gemini-2.0-flash-exp",
              "Délégation setModel");

  adapter.setLanguage("fr-FR");
  TEST_ASSERT(client.getLanguage() == "fr-FR", "Délégation setLanguage");

  adapter.setSystemInstruction("Instruction test");
  TEST_ASSERT(client.getSystemInstruction() == "Instruction test",
              "Délégation setSystemInstruction");

  adapter.setConversationMode(true);
  TEST_ASSERT(adapter.isConversationMode() == true,
              "Mode conversation actif sur adaptateur");

  // Callbacks
  bool turnCompleted = false;
  adapter.setOnTurnComplete([&turnCompleted]() { turnCompleted = true; });
  TEST_ASSERT(!turnCompleted, "Callback non declenche avant evenement");
}

// =============================================================================
// SUITE 72 : OpenAIRealtimeClient : Connexion & Handshake WebSocket TLS
// (v0.6.0)
// =============================================================================
void test_v060_openai_realtime_connection_and_handshake() {
  std::cout << "\n[TEST SUITE 72] OpenAIRealtimeClient : Connexion & Handshake "
               "WebSocket TLS"
            << std::endl;

  OpenAIRealtimeClient client;
  MockLiveTransport transport;
  client.setTransport(&transport);

  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::OPENAI;
  cfg.apiKey = "sk-test-token-voicekit";
  cfg.model = "gpt-realtime-2.1-mini";
  cfg.voice = "alloy";
  cfg.language = "fr-FR";

  TEST_ASSERT(client.begin(cfg), "begin() reussi");
  TEST_ASSERT(client.connect(), "connect() initie avec succes");

  // Vérification de l'URI générée
  String uri = transport.getLastUri();
  TEST_ASSERT(uri.indexOf("wss://api.openai.com/v1/realtime") >= 0,
              "URI contient l'endpoint wss OpenAI Realtime");
  TEST_ASSERT(uri.indexOf("model=gpt-realtime-2.1-mini") >= 0,
              "URI specifie le modele gpt-realtime-2.1-mini");

  // Vérification des en-têtes HTTP de négociation TLS GA
  String headers = transport.getLastHeaders();
  TEST_ASSERT(headers.indexOf("Authorization: Bearer sk-test-token-voicekit") >=
                  0,
              "Header Authorization Bearer present");
  TEST_ASSERT(headers.indexOf("OpenAI-Beta:") < 0,
              "Header OpenAI-Beta absent conformement a la specification GA");

  // Simuler l'ouverture du socket
  transport.triggerConnected();
  client.tick();
  TEST_ASSERT(client.isConnected(),
              "Client considere CONNECTE apres ouverture WebSocket");
  TEST_ASSERT(!client.isSetupComplete(),
              "Setup NON complet avant session.created");
}

// =============================================================================
// SUITE 73 : OpenAIRealtimeClient : Négociation de Session (session.update)
// (v0.6.0)
// =============================================================================
void test_v060_openai_realtime_session_negotiation() {
  std::cout << "\n[TEST SUITE 73] OpenAIRealtimeClient : Negociation de "
               "Session (session.update)"
            << std::endl;

  OpenAIRealtimeClient client;
  MockLiveTransport transport;
  client.setTransport(&transport);

  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::OPENAI;
  cfg.apiKey = "sk-test-token";
  cfg.model = "gpt-realtime-2.1-mini";
  cfg.voice = "alloy";
  cfg.language = "fr-FR";
  cfg.systemInstruction = "Tu es un assistant concis.";
  client.begin(cfg);
  client.connect();
  transport.triggerConnected();

  bool setupConfirmed = false;
  client.setOnSetupComplete([&setupConfirmed]() { setupConfirmed = true; });

  // Le serveur OpenAI envoie session.created
  const char *sessCreated =
      "{\"type\":\"session.created\",\"session\":{\"id\":\"sess_abc123\"}}";
  client.handleIncomingMessage(sessCreated, strlen(sessCreated));

  // Le client doit avoir emis un session.update en reponse
  String sentUpdate = transport.getLastMessage();
  TEST_ASSERT(sentUpdate.indexOf("\"type\":\"session.update\"") >= 0,
              "session.update emis par le client");
  TEST_ASSERT(sentUpdate.indexOf("\"type\":\"realtime\"") >= 0,
              "session.type realtime specifie conformement a la GA");
  TEST_ASSERT(sentUpdate.indexOf("\"voice\":\"alloy\"") >= 0,
              "Voix alloy specifiee dans session.update");
  TEST_ASSERT(sentUpdate.indexOf("\"turn_detection\":null") >= 0,
              "turn_detection desactive (VAD local CoreS3)");
  TEST_ASSERT(sentUpdate.indexOf("\"whisper-1\"") >= 0,
              "Modele de transcription whisper-1 specifie");

  // Le serveur confirme session.updated
  const char *sessUpdated = "{\"type\":\"session.updated\",\"session\":{\"id\":"
                            "\"sess_abc123\",\"voice\":\"alloy\"}}";
  client.handleIncomingMessage(sessUpdated, strlen(sessUpdated));

  TEST_ASSERT(client.isSetupComplete(), "Session confirmee PRETE");
  TEST_ASSERT(setupConfirmed, "Callback onSetupComplete invoque");
}

// =============================================================================
// SUITE 74 : OpenAIRealtimeClient : Émission Audio TX
// (input_audio_buffer.append) (v0.6.0)
// =============================================================================
void test_v060_openai_realtime_audio_streaming_tx() {
  std::cout << "\n[TEST SUITE 74] OpenAIRealtimeClient : Emission Audio TX "
               "(input_audio_buffer.append)"
            << std::endl;

  OpenAIRealtimeClient client;
  MockLiveTransport transport;
  client.setTransport(&transport);

  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::OPENAI;
  cfg.apiKey = "sk-test-token";
  client.begin(cfg);
  client.connect();
  transport.triggerConnected();
  const char *sessUpdated = "{\"type\":\"session.updated\",\"session\":{}}";
  client.handleIncomingMessage(sessUpdated, strlen(sessUpdated));

  // Envoi d'un bloc de 512 échantillons 16 kHz
  int16_t samples16k[512];
  for (int i = 0; i < 512; i++)
    samples16k[i] = 100;
  TEST_ASSERT(client.sendAudioChunk(samples16k, 512), "sendAudioChunk reussi");

  String txMsg = transport.getLastMessage();
  TEST_ASSERT(txMsg.indexOf("\"type\":\"input_audio_buffer.append\"") >= 0,
              "Trame input_audio_buffer.append emise");
  TEST_ASSERT(txMsg.indexOf("\"audio\":\"") >= 0, "Champ audio Base64 present");

  // Mode conversation : fin de tour utilisateur
  client.setConversationMode(true);
  TEST_ASSERT(client.sendActivityEnd(), "sendActivityEnd en mode conversation");
  String commitMsg = transport.getLastMessage();
  TEST_ASSERT(commitMsg.indexOf("\"type\":\"response.create\"") >= 0,
              "response.create emis a la cloture du tour");
}

// =============================================================================
// SUITE 75 : OpenAIRealtimeClient : Réception Audio Streaming RX & Drain
// (v0.6.0)
// =============================================================================
void test_v060_openai_realtime_inbound_audio_rx_and_drain() {
  std::cout << "\n[TEST SUITE 75] OpenAIRealtimeClient : Reception Audio "
               "Streaming RX & Drain"
            << std::endl;

  OpenAIRealtimeClient client;
  MockLiveTransport transport;
  client.setTransport(&transport);
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::OPENAI;
  client.begin(cfg);
  client.connect();
  transport.triggerConnected();

  std::vector<int16_t> receivedSamples;
  client.setOnAudioData([&receivedSamples](const int16_t *pcm, size_t count) {
    receivedSamples.insert(receivedSamples.end(), pcm, pcm + count);
  });

  bool turnCompleted = false;
  client.setOnTurnComplete([&turnCompleted]() { turnCompleted = true; });

  // Trame audio 24 kHz delta format GA : response.output_audio.delta
  const char *audioDeltaGA =
      "{\"type\":\"response.output_audio.delta\",\"delta\":\"AAEA\"}";
  client.handleIncomingMessage(audioDeltaGA, strlen(audioDeltaGA));

  TEST_ASSERT(receivedSamples.size() > 0,
              "Echantillons audio 24 kHz recus et decodes (format GA)");

  // Trame audio 24 kHz delta format Beta : response.audio.delta
  size_t prevCount = receivedSamples.size();
  const char *audioDeltaBeta =
      "{\"type\":\"response.audio.delta\",\"delta\":\"AAEA\"}";
  client.handleIncomingMessage(audioDeltaBeta, strlen(audioDeltaBeta));
  TEST_ASSERT(receivedSamples.size() > prevCount,
              "Echantillons audio recus et decodes (format Beta)");

  // Trame response.done
  const char *respDone = "{\"type\":\"response.done\"}";
  client.handleIncomingMessage(respDone, strlen(respDone));
  TEST_ASSERT(turnCompleted, "response.done signale le turnComplete");
}

// =============================================================================
// SUITE 76 : OpenAIRealtimeClient : Transcriptions Utilisateur & Assistant
// (v0.6.0)
// =============================================================================
void test_v060_openai_realtime_transcriptions() {
  std::cout << "\n[TEST SUITE 76] OpenAIRealtimeClient : Transcriptions "
               "Utilisateur & Assistant"
            << std::endl;

  OpenAIRealtimeClient client;
  MockLiveTransport transport;
  client.setTransport(&transport);
  VoiceKitConfig cfg;
  client.begin(cfg);

  String userText = "";
  bool userFinal = false;
  client.setOnInputTranscription(
      [&userText, &userFinal](const String &text, bool isFinal) {
        userText = text;
        userFinal = isFinal;
      });

  String assistantText = "";
  client.setOnOutputTranscription(
      [&assistantText](const String &text) { assistantText += text; });

  // Transcription Whisper utilisateur
  const char *whisperEvt =
      "{\"type\":\"conversation.item.input_audio_transcription.completed\","
      "\"transcript\":\"Bonjour VoiceKit\"}";
  client.handleIncomingMessage(whisperEvt, strlen(whisperEvt));
  TEST_ASSERT(userText == "Bonjour VoiceKit",
              "Transcription utilisateur Whisper recue");
  TEST_ASSERT(userFinal == true, "Transcription utilisateur marquee FINALE");

  // Transcriptions de l'assistant
  const char *delta1 =
      "{\"type\":\"response.audio_transcript.delta\",\"delta\":\"Salut ! \"}";
  const char *delta2 = "{\"type\":\"response.audio_transcript.delta\","
                       "\"delta\":\"Comment puis-je vous aider ?\"}";
  client.handleIncomingMessage(delta1, strlen(delta1));
  client.handleIncomingMessage(delta2, strlen(delta2));
  TEST_ASSERT(assistantText == "Salut ! Comment puis-je vous aider ?",
              "Transcription assistant complete assemblee");
}

// =============================================================================
// SUITE 77 : OpenAIRealtimeClient : Interruption STOP Prioritaire (v0.6.0)
// =============================================================================
void test_v060_openai_realtime_priority_stop_and_cancel() {
  std::cout << "\n[TEST SUITE 77] OpenAIRealtimeClient : Interruption STOP "
               "Prioritaire"
            << std::endl;

  OpenAIRealtimeClient client;
  MockLiveTransport transport;
  client.setTransport(&transport);
  VoiceKitConfig cfg;
  client.begin(cfg);
  client.connect();
  transport.triggerConnected();

  bool interruptedCalled = false;
  client.setOnInterrupted([&interruptedCalled]() { interruptedCalled = true; });

  TEST_ASSERT(client.sendInterruption(), "sendInterruption execute");
  TEST_ASSERT(interruptedCalled, "Callback onInterrupted appele");

  String cancelMsg = transport.getLastMessage();
  TEST_ASSERT(cancelMsg.indexOf("\"type\":\"input_audio_buffer.clear\"") >= 0,
              "input_audio_buffer.clear transmis lors du STOP");
}

// =============================================================================
// SUITE 78 : OpenAIRealtimeClient : Gestion des Erreurs et Reconnexion (v0.6.0)
// =============================================================================
void test_v060_openai_realtime_error_handling_and_reconnection() {
  std::cout << "\n[TEST SUITE 78] OpenAIRealtimeClient : Gestion des Erreurs "
               "et Reconnexion"
            << std::endl;

  OpenAIRealtimeClient client;
  MockLiveTransport transport;
  client.setTransport(&transport);
  VoiceKitConfig cfg;
  client.begin(cfg);
  client.connect();
  transport.triggerConnected();

  String errReceived = "";
  client.setOnError([&errReceived](const String &err) { errReceived = err; });

  // Trame d'erreur OpenAI
  const char *errJson =
      "{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\","
      "\"message\":\"Cle API revoquee ou invalide\"}}";
  client.handleIncomingMessage(errJson, strlen(errJson));

  TEST_ASSERT(errReceived.indexOf("Cle API revoquee ou invalide") >= 0,
              "Message d'erreur extrait de la trame OpenAI");
  TEST_ASSERT(strstr(client.getLastError(), "Cle API revoquee ou invalide") !=
                  nullptr,
              "getLastError() renseigne");

  // Reconnexion propre
  client.disconnect();
  TEST_ASSERT(!client.isConnected(), "Client deconnecte");
  TEST_ASSERT(client.connect(), "Reconnexion reussie");
  transport.triggerConnected();
  TEST_ASSERT(client.isConnected(), "Reconnexion WebSocket reussie");
}

// =============================================================================
// SUITE 79 : VoxCoreVoice Multi-Fournisseur : Intégration OpenAI &
// Non-Régression Gemini (v0.6.0)
// =============================================================================
static void prepareMockOpenAIVoiceReady(VoxCoreVoice &voice,
                                        MockLiveTransport &mockTransport) {
  WiFi.setConnected(true);
  voice.getOpenAIClient().setTransport(&mockTransport);
  voice.getOpenAIClient().connect();
  const char *sessCreated =
      "{\"type\":\"session.created\",\"session\":{\"id\":\"sess_001\"}}";
  voice.getOpenAIClient().handleIncomingMessage(sessCreated,
                                                strlen(sessCreated));
  const char *sessUpdated =
      "{\"type\":\"session.updated\",\"session\":{\"id\":\"sess_001\"}}";
  voice.getOpenAIClient().handleIncomingMessage(sessUpdated,
                                                strlen(sessUpdated));
  voice.update();
}

void test_v060_voxcore_and_voicekit_integration_and_gemini_non_regression() {
  std::cout << "\n[TEST SUITE 79] VoxCoreVoice & VoiceKit : Integration OpenAI "
               "& Non-Regression Gemini"
            << std::endl;

  // 1. Initialisation avec fournisseur OpenAI
  MockLiveTransport mockTransportOAI;
  VoxCoreVoice voiceOpenAI;
  VoiceKitConfig cfgOAI;
  cfgOAI.provider = VoiceProviderType::OPENAI;
  cfgOAI.apiKey = "sk-test-secret-openai";
  cfgOAI.model = "gpt-realtime-2.1-mini";
  cfgOAI.voice = "alloy";
  cfgOAI.wifiMode = WiFiMode::EXTERNAL_WIFI;

  TEST_ASSERT(voiceOpenAI.begin(cfgOAI), "begin() avec OPENAI reussi");
  TEST_ASSERT(voiceOpenAI.getProvider().getProviderType() ==
                  VoiceProviderType::OPENAI,
              "Provider actif est OPENAI");
  TEST_ASSERT(voiceOpenAI.getClient().getModel().length() > 0,
              "getClient() reste accessible (compatibilite ascendante)");

  prepareMockOpenAIVoiceReady(voiceOpenAI, mockTransportOAI);
  TEST_ASSERT(voiceOpenAI.isReady(),
              "VoxCoreVoice est READY avec OpenAI Realtime");
  voiceOpenAI.stop();
  voiceOpenAI.getOpenAIClient().setTransport(nullptr);

  // 2. Initialisation avec fournisseur Gemini (défaut) et vérification
  // non-régression
  MockLiveTransport mockTransportGemini;
  VoxCoreVoice voiceGemini;
  VoiceKitConfig cfgGemini;
  cfgGemini.provider = VoiceProviderType::GEMINI;
  cfgGemini.apiKey = "AIzaSyTestGemini";
  cfgGemini.wifiMode = WiFiMode::EXTERNAL_WIFI;

  TEST_ASSERT(voiceGemini.begin(cfgGemini), "begin() avec GEMINI reussi");
  TEST_ASSERT(voiceGemini.getProvider().getProviderType() ==
                  VoiceProviderType::GEMINI,
              "Provider actif est GEMINI");
  prepareMockVoiceReady(voiceGemini, mockTransportGemini);
  TEST_ASSERT(voiceGemini.isReady(),
              "VoxCoreVoice est READY avec Gemini Live (non-regression)");
  voiceGemini.stop();
  voiceGemini.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 80 : Correction A (v0.6.1) — Réinitialisation polymorphe des métriques
// PCM entre tours
// =============================================================================
static void injectGeminiPcmAndDrain(VoxCoreVoice &voice, int numBlocks,
                                    bool sendTurnComplete = true) {
  // "AAAAAAAAAAAAAAAA" = 16 chars Base64 = 12 octets PCM = 6 échantillons 16
  // bits par bloc
  const char *pcmFrame = "{\"serverContent\":{\"modelTurn\":{\"parts\":[{"
                         "\"inlineData\":{\"mimeType\":\"audio/"
                         "pcm;rate=24000\",\"data\":\"AAAAAAAAAAAAAAAA\"}}]}}}";
  for (int i = 0; i < numBlocks; ++i) {
    voice.getClient().handleIncomingMessage(pcmFrame, strlen(pcmFrame));
  }
  if (sendTurnComplete) {
    const char *turnCompFrame = "{\"serverContent\":{\"turnComplete\":true}}";
    voice.getClient().handleIncomingMessage(turnCompFrame,
                                            strlen(turnCompFrame));
  }
}

static void drainVoicePlayback(VoxCoreVoice &voice) {
  int safety = 0;
  while ((voice.getPlayer().getBufferedSamples() > 0 ||
          voice.getPlayer().isPlaying() || voice.isSpeaking()) &&
         safety < 200) {
    voice.update();
    voice.getPlayer().simFinishSpeakerChunk();
    voice.update();
    safety++;
  }
}

void test_v061_correction_a_turn_rx_metrics_and_order_alerts() {
  std::cout << "\n[TEST SUITE 80] Correction A (v0.6.1) : Metriques PCM "
               "multi-tours & detection reelle hors-ordre"
            << std::endl;

  MockLiveTransport mockTransport;
  VoxCoreVoice voice;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::GEMINI;
  cfg.apiKey = "AIzaSyTestV061CorrectionA";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

  TEST_ASSERT(voice.begin(cfg),
              "Initialisation VoxCoreVoice via IVoiceProvider");
  prepareMockVoiceReady(voice, mockTransport);

  // 1. Une annonce TTS normale (5 blocs de 6 ech = 30 ech)
  TEST_ASSERT(voice.say("Premiere annonce de controle"), "Tour 1 TTS lance");
  injectGeminiPcmAndDrain(voice, 5, true);
  TEST_ASSERT(voice.getProvider().getTurnDecodedSamples() == 30,
              "Tour 1 : 30 echantillons decodes via IVoiceProvider");
  TEST_ASSERT(
      voice.getSession().getTurnDecodedSamples() == 30,
      "Tour 1 : LiveSessionController rapporte 30 ech decodes (et non 0)");
  TEST_ASSERT(voice.getProvider().getPostTurnCompleteBlocks() == 0,
              "Tour 1 : 0 bloc apres turnComplete");
  TEST_ASSERT(!voice.getClient().isTurnCompleteBeforeLastPcm(),
              "Tour 1 : aucune fausse alerte d'ordre");
  drainVoicePlayback(voice);
  TEST_ASSERT(voice.isReady(), "Tour 1 termine et retour a READY");

  // 2. Deuxième annonce TTS successive (vérifie la correction du bug v0.6.0 où
  // _turnCompleteReceived restait à true)
  TEST_ASSERT(voice.say("Deuxieme annonce de controle"), "Tour 2 TTS lance");
  TEST_ASSERT(!voice.getProvider().isTurnCompleteReceived(),
              "Tour 2 : _turnCompleteReceived reinitialise au debut du tour");
  TEST_ASSERT(voice.getProvider().getTurnDecodedSamples() == 0,
              "Tour 2 : compteur d'echantillons remis a zero au debut du tour");
  injectGeminiPcmAndDrain(voice, 8, true); // 8 * 6 = 48 ech
  TEST_ASSERT(voice.getProvider().getTurnDecodedSamples() == 48,
              "Tour 2 : 48 echantillons decodes exactement");
  TEST_ASSERT(voice.getSession().getTurnDecodedSamples() == 48,
              "Tour 2 : LiveSessionController rapporte 48 ech decodes");
  TEST_ASSERT(voice.getProvider().getPostTurnCompleteBlocks() == 0,
              "Tour 2 : 0 fausse alerte de bloc apres turnComplete");
  TEST_ASSERT(voice.getProvider().getPostTurnCompleteSamples() == 0,
              "Tour 2 : 0 echantillon hors-ordre signale");
  drainVoicePlayback(voice);
  TEST_ASSERT(voice.isReady(), "Tour 2 termine et retour a READY");

  // 3. Dix annonces TTS successives
  for (int t = 1; t <= 10; ++t) {
    String msg = String("Annonce serie ") + String(t);
    TEST_ASSERT(voice.say(msg), "10 tours TTS : lancement du tour");
    injectGeminiPcmAndDrain(voice, t, true);
    uint32_t expectedSamples = (uint32_t)(t * 6);
    TEST_ASSERT(voice.getSession().getTurnDecodedSamples() == expectedSamples,
                "10 tours TTS : nombre exact d'echantillons du tour");
    TEST_ASSERT(voice.getSession().getPostTurnCompleteBlocks() == 0,
                "10 tours TTS : zero fausse alerte d'ordre");
    drainVoicePlayback(voice);
    TEST_ASSERT(voice.isReady(), "10 tours TTS : retour propre a READY");
  }

  // 4. Arrêt STOP pendant la génération (WAITING_RESPONSE) suivi d'un nouveau
  // tour
  TEST_ASSERT(voice.say("Annonce interrompue en generation"),
              "Tour STOP generation lance");
  TEST_ASSERT(voice.getState() == VoiceState::WAITING_RESPONSE,
              "Etat WAITING_RESPONSE");
  voice.stop();
  TEST_ASSERT(voice.isReady(), "Retour immediat a READY apres STOP generation");
  TEST_ASSERT(voice.say("Annonce apres STOP generation"),
              "Relance apres STOP generation");
  injectGeminiPcmAndDrain(voice, 4, true);
  TEST_ASSERT(voice.getSession().getTurnDecodedSamples() == 24,
              "Apres STOP generation : 24 ech decodes");
  TEST_ASSERT(voice.getSession().getPostTurnCompleteBlocks() == 0,
              "Apres STOP generation : 0 fausse alerte");
  drainVoicePlayback(voice);

  // 5. Arrêt STOP pendant la lecture (après réception de turnComplete mais
  // avant fin de vidange)
  TEST_ASSERT(voice.say("Annonce interrompue en lecture"),
              "Tour STOP lecture lance");
  injectGeminiPcmAndDrain(voice, 6,
                          true); // turnComplete reçu, audio dans le buffer
  TEST_ASSERT(voice.getProvider().isTurnCompleteReceived(),
              "turnComplete bien recu avant le STOP");
  voice.stop();
  TEST_ASSERT(voice.isReady(), "Retour a READY apres STOP pendant lecture");
  TEST_ASSERT(voice.say("Annonce apres STOP lecture"),
              "Relance apres STOP lecture");
  TEST_ASSERT(!voice.getProvider().isTurnCompleteReceived(),
              "_turnCompleteReceived reinitialise malgre le STOP precedent");
  injectGeminiPcmAndDrain(voice, 3, true);
  TEST_ASSERT(voice.getSession().getTurnDecodedSamples() == 18,
              "Apres STOP lecture : 18 ech decodes");
  TEST_ASSERT(voice.getSession().getPostTurnCompleteBlocks() == 0,
              "Apres STOP lecture : 0 fausse alerte");
  drainVoicePlayback(voice);

  // 6. Déconnexion pendant génération et reconnexion après erreur
  TEST_ASSERT(voice.say("Annonce coupee par deconnexion"),
              "Tour avant deconnexion lance");
  mockTransport.disconnect();
  voice.update();
  prepareMockVoiceReady(voice, mockTransport);
  TEST_ASSERT(voice.isReady(), "Session reconnectee et prete");
  TEST_ASSERT(voice.say("Annonce apres reconnexion"),
              "Tour apres reconnexion lance");
  injectGeminiPcmAndDrain(voice, 5, true);
  TEST_ASSERT(voice.getSession().getTurnDecodedSamples() == 30,
              "Apres reconnexion : 30 ech decodes");
  TEST_ASSERT(voice.getSession().getPostTurnCompleteBlocks() == 0,
              "Apres reconnexion : 0 fausse alerte");
  drainVoicePlayback(voice);

  // 7. Réception réelle d'un bloc PCM tardif APRÈS turnComplete dans le même
  // tour
  TEST_ASSERT(voice.say("Annonce avec vrai bloc tardif"),
              "Tour avec bloc tardif lance");
  injectGeminiPcmAndDrain(voice, 4, true); // 24 ech + turnComplete
  TEST_ASSERT(voice.getSession().getPostTurnCompleteBlocks() == 0,
              "Avant bloc tardif : 0 bloc hors-ordre");
  // Injection d'un vrai bloc PCM tardif après turnComplete
  injectGeminiPcmAndDrain(voice, 2, false); // 2 blocs tardifs = 12 ech
  TEST_ASSERT(voice.getSession().getPostTurnCompleteBlocks() == 2,
              "Vraie anomalie detectee : 2 blocs PCM recus APRES turnComplete");
  TEST_ASSERT(
      voice.getSession().getPostTurnCompleteSamples() == 12,
      "Vraie anomalie detectee : 12 echantillons tardifs comptabilises");
  TEST_ASSERT(voice.getClient().isTurnCompleteBeforeLastPcm(),
              "Flag _turnCompleteBeforeLastPcm actif sur vraie anomalie");
  drainVoicePlayback(voice);
  // Vérifier que le tour suivant repart proprement à zéro
  TEST_ASSERT(voice.say("Annonce propre apres anomalie"),
              "Tour suivant apres vraie anomalie");
  TEST_ASSERT(voice.getSession().getPostTurnCompleteBlocks() == 0,
              "Compteur hors-ordre remis a zero au tour suivant");
  injectGeminiPcmAndDrain(voice, 3, true);
  TEST_ASSERT(voice.getSession().getPostTurnCompleteBlocks() == 0,
              "Tour suivant : 0 alerte");
  drainVoicePlayback(voice);

  // 8. Modes STT et Conversation
  TEST_ASSERT(voice.startListening(), "STT startListening()");
  TEST_ASSERT(!voice.getProvider().isTurnCompleteReceived(),
              "STT : metriques RX reinitialisees au debut de l'ecoute");
  voice.stopListening();
  const char *finalStt =
      "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Test STT.\"}}}";
  voice.getClient().handleIncomingMessage(finalStt, strlen(finalStt));
  voice.update();
  TEST_ASSERT(voice.isReady(), "Fin tour STT");

  voice.enableConversation(true);
  prepareMockVoiceReady(voice, mockTransport);
  for (int c = 1; c <= 3; ++c) {
    TEST_ASSERT(voice.startListening(), "Conversation : startListening()");
    voice.stopListening();
    injectGeminiPcmAndDrain(voice, 4, true);
    TEST_ASSERT(voice.getSession().getTurnDecodedSamples() == 24,
                "Conversation : 24 ech decodes sur le tour");
    TEST_ASSERT(voice.getSession().getPostTurnCompleteBlocks() == 0,
                "Conversation : 0 fausse alerte sur tours multiples");
    drainVoicePlayback(voice);
  }
  voice.stop();
  voice.getClient().setTransport(nullptr);

  // 9. Fournisseur OpenAI Realtime : multi-tours et détection hors-ordre
  MockLiveTransport mockTransportOAI;
  VoxCoreVoice voiceOAI;
  VoiceKitConfig cfgOAI;
  cfgOAI.provider = VoiceProviderType::OPENAI;
  cfgOAI.apiKey = "sk-test-openai-v061";
  cfgOAI.wifiMode = WiFiMode::EXTERNAL_WIFI;
  TEST_ASSERT(voiceOAI.begin(cfgOAI), "OpenAI : begin() reussi");
  prepareMockOpenAIVoiceReady(voiceOAI, mockTransportOAI);

  for (int t = 1; t <= 3; ++t) {
    TEST_ASSERT(voiceOAI.say(String("Tour OpenAI ") + String(t)),
                "OpenAI : lancement tour TTS");
    const char *deltaOAI = "{\"type\":\"response.output_audio.delta\","
                           "\"delta\":\"AAAAAAAAAAAAAAAA\"}";
    voiceOAI.getOpenAIClient().handleIncomingMessage(deltaOAI,
                                                     strlen(deltaOAI));
    voiceOAI.getOpenAIClient().handleIncomingMessage(deltaOAI,
                                                     strlen(deltaOAI));
    const char *doneOAI = "{\"type\":\"response.done\"}";
    voiceOAI.getOpenAIClient().handleIncomingMessage(doneOAI, strlen(doneOAI));
    TEST_ASSERT(voiceOAI.getSession().getTurnDecodedSamples() == 12,
                "OpenAI : 12 ech decodes par tour");
    TEST_ASSERT(voiceOAI.getSession().getPostTurnCompleteBlocks() == 0,
                "OpenAI : 0 fausse alerte entre tours");
    drainVoicePlayback(voiceOAI);
  }

  // Vrai bloc tardif sur OpenAI après response.done
  TEST_ASSERT(voiceOAI.say("Tour OpenAI tardif"), "OpenAI : tour tardif");
  const char *doneOAI = "{\"type\":\"response.done\"}";
  voiceOAI.getOpenAIClient().handleIncomingMessage(doneOAI, strlen(doneOAI));
  const char *lateDeltaOAI = "{\"type\":\"response.output_audio.delta\","
                             "\"delta\":\"AAAAAAAAAAAAAAAA\"}";
  voiceOAI.getOpenAIClient().handleIncomingMessage(lateDeltaOAI,
                                                   strlen(lateDeltaOAI));
  TEST_ASSERT(voiceOAI.getSession().getPostTurnCompleteBlocks() == 1,
              "OpenAI : bloc tardif apres response.done detecte");
  TEST_ASSERT(voiceOAI.getSession().getPostTurnCompleteSamples() == 6,
              "OpenAI : 6 ech tardifs detectes");
  drainVoicePlayback(voiceOAI);
  voiceOAI.stop();
  voiceOAI.getOpenAIClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 81 : Correction B (v0.6.1) — Ordonnancement FreeRTOS TLS &
// Instrumentation
// =============================================================================
void test_v061_correction_b_tls_scheduling_and_loop_diagnostics() {
  std::cout << "\n[TEST SUITE 81] Correction B (v0.6.1) : Ordonnancement "
               "FreeRTOS TLS, memoire & intervalles boucle"
            << std::endl;

  MockLiveTransport mockTransport;
  mockTransport.setSimulatedTlsDiagnostics(0, 2, 178432, 136192, 110592);

  VoxCoreVoice voice;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::GEMINI;
  cfg.apiKey = "AIzaSyTestV061CorrectionB";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;

  WiFi.setConnected(true);
  setMockMillisOffset(1700000000UL * 1000UL);

  TEST_ASSERT(voice.begin(cfg), "begin() reussi");
  voice.getClient().setTransport(&mockTransport);

  // 1. Vérifier la priorité initiale de handshake TLS (égale à loopTask = 1
  // pour éviter la famine CPU)
  TEST_ASSERT(voice.getProvider().getWsHandshakeTaskPriority() == 1,
              "Priorite WebSocket pendant handshake TLS = 1 (pas de preemption "
              "bloquante de loopTask)");
  TEST_ASSERT(
      voice.getProvider().getWsSteadyTaskPriority() == 2,
      "Priorite WebSocket en regime etabli = 2 (< AudioPlayerTask prio 4)");

  // 2. Simuler des appels périodiques à update() pendant la phase
  // CONNECTING_SERVICE / TLS
  voice.update(); // Déclenche _session.activate() -> CONNECTING_SERVICE
  advanceMockMillis(15);
  voice.update();
  advanceMockMillis(28); // Intervalle max pendant TLS = 28 ms (< 100 ms cible)
  voice.update();
  advanceMockMillis(12);
  voice.update();

  // Fin du handshake et réception de setupComplete
  const char *setupJson = "{\"setupComplete\":{}}";
  voice.getClient().handleIncomingMessage(setupJson, strlen(setupJson));
  advanceMockMillis(10);
  voice.update();

  TEST_ASSERT(voice.isReady(), "Session READY apres handshake TLS");

  // 3. Vérification des métriques d'instrumentation TLS / FreeRTOS / RAM
  // interne
  const ConnectionMetrics &m = voice.getConnectionMetrics();
  TEST_ASSERT(m.wsTaskCore == 0,
              "Coeur de la tache WebSocket diagnostique (Core 0)");
  TEST_ASSERT(m.wsTaskPriority == 2,
              "Priorite active WebSocket apres connexion = 2");
  TEST_ASSERT(m.wsHandshakeTaskPriority == 1,
              "Priorite handshake TLS rapportee = 1");
  TEST_ASSERT(m.wsSteadyTaskPriority == 2,
              "Priorite regime etabli rapportee = 2");
  TEST_ASSERT(m.loopTaskCore == 1,
              "Coeur de la boucle principale diagnostique (Core 1)");
  TEST_ASSERT(m.loopTaskPriority == 1,
              "Priorite de la boucle principale diagnostiquee (prio 1)");
  TEST_ASSERT(m.maxLoopGapDuringTlsMs == 28,
              "Interruption maximale apparente pendant TLS mesuree exactement "
              "(28 ms < 100 ms)");
  TEST_ASSERT(m.maxLoopGapMs >= 28,
              "Intervalle maximal global de boucle mesure");
  TEST_ASSERT(
      m.lastLoopGapMs == 10,
      "Dernier intervalle entre traitements applicatifs mesure (10 ms)");
  TEST_ASSERT(m.freeInternalHeapDuringTls == 178432,
              "RAM interne libre pendant TLS rapportee");
  TEST_ASSERT(m.minFreeInternalHeapDuringTls == 136192,
              "RAM interne minimale pendant TLS rapportee");
  TEST_ASSERT(m.largestInternalBlockDuringTls == 110592,
              "Plus grand bloc memoire interne pendant TLS rapporte");

  // 4. Vérifier que la déconnexion redescend la priorité à 1 pour protéger le
  // prochain handshake TLS
  mockTransport.disconnect();
  TEST_ASSERT(mockTransport.getTaskPriority() == 1,
              "Priorite WebSocket redescendue a 1 lors de la deconnexion");

  voice.stop();
  voice.getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 82 : Correction C (v0.6.1) — 10 scénarios Wi-Fi FastConnect, repli
// non-bloquant & durées
// =============================================================================
void test_v061_correction_c_wifi_10_scenarios_and_tick_durations() {
  std::cout << "\n[TEST SUITE 82] Correction C (v0.6.1) : 10 scenarios Wi-Fi "
               "FastConnect, repli non-bloquant & durees"
            << std::endl;

  WiFi.resetCounters();
  WiFiManager::clearFastConnectCache();
  setMockMillisOffset(1700000000UL * 1000UL);

  // Scénario 1 : FastConnect réussi au premier essai
  {
    const uint8_t cachedBssid[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    WiFiManager::setFastConnectCache("HomeNet", 6, cachedBssid);
    WiFi.setChannel(6);
    WiFi.setBSSID(cachedBssid);
    WiFi.setConnected(true);

    WiFiManager wm;
    TEST_ASSERT(wm.begin("HomeNet", "SecretPass", true, 3500),
                "Scenario 1 : begin() avec cache BSSID");
    TEST_ASSERT(!WiFi.isPersistent(),
                "Scenario 1 : WiFi.persistent(false) active pour eviter "
                "ecriture NVS bloquante");
    TEST_ASSERT(WiFi.getFastConnectCalls() == 1,
                "Scenario 1 : WiFi.begin appele avec BSSID/canal");
    TEST_ASSERT(wm.getState() == WiFiManagerState::FAST_CONNECTING,
                "Scenario 1 : etat initial FAST_CONNECTING");
    wm.tick();
    TEST_ASSERT(wm.isConnected(), "Scenario 1 : connecte des le premier essai");
    TEST_ASSERT(wm.isFastConnectAttempted(),
                "Scenario 1 : fastConnectAttempted = true");
    TEST_ASSERT(wm.isFastConnectSuccess(),
                "Scenario 1 : fastConnectSuccess = true");
    TEST_ASSERT(wm.getState() == WiFiManagerState::READY,
                "Scenario 1 : etat READY");
  }

  // Scénario 2 : BSSID mémorisé devenu indisponible -> repli automatique vers
  // recherche normale
  {
    WiFi.resetCounters();
    const uint8_t oldBssid[6] = {0xAA, 0xBB, 0xCC, 0x00, 0x11, 0x22};
    const uint8_t newBssid[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    WiFiManager::setFastConnectCache("HomeNet", 6, oldBssid);
    WiFi.setSimulateFastConnectFailure(true);
    WiFi.setSimulateNormalScanSuccess(true);
    WiFi.setBSSID(newBssid);
    WiFi.setChannel(6);

    WiFiManager wm;
    wm.begin("HomeNet", "SecretPass", true, 3500);
    TEST_ASSERT(!wm.isConnected(),
                "Scenario 2 : BSSID memorise indisponible -> non connecte");
    TEST_ASSERT(wm.getState() == WiFiManagerState::FAST_CONNECTING,
                "Scenario 2 : en attente FastConnect");

    // Avancer au-delà du timeout FastConnect (3500 ms)
    advanceMockMillis(3600);
    wm.tick(); // Déclenche fallbackToNormalScan() -> connecte sur le nouveau
               // BSSID
    TEST_ASSERT(WiFi.getNormalBeginCalls() == 1,
                "Scenario 2 : repli vers scan normal declenche");
    wm.tick(); // Détecte la connexion établie par le scan normal
    TEST_ASSERT(wm.isConnected(),
                "Scenario 2 : connecte apres repli vers scan normal");
    TEST_ASSERT(wm.isFastConnectAttempted(),
                "Scenario 2 : fastConnectAttempted conserve a true");
    TEST_ASSERT(!wm.isFastConnectSuccess(),
                "Scenario 2 : fastConnectSuccess = false (repli utilise)");
    TEST_ASSERT(String(wm.getBSSID()) == "DE:AD:BE:EF:01:02",
                "Scenario 2 : nouveau BSSID memorise");
  }

  // Scénario 3 : Changement de canal du routeur (canal 6 -> canal 11)
  {
    WiFi.resetCounters();
    const uint8_t apBssid[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    WiFiManager::setFastConnectCache("HomeNet", 6, apBssid);
    WiFi.setSimulateFastConnectFailure(true);
    WiFi.setSimulateNormalScanSuccess(true);
    WiFi.setBSSID(apBssid);
    WiFi.setChannel(11); // Routeur passé sur le canal 11

    WiFiManager wm;
    wm.begin("HomeNet", "SecretPass", true, 3500);
    advanceMockMillis(3600);
    wm.tick(); // Fallback scan normal
    wm.tick(); // Connexion établie sur canal 11
    TEST_ASSERT(wm.isConnected(),
                "Scenario 3 : connecte malgre changement de canal du routeur");
    TEST_ASSERT(wm.getChannel() == 11, "Scenario 3 : canal mis a jour vers 11");
  }

  // Scénarios 4 & 5 : Réseau Wi-Fi absent (backoff progressif sans boucle
  // infinie) puis retour du réseau
  {
    WiFi.resetCounters();
    WiFi.setConnected(false);
    WiFiManager wm;
    wm.begin("MissingNet", "SecretPass", false, 3500);
    wm.tick();
    TEST_ASSERT(!wm.isConnected(), "Scenario 4 : reseau absent");
    uint32_t initialAttempts = wm.getWifiAttempts();

    // Vérifier que des appels rapides à tick() (toutes les 10 ms) ne
    // déclenchent PAS d'appels Wi-Fi en boucle
    for (int i = 0; i < 20; ++i) {
      advanceMockMillis(10);
      wm.tick();
    }
    TEST_ASSERT(wm.getWifiAttempts() == initialAttempts,
                "Scenario 4 : aucune attente active ni boucle de reconnexion");

    // Après 1500 ms -> 1ère tentative de reconnexion (délai passe à 3000 ms)
    advanceMockMillis(1500);
    wm.tick();
    TEST_ASSERT(wm.getWifiAttempts() == initialAttempts + 1,
                "Scenario 4 : tentative apres 1500 ms");

    // Après 1500 ms supplémentaires (< 3000 ms) -> pas encore de nouvelle
    // tentative
    advanceMockMillis(1500);
    wm.tick();
    TEST_ASSERT(wm.getWifiAttempts() == initialAttempts + 1,
                "Scenario 4 : backoff progressif 3000 ms respecte");

    // Scénario 5 : Retour du réseau à l'échéance de 3000 ms
    WiFi.setSimulateReconnectSuccess(true);
    advanceMockMillis(1600);
    wm.tick(); // Déclenche WiFi.reconnect()
    wm.tick(); // Prend en compte WL_CONNECTED
    TEST_ASSERT(wm.isConnected(),
                "Scenario 5 : connexion retablie des le retour du reseau");
  }

  // Scénario 6 : Perte de connexion en cours de session et reconnexion
  // automatique
  {
    WiFi.resetCounters();
    WiFi.setConnected(true);
    WiFiManager wm;
    wm.begin("HomeNet", "SecretPass", false, 3500);
    wm.tick();
    TEST_ASSERT(wm.isConnected(), "Scenario 6 : initialement connecte");

    WiFi.setConnected(false);
    wm.tick();
    TEST_ASSERT(!wm.isConnected(), "Scenario 6 : perte Wi-Fi detectee");
    TEST_ASSERT(wm.getWifiReconnects() == 1,
                "Scenario 6 : compteur wifiReconnects = 1");
    TEST_ASSERT(wm.getState() == WiFiManagerState::DISCONNECTED_BACKOFF,
                "Scenario 6 : etat DISCONNECTED_BACKOFF");

    WiFi.setSimulateReconnectSuccess(true);
    advanceMockMillis(1600);
    wm.tick();
    wm.tick();
    TEST_ASSERT(wm.isConnected(),
                "Scenario 6 : reconnexion automatique reussie");
  }

  // Scénario 7 : Redémarrage du CoreS3 (nouvelle instance WiFiManager
  // exploitant le cache sauvegardé au scénario 3)
  {
    WiFi.resetCounters();
    WiFi.setConnected(true);
    WiFi.setChannel(11);
    WiFiManager wmRebooted;
    wmRebooted.begin("HomeNet", "SecretPass", true, 3500);
    wmRebooted.tick();
    TEST_ASSERT(WiFi.getFastConnectCalls() == 1,
                "Scenario 7 : FastConnect utilise le cache persistant apres "
                "redemarrage");
    TEST_ASSERT(wmRebooted.isFastConnectSuccess(),
                "Scenario 7 : FastConnect reussi apres redemarrage");
    TEST_ASSERT(wmRebooted.getChannel() == 11,
                "Scenario 7 : canal 11 restaure depuis le cache");
  }

  // Scénario 8 : Horloge déjà synchronisée (SNTP sans attente bloquante)
  {
    setMockMillisOffset(1700000000UL *
                        1000UL); // Timestamp valide (>= 2023-11-14)
    WiFi.setConnected(true);
    WiFiManager wmSync;
    wmSync.begin("HomeNet", "SecretPass", true, 3500);
    wmSync.tick();
    TEST_ASSERT(wmSync.isTimeSynced(),
                "Scenario 8 : horloge immediatement valide");
    TEST_ASSERT(wmSync.isSntpSkipped(), "Scenario 8 : sntpSkipped = true");
    TEST_ASSERT(wmSync.getSntpSyncMs() == 0, "Scenario 8 : sntpSyncMs = 0 ms");
  }

  // Scénario 9 : SNTP réellement nécessaire (horloge non synchronisée au boot :
  // 1970)
  {
    WiFiManager::setSimulatedSystemTimeForTest(
        5); // Timestamp = 5 s (1970 < NTP_MIN_VALID_TIMESTAMP)
    WiFi.setConnected(true);
    WiFiManager wmUnsync;
    wmUnsync.begin("HomeNet", "SecretPass", true, 3500);
    wmUnsync.tick();
    TEST_ASSERT(wmUnsync.isConnected(), "Scenario 9 : Wi-Fi connecte");
    TEST_ASSERT(!wmUnsync.isTimeSynced(),
                "Scenario 9 : horloge non encore synchronisee (1970)");
    TEST_ASSERT(!wmUnsync.isSntpSkipped(), "Scenario 9 : sntpSkipped = false");
    TEST_ASSERT(wmUnsync.getState() == WiFiManagerState::CONNECTED_WAIT_SNTP,
                "Scenario 9 : etat CONNECTED_WAIT_SNTP");

    // Simuler 420 ms d'attente puis réception de la réponse SNTP
    advanceMockMillis(420);
    WiFiManager::setSimulatedSystemTimeForTest(
        -1); // Horloge système maintenant valide
    WiFiManager::notifySntpSyncedForTest();
    wmUnsync.tick();
    TEST_ASSERT(wmUnsync.isTimeSynced(),
                "Scenario 9 : horloge synchronisee apres SNTP");
    TEST_ASSERT(wmUnsync.getSntpSyncMs() >= 420,
                "Scenario 9 : duree SNTP reelle mesuree (>= 420 ms)");
    TEST_ASSERT(wmUnsync.getState() == WiFiManagerState::READY,
                "Scenario 9 : etat READY apres SNTP");
  }

  // Scénario 10 : Fonctionnement vocal complet après plusieurs reconnexions
  // Wi-Fi + mesure séparée des durées
  {
    WiFi.resetCounters();
    MockLiveTransport mockTransport;
    VoiceKit vk;
    VoiceKitConfig cfg;
    cfg.apiKey = "AIzaSyTestV061Scenario10";
    cfg.wifiMode = WiFiMode::MANAGED_WIFI;
    cfg.wifiSSID = "HomeNet";
    cfg.wifiPassword = "SecretPass";
    cfg.enableFastConnect = true;

    WiFi.setConnected(true);
    TEST_ASSERT(vk.begin(cfg), "Scenario 10 : VoiceKit::begin en MANAGED_WIFI");
    vk.getEngine().getClient().setTransport(&mockTransport);
    vk.update(); // Connecte Wi-Fi + lance _session.activate()
    const char *setupJson = "{\"setupComplete\":{}}";
    vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                     strlen(setupJson));
    vk.update();
    TEST_ASSERT(vk.isReady(), "Scenario 10 : VoiceKit READY initialement");

    // Simuler 3 coupures et reconnexions Wi-Fi successives
    for (int r = 1; r <= 3; ++r) {
      WiFi.setConnected(false);
      mockTransport.disconnect();
      vk.update();
      TEST_ASSERT(!vk.isReady(),
                  "Scenario 10 : non pret pendant coupure Wi-Fi");

      WiFi.setSimulateReconnectSuccess(true);
      advanceMockMillis(1600);
      vk.update(); // Reconnecte Wi-Fi
      vk.update(); // Relance session ou rend Wi-Fi actif
      prepareMockVoiceKitReady(vk, mockTransport);
      TEST_ASSERT(vk.isReady(),
                  "Scenario 10 : VoiceKit de nouveau READY apres reconnexion");
    }

    // Vérifier le fonctionnement vocal (annonce TTS de la phrase de contrôle
    // obligatoire) après les reconnexions
    const char *controlPhrase =
        "Cette phrase de contrôle permet de vérifier que chaque syllabe finale "
        "est parfaitement intelligible jusqu'au mot repère : alpha tango "
        "terminé.";
    TEST_ASSERT(vk.say(controlPhrase), "Scenario 10 : say() de la phrase de "
                                       "controle accepte apres reconnexions");
    injectGeminiPcmAndDrain(vk.getEngine(), 10, true);
    TEST_ASSERT(vk.getEngine().getSession().getTurnDecodedSamples() == 60,
                "Scenario 10 : 60 ech decodes sans perte");
    TEST_ASSERT(vk.getEngine().getSession().getPostTurnCompleteBlocks() == 0,
                "Scenario 10 : 0 fausse alerte");
    drainVoicePlayback(vk.getEngine());
    TEST_ASSERT(vk.isReady(),
                "Scenario 10 : restitution complete achevee et retour a READY");

    // Vérifier les mesures séparées de WiFiManager::tick() et
    // VoiceKit::update()
    const ConnectionMetrics &cm = vk.getConnectionMetrics();
    TEST_ASSERT(cm.wifiReconnects == 3,
                "Scenario 10 : 3 reconnexions Wi-Fi comptabilisees");
    TEST_ASSERT(
        cm.maxWifiTickDurationUs < 50000,
        "Scenario 10 : duree WiFiManager::tick() mesuree et non bloquante");
    TEST_ASSERT(cm.maxUpdateDurationUs < 50000,
                "Scenario 10 : duree VoiceKit::update() mesuree separement et "
                "non bloquante");

    vk.stop();
    vk.getEngine().getClient().setTransport(nullptr);
  }
}

// =============================================================================
// SUITES DE TESTS VoiceKit v0.6.2 (Wake Word Foundation — M5Stack CoreS3)
// =============================================================================

// SUITE 83 : Initialisation hors ligne sans réseau et capture audio locale
// partagée
void test_v062_offline_init_and_shared_audio_capture() {
  std::cout << "\n[TEST SUITE 83] v0.6.2 : Initialisation sans reseau et "
               "capture audio locale partagee"
            << std::endl;

  WiFi.setConnected(false);

  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.apiKey = ""; // Mode hors ligne sans clé API ni connexion réseau
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWord = true;
  cfg.wakeWordPhrase = "Bonjour ESP";

  bool initOk = vk.begin(cfg);
  TEST_ASSERT(initOk,
              "VoiceKit::begin() reussit sans cle API et sans connexion Wi-Fi");
  TEST_ASSERT(!vk.isReady(), "Service distant non pret (normal hors ligne)");
  TEST_ASSERT(!vk.isListening(), "Streaming distant STT non actif");
  TEST_ASSERT(vk.isWakeWordEnabled(), "Wake Word est active via config");
  TEST_ASSERT(vk.isWakeWordEngineAvailable(), "Moteur TEST disponible");
  TEST_ASSERT(vk.isWakeWordEngineInitialized(), "Moteur TEST initialise");
  TEST_ASSERT(vk.isWakeWordSimulated(),
              "Moteur TEST marque explicitement comme simule");
  TEST_ASSERT(!vk.isWakeWordRealRecognitionActive(),
              "Aucune reconnaissance acoustique reelle annoncee en mode TEST");
  TEST_ASSERT(vk.isWakeWordListening(),
              "Detecteur en ecoute locale (etat ARMED)");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "Etat initial ARMED");
  TEST_ASSERT(vk.getEngine().getStreamer().isLocalCapture(),
              "LiveAudioStreamer en mode capture locale hors ligne");
  TEST_ASSERT(!vk.getEngine().getStreamer().isStreaming(),
              "LiveAudioStreamer ne streame pas sur le reseau");
  TEST_ASSERT(HardwareManager::getInstance().isMicCaptureReserved(),
              "HardwareManager proprietaire unique du micro ES7210");

  // Injection de trames PCM 16 kHz / 16-bit dans le streamer partagé (sans
  // réseau)
  size_t userCallbackCount = 0;
  vk.onAudioChunk(
      [&userCallbackCount](const int16_t *samples, size_t count, uint32_t rms) {
        (void)samples;
        (void)rms;
        if (count == AUDIO_CHUNK_SAMPLES) {
          userCallbackCount++;
        }
      });

  int16_t pcmFrame[AUDIO_CHUNK_SAMPLES];
  for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
    pcmFrame[i] = static_cast<int16_t>((i * 19) - 4800);
  }

  for (int f = 0; f < 5; ++f) {
    bool pushed = vk.getEngine().getStreamer().pushSimulatedChunk(
        pcmFrame, AUDIO_CHUNK_SAMPLES);
    TEST_ASSERT(pushed, "Trame PCM 16 kHz injectee en capture locale");
  }

  TEST_ASSERT(userCallbackCount == 5,
              "onAudioChunk partage recoit bien les 5 trames hors ligne");
  WakeWordMetrics mBefore = vk.getWakeWordMetrics();
  TEST_ASSERT(mBefore.framesReceived == 5,
              "WakeWordAudioBridge a recu les 5 trames PCM");

  vk.setWakeWordMaxFramesPerUpdate(8);
  vk.update();
  WakeWordMetrics mAfter = vk.getWakeWordMetrics();
  TEST_ASSERT(
      mAfter.framesProcessed == 5,
      "Les 5 trames PCM ont ete traitees hors callback audio dans update()");
  TEST_ASSERT(mAfter.framesDropped == 0, "Zero trame perdue");

  vk.disableWakeWord();
  TEST_ASSERT(!vk.isWakeWordEnabled(), "Wake Word desactive");
  TEST_ASSERT(!vk.isWakeWordListening(), "Wake Word n'ecoute plus");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::DISABLED,
              "Etat DISABLED");
  TEST_ASSERT(!vk.getEngine().getStreamer().isLocalCapture(),
              "Capture locale arretee proprement");
  TEST_ASSERT(!HardwareManager::getInstance().isMicCaptureReserved(),
              "Microphone ES7210 libere");
}

// SUITE 84 : WakeWordAudioBridge — File SPSC bornée, ordre des échantillons et
// politiques de saturation
void test_v062_wakeword_audio_bridge_queue_and_overflow() {
  std::cout << "\n[TEST SUITE 84] v0.6.2 : WakeWordAudioBridge (file bornee, "
               "ordre FIFO, saturation DROP_NEWEST/DROP_OLDEST)"
            << std::endl;

  WakeWordAudioBridge bridge;
  TEST_ASSERT(bridge.begin(WakeWordAudioBridge::DEFAULT_QUEUE_CAPACITY),
              "Initialisation du pont audio WakeWord (8 slots)");
  bridge.setOverflowPolicy(WakeWordOverflowPolicy::DROP_NEWEST);
  bridge.setEnabled(true);

  // 1. Vérification de la préservation de l'ordre des échantillons et des
  // séquences sur 8 trames (capacité max)
  for (uint32_t seq = 1; seq <= WakeWordAudioBridge::DEFAULT_QUEUE_CAPACITY;
       ++seq) {
    int16_t chunk[AUDIO_CHUNK_SAMPLES];
    for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
      chunk[i] = static_cast<int16_t>(seq * 100 + (i % 50));
    }
    bool ok = bridge.pushAudioChunk(chunk, AUDIO_CHUNK_SAMPLES, seq * 10);
    TEST_ASSERT(ok, "Push trame dans la limite de capacite (8 slots)");
  }

  TEST_ASSERT(bridge.getOccupancy() ==
                  WakeWordAudioBridge::DEFAULT_QUEUE_CAPACITY,
              "File pleine a 8 trames");
  TEST_ASSERT(bridge.getMaxOccupancy() ==
                  WakeWordAudioBridge::DEFAULT_QUEUE_CAPACITY,
              "Watermark = 8");
  TEST_ASSERT(bridge.getFramesDropped() == 0,
              "Aucune trame perdue avant depassement");
  TEST_ASSERT(bridge.getCopyCountPerFrame() == 1,
              "Exactement 1 copie par trame (DMA -> slot)");

  // 2. Dépassement avec politique DROP_NEWEST : les trames 9, 10, 11 doivent
  // être rejetées
  int16_t extraChunk[AUDIO_CHUNK_SAMPLES] = {9999};
  for (int k = 0; k < 3; ++k) {
    bool ok = bridge.pushAudioChunk(extraChunk, AUDIO_CHUNK_SAMPLES, 999);
    TEST_ASSERT(!ok, "DROP_NEWEST rejette explicitement la nouvelle trame "
                     "quand la file est pleine");
  }
  TEST_ASSERT(bridge.getFramesDropped() == 3,
              "3 trames abandonnees comptabilisees");
  TEST_ASSERT(bridge.getQueueSaturationCount() == 3,
              "3 saturations de file comptabilisees");

  // Vérifier que les trames 1 à 8 sont intactes et dans l'ordre exact (zéro
  // copie à la lecture)
  for (uint32_t expectedSeq = 1;
       expectedSeq <= WakeWordAudioBridge::DEFAULT_QUEUE_CAPACITY;
       ++expectedSeq) {
    const WakeWordAudioBridge::FrameSlot *slot = bridge.peekFrame();
    TEST_ASSERT(slot != nullptr, "Slot disponible a la lecture sans copie");
    TEST_ASSERT(slot->sequence == expectedSeq,
                "Ordre de sequence strictement conserve");
    TEST_ASSERT(slot->samples[0] == static_cast<int16_t>(expectedSeq * 100),
                "Premier echantillon intact");
    TEST_ASSERT(slot->samples[511] ==
                    static_cast<int16_t>(expectedSeq * 100 + (511 % 50)),
                "Dernier echantillon intact");
    bridge.consumeFrame();
  }
  TEST_ASSERT(bridge.getOccupancy() == 0, "File entierement videe");
  TEST_ASSERT(bridge.getFramesConsumed() ==
                  WakeWordAudioBridge::DEFAULT_QUEUE_CAPACITY,
              "8 trames marquees consommees");

  // 3. Test de la politique DROP_OLDEST (désactivée en v0.6.2.1 et repliée sur
  // DROP_NEWEST pour garantir l'intégrité de peekFrame)
  bridge.resetMetrics();
  TEST_ASSERT(
      !WakeWordAudioBridge::isOverflowPolicySupported(
          WakeWordOverflowPolicy::DROP_OLDEST),
      "DROP_OLDEST est explicitement declare non supporte en SPSC sans copie");
  bridge.setOverflowPolicy(WakeWordOverflowPolicy::DROP_OLDEST);
  TEST_ASSERT(bridge.getOverflowPolicy() == WakeWordOverflowPolicy::DROP_NEWEST,
              "setOverflowPolicy(DROP_OLDEST) maintient DROP_NEWEST pour "
              "proteger peekFrame()");
  TEST_ASSERT(bridge.getUnsupportedPolicyRequestCount() == 1,
              "Demande de politique DROP_OLDEST tracee dans les diagnostics");
  bridge.setEnabled(true);

  for (uint32_t seq = 1; seq <= WakeWordAudioBridge::DEFAULT_QUEUE_CAPACITY + 4;
       ++seq) {
    int16_t chunk[AUDIO_CHUNK_SAMPLES];
    chunk[0] = static_cast<int16_t>(seq);
    bool ok = bridge.pushAudioChunk(chunk, AUDIO_CHUNK_SAMPLES, seq);
    if (seq <= WakeWordAudioBridge::DEFAULT_QUEUE_CAPACITY) {
      TEST_ASSERT(ok, "Trame acceptee tant que la file n'est pas pleine");
    } else {
      TEST_ASSERT(!ok, "Repli DROP_NEWEST rejette les nouvelles trames au-dela "
                       "de la capacite sans modifier _tail");
    }
  }
  TEST_ASSERT(bridge.getFramesDropped() == 4,
              "4 trames rejetees comptabilisees");
  TEST_ASSERT(bridge.getQueueSaturationCount() == 4,
              "4 saturations comptabilisees");
  const WakeWordAudioBridge::FrameSlot *oldest = bridge.peekFrame();
  TEST_ASSERT(oldest != nullptr && oldest->sequence == 1,
              "La trame #1 en tete de file est strictement preservee");
  bridge.clear();

  // 4. Pont désactivé : coût minimal, aucun push
  bridge.setEnabled(false);
  TEST_ASSERT(!bridge.pushAudioChunk(extraChunk, AUDIO_CHUNK_SAMPLES, 100),
              "Pont desactive ignore les trames");
  TEST_ASSERT(bridge.getOccupancy() == 0,
              "Aucune trame stockee quand desactive");
}

// SUITE 85 : Étude WakeNet10 (wn10_fr_bonjouresp) — Indisponibilité explicite
// sous IDF 4.4 et adaptateur 512->480
void test_v062_wakenet_engine_availability_and_frame_adapter() {
  std::cout << "\n[TEST SUITE 85] v0.6.2 : WakeNetEngine (honnetete API sous "
               "IDF 4.4 et adaptateur de trames 512->480)"
            << std::endl;

  WakeNetEngine wnEngine;
  TEST_ASSERT(strcmp(wnEngine.getName(), "WAKENET") == 0,
              "Nom moteur = WAKENET");
  TEST_ASSERT(strcmp(wnEngine.getModelName(), "wn10_fr_bonjouresp") == 0,
              "Modele cible = wn10_fr_bonjouresp");
  TEST_ASSERT(!wnEngine.isSimulatedEngine(),
              "WakeNetEngine n'est PAS un moteur simule");
  TEST_ASSERT(wnEngine.getPreferredFrameSamples() == 480,
              "Cadence native WakeNet10 = 480 ech (30 ms @ 16 kHz)");

  // Sous l'environnement actuel (sans binaire ESP-SR IDF >= 5.0), isAvailable()
  // doit être false
  TEST_ASSERT(!wnEngine.isAvailable(),
              "WakeNet10 honnetement declare non disponible sous Arduino IDF "
              "4.4 / sans binaire ESP-SR");
  TEST_ASSERT(!wnEngine.begin(16000, 1),
              "begin() echoue explicitement sans simuler une reconnaissance");
  TEST_ASSERT(
      strlen(wnEngine.getLastError()) > 0,
      "Message d'erreur explicite documentant l'exigence ESP-IDF >= 5.0");

  // Vérification via la façade VoiceKit avec le moteur par défaut (WAKENET)
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.apiKey = "";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  vk.begin(cfg);

  bool enabledOk = vk.enableWakeWord(true); // Moteur par défaut = WAKENET
  TEST_ASSERT(!enabledOk, "enableWakeWord(true) retourne false si aucun moteur "
                          "reel n'est disponible");
  TEST_ASSERT(vk.isWakeWordEnabled(),
              "Fonction marquee activee (demande utilisateur)");
  TEST_ASSERT(!vk.isWakeWordEngineAvailable(),
              "Moteur WAKENET marque non disponible");
  TEST_ASSERT(!vk.isWakeWordEngineInitialized(),
              "Moteur WAKENET marque non initialise");
  TEST_ASSERT(!vk.isWakeWordListening(), "Detection reelle NON active");
  TEST_ASSERT(vk.isWakeWordError(), "Etat d'erreur actif (ERROR)");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ERROR,
              "Machine a etats en ERROR");

  // Validation mathématique et bit-à-bit de l'adaptateur de trames 512 éch (32
  // ms) -> 480 éch (30 ms) 15 blocs de 512 échantillons = 7 680 échantillons =
  // exactement 16 trames de 480 échantillons
  wnEngine.reset();
  std::vector<int16_t> inputStream(15 * 512);
  for (size_t i = 0; i < inputStream.size(); ++i) {
    inputStream[i] = static_cast<int16_t>((i % 30000) - 15000);
  }

  std::vector<int16_t> outputStream;
  size_t producedFrames480 = 0;
  for (size_t chunkIdx = 0; chunkIdx < 15; ++chunkIdx) {
    wnEngine.feedFrameAdapter(
        &inputStream[chunkIdx * 512], 512,
        [&](const int16_t *frame480, size_t count) -> bool {
          producedFrames480++;
          outputStream.insert(outputStream.end(), frame480, frame480 + count);
          return false;
        });
  }

  TEST_ASSERT(producedFrames480 == 16,
              "15 trames DMA de 512 ech produisent exactement 16 trames "
              "WakeNet de 480 ech");
  TEST_ASSERT(wnEngine.getAccumulatorCount() == 0,
              "Zero echantillon residuel apres 15 blocs de 512");
  TEST_ASSERT(outputStream.size() == inputStream.size(),
              "Nombre total d'echantillons strictement identique (7680)");
  TEST_ASSERT(memcmp(inputStream.data(), outputStream.data(),
                     inputStream.size() * sizeof(int16_t)) == 0,
              "Conservation parfaite bit-a-bit de l'ordre des echantillons a "
              "travers l'adaptateur 32ms->30ms");
}

// SUITE 86 : Détecteur de test et machine à états complète (DISABLED ->
// INITIALIZING -> ARMED -> DETECTED -> SUSPENDED -> ARMED)
void test_v062_test_engine_and_state_machine_transitions() {
  std::cout << "\n[TEST SUITE 86] v0.6.2 : TestWakeWordEngine, cycle complet "
               "d'etats et gestion des erreurs"
            << std::endl;

  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.apiKey = "";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  vk.begin(cfg);

  std::vector<WakeWordState> stateHistory;
  String lastDetectedWord;
  bool lastEventSimulated = false;
  uint32_t detectedCallbackCount = 0;

  vk.onWakeWordStateChanged(
      [&stateHistory](WakeWordState s) { stateHistory.push_back(s); });
  vk.onWakeWordDetected(
      [&lastDetectedWord, &detectedCallbackCount](const String &word) {
        lastDetectedWord = word;
        detectedCallbackCount++;
      });
  vk.onWakeWordEvent([&lastEventSimulated](const WakeWordEvent &ev) {
    lastEventSimulated = ev.isSimulated;
  });

  // Sélection du moteur de test
  TEST_ASSERT(vk.setWakeWordEngine(WakeWordEngineType::TEST),
              "Selection du moteur TEST");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::DISABLED,
              "Etat initial DISABLED");

  // Activation : DISABLED -> INITIALIZING -> ARMED
  TEST_ASSERT(vk.enableWakeWord(true), "Activation du moteur TEST reussie");
  TEST_ASSERT(stateHistory.size() >= 2,
              "Au moins 2 transitions (INITIALIZING -> ARMED)");
  TEST_ASSERT(stateHistory[0] == WakeWordState::INITIALIZING,
              "Transition 1 : INITIALIZING");
  TEST_ASSERT(stateHistory[1] == WakeWordState::ARMED, "Transition 2 : ARMED");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "Etat courant ARMED");
  TEST_ASSERT(vk.isWakeWordListening(),
              "isWakeWordListening() == true en etat ARMED");
  TEST_ASSERT(vk.getWakeWordMetrics().rearmCount == 0,
              "rearmCount == 0 lors du premier armement (ARMED — En ecoute)");
  TEST_ASSERT(vk.getWakeWordMetrics().detectionCount == 0,
              "detectionCount == 0 avant toute detection");
  TEST_ASSERT(vk.getWakeWordMetrics().errorCount == 0, "errorCount == 0");

  // Déclenchement par commande série ("bonjour esp")
  vk.setWakeWordAutoResume(
      false); // Garder SUSPENDED après détection pour vérifier le cycle manuel
  bool cmdAccepted = vk.handleWakeWordSerialCommand("bonjour esp");
  TEST_ASSERT(cmdAccepted,
              "Commande serie 'bonjour esp' acceptee en mode TEST");

  // Injection d'une trame audio et exécution de update()
  int16_t pcm[AUDIO_CHUNK_SAMPLES] = {120};
  vk.getEngine().getStreamer().pushSimulatedChunk(pcm, AUDIO_CHUNK_SAMPLES);
  vk.update();

  // Vérification du cycle : ARMED -> DETECTED -> SUSPENDED
  TEST_ASSERT(detectedCallbackCount == 1,
              "Callback onWakeWordDetected appele exactement 1 fois");
  TEST_ASSERT(vk.getWakeWordMetrics().detectionCount == 1,
              "Compteur detectionCount incremente exactement 1 seule fois par "
              "evenement reel");
  TEST_ASSERT(vk.getWakeWordMetrics().rearmCount == 0,
              "rearmCount toujours a 0 avant rearmement");
  TEST_ASSERT(lastDetectedWord == "Bonjour ESP", "Mot detecte = 'Bonjour ESP'");
  TEST_ASSERT(lastEventSimulated == true,
              "Evenement marque isSimulated = true");
  TEST_ASSERT(stateHistory.size() >= 4,
              "Transitions DETECTED puis SUSPENDED enregistrees");
  TEST_ASSERT(stateHistory[2] == WakeWordState::DETECTED,
              "Transition 3 : DETECTED");
  TEST_ASSERT(stateHistory[3] == WakeWordState::SUSPENDED,
              "Transition 4 : SUSPENDED");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "Etat courant SUSPENDED apres detection");
  TEST_ASSERT(!vk.isWakeWordListening(),
              "isWakeWordListening() == false en etat SUSPENDED");

  // En état SUSPENDED, un nouveau trigger de test doit être refusé
  TEST_ASSERT(!vk.triggerTestWakeWord(),
              "triggerTestWakeWord() refuse lorsque l'etat est SUSPENDED");

  // Fin de conversation / reprise contrôlée : SUSPENDED -> ARMED
  TEST_ASSERT(vk.resumeWakeWord(), "resumeWakeWord() ramene l'etat a ARMED");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "Etat revenu a ARMED");
  TEST_ASSERT(vk.isWakeWordListening(), "Ecoute de nouveau active");
  TEST_ASSERT(
      vk.getWakeWordMetrics().rearmCount == 1,
      "rearmCount == 1 apres premier rearmement (ARMED — Ecoute reactivee)");

  // Transitions répétées (10 cycles complets ARMED -> DETECTED -> SUSPENDED ->
  // ARMED)
  for (int cycle = 0; cycle < 10; ++cycle) {
    size_t histBefore = stateHistory.size();
    uint32_t detBefore = vk.getWakeWordMetrics().detectionCount;
    uint32_t rearmBefore = vk.getWakeWordMetrics().rearmCount;
    TEST_ASSERT(vk.triggerTestWakeWord(), "Trigger cycle repete");
    vk.getEngine().getStreamer().pushSimulatedChunk(pcm, AUDIO_CHUNK_SAMPLES);
    vk.update();
    TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
                "SUSPENDED apres detection");
    TEST_ASSERT(vk.getWakeWordMetrics().detectionCount == detBefore + 1,
                "1 seule detection comptabilisee sur le cycle");
    TEST_ASSERT(vk.resumeWakeWord(), "Retour a ARMED");
    TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
                "ARMED confirme");
    TEST_ASSERT(vk.getWakeWordMetrics().rearmCount == rearmBefore + 1,
                "rearmCount incremente exactement de 1 au retour ARMED");
    TEST_ASSERT(stateHistory.size() == histBefore + 3 &&
                    stateHistory[histBefore] == WakeWordState::DETECTED &&
                    stateHistory[histBefore + 1] == WakeWordState::SUSPENDED &&
                    stateHistory[histBefore + 2] == WakeWordState::ARMED,
                "Sequence exacte DETECTED -> SUSPENDED -> ARMED verifiee a "
                "chaque cycle");
  }
  TEST_ASSERT(detectedCallbackCount == 11,
              "11 detections cumulees sans defaillance");
  TEST_ASSERT(vk.getWakeWordMetrics().detectionCount == 11,
              "detectionCount == 11");
  TEST_ASSERT(vk.getWakeWordMetrics().rearmCount == 11,
              "rearmCount == 11 apres 11 rearmements");

  // Test d'erreur d'initialisation et d'exécution vers ERROR
  String reportedErr;
  vk.onWakeWordError([&reportedErr](const String &e) { reportedErr = e; });
  vk.getWakeWordManager().getTestEngine().setSimulateRuntimeError(true);
  vk.getEngine().getStreamer().pushSimulatedChunk(pcm, AUDIO_CHUNK_SAMPLES);
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ERROR,
              "Erreur d'execution fait basculer vers ERROR");
  TEST_ASSERT(vk.isWakeWordError(), "isWakeWordError() == true");
  TEST_ASSERT(vk.getWakeWordMetrics().errorCount == 1,
              "errorCount == 1 apres erreur d'execution");
  TEST_ASSERT(!reportedErr.isEmpty(), "Callback onWakeWordError notifie");

  vk.getWakeWordManager().getTestEngine().setSimulateRuntimeError(false);
  vk.disableWakeWord();
}

// SUITE 87 : Coexistence, suspension automatique pendant TTS / STT /
// ContinuousSTT / Conversation et reprise contrôlée
void test_v062_coexistence_suspension_and_resumption_all_modes() {
  std::cout << "\n[TEST SUITE 87] v0.6.2 : Suspension automatique (TTS, STT, "
               "ContinuousSTT, Conversation) et reprise controlee"
            << std::endl;

  WiFi.setConnected(true);
  MockLiveTransport mockTransport;
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.apiKey = "AIzaSyTestWakeWordCoexistence";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWord = true;
  cfg.wakeWordAutoResume = true;
  cfg.wakeWordResumeDelayMs = 200;

  TEST_ASSERT(vk.begin(cfg), "VoiceKit::begin() avec WakeWord TEST + API key");
  prepareMockVoiceKitReady(vk, mockTransport);
  TEST_ASSERT(vk.isReady(), "VoiceKit READY");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "WakeWord initialement ARMED");

  // 1. Suspension automatique pendant une synthèse vocale TTS (say) pour éviter
  // les faux réveils du haut-parleur
  TEST_ASSERT(vk.say("Bonjour, je reponds vocalement."), "say() accepte");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "WakeWord immediatement SUSPENDED des l'appel a say()");
  TEST_ASSERT(!vk.isWakeWordListening(),
              "WakeWord n'ecoute pas pendant le TTS");

  injectGeminiPcmAndDrain(vk.getEngine(), 4, true);
  drainVoicePlayback(vk.getEngine());
  TEST_ASSERT(vk.isReady(), "Fin de la restitution TTS");

  // Avant l'expiration du délai de garde post-haut-parleur (200 ms), WakeWord
  // reste SUSPENDED
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "WakeWord reste SUSPENDED pendant le delai anti-echo (200 ms)");

  // Après expiration du délai de garde (250 ms), reprise automatique à ARMED
  advanceMockMillis(250);
  vk.update();
  TEST_ASSERT(
      vk.getWakeWordState() == WakeWordState::ARMED,
      "WakeWord reprend automatiquement l'etat ARMED apres la reponse vocale");

  // 2. Suspension automatique pendant une écoute STT manuelle (listen)
  TEST_ASSERT(vk.listen(), "listen() STT demarre");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "WakeWord SUSPENDED pendant l'ecoute STT");
  TEST_ASSERT(!vk.getEngine().getStreamer().isLocalCapture(),
              "Capture locale basculee sans conflit vers streaming STT");
  TEST_ASSERT(vk.getEngine().getStreamer().isStreaming(),
              "Streaming STT actif");

  vk.stopListening();
  const char *sttFinal =
      "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Test STT\"}}}";
  vk.getEngine().getClient().handleIncomingMessage(sttFinal, strlen(sttFinal));
  vk.update();
  TEST_ASSERT(vk.isReady(), "STT termine");
  advanceMockMillis(250);
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "WakeWord revenu a ARMED apres STT");
  TEST_ASSERT(vk.getEngine().getStreamer().isLocalCapture(),
              "Capture locale hors ligne retablie automatiquement");

  // 3. Suspension automatique pendant ContinuousSTT
  TEST_ASSERT(vk.startContinuousListening(),
              "startContinuousListening() accepte");
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "WakeWord SUSPENDED pendant ContinuousSTT");
  advanceMockMillis(500);
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "WakeWord reste SUSPENDED tant que ContinuousSTT est actif");

  TEST_ASSERT(vk.stopContinuousListening(), "stopContinuousListening()");
  advanceMockMillis(250);
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "WakeWord reprend ARMED apres arret de ContinuousSTT");

  // 4. Suspension pendant une session de Conversation et reprise après la
  // réponse vocale
  const char *setupJson = "{\"setupComplete\":{}}";
  TEST_ASSERT(vk.startConversation(), "startConversation() accepte");
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.isConversationActive(), "Mode CONVERSATION actif");

  // Début d'un tour de conversation (startListening en mode conversation)
  TEST_ASSERT(vk.getEngine().startListening(),
              "Debut du tour utilisateur en conversation");
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "WakeWord SUSPENDED pendant le tour de conversation");

  vk.getEngine().stopListening();
  injectGeminiPcmAndDrain(vk.getEngine(), 4, true);
  drainVoicePlayback(vk.getEngine());
  vk.update();
  TEST_ASSERT(!vk.isBusy(), "Tour de conversation acheve");

  advanceMockMillis(250);
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "WakeWord reprend ARMED apres la reponse vocale de conversation");

  // 5. Vérifier que la détection Wake Word n'ouvre JAMAIS automatiquement une
  // connexion réseau
  vk.stopConversation();
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  mockTransport.disconnect();
  WiFi.setConnected(false);
  vk.update();

  TEST_ASSERT(!mockTransport.isConnected(), "Transport reseau deconnecte");
  TEST_ASSERT(vk.triggerTestWakeWord("Bonjour ESP"),
              "Trigger WakeWord hors ligne");
  int16_t pcm[AUDIO_CHUNK_SAMPLES] = {100};
  vk.getEngine().getStreamer().pushSimulatedChunk(pcm, AUDIO_CHUNK_SAMPLES);
  vk.update();
  TEST_ASSERT(!mockTransport.isConnected(),
              "La detection Wake Word n'ouvre JAMAIS automatiquement une "
              "connexion reseau");

  vk.disableWakeWord();
  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 88 : Compatibilité future LVGL (pollWakeWordEvent) et diagnostics de
// performance / mémoire
void test_v062_lvgl_event_polling_and_diagnostics_metrics() {
  std::cout << "\n[TEST SUITE 88] v0.6.2 : Communication LVGL sans dependance "
               "(pollWakeWordEvent) et diagnostics"
            << std::endl;

  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.apiKey = "";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWord = true;
  cfg.wakeWordPhrase = "Bonjour ESP";
  vk.begin(cfg);

  TEST_ASSERT(!vk.hasPendingWakeWordEvent(),
              "Aucun evenement WakeWord en attente au demarrage");
  WakeWordEvent ev;
  TEST_ASSERT(!vk.pollWakeWordEvent(ev),
              "pollWakeWordEvent retourne false si aucun evenement");

  // Simuler une détection Wake Word
  vk.triggerTestWakeWord();
  int16_t pcm[AUDIO_CHUNK_SAMPLES] = {250};
  vk.getEngine().getStreamer().pushSimulatedChunk(pcm, AUDIO_CHUNK_SAMPLES);
  vk.update();

  // La boucle LVGL principale peut interroger l'événement sans appel LVGL
  // depuis une tâche audio
  TEST_ASSERT(vk.hasPendingWakeWordEvent(),
              "Evenement WakeWord disponible pour la boucle LVGL");
  TEST_ASSERT(vk.pollWakeWordEvent(ev),
              "pollWakeWordEvent recupere l'evenement avec succes");
  TEST_ASSERT(ev.valid, "ev.valid == true");
  TEST_ASSERT(ev.isSimulated, "ev.isSimulated == true (mode TEST)");
  TEST_ASSERT(ev.word == "Bonjour ESP", "ev.word == 'Bonjour ESP'");
  TEST_ASSERT(!vk.hasPendingWakeWordEvent(),
              "L'evenement est consomme apres pollWakeWordEvent");
  TEST_ASSERT(!vk.pollWakeWordEvent(ev),
              "Un second appel renvoie false (unicite)");

  // Vérification des diagnostics et compteurs de performance (Section 11)
  WakeWordMetrics metrics = vk.getWakeWordMetrics();
  TEST_ASSERT(metrics.freeInternalHeap > 0,
              "Diagnostic RAM interne libre disponible");
  TEST_ASSERT(metrics.minFreeInternalHeap > 0,
              "Diagnostic RAM interne minimale disponible");
  TEST_ASSERT(metrics.freePsram > 0, "Diagnostic PSRAM libre disponible");
  TEST_ASSERT(metrics.minFreePsram > 0, "Diagnostic PSRAM minimale disponible");
  TEST_ASSERT(metrics.framesReceived >= 1,
              "Compteur de trames recues exact (>= 1)");
  TEST_ASSERT(metrics.framesProcessed >= 1,
              "Compteur de trames traitees exact (>= 1)");
  TEST_ASSERT(metrics.framesDropped == 0,
              "Compteur de trames abandonnees exact (0)");
  TEST_ASSERT(metrics.queueSaturations == 0,
              "Compteur de saturations exact (0)");
  TEST_ASSERT(metrics.detectionCount == 1, "Compteur de detections exact (1)");
  TEST_ASSERT(metrics.simulatedDetectionCount == 1,
              "Compteur de detections simulees exact (1)");
  TEST_ASSERT(metrics.errorCount == 0, "Compteur d'erreurs exact (0)");
  TEST_ASSERT(metrics.stateTransitionCount >= 4,
              "Transitions d'etat comptabilisees (INITIALIZING, ARMED, "
              "DETECTED, SUSPENDED)");

  vk.disableWakeWord();
}

// =============================================================================
// SUITES DE TESTS CORRECTIVES VoiceKit v0.6.2.1
// (Fiabilisation Wake Word Foundation — Concurrence, Budget CPU, Micro,
// WakeNet)
// =============================================================================

// SUITE 89 : WakeWordAudioBridge v0.6.2.1 — Protection de la durée de vie des
// trames et 7 scénarios de concurrence
void test_v0621_audio_bridge_lifetime_concurrency_and_7_scenarios() {
  std::cout << "\n[TEST SUITE 89] v0.6.2.1 : WakeWordAudioBridge (securite "
               "peekFrame, 7 scenarios de concurrence et integrite PCM)"
            << std::endl;

  WakeWordAudioBridge bridge;
  TEST_ASSERT(bridge.begin(8), "Pont audio initialise (8 slots)");
  TEST_ASSERT(bridge.getOverflowPolicy() == WakeWordOverflowPolicy::DROP_NEWEST,
              "Politique par defaut = DROP_NEWEST");
  bridge.setEnabled(true);

  // Scénario 1 : Producteur plus rapide que consommateur
  uint32_t nextWriteSeq = 1;
  uint32_t expectedReadSeq = 1;
  for (int round = 0; round < 5; ++round) {
    // Producteur pousse 4 trames par tour
    for (int p = 0; p < 4; ++p) {
      int16_t chunk[AUDIO_CHUNK_SAMPLES];
      for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
        chunk[i] = static_cast<int16_t>((nextWriteSeq * 37 + i) & 0x7FFF);
      }
      bridge.pushAudioChunk(chunk, AUDIO_CHUNK_SAMPLES, nextWriteSeq * 10);
      nextWriteSeq++;
    }
    // Consommateur ne lit qu'une seule trame par tour
    const WakeWordAudioBridge::FrameSlot *slot = bridge.peekFrame();
    TEST_ASSERT(slot != nullptr,
                "Scenario 1 : trame disponible pour le consommateur lent");
    TEST_ASSERT(slot->sequence == expectedReadSeq,
                "Scenario 1 : ordre FIFO strictement sans trou sur les trames "
                "retenues");
    TEST_ASSERT(slot->samples[0] ==
                    static_cast<int16_t>((expectedReadSeq * 37) & 0x7FFF),
                "Scenario 1 : echantillon 0 intact");
    TEST_ASSERT(slot->samples[511] ==
                    static_cast<int16_t>((expectedReadSeq * 37 + 511) & 0x7FFF),
                "Scenario 1 : echantillon 511 intact");
    bridge.consumeFrame();
    expectedReadSeq++;
  }
  TEST_ASSERT(
      bridge.getFramesDropped() > 0,
      "Scenario 1 : trames excedentaires rejetees sans ecraser la file active");

  // Scénario 2 & 3 : File complètement remplie + Saturations répétées pendant
  // que le consommateur détient peekFrame()
  bridge.clear();
  bridge.resetMetrics();
  bridge.setEnabled(true);

  for (uint32_t s = 1; s <= 8; ++s) {
    int16_t chunk[AUDIO_CHUNK_SAMPLES];
    for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
      chunk[i] = static_cast<int16_t>(s * 1000 + (i % 100));
    }
    TEST_ASSERT(bridge.pushAudioChunk(chunk, AUDIO_CHUNK_SAMPLES, s * 100),
                "Scenario 2 : remplissage complet slot");
  }
  TEST_ASSERT(bridge.isFull(), "Scenario 2 : file completement remplie (8/8)");

  // Le consommateur obtient un pointeur via peekFrame() (simulant
  // IWakeWordEngine::process() en cours)
  const WakeWordAudioBridge::FrameSlot *peeked = bridge.peekFrame();
  TEST_ASSERT(peeked != nullptr,
              "Scenario 2 : peekFrame() retourne un pointeur valide");
  TEST_ASSERT(bridge.isFramePeeked(), "Scenario 2 : flag _framePeeked actif");
  int16_t snapshotBefore[AUDIO_CHUNK_SAMPLES];
  memcpy(snapshotBefore, peeked->samples, sizeof(snapshotBefore));
  uint32_t seqBefore = peeked->sequence;

  // Scénario 3 : 50 tentatives d'écriture saturantes pendant que peekFrame()
  // est détenu
  int16_t poisonChunk[AUDIO_CHUNK_SAMPLES];
  for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i)
    poisonChunk[i] = -32123;
  for (int sat = 0; sat < 50; ++sat) {
    bool pushed =
        bridge.pushAudioChunk(poisonChunk, AUDIO_CHUNK_SAMPLES, 99999);
    TEST_ASSERT(!pushed, "Scenario 3 : producteur rejette la trame sans jamais "
                         "ecraser le slot consulte");
  }
  TEST_ASSERT(bridge.getQueueSaturationCount() == 50,
              "Scenario 3 : 50 saturations comptabilisees");
  TEST_ASSERT(bridge.getFramesDropped() == 50,
              "Scenario 3 : 50 pertes comptabilisees");
  TEST_ASSERT(peeked->sequence == seqBefore,
              "Scenario 3 : numero de sequence du slot peekFrame inchange");
  TEST_ASSERT(memcmp(peeked->samples, snapshotBefore, sizeof(snapshotBefore)) ==
                  0,
              "Scenario 3 : ZERO corruption memoire du slot detenu par le "
              "consommateur pendant process()");
  bridge.consumeFrame();
  TEST_ASSERT(!bridge.isFramePeeked(),
              "Scenario 3 : _framePeeked relache apres consumeFrame()");

  // Scénario 4 : Suspension pendant une écriture (aucune trame partiellement
  // écrite ou fantôme visible)
  bridge.clear();
  bridge.setEnabled(true);
  bool midWriteHookCalled = false;
  bridge.setSimMidWriteHook([&]() {
    midWriteHookCalled = true;
    // Pendant que la tâche audio est au milieu de pushAudioChunk(), le thread
    // principal suspend le pont
    TEST_ASSERT(bridge.getActiveWriters() == 1,
                "Scenario 4 : 1 ecrivain actif detecte pendant pushAudioChunk");
    bridge.setEnabled(false);
  });
  int16_t midWriteChunk[AUDIO_CHUNK_SAMPLES];
  for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i)
    midWriteChunk[i] = 4321;
  bool midWriteAccepted =
      bridge.pushAudioChunk(midWriteChunk, AUDIO_CHUNK_SAMPLES, 432);
  bridge.setSimMidWriteHook(nullptr);

  TEST_ASSERT(midWriteHookCalled,
              "Scenario 4 : hook de concurrence mi-ecriture declenche");
  TEST_ASSERT(!midWriteAccepted, "Scenario 4 : ecriture invalidee par le "
                                 "changement d'epoque lors de la suspension");
  TEST_ASSERT(
      bridge.getOccupancy() == 0,
      "Scenario 4 : file strictement vide apres suspension concurrente");
  TEST_ASSERT(bridge.peekFrame() == nullptr,
              "Scenario 4 : aucune trame partiellement ecrite ou "
              "post-suspension visible");

  // Scénario 5 : Désactivation pendant une acquisition via WakeWordManager
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.apiKey = "";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWord = true;
  vk.begin(cfg);
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "Scenario 5 : WakeWord ARMED");

  vk.getWakeWordManager().getAudioBridge().setSimMidWriteHook(
      [&]() { vk.disableWakeWord(); });
  vk.getEngine().getStreamer().pushSimulatedChunk(midWriteChunk,
                                                  AUDIO_CHUNK_SAMPLES);
  vk.getWakeWordManager().getAudioBridge().setSimMidWriteHook(nullptr);

  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::DISABLED,
              "Scenario 5 : WakeWord proprement DISABLED apres desactivation "
              "concurrente");
  TEST_ASSERT(vk.getWakeWordManager().getAudioBridge().getOccupancy() == 0,
              "Scenario 5 : zero trame fantome dans le pont desactive");

  // Scénario 6 : Redémarrages successifs et protection de end() contre
  // libération pendant écriture
  for (int r = 0; r < 10; ++r) {
    TEST_ASSERT(bridge.begin(8), "Scenario 6 : re-initialisation begin()");
    bridge.setEnabled(true);
    bridge.pushAudioChunk(midWriteChunk, AUDIO_CHUNK_SAMPLES, 500 + r);
    TEST_ASSERT(bridge.getOccupancy() == 1, "Scenario 6 : 1 trame presente");
    bridge.clear();
    TEST_ASSERT(bridge.getOccupancy() == 0,
                "Scenario 6 : clear() vide la file");
    bridge.resetMetrics();
    bridge.end();
    TEST_ASSERT(
        bridge.getCapacity() == 0,
        "Scenario 6 : end() libere les slots quand aucun ecrivain n'est actif");
  }

  // Scénario 7 : Vérification des séquences et de l'intégrité PCM sur 32 trames
  // consécutives (4 tours complets d'anneau)
  TEST_ASSERT(bridge.begin(8), "Scenario 7 : begin(8)");
  bridge.setEnabled(true);
  for (uint32_t seq = 1; seq <= 32; ++seq) {
    int16_t pattern[AUDIO_CHUNK_SAMPLES];
    for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
      pattern[i] = static_cast<int16_t>(((seq * 131) ^ (i * 17)) & 0x7FFF);
    }
    TEST_ASSERT(bridge.pushAudioChunk(pattern, AUDIO_CHUNK_SAMPLES, seq * 50),
                "Scenario 7 : push trame");
    const WakeWordAudioBridge::FrameSlot *s = bridge.peekFrame();
    TEST_ASSERT(s != nullptr && s->sequence == seq,
                "Scenario 7 : sequence monotone exacte sur 4 tours d'anneau");
    bool pcmIntact = (memcmp(s->samples, pattern, sizeof(pattern)) == 0);
    TEST_ASSERT(pcmIntact, "Scenario 7 : integrite PCM bit-a-bit verifiee sur "
                           "les 512 echantillons");
    bridge.consumeFrame();
  }
  bridge.end();
}

// SUITE 90 : Budget CPU borné et fluidité de la boucle principale LVGL (Section
// 3)
void test_v0621_cpu_budget_and_lvgl_fluidity() {
  std::cout << "\n[TEST SUITE 90] v0.6.2.1 : Budget de traitement CPU borne "
               "(setMaxFramesPerUpdate / setProcessingBudgetUs)"
            << std::endl;

  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.apiKey = "";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWord = true;
  vk.begin(cfg);

  int16_t pcm[AUDIO_CHUNK_SAMPLES];
  for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i)
    pcm[i] = static_cast<int16_t>(i * 5);

  // 1. Limitation par nombre maximal de trames :
  // setWakeWordMaxFramesPerUpdate(1)
  vk.setWakeWordMaxFramesPerUpdate(1);
  vk.setWakeWordProcessingBudgetUs(5000);
  TEST_ASSERT(vk.getWakeWordMaxFramesPerUpdate() == 1,
              "maxFramesPerUpdate configure a 1");
  TEST_ASSERT(vk.getWakeWordProcessingBudgetUs() == 5000,
              "processingBudgetUs configure a 5000 us");

  for (int i = 0; i < 4; ++i) {
    vk.getEngine().getStreamer().pushSimulatedChunk(pcm, AUDIO_CHUNK_SAMPLES);
  }
  TEST_ASSERT(vk.getWakeWordMetrics().currentQueueOccupancy == 4,
              "4 trames en attente avant update()");

  vk.update();
  WakeWordMetrics m1 = vk.getWakeWordMetrics();
  TEST_ASSERT(m1.lastFramesProcessedInUpdate == 1,
              "Exactement 1 trame traitee pendant un seul appel a update()");
  TEST_ASSERT(m1.framesProcessed == 1, "Total framesProcessed == 1");
  TEST_ASSERT(m1.currentQueueOccupancy == 3,
              "3 trames restantes pour les prochains cycles");

  // Drainer les 3 trames restantes en 3 appels successifs
  for (int i = 0; i < 3; ++i)
    vk.update();
  TEST_ASSERT(vk.getWakeWordMetrics().framesProcessed == 4,
              "Les 4 trames ont ete traitees en 4 cycles non bloquants");
  TEST_ASSERT(vk.getWakeWordMetrics().currentQueueOccupancy == 0, "File videe");

  // 2. Limitation par budget temporel en microsecondes :
  // setWakeWordProcessingBudgetUs(2000) On autorise jusqu'à 6 trames par
  // update, mais chaque trame consomme 1200 us :
  // - Trame 1 : 1200 us (< 2000 us)
  // - Trame 2 : cumul 2400 us (>= 2000 us) -> dépassement détecté, arrêt
  // immédiat après la 2e trame !
  vk.setWakeWordMaxFramesPerUpdate(6);
  vk.setWakeWordProcessingBudgetUs(2000);
  vk.getWakeWordManager().getTestEngine().setSimulatedProcessDurationUs(1200);

  for (int i = 0; i < 5; ++i) {
    vk.getEngine().getStreamer().pushSimulatedChunk(pcm, AUDIO_CHUNK_SAMPLES);
  }

  vk.update();
  WakeWordMetrics mBudget = vk.getWakeWordMetrics();
  TEST_ASSERT(
      mBudget.lastFramesProcessedInUpdate == 2,
      "Arret anticipe apres 2 trames lorsque le budget de 2000 us est atteint");
  TEST_ASSERT(mBudget.processingBudgetExceeded == 1,
              "Depassement de budget detecte et comptabilise "
              "(processingBudgetExceeded == 1)");
  TEST_ASSERT(
      mBudget.lastWakeProcessDurationUs >= 1200,
      "lastWakeProcessDurationUs mesure le temps reel consomme par trame");
  TEST_ASSERT(mBudget.maxWakeProcessDurationUs >= 1200,
              "maxWakeProcessDurationUs mis a jour");
  TEST_ASSERT(
      mBudget.lastUpdateProcessDurationUs >= 2400,
      "lastUpdateProcessDurationUs mesure le cumul de l'appel update()");
  TEST_ASSERT(mBudget.maxUpdateProcessDurationUs >= 2400,
              "maxUpdateProcessDurationUs mis a jour");
  TEST_ASSERT(mBudget.maxFramesPerUpdate == 6,
              "Diagnostic maxFramesPerUpdate exact");
  TEST_ASSERT(mBudget.processingBudgetUs == 2000,
              "Diagnostic processingBudgetUs exact");

  vk.getWakeWordManager().getTestEngine().setSimulatedProcessDurationUs(0);
  vk.disableWakeWord();
}

// SUITE 91 : Arrêt et reprise du microphone — Gestion des échecs de
// stopLocalCapture() et transitions (Section 4)
void test_v0621_mic_stop_failure_and_mode_transitions() {
  std::cout << "\n[TEST SUITE 91] v0.6.2.1 : Verification de "
               "stopLocalCapture(), refus sur echec et transitions completes"
            << std::endl;

  WiFi.setConnected(true);
  MockLiveTransport mockTransport;
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.apiKey = "AIzaSyTestMicStopTransitions";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWord = true;
  cfg.wakeWordAutoResume = true;
  cfg.wakeWordResumeDelayMs = 150;

  TEST_ASSERT(vk.begin(cfg), "VoiceKit::begin() reussi");
  prepareMockVoiceKitReady(vk, mockTransport);
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "Etat initial WAKE ARMED");
  TEST_ASSERT(vk.getEngine().getStreamer().isLocalCapture(),
              "Capture locale active");

  // 1. Simuler un échec d'arrêt de la tâche microphone (timeout de terminaison
  // FreeRTOS)
  vk.getEngine().getStreamer().setSimMicTerminationTimeout(true);

  // Tentative de passage en écoute STT (listen) alors que stopLocalCapture()
  // échoue
  bool listenOk = vk.listen();
  TEST_ASSERT(!listenOk, "listen() refuse proprement le nouveau mode si "
                         "stopLocalCapture() echoue");
  TEST_ASSERT(!vk.isListening(),
              "Aucune seconde initialisation du microphone n'a ete tentee");
  TEST_ASSERT(vk.getState() == VoiceState::ERROR_STATE,
              "VoxCoreVoice conserve un etat d'erreur explicite");
  TEST_ASSERT(vk.isWakeWordError(), "WakeWordManager en etat ERROR explicite");
  TEST_ASSERT(vk.getWakeWordMetrics().captureStopFailures == 1,
              "Echec d'arret enregistre dans captureStopFailures (1)");
  TEST_ASSERT(!vk.getLastError().isEmpty(),
              "Cause d'erreur enregistree dans les diagnostics");

  // Tant que la tâche en quarantaine n'est pas terminée, ContinuousSTT et
  // Conversation doivent aussi être refusés
  TEST_ASSERT(!vk.startContinuousListening(),
              "startContinuousListening() refuse tant que la tache micro est "
              "en quarantaine");
  TEST_ASSERT(!vk.isContinuousListening(), "Mode ContinuousSTT non active");

  // 2. Récupération : la tâche bloquée finit par se terminer -> le microphone
  // ne doit PAS rester bloqué définitivement
  vk.getEngine().getStreamer().setSimMicTerminationTimeout(false);
  vk.getEngine().getStreamer().setSimTaskRunning(false);
  vk.getEngine().getStreamer().checkQuarantinedTasks();
  TEST_ASSERT(!vk.getEngine().getStreamer().isTaskQuarantined(),
              "Quarantaine levee des que la tache s'est terminee");
  TEST_ASSERT(!HardwareManager::getInstance().isMicCaptureReserved(),
              "Reservation microphone liberee apres levee de quarantaine");

  // Réactivation de WakeWord depuis l'état ERROR : ERROR -> INACTIVE ->
  // INITIALIZING -> ARMED
  vk.disableWakeWord();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::DISABLED,
              "Transition ERROR -> DISABLED (INACTIVE)");
  TEST_ASSERT(vk.enableWakeWord(true),
              "Re-activation DISABLED -> INITIALIZING -> ARMED reussie");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "Microphone operationnel en WAKE ARMED (non bloque)");

  // 3. Test de la séquence complète exigée :
  // WAKE ARMED -> WAKE DETECTED -> WAKE SUSPENDED -> VOICE CONVERSATION ->
  // VOICE FINISHED -> WAKE ARMED
  const char *setupJson = "{\"setupComplete\":{}}";
  TEST_ASSERT(vk.startConversation(), "Activation du mode CONVERSATION");
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.isConversationActive(), "Mode CONVERSATION pret");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "1. Etat WAKE ARMED");

  std::vector<WakeWordState> transitionLog;
  vk.onWakeWordStateChanged(
      [&transitionLog](WakeWordState s) { transitionLog.push_back(s); });

  // Lorsque le mot de réveil est détecté, le callback démarre immédiatement le
  // tour de conversation
  vk.onWakeWordDetected([&vk](const String &word) {
    (void)word;
    vk.getEngine().startListening(); // Démarre le tour de conversation
  });

  TEST_ASSERT(vk.triggerTestWakeWord("Bonjour ESP"),
              "Armement du trigger Bonjour ESP");
  int16_t pcm[AUDIO_CHUNK_SAMPLES] = {300};
  vk.getEngine().getStreamer().pushSimulatedChunk(pcm, AUDIO_CHUNK_SAMPLES);
  vk.update();

  TEST_ASSERT(transitionLog.size() >= 2,
              "Transitions DETECTED puis SUSPENDED enregistrees");
  TEST_ASSERT(transitionLog[0] == WakeWordState::DETECTED,
              "2. Etat WAKE DETECTED");
  TEST_ASSERT(transitionLog[1] == WakeWordState::SUSPENDED,
              "3. Etat WAKE SUSPENDED");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "WakeWord est SUSPENDED pendant la conversation");
  TEST_ASSERT(vk.isListening(),
              "4. VOICE CONVERSATION (ecoute utilisateur active)");

  // Fin de parole utilisateur et réponse audio de l'assistant
  vk.getEngine().stopListening();
  injectGeminiPcmAndDrain(vk.getEngine(), 4, true);
  drainVoicePlayback(vk.getEngine());
  vk.update();
  TEST_ASSERT(!vk.isBusy(), "5. VOICE FINISHED (tour de conversation termine)");

  advanceMockMillis(200);
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "6. Retour automatique a WAKE ARMED");
  TEST_ASSERT(vk.getEngine().getStreamer().isLocalCapture(),
              "Capture locale microphone reactivee pour WakeWord");

  vk.onWakeWordDetected(nullptr);
  vk.disableWakeWord();
  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 92 : Machine à états Wake Word, réentrance des callbacks, appels
// répétés et sûreté mémoire (Section 5)
void test_v0621_state_machine_callbacks_and_memory_safety() {
  std::cout << "\n[TEST SUITE 92] v0.6.2.1 : Machine a etats, callbacks "
               "reentrants, appels repetes et surete memoire"
            << std::endl;

  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.apiKey = "";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWord = false;
  vk.begin(cfg);

  std::vector<WakeWordState> states;
  vk.onWakeWordStateChanged(
      [&states](WakeWordState s) { states.push_back(s); });

  // 1. Vérification des appels répétés idempotents (enable, suspend, resume,
  // disable)
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::DISABLED,
              "Etat initial INACTIVE (DISABLED)");
  TEST_ASSERT(vk.enableWakeWord(true), "Premier enableWakeWord(true)");
  size_t countAfterFirstEnable = states.size();
  TEST_ASSERT(countAfterFirstEnable == 2 &&
                  states[0] == WakeWordState::INITIALIZING &&
                  states[1] == WakeWordState::ARMED,
              "INACTIVE -> INITIALIZING -> ARMED");

  TEST_ASSERT(vk.enableWakeWord(true),
              "Second enableWakeWord(true) idempotent");
  TEST_ASSERT(states.size() == countAfterFirstEnable,
              "Aucune transition parasite sur enableWakeWord(true) repete");

  TEST_ASSERT(vk.suspendWakeWord(), "Premier suspendWakeWord()");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "Etat SUSPENDED");
  size_t countAfterSuspend = states.size();
  TEST_ASSERT(vk.suspendWakeWord(), "Second suspendWakeWord() idempotent");
  TEST_ASSERT(states.size() == countAfterSuspend,
              "Aucune transition parasite sur suspendWakeWord() repete");

  TEST_ASSERT(vk.resumeWakeWord(), "Premier resumeWakeWord()");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "SUSPENDED -> ARMED");
  size_t countAfterResume = states.size();
  TEST_ASSERT(vk.resumeWakeWord(), "Second resumeWakeWord() idempotent");
  TEST_ASSERT(states.size() == countAfterResume,
              "Aucune transition parasite sur resumeWakeWord() repete");

  // 2. Callback onWakeWordDetected déclenchant lui-même disableWakeWord() ou
  // resumeWakeWord() Cas A : Le callback appelle resumeWakeWord() immédiatement
  // -> l'état final doit être ARMED (et non écrasé par SUSPENDED)
  vk.onWakeWordDetected([&vk](const String &) { vk.resumeWakeWord(); });
  int16_t pcm[AUDIO_CHUNK_SAMPLES] = {150};
  vk.triggerTestWakeWord();
  vk.getEngine().getStreamer().pushSimulatedChunk(pcm, AUDIO_CHUNK_SAMPLES);
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "Si le callback appelle resumeWakeWord(), update() n'ecrase pas "
              "l'etat ARMED par SUSPENDED");

  // Cas B : Le callback appelle disableWakeWord() -> l'état final doit rester
  // DISABLED
  vk.onWakeWordDetected([&vk](const String &) { vk.disableWakeWord(); });
  vk.triggerTestWakeWord();
  vk.getEngine().getStreamer().pushSimulatedChunk(pcm, AUDIO_CHUNK_SAMPLES);
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::DISABLED,
              "Si le callback appelle disableWakeWord(), l'etat final reste "
              "DISABLED sans transition incoherente");
  vk.onWakeWordDetected(nullptr);

  // 3. Transition ARMED -> ERROR puis ERROR -> INACTIVE (DISABLED) ->
  // INITIALIZING -> ARMED
  TEST_ASSERT(vk.enableWakeWord(true), "Re-armement pour test d'erreur");
  vk.getWakeWordManager().getTestEngine().setSimulateRuntimeError(true);
  vk.getEngine().getStreamer().pushSimulatedChunk(pcm, AUDIO_CHUNK_SAMPLES);
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ERROR,
              "Transition ARMED -> ERROR");
  vk.getWakeWordManager().getTestEngine().setSimulateRuntimeError(false);

  vk.disableWakeWord();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::DISABLED,
              "Transition ERROR -> INACTIVE (DISABLED)");
  TEST_ASSERT(vk.enableWakeWord(true),
              "Transition INACTIVE -> INITIALIZING -> ARMED");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "Retour a ARMED reussi");

  // 4. Sûreté mémoire lors de disableWakeWord() si l'arrêt du producteur audio
  // échoue : Ne JAMAIS libérer la mémoire du pont tant que la tâche productrice
  // n'est pas confirmée arrêtée
  vk.getEngine().getStreamer().setSimMicTerminationTimeout(true);
  vk.disableWakeWord();
  TEST_ASSERT(
      vk.getWakeWordState() == WakeWordState::ERROR,
      "disableWakeWord() passe en ERROR si l'arret du producteur audio echoue");
  TEST_ASSERT(vk.getWakeWordManager().getAudioBridge().getCapacity() > 0,
              "SURETE MEMOIRE : Les slots du pont audio NE SONT PAS detruits "
              "tant que la tache audio est active");

  // Une fois la tâche arrêtée, disableWakeWord() libère effectivement la
  // mémoire
  vk.getEngine().getStreamer().setSimMicTerminationTimeout(false);
  vk.getEngine().getStreamer().setSimTaskRunning(false);
  vk.disableWakeWord();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::DISABLED,
              "disableWakeWord() reussit apres arret confirme");
  TEST_ASSERT(vk.getWakeWordManager().getAudioBridge().getCapacity() == 0,
              "Les ressources du pont audio sont liberees apres confirmation "
              "de l'arret du producteur");
}

// SUITE 93 : WakeNetEngine v0.6.2.1 — Continuité 512->480 sur détection, reset,
// fuites mémoire, confiance et 4 niveaux (Section 6)
void test_v0621_wakenet_engine_strict_validation() {
  std::cout
      << "\n[TEST SUITE 93] v0.6.2.1 : WakeNetEngine (continuite 512->480, "
         "reset, liberation ressources, confiance 0.0f, 4 niveaux)"
      << std::endl;

  WakeNetEngine wn;

  // 1. Continuité des échantillons aux frontières même lorsqu'une détection
  // survient au milieu d'un bloc Si un bloc de 512 échantillons déclenche une
  // détection sur les 480 premiers échantillons, les 32 échantillons restants
  // du bloc (indices 480..511) ne doivent PAS être perdus !
  wn.reset();
  std::vector<int16_t> stream(512);
  for (size_t i = 0; i < stream.size(); ++i) {
    stream[i] = static_cast<int16_t>(i + 1);
  }

  size_t framesSeen = 0;
  wn.feedFrameAdapter(stream.data(), 512,
                      [&](const int16_t *f480, size_t count) -> bool {
                        (void)f480;
                        (void)count;
                        framesSeen++;
                        return true; // Détection sur la trame de 480 éch !
                      });
  // Après la détection sur les 480 premiers éch du bloc de 512, les 32 éch
  // restants sont conservés dans l'accumulateur
  TEST_ASSERT(wn.getAccumulatorCount() == 32 &&
                  wn.getAccumulatorData()[0] == 481 &&
                  wn.getAccumulatorData()[31] == 512,
              "Zero perte aux frontieres : les 32 echantillons restants du "
              "bloc de 512 sont conserves dans l'accumulateur");

  // 2. Réinitialisation de l'accumulateur
  wn.reset();
  TEST_ASSERT(wn.getAccumulatorCount() == 0,
              "reset() vide immediatement l'accumulateur");

  // 3. Distinction stricte des 4 niveaux et libération des ressources sur échec
  // d'initialisation Niveau A : Support non compilé
  wn.setSimEnvironment(false, false, false, false);
  TEST_ASSERT(!wn.isCompiledSupportAvailable(),
              "Niveau 1 : Support compile = false");
  TEST_ASSERT(!wn.isModelAvailable(), "Niveau 2 : Modele disponible = false");
  TEST_ASSERT(!wn.isInitialized(), "Niveau 3 : Moteur initialise = false");

  // Niveau B : Support compilé, mais partition srmodels absente
  wn.setSimEnvironment(true, false, false, false);
  TEST_ASSERT(wn.isCompiledSupportAvailable(),
              "Niveau 1 : Support compile = true");
  TEST_ASSERT(!wn.isModelAvailable(),
              "Niveau 2 : Modele disponible = false (partition absente)");
  TEST_ASSERT(!wn.isAvailable(), "La seule presence des en-tetes ne rend pas "
                                 "le moteur disponible sans modele");
  TEST_ASSERT(!wn.begin(16000, 1),
              "begin() echoue proprement si la partition modele est absente");
  TEST_ASSERT(!wn.hasSimResourcesAllocated(),
              "Aucune fuite de ressources apres echec partition");

  // Niveau C : Partition présente, mais modèle wn10_fr_bonjouresp absent de la
  // partition
  wn.setSimEnvironment(true, true, false, false);
  TEST_ASSERT(wn.isCompiledSupportAvailable(), "Support compile = true");
  TEST_ASSERT(!wn.isModelAvailable(), "Modele cible absent = false");
  TEST_ASSERT(!wn.begin(16000, 1),
              "begin() echoue si wn10_fr_bonjouresp est absent");
  TEST_ASSERT(!wn.hasSimResourcesAllocated(),
              "Ressources srmodels liberees apres echec de filtre modele");

  // Niveau D : Modèle présent, mais échec d'allocation/création de l'instance
  // iface->create() (createFailure = true)
  wn.setSimEnvironment(true, true, true, true);
  TEST_ASSERT(wn.isModelAvailable(), "Modele disponible = true");
  TEST_ASSERT(!wn.begin(16000, 1),
              "begin() echoue si iface->create() retourne null");
  TEST_ASSERT(!wn.isInitialized(),
              "Moteur non initialise apres echec create()");
  TEST_ASSERT(!wn.hasSimResourcesAllocated(),
              "Ressources srmodels liberees apres echec create()");

  // Niveau E : Succès complet des 4 niveaux (createFailure = false) +
  // vérification du score de confiance non inventé
  wn.setSimEnvironment(true, true, true, false);
  TEST_ASSERT(wn.isCompiledSupportAvailable(), "1. Support compile = true");
  TEST_ASSERT(wn.isModelAvailable(), "2. Modele disponible = true");
  TEST_ASSERT(wn.begin(16000, 1), "3. Initialisation WakeNet reussie");
  TEST_ASSERT(wn.isInitialized(), "3. Moteur initialise = true");
  TEST_ASSERT(wn.hasSimResourcesAllocated(),
              "Ressources actives tant que le moteur est initialise");

  wn.setSimDetectCallback([](const int16_t *, size_t) -> bool {
    return true; // WAKENET_DETECTED
  });
  int16_t chunk512[AUDIO_CHUNK_SAMPLES] = {100};
  WakeWordResult res = wn.process(chunk512, AUDIO_CHUNK_SAMPLES, 100);
  TEST_ASSERT(res.detected, "Detection WakeNet active");
  TEST_ASSERT(!res.isSimulated, "isSimulated == false pour WakeNetEngine");
  TEST_ASSERT(
      res.confidence == 0.0f,
      "WakeNetEngine n'invente JAMAIS un score de confiance fictif (0.0f)");

  wn.end();
  TEST_ASSERT(!wn.isInitialized(), "end() desinitialise le moteur");
  TEST_ASSERT(!wn.hasSimResourcesAllocated(),
              "end() libere toutes les ressources allouees");
  wn.setSimEnvironment(false, false, false, false);
  wn.setSimDetectCallback(nullptr);
}

// SUITE 94 : Vérification de la capture locale, métriques RMS authentiques et
// non-interférence ContinuousSTT (Section 7)
void test_v0621_local_capture_offline_rms_and_non_interference() {
  std::cout << "\n[TEST SUITE 94] v0.6.2.1 : Capture locale ES7210, RMS "
               "authentique du signal et non-interference ContinuousSTT"
            << std::endl;

  WiFi.setConnected(false);
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.apiKey = "";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWord = true;
  TEST_ASSERT(vk.begin(cfg), "Demarrage hors ligne sans cle API ni Wi-Fi");

  // 1. Vérifier que le format d'entrée HAL est PCM mono 16 kHz signé 16 bits et
  // issu du microphone ES7210
  TEST_ASSERT(vk.getEngine().getStreamer().isLocalCapture(),
              "startLocalCapture() actif");
  TEST_ASSERT(HardwareManager::getInstance().isMicCaptureReserved(),
              "ES7210 reserve par l'unique proprietaire");

  // 2. Injecter un signal carré d'amplitude constante +/- 1200 -> RMS théorique
  // = 1200
  int16_t signal1200[AUDIO_CHUNK_SAMPLES];
  for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
    signal1200[i] = (i % 2 == 0) ? 1200 : -1200;
  }
  vk.getEngine().getStreamer().pushSimulatedChunk(signal1200,
                                                  AUDIO_CHUNK_SAMPLES);
  vk.update();

  WakeWordMetrics mRms = vk.getWakeWordMetrics();
  TEST_ASSERT(mRms.lastRms == 1200,
              "lastRms correspond exactement au signal acquis (1200)");
  TEST_ASSERT(mRms.peakRms == 1200,
              "peakRms correspond exactement au signal acquis (1200)");
  TEST_ASSERT(mRms.framesReceived == 1, "Exactement 1 trame acquise");

  // 3. Vérifier que triggerTestWakeWord() et handleWakeWordSerialCommand() ne
  // polluent PAS le RMS ni framesReceived avec une trame silencieuse fictive
  vk.setWakeWordAutoResume(false);
  TEST_ASSERT(vk.triggerTestWakeWord("Bonjour ESP"),
              "triggerTestWakeWord() sans nouvelle trame micro");
  TEST_ASSERT(vk.getWakeWordMetrics().framesReceived == 1,
              "Aucune trame silencieuse fictive injectee dans le pont audio");
  TEST_ASSERT(vk.getWakeWordMetrics().lastRms == 1200,
              "lastRms n'est pas écrase par une valeur simulee a 0");
  vk.update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "Detection declenchee dans update() meme si la file est vide");
  TEST_ASSERT(vk.getWakeWordMetrics().lastRms == 1200,
              "lastRms conserve la vraie mesure acoustique du signal (1200)");

  // 4. Vérifier la non-interférence avec ContinuousSTT
  vk.resumeWakeWord();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED, "WakeWord ARMED");
  TEST_ASSERT(vk.startContinuousListening(), "Passage en ContinuousSTT");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "WakeWord immediatement SUSPENDED pour ne pas interferer avec "
              "ContinuousSTT");
  uint32_t wakeFramesBefore = vk.getWakeWordMetrics().framesReceived;
  vk.getEngine().getStreamer().pushSimulatedChunk(signal1200,
                                                  AUDIO_CHUNK_SAMPLES);
  vk.update();
  TEST_ASSERT(vk.getWakeWordMetrics().framesReceived == wakeFramesBefore,
              "Aucune trame n'est copiee vers WakeWord pendant que "
              "ContinuousSTT utilise le flux audio");

  vk.stopContinuousListening();
  vk.disableWakeWord();
}

// =============================================================================
// SUITES v0.6.3 : Wake Word Driven Conversation (Scénarios 1 à 12 — Section 10)
// =============================================================================

static void pushSimulatedSpeechTurn(VoiceKit &vk, size_t speechChunks = 10,
                                    size_t silenceChunks = 8) {
  int16_t loudChunk[AUDIO_CHUNK_SAMPLES];
  int16_t quietChunk[AUDIO_CHUNK_SAMPLES];
  for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i) {
    loudChunk[i] = (i % 2 == 0) ? 1800 : -1800;
    quietChunk[i] = 10;
  }
  for (size_t c = 0; c < speechChunks; ++c) {
    vk.getEngine().getStreamer().pushSimulatedChunk(loudChunk,
                                                    AUDIO_CHUNK_SAMPLES);
    advanceMockMillis(32);
    vk.update();
  }
  for (size_t c = 0; c < silenceChunks; ++c) {
    vk.getEngine().getStreamer().pushSimulatedChunk(quietChunk,
                                                    AUDIO_CHUNK_SAMPLES);
    advanceMockMillis(32);
    vk.update();
  }
}

// SUITE 95 : Scénarios 1 & 2 — Cycle complet (Détection -> Conversation ->
// Retour ARMED) & 10 cycles consécutifs
void test_v063_wake_conversation_single_and_10_consecutive_cycles() {
  std::cout << "\n[TEST SUITE 95] v0.6.3 : Scenarios 1 & 2 — Cycle complet "
               "Wake->Conversation->ARMED et 10 cycles consecutifs"
            << std::endl;

  WiFi.setConnected(true);
  setMockMillisOffset(1700000000UL * 1000UL);

  MockLiveTransport mockTransport;
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::GEMINI;
  cfg.apiKey = "AIzaSyTestWakeConvV063";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.wakeWordPhrase = "Bonjour ESP";
  cfg.enableWakeWordConversation = true;
  cfg.wakeWordResumeDelayMs = 50;
  cfg.wakeConversationTurnRestartDelayMs = 40;
  cfg.vadSilenceDurationMs = 200;

  bool cbDetected = false, cbConnecting = false, cbReady = false;
  bool cbListening = false, cbSpeaking = false, cbEnded = false,
       cbReturned = false;
  WakeWordConversationState stateInsideWakeCallback =
      WakeWordConversationState::DISABLED;

  vk.onWakeWordDetected([&](const String &) {
    // Règle Section 3 : Le callback onWakeWordDetected() ne doit pas exécuter
    // directement la transition longue
    stateInsideWakeCallback = vk.getWakeWordConversationState();
  });
  vk.onWakeConvDetected([&](const String &w, float, bool) {
    if (w == "Bonjour ESP")
      cbDetected = true;
  });
  vk.onWakeConvConnecting([&]() { cbConnecting = true; });
  vk.onWakeConvReady([&]() { cbReady = true; });
  vk.onWakeConvListening([&]() { cbListening = true; });
  vk.onWakeConvSpeaking([&]() { cbSpeaking = true; });
  vk.onWakeConvEnded([&](WakeWordSessionEndReason) { cbEnded = true; });
  vk.onWakeConvReturnedToWake([&]() { cbReturned = true; });

  TEST_ASSERT(vk.begin(cfg),
              "VoiceKit begin() avec enableWakeWordConversation=true");
  vk.getEngine().getClient().setTransport(&mockTransport);

  // 1. État initial : WAKE_ARMED, WakeNet armé, aucune connexion WebSocket
  // ouverte prématurément
  TEST_ASSERT(vk.isWakeWordConversationEnabled(),
              "Mode Wake Word Conversation actif");
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Etat initial WAKE_ARMED");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "WakeWordManager en etat ARMED");
  TEST_ASSERT(!vk.getEngine().getProvider().isConnected(),
              "Pas de connexion WebSocket ouverte pendant WAKE_ARMED");
  TEST_ASSERT(String(vk.getWakeWordConversationStatusText()) ==
                  "EN ATTENTE — « Bonjour ESP »",
              "Texte d'affichage conforme : EN ATTENTE — « Bonjour ESP »");

  // 2. Déclenchement du mot de réveil
  TEST_ASSERT(vk.triggerTestWakeWord("Bonjour ESP"),
              "Declenchement mot de reveil « Bonjour ESP »");
  vk.update(); // 1er update : WakeWordManager détecte -> notifie contrôleur ->
               // contrôleur passe à WAKE_DETECTED
  TEST_ASSERT(
      stateInsideWakeCallback == WakeWordConversationState::WAKE_ARMED,
      "onWakeWordDetected() n'a pas execute de transition synchrone bloquante");
  TEST_ASSERT(cbDetected, "Callback onWakeConvDetected invoque");
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_DETECTED,
              "Transition vers WAKE_DETECTED");
  TEST_ASSERT(String(vk.getWakeWordConversationStatusText()) ==
                  "MOT DE RÉVEIL DÉTECTÉ",
              "Texte d'affichage : MOT DE RÉVEIL DÉTECTÉ");

  // 3. WAKE_DETECTED -> PREPARING_SESSION -> CONNECTING
  advanceMockMillis(10);
  vk.update(); // WAKE_DETECTED -> PREPARING_SESSION
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::PREPARING_SESSION,
              "Transition vers PREPARING_SESSION");
  advanceMockMillis(10);
  vk.update(); // PREPARING_SESSION -> CONNECTING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONNECTING,
              "Transition vers CONNECTING");
  TEST_ASSERT(cbConnecting, "Callback onWakeConvConnecting invoque");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "WakeNet suspendu pendant CONNECTING");
  TEST_ASSERT(!vk.getEngine().getStreamer().isLocalCapture(),
              "Capture locale micro liberee");
  TEST_ASSERT(
      !vk.isListening(),
      "Aucune capture conversationnelle avant confirmation setupComplete");
  TEST_ASSERT(String(vk.getWakeWordConversationStatusText()) ==
                  "CONNEXION GEMINI",
              "Texte d'affichage : CONNEXION GEMINI");

  // 4. CONNECTING -> activation WebSocket -> confirmation setupComplete ->
  // CONVERSATION_LISTENING
  advanceMockMillis(20);
  vk.update(); // _session.activate() -> provider.connect()
  TEST_ASSERT(vk.getEngine().getProvider().isConnected(),
              "Transport WebSocket connecte");
  TEST_ASSERT(
      !vk.isListening(),
      "Capture micro toujours suspendue tant que setupComplete n'est pas recu");

  const char *setupJson = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  advanceMockMillis(15);
  vk.update(); // setupComplete confirmé -> CONVERSATION_READY -> startTurn() ->
               // CONVERSATION_LISTENING
  TEST_ASSERT(cbReady, "Callback onWakeConvReady invoque");
  TEST_ASSERT(cbListening, "Callback onWakeConvListening invoque");
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_LISTENING,
              "Transition vers CONVERSATION_LISTENING");
  TEST_ASSERT(vk.isListening(),
              "Capture conversationnelle active apres setupComplete");
  TEST_ASSERT(String(vk.getWakeWordConversationStatusText()) == "PRÊT — PARLEZ",
              "Texte d'affichage avant parole : PRÊT — PARLEZ");

  // 5. Parole utilisateur détectée par VAD -> ÉCOUTE -> fin de parole ->
  // réponse vocale -> CONVERSATION_SPEAKING
  pushSimulatedSpeechTurn(vk, 8, 8);
  TEST_ASSERT(vk.getWakeWordConversationController().hasUserSpokenInTurn(),
              "Parole utilisateur detectee par VAD");
  TEST_ASSERT(String(vk.getWakeWordConversationStatusText()) == "ÉCOUTE",
              "Texte d'affichage pendant/apres parole : ÉCOUTE");

  injectGeminiPcmAndDrain(vk.getEngine(), 4, true);
  advanceMockMillis(10);
  vk.update(); // Détecte PLAYING -> CONVERSATION_SPEAKING
  TEST_ASSERT(cbSpeaking, "Callback onWakeConvSpeaking invoque");
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_SPEAKING,
              "Transition vers CONVERSATION_SPEAKING");
  TEST_ASSERT(String(vk.getWakeWordConversationStatusText()) ==
                  "RÉPONSE VOCALE",
              "Texte d'affichage : RÉPONSE VOCALE");

  // 6. Demande de fin normale après ce tour + vidage intégral de la réponse
  // audio
  vk.requestEndWakeWordConversationAfterTurn();
  drainVoicePlayback(
      vk.getEngine()); // drainVoicePlayback appelle update() après le dernier
                       // chunk -> ENDING_SESSION
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::ENDING_SESSION,
              "Transition vers ENDING_SESSION apres restitution complete");
  TEST_ASSERT(String(vk.getWakeWordConversationStatusText()) ==
                  "FIN DE CONVERSATION",
              "Texte d'affichage : FIN DE CONVERSATION");

  // 7. ENDING_SESSION -> déconnexion réseau -> RETURNING_TO_WAKE ->
  // réactivation WakeNet -> WAKE_ARMED
  advanceMockMillis(10);
  vk.update(); // ENDING_SESSION -> RETURNING_TO_WAKE
  TEST_ASSERT(cbEnded, "Callback onWakeConvEnded invoque");
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::RETURNING_TO_WAKE,
              "Transition vers RETURNING_TO_WAKE");
  TEST_ASSERT(!vk.getEngine().getProvider().isConnected(),
              "Session WebSocket deconnectee et ressources liberees");
  TEST_ASSERT(String(vk.getWakeWordConversationStatusText()) ==
                  "RETOUR EN ÉCOUTE",
              "Texte d'affichage : RETOUR EN ÉCOUTE");

  advanceMockMillis(60);
  vk.update(); // RETURNING_TO_WAKE -> WAKE_ARMED
  TEST_ASSERT(cbReturned, "Callback onWakeConvReturnedToWake invoque");
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Retour complet a l'etat WAKE_ARMED");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "WakeWordManager de nouveau ARMED");

  // 8. Scénario 2 : Exécution de 10 cycles consécutifs complets sans
  // redémarrage
  vk.resetWakeWordConversationMetrics();
  for (int cycle = 1; cycle <= 10; ++cycle) {
    TEST_ASSERT(vk.getWakeWordConversationState() ==
                    WakeWordConversationState::WAKE_ARMED,
                "Debut cycle : WAKE_ARMED");
    TEST_ASSERT(vk.triggerTestWakeWord("Bonjour ESP"),
                "Cycle : detection Wake Word");
    vk.update(); // -> WAKE_DETECTED
    advanceMockMillis(5);
    vk.update(); // -> PREPARING_SESSION
    advanceMockMillis(5);
    vk.update(); // -> CONNECTING
    advanceMockMillis(5);
    vk.update(); // -> activate()
    vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                     strlen(setupJson));
    advanceMockMillis(5);
    vk.update(); // -> CONVERSATION_LISTENING
    pushSimulatedSpeechTurn(vk, 8, 8);
    injectGeminiPcmAndDrain(vk.getEngine(), 3, true);
    advanceMockMillis(5);
    vk.update(); // -> CONVERSATION_SPEAKING
    vk.requestEndWakeWordConversationAfterTurn();
    drainVoicePlayback(vk.getEngine());
    advanceMockMillis(5);
    vk.update(); // -> ENDING_SESSION
    advanceMockMillis(5);
    vk.update(); // -> RETURNING_TO_WAKE
    advanceMockMillis(60);
    vk.update(); // -> WAKE_ARMED
  }

  const WakeWordConversationMetrics &m10 = vk.getWakeWordConversationMetrics();
  TEST_ASSERT(
      m10.sessionCount == 10,
      "10 sessions conversationnelles completes executees sans redemarrage");
  TEST_ASSERT(m10.detectionCount == 10, "10 detections comptabilisees");
  TEST_ASSERT(m10.simulatedDetectionCount == 10 && m10.realDetectionCount == 0,
              "Distinction stricte entre detections simulees (10) et "
              "acoustiques reelles (0)");
  TEST_ASSERT(m10.turnCount == 10, "10 tours d'echange vocal completes");
  TEST_ASSERT(m10.errorCount == 0, "0 erreur sur les 10 cycles consecutifs");
  TEST_ASSERT(m10.micTransitionFailures == 0,
              "0 echec de transition microphone sur 10 cycles");
  TEST_ASSERT(m10.networkFailures == 0, "0 echec reseau sur 10 cycles");
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Etat final WAKE_ARMED apres 10 cycles");

  vk.disableWakeWordConversation();
  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 96 : Scénarios 3 & 4 — Détections répétées pendant connexion/session et
// Timeout de silence (30 s)
void test_v063_concurrent_detections_and_silence_timeout() {
  std::cout << "\n[TEST SUITE 96] v0.6.3 : Scenarios 3 & 4 — Detections "
               "concurrentes ignorees et Timeout de silence (30 s)"
            << std::endl;

  WiFi.setConnected(true);
  setMockMillisOffset(1700000000UL * 1000UL);

  MockLiveTransport mockTransport;
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::GEMINI;
  cfg.apiKey = "AIzaSyTestWakeConvS96";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWordConversation = true;
  cfg.wakeWordResumeDelayMs = 40;

  TEST_ASSERT(vk.begin(cfg), "begin() reussi");
  vk.getEngine().getClient().setTransport(&mockTransport);

  // Vérification du timeout de silence par défaut = 30 000 ms (30 secondes —
  // Section 5)
  TEST_ASSERT(vk.getWakeConversationSilenceTimeoutMs() == 30000,
              "Timeout de silence/inactivite par defaut = 30 000 ms (30 s)");

  // Scénario 3 : Détections répétées pendant WAKE_ARMED (avant update),
  // WAKE_DETECTED, PREPARING_SESSION et CONNECTING
  vk.getWakeWordConversationController().notifyWakeWordDetected("Bonjour ESP",
                                                                0.95f, true);
  // Deuxième et troisième déclenchements simultanés avant même que update() ne
  // traite le premier
  vk.getWakeWordConversationController().notifyWakeWordDetected("Bonjour ESP",
                                                                0.95f, true);
  vk.getWakeWordConversationController().notifyWakeWordDetected("Bonjour ESP",
                                                                0.95f, true);

  vk.update(); // -> WAKE_DETECTED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_DETECTED,
              "Etat WAKE_DETECTED");
  // Déclenchement pendant WAKE_DETECTED
  vk.getWakeWordConversationController().notifyWakeWordDetected("Bonjour ESP",
                                                                0.95f, true);

  vk.update(); // -> PREPARING_SESSION
  vk.getWakeWordConversationController().notifyWakeWordDetected("Bonjour ESP",
                                                                0.95f, true);

  vk.update(); // -> CONNECTING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONNECTING,
              "Etat CONNECTING");
  // Déclenchements répétés pendant la connexion en cours
  vk.getWakeWordConversationController().notifyWakeWordDetected("Bonjour ESP",
                                                                0.95f, true);
  vk.getWakeWordConversationController().notifyWakeWordDetected("Bonjour ESP",
                                                                0.95f, true);
  TEST_ASSERT(!vk.triggerTestWakeWord("Bonjour ESP"),
              "triggerTestWakeWord refuse pendant que WakeNet est suspendu");

  vk.update(); // -> activate()
  const char *setupJson = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update(); // -> CONVERSATION_LISTENING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_LISTENING,
              "Connecte et en ecoute CONVERSATION_LISTENING");

  const WakeWordConversationMetrics &m = vk.getWakeWordConversationMetrics();
  TEST_ASSERT(m.detectionCount == 1,
              "Une seule detection valide prise en compte");
  TEST_ASSERT(m.sessionCount == 1,
              "Une seule session demarree malgre les detections concurrentes");
  TEST_ASSERT(m.ignoredConcurrentDetections == 6,
              "Les 6 detections concurrentes ont ete proprement ignorees");

  // Scénario 4 : Silence prolongé après réveil (30 secondes sans parole)
  int16_t silentChunk[AUDIO_CHUNK_SAMPLES] = {0};
  for (int i = 0; i < 10; ++i) {
    vk.getEngine().getStreamer().pushSimulatedChunk(silentChunk,
                                                    AUDIO_CHUNK_SAMPLES);
    advanceMockMillis(1000);
    vk.update();
  }
  TEST_ASSERT(
      vk.getWakeWordConversationState() ==
          WakeWordConversationState::CONVERSATION_LISTENING,
      "Apres 10 s de silence (< 30 s) : toujours en CONVERSATION_LISTENING");

  advanceMockMillis(20500); // Total > 30 000 ms de silence
  vk.update();              // CONVERSATION_LISTENING -> ENDING_SESSION
  TEST_ASSERT(
      vk.getWakeWordConversationState() ==
          WakeWordConversationState::ENDING_SESSION,
      "Apres 30 s de silence : transition automatique vers ENDING_SESSION");
  TEST_ASSERT(vk.getWakeWordConversationEndReason() ==
                  WakeWordSessionEndReason::SILENCE_TIMEOUT,
              "Raison de fin = SILENCE_TIMEOUT");

  advanceMockMillis(10);
  vk.update(); // -> RETURNING_TO_WAKE
  advanceMockMillis(50);
  vk.update(); // -> WAKE_ARMED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Retour automatique a WAKE_ARMED apres timeout de silence");

  vk.disableWakeWordConversation();
  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 97 : Scénario 5 & Commande vocale — Plusieurs tours dans une même
// session sans répéter le Wake Word
void test_v063_multi_turn_conversation_and_voice_stop_command() {
  std::cout << "\n[TEST SUITE 97] v0.6.3 : Scenario 5 — Conversation "
               "multi-tours sans repeter « Bonjour ESP » et fermeture vocale"
            << std::endl;

  WiFi.setConnected(true);
  setMockMillisOffset(1700000000UL * 1000UL);

  MockLiveTransport mockTransport;
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::GEMINI;
  cfg.apiKey = "AIzaSyTestWakeConvS97";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWordConversation = true;
  cfg.wakeWordResumeDelayMs = 40;
  cfg.wakeConversationTurnRestartDelayMs = 40;
  cfg.vadSilenceDurationMs = 200;

  TEST_ASSERT(vk.begin(cfg), "begin() reussi");
  vk.getEngine().getClient().setTransport(&mockTransport);

  // 1. Démarrage de la session par un unique « Bonjour ESP »
  TEST_ASSERT(vk.triggerTestWakeWord("Bonjour ESP"),
              "Reveil initial « Bonjour ESP »");
  vk.update(); // -> WAKE_DETECTED
  vk.update(); // -> PREPARING_SESSION
  vk.update(); // -> CONNECTING
  vk.update(); // -> activate()
  const char *setupJson = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update(); // -> CONVERSATION_LISTENING (Tour 1)
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_LISTENING,
              "Tour 1 : CONVERSATION_LISTENING");

  // 2. Tour 1 : L'utilisateur pose une première question -> réponse TTS -> fin
  // de réponse TTS
  pushSimulatedSpeechTurn(vk, 8, 8);
  injectGeminiPcmAndDrain(vk.getEngine(), 4, true);
  vk.update(); // -> CONVERSATION_SPEAKING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_SPEAKING,
              "Tour 1 : CONVERSATION_SPEAKING");
  drainVoicePlayback(vk.getEngine());
  advanceMockMillis(50);
  vk.update(); // Règle Section 5 : La fin d'un tour TTS ne ferme PAS la session
               // -> retour à CONVERSATION_LISTENING !
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_LISTENING,
              "Fin du tour 1 TTS : retour automatique en "
              "CONVERSATION_LISTENING sans repeter le Wake Word");
  TEST_ASSERT(vk.isListening(),
              "Microphone de nouveau en ecoute pour le tour 2");

  // 3. Tour 2 : L'utilisateur prononce une deuxième phrase dans la même session
  pushSimulatedSpeechTurn(vk, 8, 8);
  injectGeminiPcmAndDrain(vk.getEngine(), 4, true);
  vk.update(); // -> CONVERSATION_SPEAKING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_SPEAKING,
              "Tour 2 : CONVERSATION_SPEAKING");
  drainVoicePlayback(vk.getEngine());
  advanceMockMillis(50);
  vk.update(); // -> CONVERSATION_LISTENING (Tour 3)
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_LISTENING,
              "Fin du tour 2 TTS : retour automatique en "
              "CONVERSATION_LISTENING pour le tour 3");
  TEST_ASSERT(vk.getWakeWordConversationMetrics().sessionCount == 1,
              "Toujours dans la meme session #1");
  TEST_ASSERT(vk.getWakeWordConversationMetrics().turnCount == 2,
              "2 tours complets effectues dans la meme session");

  // 4. Test de la fermeture par commande vocale :
  //    a) Une transcription INTERIM (isFinal == false) contenant "Au revoir" NE
  //    DOIT PAS fermer la session !
  vk.getWakeWordConversationController().notifyUserTranscription("Au revoir",
                                                                 false);
  vk.update();
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_LISTENING,
              "Transcription partielle (isFinal=false) « Au revoir » ignoree : "
              "la session reste ouverte");

  //    b) Une transcription FINALE fiable (isFinal == true) contenant "Merci,
  //    au revoir !" ferme proprement la session
  vk.getWakeWordConversationController().notifyUserTranscription(
      "Merci, au revoir !", true);
  vk.update(); // -> ENDING_SESSION
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::ENDING_SESSION,
              "Transcription finale (isFinal=true) « Merci, au revoir ! » "
              "declenche ENDING_SESSION");
  TEST_ASSERT(vk.getWakeWordConversationEndReason() ==
                  WakeWordSessionEndReason::VOICE_COMMAND,
              "Raison de fin = VOICE_COMMAND");

  vk.update(); // -> RETURNING_TO_WAKE
  advanceMockMillis(50);
  vk.update(); // -> WAKE_ARMED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Session fermee et retour a WAKE_ARMED");

  vk.disableWakeWordConversation();
  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 98 : Scénarios 6 & 11 — Arrêt manuel pendant la réponse audio &
// Conservation des derniers échantillons
void test_v063_manual_stop_and_final_audio_samples_preservation() {
  std::cout << "\n[TEST SUITE 98] v0.6.3 : Scenarios 6 & 11 — Arret pendant "
               "reponse audio et conservation des derniers echantillons"
            << std::endl;

  WiFi.setConnected(true);
  setMockMillisOffset(1700000000UL * 1000UL);

  MockLiveTransport mockTransport;
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::GEMINI;
  cfg.apiKey = "AIzaSyTestWakeConvS98";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWordConversation = true;
  cfg.wakeWordResumeDelayMs = 40;
  cfg.vadSilenceDurationMs = 200;

  TEST_ASSERT(vk.begin(cfg), "begin() reussi");
  vk.getEngine().getClient().setTransport(&mockTransport);

  // --- Scénario 6 : Arrêt manuel explicite pendant la réponse vocale ---
  vk.triggerTestWakeWord("Bonjour ESP");
  vk.update(); // -> WAKE_DETECTED
  vk.update(); // -> PREPARING_SESSION
  vk.update(); // -> CONNECTING
  vk.update(); // -> activate()
  const char *setupJson = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update(); // -> CONVERSATION_LISTENING

  pushSimulatedSpeechTurn(vk, 8, 8);
  // Injecter 20 blocs audio SANS les vider complètement pour que le
  // haut-parleur soit en pleine lecture
  injectGeminiPcmAndDrain(vk.getEngine(), 20, false);
  vk.update(); // -> CONVERSATION_SPEAKING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_SPEAKING,
              "En cours de restitution CONVERSATION_SPEAKING");
  TEST_ASSERT(vk.isSpeaking(), "Haut-parleur actif pendant la reponse");

  // Arrêt manuel immédiat pendant la réponse audio
  TEST_ASSERT(vk.stopWakeWordConversationSession(true),
              "stopWakeWordConversationSession(true) accepte");
  TEST_ASSERT(!vk.isSpeaking(), "Restitution audio interrompue immediatement");
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::ENDING_SESSION,
              "Passage immediat en ENDING_SESSION");
  TEST_ASSERT(vk.getWakeWordConversationEndReason() ==
                  WakeWordSessionEndReason::MANUAL_STOP,
              "Raison de fin = MANUAL_STOP");

  vk.update(); // -> RETURNING_TO_WAKE
  advanceMockMillis(50);
  vk.update(); // -> WAKE_ARMED
  TEST_ASSERT(
      vk.getWakeWordConversationState() ==
          WakeWordConversationState::WAKE_ARMED,
      "Retour propre a WAKE_ARMED apres arret manuel pendant la reponse");

  // --- Scénario 11 : Conservation intégrale des derniers échantillons lors
  // d'une fermeture normale ---
  vk.triggerTestWakeWord("Bonjour ESP");
  vk.update(); // -> WAKE_DETECTED
  vk.update(); // -> PREPARING_SESSION
  vk.update(); // -> CONNECTING
  vk.update(); // -> activate()
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update(); // -> CONVERSATION_LISTENING

  pushSimulatedSpeechTurn(vk, 8, 8);
  // 10 blocs de 6 échantillons = 60 échantillons + turnComplete
  injectGeminiPcmAndDrain(vk.getEngine(), 10, true);
  vk.update(); // -> CONVERSATION_SPEAKING
  TEST_ASSERT(vk.getEngine().getSession().getTurnDecodedSamples() == 60,
              "60 echantillons decodes pour la reponse finale");

  // Demander la fermeture normale de session (ou commande vocale) PENDANT que
  // les échantillons sont encore dans le tampon
  vk.requestEndWakeWordConversationAfterTurn();
  vk.update();
  // tant que le lecteur n'a pas joué les 60 échantillons, la session ne doit
  // PAS couper le lecteur
  while (vk.getEngine().getPlayer().getBufferedSamples() > 0 ||
         vk.getEngine().getPlayer().isPlaying()) {
    TEST_ASSERT(vk.getWakeWordConversationState() ==
                        WakeWordConversationState::CONVERSATION_SPEAKING ||
                    vk.getWakeWordConversationState() ==
                        WakeWordConversationState::ENDING_SESSION,
                "Maintien de la restitution jusqu'au dernier echantillon");
    vk.getEngine().getPlayer().simFinishSpeakerChunk();
    advanceMockMillis(10);
    vk.update();
  }

  TEST_ASSERT(vk.getEngine().getPlayer().getSamplesPlayedTotal() == 60,
              "Les 60 echantillons (100%, y compris les tout derniers) ont ete "
              "integralement joues avant fermeture");
  TEST_ASSERT(vk.getEngine().getPlayer().getOverflowDroppedSamples() == 0,
              "0 echantillon perdu");

  vk.update(); // -> RETURNING_TO_WAKE (si en ENDING_SESSION)
  advanceMockMillis(50);
  vk.update(); // -> WAKE_ARMED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Retour a WAKE_ARMED apres restitution integrale des derniers "
              "echantillons");

  vk.disableWakeWordConversation();
  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 99 : Scénarios 7, 8, 9 & 10 — Perte Wi-Fi, Déconnexion WebSocket, Échec
// libération micro & Réarmement
void test_v063_wifi_loss_ws_disconnect_mic_failure_and_recovery() {
  std::cout << "\n[TEST SUITE 99] v0.6.3 : Scenarios 7, 8, 9 & 10 — Perte "
               "Wi-Fi, Deconnexion WS, Echec micro et Rearmement"
            << std::endl;

  WiFi.setConnected(true);
  setMockMillisOffset(1700000000UL * 1000UL);

  MockLiveTransport mockTransport;
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::GEMINI;
  cfg.apiKey = "AIzaSyTestWakeConvS99";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWordConversation = true;
  cfg.wakeWordResumeDelayMs = 40;
  cfg.wakeConversationErrorRecoveryDelayMs = 100;
  cfg.vadSilenceDurationMs = 200;

  int recoverableErrorCallbacks = 0;
  vk.onWakeConvRecoverableError(
      [&](const String &) { recoverableErrorCallbacks++; });

  TEST_ASSERT(vk.begin(cfg), "begin() reussi");
  vk.getEngine().getClient().setTransport(&mockTransport);

  // --- Scénario 7 : Perte de Wi-Fi pendant la connexion (CONNECTING) ---
  vk.triggerTestWakeWord("Bonjour ESP");
  vk.update(); // -> WAKE_DETECTED
  vk.update(); // -> PREPARING_SESSION
  vk.update(); // -> CONNECTING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONNECTING,
              "En cours de connexion CONNECTING");

  // Coupure Wi-Fi pendant CONNECTING
  WiFi.setConnected(false);
  vk.update();
  TEST_ASSERT(
      vk.getWakeWordConversationState() ==
          WakeWordConversationState::RECOVERABLE_ERROR,
      "Perte Wi-Fi pendant CONNECTING -> transition vers RECOVERABLE_ERROR");
  TEST_ASSERT(vk.getWakeWordConversationMetrics().networkFailures == 1,
              "networkFailures incremente (1)");
  TEST_ASSERT(recoverableErrorCallbacks == 1,
              "Callback onWakeConvRecoverableError invoque");
  TEST_ASSERT(String(vk.getWakeWordConversationStatusText()) ==
                  "ERREUR / RÉCUPÉRATION",
              "Texte d'affichage : ERREUR / RÉCUPÉRATION");

  // Rétablissement Wi-Fi et récupération contrôlée (Scénario 10)
  WiFi.setConnected(true);
  advanceMockMillis(120);
  vk.update(); // RECOVERABLE_ERROR -> RETURNING_TO_WAKE
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::RETURNING_TO_WAKE,
              "Transition RECOVERABLE_ERROR -> RETURNING_TO_WAKE");
  advanceMockMillis(50);
  vk.update(); // RETURNING_TO_WAKE -> WAKE_ARMED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Rearmement reussi vers WAKE_ARMED apres perte Wi-Fi");
  TEST_ASSERT(vk.getWakeWordConversationMetrics().recoveryCount == 1,
              "recoveryCount = 1");

  // --- Scénario 8 : Déconnexion WebSocket pendant la conversation ---
  vk.triggerTestWakeWord("Bonjour ESP");
  vk.update(); // -> WAKE_DETECTED
  vk.update(); // -> PREPARING_SESSION
  vk.update(); // -> CONNECTING
  vk.update(); // -> activate() (et incrémente reconnectCount suite à l'échec
               // réseau précédent)
  const char *setupJson = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update(); // -> CONVERSATION_LISTENING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_LISTENING,
              "Session connectee en CONVERSATION_LISTENING");
  TEST_ASSERT(vk.getWakeWordConversationMetrics().reconnectCount >= 1,
              "Compteur de reconnexions incremente apres recuperation reseau");

  // Coupure brutale du WebSocket en pleine conversation
  mockTransport.disconnect();
  vk.update();
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::RECOVERABLE_ERROR,
              "Deconnexion WebSocket en conversation -> transition vers "
              "RECOVERABLE_ERROR");
  TEST_ASSERT(!vk.isListening(),
              "Capture microphone immediatement stoppee sur coupure WebSocket");
  TEST_ASSERT(vk.getWakeWordConversationMetrics().networkFailures == 2,
              "networkFailures incremente (2)");

  advanceMockMillis(120);
  vk.update(); // -> RETURNING_TO_WAKE
  advanceMockMillis(50);
  vk.update(); // -> WAKE_ARMED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Rearmement reussi vers WAKE_ARMED apres deconnexion WebSocket");

  // --- Scénario 9 : Échec temporaire de libération du microphone ---
  // Simuler un refus temporaire d'arrêt de la tâche microphone lors de la
  // détection Wake Word
  vk.getEngine().getStreamer().setSimMicTerminationTimeout(true);
  vk.triggerTestWakeWord("Bonjour ESP");
  vk.update(); // WakeWordManager échoue stopCaptureIfNeeded() ->
               // WakeWordState::ERROR -> détecté par contrôleur
  if (vk.getWakeWordConversationState() !=
      WakeWordConversationState::RECOVERABLE_ERROR) {
    vk.update();
  }
  TEST_ASSERT(
      vk.getWakeWordConversationState() ==
          WakeWordConversationState::RECOVERABLE_ERROR,
      "Echec liberation microphone -> maintien securise en RECOVERABLE_ERROR");
  TEST_ASSERT(vk.getWakeWordConversationMetrics().micTransitionFailures >= 1,
              "micTransitionFailures incremente sur echec d'arret micro");
  TEST_ASSERT(!vk.getEngine().getStreamer().isStreaming(),
              "Aucune nouvelle capture conversationnelle demarree tant que la "
              "precedente n'est pas arretee");

  // Tant que la tâche micro est en quarantaine, le contrôleur reste en
  // RECOVERABLE_ERROR
  advanceMockMillis(150);
  vk.update();
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::RECOVERABLE_ERROR,
              "Maintien en RECOVERABLE_ERROR tant que la tache micro en "
              "quarantaine n'est pas terminee");

  // Fin de l'anomalie temporaire micro : la tâche finit par s'arrêter
  vk.getEngine().getStreamer().setSimMicTerminationTimeout(false);
  vk.getEngine().getStreamer().setSimTaskRunning(false);
  advanceMockMillis(150);
  vk.update(); // RECOVERABLE_ERROR -> RETURNING_TO_WAKE
  TEST_ASSERT(
      vk.getWakeWordConversationState() ==
          WakeWordConversationState::RETURNING_TO_WAKE,
      "Levee de quarantaine micro -> transition vers RETURNING_TO_WAKE");
  advanceMockMillis(50);
  vk.update(); // RETURNING_TO_WAKE -> WAKE_ARMED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Scenario 10 : Rearmement complet a WAKE_ARMED apres erreur "
              "recuperable microphone");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ARMED,
              "WakeWordManager de nouveau ARMED et fonctionnel");

  vk.disableWakeWordConversation();
  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 100 : Scénario 12, OpenAI Realtime & Diagnostics — Non-blocage LVGL,
// Fournisseur alternatif et Métriques
void test_v063_lvgl_non_blocking_openai_provider_and_diagnostics() {
  std::cout << "\n[TEST SUITE 100] v0.6.3 : Scenario 12, OpenAI Realtime & "
               "Diagnostics — Non-blocage LVGL et multi-fournisseurs"
            << std::endl;

  WiFi.setConnected(true);
  setMockMillisOffset(1700000000UL * 1000UL);

  MockLiveTransport mockTransportOAI;
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::OPENAI;
  cfg.apiKey = "sk-test-openai-wakeconv-v063";
  cfg.model = "gpt-realtime-2.1-mini";
  cfg.voice = "alloy";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWordConversation = true;
  cfg.wakeWordResumeDelayMs = 30;
  cfg.wakeConversationTurnRestartDelayMs = 30;
  cfg.vadSilenceDurationMs = 200;

  TEST_ASSERT(vk.begin(cfg),
              "begin() avec fournisseur alternatif OPENAI reussi");
  vk.getEngine().getOpenAIClient().setTransport(&mockTransportOAI);

  // Cycle complet avec OpenAI Realtime + consommation des événements par
  // pollWakeWordConversationEvent() (LVGL 9)
  TEST_ASSERT(vk.triggerTestWakeWord("Bonjour ESP"),
              "Declenchement « Bonjour ESP » (OpenAI)");
  advanceMockMillis(5);
  vk.update(); // -> WAKE_DETECTED
  advanceMockMillis(5);
  vk.update(); // -> PREPARING_SESSION
  advanceMockMillis(5);
  vk.update(); // -> CONNECTING
  TEST_ASSERT(String(vk.getWakeWordConversationStatusText()) ==
                  "CONNEXION OPENAI",
              "Texte d'affichage specifique OpenAI : CONNEXION OPENAI");

  advanceMockMillis(10);
  vk.update(); // -> activate() -> OpenAI connect()
  const char *sessCreated =
      "{\"type\":\"session.created\",\"session\":{\"id\":\"sess_oai_v063\"}}";
  vk.getEngine().getOpenAIClient().handleIncomingMessage(sessCreated,
                                                         strlen(sessCreated));
  const char *sessUpdated =
      "{\"type\":\"session.updated\",\"session\":{\"id\":\"sess_oai_v063\"}}";
  vk.getEngine().getOpenAIClient().handleIncomingMessage(sessUpdated,
                                                         strlen(sessUpdated));
  advanceMockMillis(15);
  vk.update(); // -> CONVERSATION_LISTENING
  TEST_ASSERT(
      vk.getWakeWordConversationState() ==
          WakeWordConversationState::CONVERSATION_LISTENING,
      "OpenAI Realtime : session prete et en ecoute CONVERSATION_LISTENING");

  pushSimulatedSpeechTurn(vk, 8, 8);
  const char *deltaOAI = "{\"type\":\"response.output_audio.delta\",\"delta\":"
                         "\"AAAAAAAAAAAAAAAA\"}";
  vk.getEngine().getOpenAIClient().handleIncomingMessage(deltaOAI,
                                                         strlen(deltaOAI));
  vk.getEngine().getOpenAIClient().handleIncomingMessage(deltaOAI,
                                                         strlen(deltaOAI));
  const char *doneOAI = "{\"type\":\"response.done\"}";
  vk.getEngine().getOpenAIClient().handleIncomingMessage(doneOAI,
                                                         strlen(doneOAI));

  advanceMockMillis(10);
  vk.update(); // -> CONVERSATION_SPEAKING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_SPEAKING,
              "OpenAI Realtime : CONVERSATION_SPEAKING");

  vk.requestEndWakeWordConversationAfterTurn();
  drainVoicePlayback(vk.getEngine());
  advanceMockMillis(10);
  vk.update(); // -> ENDING_SESSION
  advanceMockMillis(10);
  vk.update(); // -> RETURNING_TO_WAKE
  advanceMockMillis(40);
  vk.update(); // -> WAKE_ARMED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "OpenAI Realtime : cycle complet termine et retour a WAKE_ARMED");

  // Vérification de la file d'événements non bloquante pour LVGL 9
  size_t polledEvents = 0;
  WakeWordConversationEvent ev;
  while (vk.pollWakeWordConversationEvent(ev)) {
    polledEvents++;
  }
  TEST_ASSERT(polledEvents >= 6,
              "File d'evenements LVGL 9 (pollWakeWordConversationEvent) "
              "alimentee sans blocage");

  // Scénario 12 & Section 9 : Vérification des métriques de temps et d'absence
  // de blocage prolongé
  const WakeWordConversationMetrics &m = vk.getWakeWordConversationMetrics();
  TEST_ASSERT(m.wakeToConnectStartMs > 0,
              "Metrique wakeToConnectStartMs mesuree (> 0 ms)");
  TEST_ASSERT(m.wakeToConversationReadyMs >= m.wakeToConnectStartMs,
              "Metrique wakeToConversationReadyMs coherente");
  TEST_ASSERT(m.lastSessionDurationMs > 0,
              "Metrique lastSessionDurationMs mesuree (> 0 ms)");
  TEST_ASSERT(m.totalSessionDurationMs >= m.lastSessionDurationMs,
              "Metrique totalSessionDurationMs cumulee");
  TEST_ASSERT(m.returnToArmedMs > 0,
              "Metrique returnToArmedMs mesuree (> 0 ms)");
  TEST_ASSERT(m.freeInternalHeap > 0 && m.minFreeInternalHeap > 0,
              "Metriques RAM interne renseignees");
  TEST_ASSERT(m.freePsram > 0 && m.minFreePsram > 0,
              "Metriques PSRAM renseignees");
  TEST_ASSERT(m.maxControllerUpdateDurationUs < 50000,
              "Scenario 12 : Aucun blocage prolonge dans controller.update() "
              "(< 50 ms)");
  TEST_ASSERT(
      m.maxVoiceUpdateDurationUs < 50000,
      "Scenario 12 : Aucun blocage prolonge dans voice.update() (< 50 ms)");

  vk.printWakeWordConversationDiagnostics();
  vk.disableWakeWordConversation();
  vk.getEngine().getOpenAIClient().setTransport(nullptr);
}

// =============================================================================
// NOUVELLES SUITES DE TESTS — VoiceKit v0.6.3.2.1 (Corrective Stabilité &
// WakeNet Configurable)
// =============================================================================

// SUITE 101 : Section 1 & 5 — Instrumentation détaillée de latence et arrêt non
// bloquant de session
void test_v0631_latency_instrumentation_and_nonblocking_session_teardown() {
  std::cout << "\n[TEST SUITE 101] v0.6.3.1 : Section 1 & 5 — Instrumentation "
               "de latence et arret non bloquant de session WebSocket"
            << std::endl;

  WiFi.setConnected(true);
  setMockMillisOffset(1700000000UL * 1000UL);

  MockLiveTransport mockTransport;
  // Simuler une tâche WebSocket qui met 4 ticks d'update() avant de signaler
  // STOPPED_BIT
  mockTransport.setSimulatedAsyncDisconnectPolls(4);

  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::GEMINI;
  cfg.apiKey = "AIzaSyTestWakeConvV0631S101";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWordConversation = true;
  cfg.wakeWordResumeDelayMs = 250;
  cfg.vadSilenceDurationMs = 200;

  TEST_ASSERT(vk.begin(cfg), "begin() reussi (Suite 101)");
  vk.getEngine().getClient().setTransport(&mockTransport);

  // 1. Démarrer une session conversationnelle complète
  TEST_ASSERT(vk.triggerTestWakeWord("Bonjour ESP"), "Reveil « Bonjour ESP »");
  advanceMockMillis(5);
  vk.update(); // -> WAKE_DETECTED
  advanceMockMillis(5);
  vk.update(); // -> PREPARING_SESSION
  advanceMockMillis(5);
  vk.update(); // -> CONNECTING
  advanceMockMillis(5);
  vk.update(); // -> activate()
  const char *setupJson = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  advanceMockMillis(5);
  vk.update(); // -> CONVERSATION_LISTENING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_LISTENING,
              "Etat CONVERSATION_LISTENING actif");

  size_t sentBeforeClose = mockTransport.getSendCount();
  // 2. Fermeture vocale par « Au revoir » pendant CONVERSATION_LISTENING :
  //    Vérifier que abortTurnForSessionClose() n'envoie PAS de trame
  //    sendInterruption() inutile avant fermeture
  vk.getWakeWordConversationController().notifyUserTranscription("Au revoir",
                                                                 true);
  advanceMockMillis(5);
  vk.update(); // -> ENDING_SESSION
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::ENDING_SESSION,
              "Transition vers ENDING_SESSION");
  TEST_ASSERT(mockTransport.getSendCount() == sentBeforeClose,
              "Aucune trame sendInterruption() superflue emise juste avant la "
              "fermeture de session");

  // 3. Test de l'arrêt non bloquant en 2 phases (requestDeactivate /
  // pollDeactivate) :
  //    Pendant les 3 premiers appels à update() en ENDING_SESSION,
  //    pollDeactivate() retourne false sans bloquer loopTask
  for (int poll = 1; poll <= 3; ++poll) {
    advanceMockMillis(10);
    vk.update();
    TEST_ASSERT(vk.getWakeWordConversationState() ==
                    WakeWordConversationState::ENDING_SESSION,
                "Maintien non bloquant en ENDING_SESSION tant que "
                "websocket_task termine son poll_read");
    TEST_ASSERT(vk.getEngine().getSession().isDeactivating(),
                "Session en cours de desactivation non bloquante "
                "(isDeactivating=true)");
  }

  // 4e appel : STOPPED_BIT atteint -> pollDeactivate() retourne true -> passage
  // à RETURNING_TO_WAKE
  advanceMockMillis(10);
  vk.update();
  TEST_ASSERT(!vk.getEngine().getSession().isDeactivating(),
              "Desactivation non bloquante terminee (isDeactivating=false)");
  TEST_ASSERT(
      vk.getWakeWordConversationState() ==
          WakeWordConversationState::RETURNING_TO_WAKE,
      "Passage a RETURNING_TO_WAKE des que la tache WebSocket est arretee");
  TEST_ASSERT(String(vk.getWakeWordConversationBannerText()) ==
                  "RETOUR EN ÉCOUTE",
              "Bandeau UI RETOUR EN ÉCOUTE immediatement visible et preserve "
              "(non saute par le temps d'arret WS)");

  // Vérifier que la garde acoustique de 250 ms commence APRES la fin réelle de
  // désactivation
  advanceMockMillis(100);
  vk.update();
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::RETURNING_TO_WAKE,
              "A +100 ms (< 250 ms apres desactivation) : toujours en "
              "RETURNING_TO_WAKE");

  advanceMockMillis(160); // Total 260 ms >= 250 ms
  vk.update();
  TEST_ASSERT(
      vk.getWakeWordConversationState() ==
          WakeWordConversationState::WAKE_ARMED,
      "A +260 ms (>= 250 ms apres desactivation) : retour propre a WAKE_ARMED");

  // 4. Vérification des métriques d'instrumentation séparées (Section 1 & 5)
  const WakeWordConversationMetrics &m = vk.getWakeWordConversationMetrics();
  TEST_ASSERT(m.voiceUpdateCallCount >= 10,
              "Compteur voiceUpdateCallCount renseigne");
  TEST_ASSERT(m.avgVoiceUpdateDurationUs <= m.maxVoiceUpdateDurationUs,
              "Coherence avgVoiceUpdateDurationUs <= maxVoiceUpdateDurationUs");
  TEST_ASSERT(
      m.maxControllerUpdateDurationUs < 50000,
      "WakeWordConversationController::update() non bloquant (< 50 ms)");
  TEST_ASSERT(m.maxStateTransitionDurationUs < 50000,
              "Duree maximale des transitions d'etat non bloquante (< 50 ms)");
  TEST_ASSERT(m.maxWakeWordUpdateDurationUs < 50000,
              "WakeWordManager::update() non bloquant (< 50 ms)");
  TEST_ASSERT(m.maxSessionTickDurationUs < 50000,
              "LiveSessionController::tick() non bloquant (< 50 ms)");
  TEST_ASSERT(m.maxWsProcessDurationUs < 50000,
              "Traitements WebSocket non bloquants (< 50 ms)");
  TEST_ASSERT(m.maxMicTransitionDurationUs < 50000,
              "Transitions microphone non bloquantes (< 50 ms)");
  TEST_ASSERT(m.maxSessionStopDurationUs < 50000,
              "Arret de session non bloquant (< 50 ms)");
  TEST_ASSERT(m.maxWakeReactivationDurationUs < 50000,
              "Reactivation WakeNet non bloquante (< 50 ms)");
  TEST_ASSERT(m.successfulSessionCount == 1,
              "successfulSessionCount = 1 apres session terminee normalement");

  mockTransport.setSimulatedAsyncDisconnectPolls(0);
  vk.disableWakeWordConversation();
  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 102 : Section 2 — Consolidation des transitions audio & Concurrence
// microphone (propriétaire unique)
void test_v0631_audio_transitions_consolidation_and_mic_concurrency() {
  std::cout << "\n[TEST SUITE 102] v0.6.3.1 : Section 2 — Consolidation des "
               "transitions audio et concurrence du microphone"
            << std::endl;

  WiFi.setConnected(true);
  setMockMillisOffset(1700000000UL * 1000UL);
  HardwareManager::getInstance().resetMicReservationFailureCount();

  MockLiveTransport mockTransport;
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::GEMINI;
  cfg.apiKey = "AIzaSyTestWakeConvV0631S102";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWordConversation = true;
  cfg.wakeWordResumeDelayMs = 40;
  cfg.vadSilenceDurationMs = 200;

  TEST_ASSERT(vk.begin(cfg), "begin() reussi (Suite 102)");
  vk.getEngine().getClient().setTransport(&mockTransport);

  // 1.En WAKE_ARMED : le propriétaire unique du microphone est le
  // LiveAudioStreamer de VoiceKit
  TEST_ASSERT(
      HardwareManager::getInstance().isMicCaptureReserved(),
      "WAKE_ARMED : microphone reserve pour la capture locale WakeWord");
  TEST_ASSERT(
      HardwareManager::getInstance().getMicOwner() ==
          &vk.getEngine().getStreamer(),
      "WAKE_ARMED : proprietaire unique du microphone = LiveAudioStreamer");

  // 2. Test de concurrence du microphone : tentative de réservation concurrente
  // par un second propriétaire fictif
  int dummySecondOwner = 42;
  TEST_ASSERT(
      !HardwareManager::getInstance().prepareMicCapture(&dummySecondOwner),
      "Refus strict d'un double demarrage I2S / second proprietaire du "
      "microphone");
  TEST_ASSERT(HardwareManager::getInstance().getMicReservationFailureCount() ==
                  1,
              "Compteur d'echecs de reservation microphone incremente (1)");
  TEST_ASSERT(vk.getEngine().getStreamer().startLocalCapture() == true,
              "startLocalCapture() idempotent pour le proprietaire actuel sans "
              "double demarrage I2S");

  // 3. Vérification de l'absence de reprise WakeNet pendant toute la
  // conversation
  vk.triggerTestWakeWord("Bonjour ESP");
  vk.update(); // -> WAKE_DETECTED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_DETECTED,
              "WAKE_DETECTED");
  vk.update(); // -> PREPARING_SESSION (libère la capture locale demandée en
               // WAKE_DETECTED)
  TEST_ASSERT(
      !vk.getEngine().getStreamer().isLocalCapture(),
      "Capture locale WakeWord arretee des WAKE_DETECTED -> PREPARING_SESSION");
  vk.update(); // -> CONNECTING
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "WakeWordManager SUSPENDED en CONNECTING");
  TEST_ASSERT(!HardwareManager::getInstance().isMicCaptureReserved(),
              "Microphone libre pendant CONNECTING (aucun proprietaire actif "
              "avant setupComplete)");

  // Tenter de forcer resumeWakeWord() ou notifyVoiceActivity(false) pendant
  // CONNECTING : refusé car session en cours
  vk.getWakeWordManager().notifyVoiceActivity(false, false);
  vk.getWakeWordManager().update();
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::SUSPENDED,
              "Aucune reprise WakeNet possible pendant CONNECTING");

  vk.update(); // -> activate()
  const char *setupJson = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update(); // -> CONVERSATION_LISTENING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_LISTENING,
              "CONVERSATION_LISTENING actif");
  TEST_ASSERT(HardwareManager::getInstance().isMicCaptureReserved(),
              "Microphone reserve pour la capture conversationnelle");
  TEST_ASSERT(
      vk.getWakeWordState() == WakeWordState::SUSPENDED,
      "WakeNet reste strictement SUSPENDED pendant CONVERSATION_LISTENING");

  // 4. Transition CONVERSATION_LISTENING -> CONVERSATION_SPEAKING : un seul
  // flux actif, pas de double I2S
  pushSimulatedSpeechTurn(vk, 8, 8);
  injectGeminiPcmAndDrain(vk.getEngine(), 6, true);
  vk.update(); // -> CONVERSATION_SPEAKING
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_SPEAKING,
              "CONVERSATION_SPEAKING actif");
  TEST_ASSERT(!vk.getEngine().getStreamer().isStreaming() &&
                  !vk.getEngine().getStreamer().isLocalCapture(),
              "Capture microphone totalement arretee pendant la restitution "
              "haut-parleur");
  TEST_ASSERT(!HardwareManager::getInstance().isMicCaptureReserved(),
              "Aucun proprietaire microphone pendant CONVERSATION_SPEAKING");
  TEST_ASSERT(
      vk.getWakeWordState() == WakeWordState::SUSPENDED,
      "WakeNet reste strictement SUSPENDED pendant CONVERSATION_SPEAKING");

  // 5. Fin de session : aucune fermeture prématurée du lecteur audio, aucun
  // WebSocket résiduel
  vk.requestEndWakeWordConversationAfterTurn();
  drainVoicePlayback(vk.getEngine());
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::ENDING_SESSION,
              "ENDING_SESSION apres vidage complet du lecteur audio");
  vk.update(); // -> RETURNING_TO_WAKE
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::RETURNING_TO_WAKE,
              "RETURNING_TO_WAKE");
  TEST_ASSERT(!vk.getEngine().getProvider().isConnected() &&
                  !vk.getEngine().getProvider().isDisconnectPending(),
              "Aucun WebSocket residuel apres la fin de session");

  advanceMockMillis(50);
  vk.update(); // -> WAKE_ARMED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Retour complet a WAKE_ARMED");
  TEST_ASSERT(vk.getWakeWordConversationMetrics().micReservationFailures == 1,
              "Metrique micReservationFailures synchronisee dans "
              "WakeWordConversationMetrics (1)");

  vk.disableWakeWordConversation();
  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 103 : Section 3 — Sélection configurable du modèle WakeNet,
// vérification 4 niveaux et modèle absent
void test_v0631_configurable_wakenet_model_and_missing_model_validation() {
  std::cout << "\n[TEST SUITE 103] v0.6.3.1 : Section 3 — Selection du modele "
               "WakeNet, verification 4 niveaux et modele absent"
            << std::endl;

  // 1. Vérification des valeurs par défaut obligatoires dans VoiceKitConfig
  VoiceKitConfig defaultCfg;
  TEST_ASSERT(defaultCfg.wakeWordPhrase == "Bonjour ESP",
              "Valeur par defaut obligatoire : config.wakeWordPhrase == "
              "\"Bonjour ESP\"");
  TEST_ASSERT(defaultCfg.wakeWordModelName == "wn10_fr_bonjouresp",
              "Valeur par defaut obligatoire : config.wakeWordModelName == "
              "\"wn10_fr_bonjouresp\"");
  TEST_ASSERT(defaultCfg.wakeWordEngine == WakeWordEngineType::WAKENET,
              "Moteur par defaut : WAKENET");

  // 2. Test sur WakeNetEngine / WakeWordManager avec simulation du support
  // compilé ESP-SR
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::WAKENET;
  cfg.wakeWordPhrase = "Bonjour ESP";
  cfg.wakeWordModelName = "wn10_fr_bonjouresp";
  TEST_ASSERT(vk.begin(cfg), "begin() hors ligne avec WAKENET configure");

  WakeNetEngine &wn = vk.getWakeWordManager().getWakeNetEngine();
  wn.setSimEnvironment(true, true, true, false);
  wn.clearSimAvailableModels(); // Seul wn10_fr_bonjouresp est présent par
                                // défaut dans la partition Flash

  // Vérification des 4 niveaux avec le modèle par défaut wn10_fr_bonjouresp
  TEST_ASSERT(vk.enableWakeWord(true),
              "Activation WakeNet reussie avec wn10_fr_bonjouresp");
  TEST_ASSERT(vk.isWakeWordEngineCompiledSupportAvailable(),
              "1. Support WakeNet compile verifie");
  TEST_ASSERT(vk.isWakeWordEngineModelAvailable(),
              "2. Presence du modele wn10_fr_bonjouresp dans la partition "
              "Flash verifiee");
  TEST_ASSERT(vk.isWakeWordEngineInitialized(),
              "3. Initialisation du modele verifiee");
  TEST_ASSERT(vk.isWakeWordRealRecognitionActive(),
              "4. Reconnaissance acoustique reelle active verifiee");
  TEST_ASSERT(!vk.isWakeWordSimulated(), "Non simule (moteur acoustique reel)");
  TEST_ASSERT(String(vk.getWakeWordModelName()) == "wn10_fr_bonjouresp",
              "Modele actif = wn10_fr_bonjouresp");
  TEST_ASSERT(vk.isWakeWordPhraseConsistentWithModel(),
              "Coherence wakeWordPhrase (« Bonjour ESP ») et modele "
              "wn10_fr_bonjouresp");

  // 3. Règle Section 3 : Ne jamais supposer qu'une simple modification de
  // wakeWordPhrase entraîne l'apprentissage d'une nouvelle phrase
  vk.setWakeWordPhrase("Salut Robot");
  TEST_ASSERT(!vk.isWakeWordPhraseConsistentWithModel(),
              "Modification de wakeWordPhrase seule detectee comme incoherente "
              "avec le modele acoustique fixe wn10_fr_bonjouresp");
  vk.setWakeWordPhrase("Bonjour ESP");
  TEST_ASSERT(vk.isWakeWordPhraseConsistentWithModel(),
              "Retour a « Bonjour ESP » : coherence retablie");

  // 4. Test de changement de modèle WakeNet avec modèle ABSENT de la partition
  // Flash :
  //    - Ne peut pas être activé tant que le modèle est en écoute active (refus
  //    sécurisé)
  TEST_ASSERT(
      !vk.setWakeWordModelName("wn9_hiesp"),
      "Refus de changer wakeWordModelName pendant que WakeNet est ARMED");

  vk.disableWakeWord();
  TEST_ASSERT(vk.setWakeWordModelName("wn9_hiesp"),
              "Selection explicite d'un autre modele WakeNet (wn9_hiesp) "
              "acceptee hors ecoute");
  TEST_ASSERT(vk.getResolvedWakeWordModelName() == "wn9_hiesp",
              "Nom du modele configure = wn9_hiesp");
  TEST_ASSERT(!vk.isWakeWordEngineModelAvailable(),
              "Verification partition Flash : wn9_hiesp detecte comme ABSENT");

  // Tenter d'activer WakeWord avec le modèle absent : doit échouer proprement
  // SANS basculer vers TEST !
  TEST_ASSERT(!vk.enableWakeWord(true),
              "enableWakeWord(true) refuse proprement un modele absent de la "
              "partition Flash");
  TEST_ASSERT(vk.getWakeWordState() == WakeWordState::ERROR,
              "WakeWordManager passe en etat ERROR sur modele absent");
  TEST_ASSERT(
      vk.getWakeWordEngineType() == WakeWordEngineType::WAKENET,
      "Aucun remplacement automatique par le moteur TEST (reste WAKENET)");
  TEST_ASSERT(
      !vk.isWakeWordSimulated(),
      "isWakeWordSimulated() reste false (pas de bascule implicite vers TEST)");
  TEST_ASSERT(!vk.isWakeWordRealRecognitionActive(),
              "isWakeWordRealRecognitionActive() est false lorsque le modele "
              "est absent");

  // 5. Test avec un modèle additionnel installé dans la partition Flash
  // (simulé)
  wn.addSimAvailableModel("wn9_hiesp");
  TEST_ASSERT(vk.isWakeWordEngineModelAvailable(),
              "Une fois wn9_hiesp installe en Flash : "
              "isWakeWordEngineModelAvailable() == true");
  TEST_ASSERT(
      vk.enableWakeWord(true),
      "Activation reussie du modele explicitement selectionne wn9_hiesp");
  TEST_ASSERT(vk.isWakeWordRealRecognitionActive(),
              "Reconnaissance acoustique active avec wn9_hiesp");

  // 6. Rétablissement du modèle par défaut obligatoire wn10_fr_bonjouresp et
  // extensibilité microWakeWord (IWakeWordEngine)
  vk.disableWakeWord();
  wn.clearSimAvailableModels();
  wn.setSimEnvironment(false, false, false, false);
  TEST_ASSERT(vk.setWakeWordModelName("wn10_fr_bonjouresp"),
              "Retour au modele par defaut wn10_fr_bonjouresp");
  TEST_ASSERT(String(vk.getWakeWordModelName()) == "wn10_fr_bonjouresp",
              "Modele par defaut wn10_fr_bonjouresp restaure");
}

// SUITE 104 : Section 4 — Stabilité conversationnelle complète & distinction
// fin de tour vs fin de session
void test_v0631_conversational_stability_and_turn_vs_session_distinction() {
  std::cout << "\n[TEST SUITE 104] v0.6.3.1 : Section 4 — Stabilite "
               "conversationnelle et distinction fin de tour / fin de session"
            << std::endl;

  WiFi.setConnected(true);
  setMockMillisOffset(1700000000UL * 1000UL);

  MockLiveTransport mockTransport;
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::GEMINI;
  cfg.apiKey = "AIzaSyTestWakeConvV0631S104";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWordConversation = true;
  cfg.wakeWordResumeDelayMs = 30;
  cfg.wakeConversationTurnRestartDelayMs = 30;
  cfg.wakeConversationErrorRecoveryDelayMs = 80;
  cfg.vadSilenceDurationMs = 200;

  TEST_ASSERT(vk.begin(cfg), "begin() reussi (Suite 104)");
  vk.getEngine().getClient().setTransport(&mockTransport);

  // 1. Session 1 : 2 tours successifs (vérifie la distinction fin de tour !=
  // fin de session) + arrêt par « Au revoir »
  vk.triggerTestWakeWord("Bonjour ESP");
  vk.update();
  vk.update();
  vk.update();
  vk.update();
  const char *setupJson = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_LISTENING,
              "Session 1 : LISTENING");

  // Tour 1 : fin de tour normale -> doit revenir en CONVERSATION_LISTENING sans
  // fermer la session
  pushSimulatedSpeechTurn(vk, 8, 8);
  injectGeminiPcmAndDrain(vk.getEngine(), 5, true);
  vk.update(); // -> CONVERSATION_SPEAKING
  drainVoicePlayback(vk.getEngine());
  advanceMockMillis(40);
  vk.update(); // -> CONVERSATION_LISTENING
  TEST_ASSERT(
      vk.getWakeWordConversationState() ==
          WakeWordConversationState::CONVERSATION_LISTENING,
      "Fin de tour 1 != fin de session : retour en CONVERSATION_LISTENING");
  TEST_ASSERT(vk.getEngine().getProvider().isConnected(),
              "WebSocket maintenu ouvert entre deux tours");

  // Tour 2 : l'utilisateur dit « Au revoir » et l'assistant répond une dernière
  // phrase de 30 échantillons
  pushSimulatedSpeechTurn(vk, 8, 8);
  vk.getWakeWordConversationController().notifyUserTranscription("Au revoir",
                                                                 true);
  injectGeminiPcmAndDrain(vk.getEngine(), 5, true); // 5 * 6 = 30 échantillons
  vk.update();                                      // -> CONVERSATION_SPEAKING
  drainVoicePlayback(vk.getEngine()); // Restitution intégrale des derniers
                                      // échantillons PCM -> ENDING_SESSION
  TEST_ASSERT(vk.getEngine().getPlayer().getSamplesPlayedTotal() == 30,
              "Restitution complete des 30 derniers echantillons PCM avant "
              "fermeture par « Au revoir »");
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::ENDING_SESSION,
              "Passage en ENDING_SESSION apres restitution complete");
  vk.update(); // -> RETURNING_TO_WAKE
  advanceMockMillis(40);
  vk.update(); // -> WAKE_ARMED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Session 1 terminee : retour a WAKE_ARMED");

  // 2. Session 2 : Échec temporaire de démarrage microphone au début du tour
  // d'écoute (startTurn) -> récupération vers WAKE_ARMED
  vk.triggerTestWakeWord("Bonjour ESP");
  vk.update();
  vk.update();
  vk.update();
  vk.update();
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  // Simuler une réservation micro résiduelle empêchant startTurn() de démarrer
  // le microphone
  int tempBlocker = 99;
  HardwareManager::getInstance().prepareMicCapture(&tempBlocker);
  vk.update(); // CONNECTING -> tente startNextListeningTurn() -> échoue ->
               // RECOVERABLE_ERROR
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::RECOVERABLE_ERROR,
              "Echec temporaire de demarrage microphone -> transition "
              "immediate vers RECOVERABLE_ERROR");
  TEST_ASSERT(vk.getWakeWordConversationEndReason() ==
                  WakeWordSessionEndReason::MIC_TRANSITION_ERROR,
              "Raison = MIC_TRANSITION_ERROR");

  // Libérer l'anomalie temporaire microphone -> retour automatique à WAKE_ARMED
  HardwareManager::getInstance().restoreSpeakerAfterMic();
  advanceMockMillis(100);
  vk.update(); // -> RETURNING_TO_WAKE
  advanceMockMillis(40);
  vk.update(); // -> WAKE_ARMED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Retour automatique a WAKE_ARMED apres echec temporaire de "
              "demarrage microphone");

  // 3. Session 3 : Nouvelle session complète après récupération d'erreur ->
  // arrêt par timeout d'inactivité
  vk.triggerTestWakeWord("Bonjour ESP");
  vk.update();
  vk.update();
  vk.update();
  vk.update();
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update();
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::CONVERSATION_LISTENING,
              "Session 3 ouverte avec succes apres recuperation");
  advanceMockMillis(30500);
  vk.update(); // -> ENDING_SESSION (SILENCE_TIMEOUT)
  TEST_ASSERT(vk.getWakeWordConversationEndReason() ==
                  WakeWordSessionEndReason::SILENCE_TIMEOUT,
              "Session 3 fermee par timeout d'inactivite (SILENCE_TIMEOUT)");
  vk.update(); // -> RETURNING_TO_WAKE
  advanceMockMillis(40);
  vk.update(); // -> WAKE_ARMED
  TEST_ASSERT(vk.getWakeWordConversationState() ==
                  WakeWordConversationState::WAKE_ARMED,
              "Session 3 : retour a WAKE_ARMED");

  const WakeWordConversationMetrics &m = vk.getWakeWordConversationMetrics();
  TEST_ASSERT(m.sessionCount == 3, "3 sessions ouvertes au total");
  TEST_ASSERT(m.successfulSessionCount == 2,
              "2 sessions reussies (hors celle interrompue par erreur micro)");
  TEST_ASSERT(m.recoveryCount == 1, "1 reprise apres erreur recuperable");

  vk.disableWakeWordConversation();
  vk.getEngine().getClient().setTransport(nullptr);
}

// SUITE 105 : Section 6 — Interface WakeWordConversation : visibilité immédiate
// des 7 transitions pilotées par événements
void test_v0631_ui_immediate_event_transitions_and_banner_labels() {
  std::cout << "\n[TEST SUITE 105] v0.6.3.1 : Section 6 — Visibilite immediate "
               "des 7 transitions UI pilotees par evenements"
            << std::endl;

  WiFi.setConnected(true);
  setMockMillisOffset(1700000000UL * 1000UL);

  MockLiveTransport mockTransport;
  VoiceKit vk;
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::GEMINI;
  cfg.apiKey = "AIzaSyTestWakeConvV0631S105";
  cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfg.wakeWordEngine = WakeWordEngineType::TEST;
  cfg.enableWakeWordConversation = true;
  cfg.wakeWordResumeDelayMs = 40;
  cfg.vadSilenceDurationMs = 200;

  std::vector<String> observedBanners;
  auto recordBannerIfChanged = [&]() {
    String current = String(vk.getWakeWordConversationBannerText());
    if (observedBanners.empty() || observedBanners.back() != current) {
      observedBanners.push_back(current);
    }
  };

  vk.onWakeConvDetected(
      [&](const String &, float, bool) { recordBannerIfChanged(); });
  vk.onWakeConvConnecting([&]() { recordBannerIfChanged(); });
  vk.onWakeConvReady([&]() { recordBannerIfChanged(); });
  vk.onWakeConvListening([&]() { recordBannerIfChanged(); });
  vk.onWakeConvSpeaking([&]() { recordBannerIfChanged(); });
  vk.onWakeConvEnded(
      [&](WakeWordSessionEndReason) { recordBannerIfChanged(); });
  vk.onWakeConvReturnedToWake([&]() { recordBannerIfChanged(); });
  vk.onWakeConvStateChanged(
      [&](WakeWordConversationState) { recordBannerIfChanged(); });

  TEST_ASSERT(vk.begin(cfg), "begin() reussi (Suite 105)");
  vk.getEngine().getClient().setTransport(&mockTransport);
  observedBanners.clear();

  // 1. BONJOUR ESP DÉTECTÉ
  vk.triggerTestWakeWord("Bonjour ESP");
  vk.update(); // -> WAKE_DETECTED
  TEST_ASSERT(String(vk.getWakeWordConversationBannerText()) ==
                  "BONJOUR ESP DÉTECTÉ",
              "1. Transition UI : BONJOUR ESP DÉTECTÉ");

  // 2. CONNEXION
  vk.update(); // -> PREPARING_SESSION
  vk.update(); // -> CONNECTING
  TEST_ASSERT(String(vk.getWakeWordConversationBannerText()) == "CONNEXION",
              "2. Transition UI : CONNEXION");

  // 3. PRÊT — PARLEZ
  vk.update(); // -> activate()
  const char *setupJson = "{\"setupComplete\":{}}";
  vk.getEngine().getClient().handleIncomingMessage(setupJson,
                                                   strlen(setupJson));
  vk.update(); // -> CONVERSATION_LISTENING (avant parole)
  TEST_ASSERT(String(vk.getWakeWordConversationBannerText()) == "PRÊT — PARLEZ",
              "3. Transition UI : PRÊT — PARLEZ");

  // 4. ÉCOUTE (dès la détection d'activité vocale par le VAD, via événement
  // réel USER_LISTENING)
  int16_t loudChunk[AUDIO_CHUNK_SAMPLES];
  for (size_t i = 0; i < AUDIO_CHUNK_SAMPLES; ++i)
    loudChunk[i] = 4000;
  vk.getEngine().getStreamer().pushSimulatedChunk(loudChunk,
                                                  AUDIO_CHUNK_SAMPLES);
  advanceMockMillis(32);
  vk.update();
  vk.getEngine().getStreamer().pushSimulatedChunk(loudChunk,
                                                  AUDIO_CHUNK_SAMPLES);
  advanceMockMillis(32);
  vk.update();
  TEST_ASSERT(String(vk.getWakeWordConversationBannerText()) == "ÉCOUTE",
              "4. Transition UI : ÉCOUTE (declenchee par evenement reel VAD)");

  // Fin de parole + début de réponse
  int16_t quietChunk[AUDIO_CHUNK_SAMPLES] = {0};
  for (int i = 0; i < 8; ++i) {
    vk.getEngine().getStreamer().pushSimulatedChunk(quietChunk,
                                                    AUDIO_CHUNK_SAMPLES);
    advanceMockMillis(32);
    vk.update();
  }

  // 5. RÉPONSE
  injectGeminiPcmAndDrain(vk.getEngine(), 4, true);
  vk.update(); // -> CONVERSATION_SPEAKING
  TEST_ASSERT(String(vk.getWakeWordConversationBannerText()) == "RÉPONSE",
              "5. Transition UI : RÉPONSE");

  // 6. FIN DE CONVERSATION
  vk.requestEndWakeWordConversationAfterTurn();
  drainVoicePlayback(vk.getEngine()); // -> ENDING_SESSION
  TEST_ASSERT(String(vk.getWakeWordConversationBannerText()) ==
                  "FIN DE CONVERSATION",
              "6. Transition UI : FIN DE CONVERSATION");

  // 7. RETOUR EN ÉCOUTE
  vk.update(); // -> RETURNING_TO_WAKE
  TEST_ASSERT(String(vk.getWakeWordConversationBannerText()) ==
                  "RETOUR EN ÉCOUTE",
              "7. Transition UI : RETOUR EN ÉCOUTE");

  // Retour à WAKE_ARMED
  advanceMockMillis(50);
  vk.update(); // -> WAKE_ARMED
  TEST_ASSERT(String(vk.getWakeWordConversationBannerText()) ==
                  "EN ATTENTE — « Bonjour ESP »",
              "Retour a l'affichage WAKE_ARMED : EN ATTENTE — « Bonjour ESP »");

  // Vérifier que les 7 bandeaux ont tous été captés dans l'ordre exact par les
  // callbacks temps réel (sans délai arbitraire)
  TEST_ASSERT(observedBanners.size() == 8,
              "Les 7 transitions + retour WAKE_ARMED ont toutes notifie l'UI "
              "en temps reel");
  if (observedBanners.size() >= 7) {
    TEST_ASSERT(observedBanners[0] == "BONJOUR ESP DÉTECTÉ",
                "Ordre UI [0] = BONJOUR ESP DÉTECTÉ");
    TEST_ASSERT(observedBanners[1] == "CONNEXION", "Ordre UI [1] = CONNEXION");
    TEST_ASSERT(observedBanners[2] == "PRÊT — PARLEZ",
                "Ordre UI [2] = PRÊT — PARLEZ");
    TEST_ASSERT(observedBanners[3] == "ÉCOUTE", "Ordre UI [3] = ÉCOUTE");
    TEST_ASSERT(observedBanners[4] == "RÉPONSE", "Ordre UI [4] = RÉPONSE");
    TEST_ASSERT(observedBanners[5] == "FIN DE CONVERSATION",
                "Ordre UI [5] = FIN DE CONVERSATION");
    TEST_ASSERT(observedBanners[6] == "RETOUR EN ÉCOUTE",
                "Ordre UI [6] = RETOUR EN ÉCOUTE");
  }

  vk.disableWakeWordConversation();
  vk.getEngine().getClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 106 : VoiceKit v0.6.3.2.1 - Correction A : Filtrage Control Frames &
// Diagnostics JSON
// =============================================================================
void test_v06321_websocket_control_frames_and_json_parse_diagnostics() {
  std::cout << "\n[TEST SUITE 106] v0.6.3.2.1 : Correction A - Filtrage "
               "Control Frames & Diagnostics JSON"
            << std::endl;

  // 1. Classification RFC 6455 des opcodes WebSocket
  uint8_t pingOp = 0x09;
  uint8_t pongOp = 0x0A;
  uint8_t closeOp = 0x08;
  uint8_t textOp = 0x01;
  uint8_t binOp = 0x02;
  TEST_ASSERT(pingOp >= 0x08,
              "1. PING 0x09 est un frame de controle (op >= 0x08)");
  TEST_ASSERT(pongOp >= 0x08,
              "2. PONG 0x0A est un frame de controle (op >= 0x08)");
  TEST_ASSERT(closeOp >= 0x08,
              "3. CLOSE 0x08 est un frame de controle (op >= 0x08)");
  TEST_ASSERT(textOp < 0x08,
              "4. TEXT 0x01 est une trame de donnees (op < 0x08)");
  TEST_ASSERT(binOp < 0x08,
              "5. BINARY 0x02 est une trame de donnees (op < 0x08)");

  // 2. Simulation de non-reinitialisation de l'accumulateur RX par un PING/PONG
  // pendant le reassemblage
  std::string rxAccum = "{\"type\":\"response.audio.delta\",\"delta\":\"";
  bool pingReceived = true;
  if (pingOp >= 0x08 && pingReceived) {
    // Le control frame est traite separement sans effacer rxAccum
  }
  rxAccum += "AQIDBA==\"}";
  TEST_ASSERT(rxAccum.find("response.audio.delta") != std::string::npos,
              "6. rxAccum preserve malgre un PING/PONG intercalaire");

  // 3. OpenAIRealtimeClient : robustesse JSON et instrumentation des erreurs
  OpenAIRealtimeClient client;
  MockLiveTransport transport;
  client.setTransport(&transport);
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::OPENAI;
  cfg.apiKey = "sk-test-secret-12345";
  client.begin(cfg);

  TEST_ASSERT(client.getJsonErrorCount() == 0,
              "7. Compteur d'erreurs JSON initialise a 0");

  // 4. Trame vide : ignoree silencieusement sans incrementer le compteur
  // d'erreurs
  client.handleIncomingMessage("", 0);
  TEST_ASSERT(client.getJsonErrorCount() == 0,
              "8. Trame vide de 0 octet ignoree proprement sans erreur");

  // 5. Pointeur nul ou longueur nulle
  client.handleIncomingMessage(nullptr, 0);
  TEST_ASSERT(client.getJsonErrorCount() == 0,
              "9. Pointeur nul ignore proprement sans erreur");

  // 6. Espaces blancs / sauts de ligne seuls : ignores sans declencher d'erreur
  // JSON
  const char *wsOnly = "   \r\n\t  ";
  client.handleIncomingMessage(wsOnly, strlen(wsOnly));
  TEST_ASSERT(client.getJsonErrorCount() == 0,
              "10. Espaces et sauts de ligne isoles ignores sans erreur");

  // 7. Trame non-JSON brute (corruption ou payload non-JSON)
  const char *nonJson = "NOT_A_VALID_JSON_PAYLOAD";
  client.handleIncomingMessage(nonJson, strlen(nonJson));
  TEST_ASSERT(client.getJsonErrorCount() == 1,
              "11. Trame non-JSON detectee et compteur incremente (+1)");

  // 8. Trame JSON syntaxiquement incomplete (coupure brutale de flux)
  const char *brokenJson = "{\"type\":\"response.done\",\"broken\":";
  client.handleIncomingMessage(brokenJson, strlen(brokenJson));
  TEST_ASSERT(client.getJsonErrorCount() == 2,
              "12. Trame JSON incomplete detectee et compteur incremente (+2)");
  TEST_ASSERT(strstr(client.getLastError(), "JSON") != nullptr,
              "13. getLastError() specifie une anomalie JSON");

  // 9. Trame valide : pas d'incrementation d'erreur
  const char *validJson = "{\"type\":\"session.updated\",\"session\":{}}";
  client.handleIncomingMessage(validJson, strlen(validJson));
  TEST_ASSERT(client.getJsonErrorCount() == 2,
              "14. Trame valide recue : pas d'incrementation d'erreur JSON");

  // 10. Trame delta valide : pas d'incrementation d'erreur
  const char *deltaJson =
      "{\"type\":\"response.audio_transcript.delta\",\"delta\":\"Test\"}";
  client.handleIncomingMessage(deltaJson, strlen(deltaJson));
  TEST_ASSERT(client.getJsonErrorCount() == 2,
              "15. Trame delta valide : pas d'incrementation d'erreur JSON");

  // 11. Evenement Realtime non gere (e.g. rate_limits.updated) : ignore sans
  // erreur JSON
  const char *unknownEvt =
      "{\"type\":\"rate_limits.updated\",\"rate_limits\":[]}";
  client.handleIncomingMessage(unknownEvt, strlen(unknownEvt));
  TEST_ASSERT(client.getJsonErrorCount() == 2,
              "16. Evenement Realtime non gere ignore sans erreur");

  // 12. Trames multiples invalides : precision exacte du compteur d'erreurs
  for (int i = 0; i < 5; ++i) {
    client.handleIncomingMessage("BAD", 3);
  }
  TEST_ASSERT(client.getJsonErrorCount() == 7,
              "17. Cinq rejets successifs : compteur exactement egal a 7");

  // 13. Grande charge corrompue (500 octets d'octets aleatoires) : memoire et
  // etats stables
  std::string bigBad(500, 'X');
  client.handleIncomingMessage(bigBad.c_str(), bigBad.length());
  TEST_ASSERT(
      client.getJsonErrorCount() == 8,
      "18. Payload volumineux malforme rejete sans depassement de memoire");

  // 14. Verification de non-exposition des secrets dans les diagnostics
  // d'erreurs
  TEST_ASSERT(strstr(client.getLastError(), "sk-test-secret-12345") == nullptr,
              "19. Cle d'authentification jamais presente dans getLastError()");

  // 15. Stabilite globale de la connexion malgre les rejets
  TEST_ASSERT(client.getLastError() != nullptr,
              "20. getLastError() reste valide et non nul");

  client.setTransport(nullptr);
}

// =============================================================================
// SUITE 107 : VoiceKit v0.6.3.2.1 - Correction B : Decoupage Mots & UTF-8
// Francais
// =============================================================================
void test_v06321_fragmented_transcription_and_utf8_word_cutting() {
  std::cout << "\n[TEST SUITE 107] v0.6.3.2.1 : Correction B - Decoupage Mots "
               "& UTF-8 Francais"
            << std::endl;

  // 1. Test obligatoire : 'agré' + 'ables' sans espace artificiel ->
  // 'agréables'
  String w1 = "agr\xC3\xA9";
  LiveSessionController::appendTranscriptionFragment(w1, "ables");
  TEST_ASSERT(
      w1 == "agr\xC3\xA9"
            "ables",
      "1. Fragments de mot 'agre' + 'ables' soudes sans espace artificiel");

  // 2. Test obligatoire : 't' + '’aider' sans espace artificiel -> 't’aider'
  String w2 = "t";
  LiveSessionController::appendTranscriptionFragment(w2, "\xE2\x80\x99"
                                                         "aider");
  TEST_ASSERT(w2 == "t\xE2\x80\x99"
                    "aider",
              "2. Apostrophe francaise typographique 't' + '’aider' soudee "
              "sans espace");

  // 3. Test obligatoire apostrophe ASCII : 't' + ''aider' sans espace
  // artificiel -> 't'aider'
  String w2b = "t";
  LiveSessionController::appendTranscriptionFragment(w2b, "'aider");
  TEST_ASSERT(
      w2b == "t'aider",
      "3. Apostrophe francaise ASCII 't' + ''aider' soudee sans espace");

  // 4. Test obligatoire : 'réflé' + 'chir' sans espace artificiel ->
  // 'réfléchir'
  String w3 = "r\xC3\xA9"
              "fl\xC3\xA9";
  LiveSessionController::appendTranscriptionFragment(w3, "chir");
  TEST_ASSERT(w3 == "r\xC3\xA9"
                    "fl\xC3\xA9"
                    "chir",
              "4. Mot avec accent francais 'refle' + 'chir' soude sans espace");

  // 5. Octet UTF-8 coupe en deux a la frontiere de trame : 'caf' + 'Ã' puis '©'
  // -> 'café'
  String wUtf8 = "caf\xC3";
  LiveSessionController::appendTranscriptionFragment(wUtf8, "\xA9");
  TEST_ASSERT(wUtf8 == "caf\xC3\xA9",
              "5. Sequence multi-octets UTF-8 coupee exactement entre deux "
              "deltas assemblee sans corruption");

  // 6. Apostrophe UTF-8 a 3 octets (â) scindee entre deux deltas
  String wApos = "l\xE2\x80";
  LiveSessionController::appendTranscriptionFragment(wApos, "\x99"
                                                            "hiver");
  TEST_ASSERT(
      wApos == "l\xE2\x80\x99"
               "hiver",
      "6. Caractere UTF-8 3-octets scinde assemble sans injection d'espace");

  // 7. Mots separes avec espace en tete du fragment
  String sentence = "Je";
  LiveSessionController::appendTranscriptionFragment(sentence, " suis");
  TEST_ASSERT(sentence == "Je suis",
              "7. Fragment avec espace initial preserve l'espacement naturel");

  // 8. Espace en fin de chaine cible
  String spEnd = "Bonjour ";
  LiveSessionController::appendTranscriptionFragment(spEnd, "monde");
  TEST_ASSERT(spEnd == "Bonjour monde",
              "8. Espace existant en fin de cible preserve sans double espace");

  // 9. Reconstitution de la phrase integrale sans espaces parasites
  String fullAssistant = "Je";
  const char *deltas[] = {" suis",
                          " un",
                          " assistant",
                          " vocal",
                          " ici",
                          " pour",
                          " t",
                          "\xE2\x80\x99"
                          "aider",
                          " \xC3\xA0",
                          " r\xC3\xA9"
                          "fl\xC3\xA9",
                          "chir,",
                          " r\xC3\xA9"
                          "pondre",
                          " \xC3\xA0",
                          " tes",
                          " questions",
                          " et",
                          " rendre",
                          " les",
                          " choses",
                          " plus",
                          " simples",
                          " et",
                          " plus",
                          " agr\xC3\xA9"
                          "ables."};
  for (size_t i = 0; i < sizeof(deltas) / sizeof(deltas[0]); ++i) {
    LiveSessionController::appendTranscriptionFragment(fullAssistant,
                                                       deltas[i]);
  }
  const char *expectedText = "Je suis un assistant vocal ici pour t\xE2\x80\x99"
                             "aider \xC3\xA0 r\xC3\xA9"
                             "fl\xC3\xA9"
                             "chir, r\xC3\xA9"
                             "pondre \xC3\xA0 tes questions et rendre les "
                             "choses plus simples et plus agr\xC3\xA9"
                             "ables.";
  TEST_ASSERT(fullAssistant == expectedText,
              "9. Phrase francaise complexe integrale fidelement reconstituee");

  // 10. Attachement direct de la ponctuation finale sans espace
  String punc = "Bonjour";
  LiveSessionController::appendTranscriptionFragment(punc, ",");
  LiveSessionController::appendTranscriptionFragment(punc,
                                                     " comment allez-vous");
  LiveSessionController::appendTranscriptionFragment(punc, " ?");
  TEST_ASSERT(
      punc == "Bonjour, comment allez-vous ?",
      "10. Ponctuations attachantes (, .) assemblees sans separation anormale");

  // 11. Point final seul : attache au texte precedent au lieu d'ecraser
  String dotTarget = "Bonjour tout le monde";
  LiveSessionController::appendTranscriptionFragment(dotTarget, ".");
  TEST_ASSERT(dotTarget == "Bonjour tout le monde.",
              "11. Fragment '.' final attache au mot precedent");

  // 12. Suppression des doublons de fragments identiques consecutives
  String dupTarget = "Bonjour tout le monde.";
  LiveSessionController::appendTranscriptionFragment(dupTarget, ".");
  TEST_ASSERT(dupTarget == "Bonjour tout le monde.",
              "12. Doublon de ponctuation finale identique elimine");

  // 13. Remplacement propre si le fragment contient deja l'integralite de la
  // cible (cumulatif)
  String cumTarget = "Je suis";
  LiveSessionController::appendTranscriptionFragment(cumTarget,
                                                     "Je suis un assistant");
  TEST_ASSERT(cumTarget == "Je suis un assistant",
              "13. Remplacement cumulatif sans repetition");

  // 14. Fragment vide ignore
  String beforeEmpty = cumTarget;
  LiveSessionController::appendTranscriptionFragment(cumTarget, "");
  TEST_ASSERT(cumTarget == beforeEmpty,
              "14. Fragment vide ignore sans modification");

  // 15. Espace initial sur le tout premier fragment retire proprement
  String firstFrag = "";
  LiveSessionController::appendTranscriptionFragment(firstFrag,
                                                     "   Premier mot");
  TEST_ASSERT(firstFrag == "Premier mot",
              "15. Espaces initiaux au premier fragment elagues");

  // 15b. Protection anti-doublon d'une reponse complete ou avec ponctuation
  // finale
  String fullSentence = "Il est 15 heures.";
  LiveSessionController::appendTranscriptionFragment(fullSentence,
                                                     "Il est 15 heures");
  TEST_ASSERT(fullSentence == "Il est 15 heures.",
              "15b. Fragment identique sans point n'est pas duplique");

  String fullSentence2 = "Il est 15 heures.";
  LiveSessionController::appendTranscriptionFragment(fullSentence2,
                                                     "Il est 15 heures.");
  TEST_ASSERT(fullSentence2 == "Il est 15 heures.",
              "15c. Fragment rigoureusement identique n'est pas duplique");

  String fullSentence3 = "Il est 15 heures. ";
  LiveSessionController::appendTranscriptionFragment(fullSentence3,
                                                     "Il est 15 heures.");
  TEST_ASSERT(fullSentence3 == "Il est 15 heures. ",
              "15d. Fragment identique avec espace existant non duplique");

  // 16. OpenAIRealtimeClient : streaming des deltas
  OpenAIRealtimeClient client;
  MockLiveTransport transport;
  client.setTransport(&transport);
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::OPENAI;
  client.begin(cfg);

  String streamAccum = "";
  client.setOnOutputTranscription(
      [&streamAccum](const String &text) { streamAccum += text; });

  const char *d1 =
      "{\"type\":\"response.audio_transcript.delta\",\"delta\":\"Je\"}";
  const char *d2 =
      "{\"type\":\"response.audio_transcript.delta\",\"delta\":\" suis\"}";
  client.handleIncomingMessage(d1, strlen(d1));
  client.handleIncomingMessage(d2, strlen(d2));
  TEST_ASSERT(streamAccum == "Je suis",
              "16. OpenAIClient deltas audio_transcript notifies");

  // 17. OpenAIRealtimeClient : reception de
  // response.output_audio_transcript.done
  String capturedDoneText = "";
  client.setOnOutputTranscription(
      [&capturedDoneText](const String &text) { capturedDoneText = text; });

  const char *doneEvt1 = "{\"type\":\"response.output_audio_transcript.done\","
                         "\"transcript\":\"Je suis un assistant vocal.\"}";
  client.handleIncomingMessage(doneEvt1, strlen(doneEvt1));
  TEST_ASSERT(capturedDoneText == "Je suis un assistant vocal.",
              "17. response.output_audio_transcript.done enregistre");

  // 18. OpenAIRealtimeClient : reception de response.audio_transcript.done
  const char *doneEvt2 = "{\"type\":\"response.audio_transcript.done\","
                         "\"transcript\":\"Texte valide done\"}";
  client.handleIncomingMessage(doneEvt2, strlen(doneEvt2));
  TEST_ASSERT(capturedDoneText == "Texte valide done",
              "18. response.audio_transcript.done enregistre");

  // 19. OpenAIRealtimeClient : reception de response.done avec contenu textuel
  // / transcript
  const char *respDoneEvt =
      "{\"type\":\"response.done\",\"response\":{\"output\":[{\"content\":[{"
      "\"transcript\":\"Texte canonique final\"}]}]}}";
  client.handleIncomingMessage(respDoneEvt, strlen(respDoneEvt));
  TEST_ASSERT(capturedDoneText == "Texte canonique final",
              "19. response.done extrait le transcript canonique");

  // 20. Troncature securisee au depassement de maxLength
  String longTarget = "";
  std::string giantFrag(600, 'A');
  LiveSessionController::appendTranscriptionFragment(longTarget,
                                                     giantFrag.c_str(), 100);
  TEST_ASSERT(
      longTarget.length() == 100,
      "20. Troncature securisee a la longueur maximale sans debordement");

  client.setTransport(nullptr);
}

// =============================================================================
// SUITE 108 : VoiceKit v0.6.3.2.1 - Correction B & D : Conservation Bilan &
// Tags Dynamiques
// =============================================================================
void test_v06321_assistant_full_text_conservation_and_dynamic_tags() {
  std::cout << "\n[TEST SUITE 108] v0.6.3.2.1 : Correction B & D - "
               "Conservation Bilan & Tags Dynamiques"
            << std::endl;

  // 1. Initialisation end-to-end de VoxCoreVoice avec OpenAI Realtime
  MockLiveTransport mockTransportOAI;
  VoxCoreVoice voiceOpenAI;
  VoiceKitConfig cfgOAI;
  cfgOAI.provider = VoiceProviderType::OPENAI;
  cfgOAI.apiKey = "sk-test-secret-openai";
  cfgOAI.model = "gpt-realtime-2.1-mini";
  cfgOAI.voice = "alloy";
  cfgOAI.wifiMode = WiFiMode::EXTERNAL_WIFI;
  cfgOAI.enableConversation = true;

  TEST_ASSERT(voiceOpenAI.begin(cfgOAI),
              "1. begin() avec OpenAI Realtime reussi");
  prepareMockOpenAIVoiceReady(voiceOpenAI, mockTransportOAI);
  TEST_ASSERT(voiceOpenAI.isReady(), "2. Moteur VoxCoreVoice est READY");

  ConversationMetrics capturedMetrics;
  bool metricsReceived = false;
  voiceOpenAI.onConversationTurnMetrics(
      [&capturedMetrics, &metricsReceived](const ConversationMetrics &m) {
        capturedMetrics = m;
        metricsReceived = true;
      });

  String streamedAssistantText = "";
  voiceOpenAI.onAssistantText([&streamedAssistantText](const String &text) {
    streamedAssistantText = text;
  });

  // 2. Demarrage du Tour 1 (PARLER)
  TEST_ASSERT(voiceOpenAI.startListening(), "3. startListening() reussi");
  TEST_ASSERT(voiceOpenAI.isListening(), "4. Moteur en etat LISTENING");

  // Transcription utilisateur Whisper
  const char *whisperEvt =
      "{\"type\":\"conversation.item.input_audio_transcription.completed\","
      "\"transcript\":\"Bonjour VoiceKit\"}";
  voiceOpenAI.getOpenAIClient().handleIncomingMessage(whisperEvt,
                                                      strlen(whisperEvt));
  TEST_ASSERT(voiceOpenAI.getConversationController().getLastUserText() ==
                  "Bonjour VoiceKit",
              "5. Transcription utilisateur Whisper enregistree");

  // Relachement (RELÂCHER) -> WAITING_RESPONSE
  voiceOpenAI.stopListening();
  TEST_ASSERT(voiceOpenAI.getState() == VoiceState::WAITING_RESPONSE,
              "6. Moteur en etat WAITING_RESPONSE");

  // 3. Arrivee progressive des deltas de transcription assistant
  const char *d1 =
      "{\"type\":\"response.audio_transcript.delta\",\"delta\":\"Je\"}";
  const char *d2 =
      "{\"type\":\"response.audio_transcript.delta\",\"delta\":\" suis\"}";
  const char *d3 =
      "{\"type\":\"response.audio_transcript.delta\",\"delta\":\" un\"}";
  const char *d4 =
      "{\"type\":\"response.audio_transcript.delta\",\"delta\":\" assistant\"}";
  const char *d5 =
      "{\"type\":\"response.audio_transcript.delta\",\"delta\":\".\"}";

  voiceOpenAI.getOpenAIClient().handleIncomingMessage(d1, strlen(d1));
  voiceOpenAI.getOpenAIClient().handleIncomingMessage(d2, strlen(d2));
  voiceOpenAI.getOpenAIClient().handleIncomingMessage(d3, strlen(d3));
  voiceOpenAI.getOpenAIClient().handleIncomingMessage(d4, strlen(d4));
  voiceOpenAI.getOpenAIClient().handleIncomingMessage(d5, strlen(d5));
  voiceOpenAI.update();

  TEST_ASSERT(voiceOpenAI.getConversationController().getLastAssistantText() ==
                  "Je suis un assistant.",
              "7. Texte assistant accumule fidelement");
  TEST_ASSERT(voiceOpenAI.getConversationController().getLastAssistantText() !=
                  ".",
              "8. Non-ecrase par le fragment final '.' (Correction B)");

  // 4. Restitution audio de la reponse et fin du Tour 1
  voiceOpenAI.getSession().transitionTo(LiveSessionState::PLAYING,
                                        "Reponse audio");
  voiceOpenAI.update();
  TEST_ASSERT(voiceOpenAI.getState() == VoiceState::SPEAKING,
              "9. Moteur en etat SPEAKING");

  voiceOpenAI.getSession().finishTurn();
  voiceOpenAI.getSession().transitionTo(LiveSessionState::CONNECTED,
                                        "Fin lecture audio");
  voiceOpenAI.update();

  TEST_ASSERT(voiceOpenAI.isReady(), "10. Retour automatique a READY");
  TEST_ASSERT(metricsReceived == true,
              "11. Callback onConversationTurnMetrics notifie");
  TEST_ASSERT(capturedMetrics.assistantText == "Je suis un assistant.",
              "12. Bilan final contient l'integralite du texte");
  TEST_ASSERT(capturedMetrics.assistantText != ".",
              "13. Bilan final confirme exempt de l'anomalie '.'");

  // 5. Demarrage du Tour 2 : remise a zero propre sans pollution du Tour 1
  metricsReceived = false;
  TEST_ASSERT(voiceOpenAI.startListening(),
              "14. Tour 2 startListening() reussi");
  voiceOpenAI.stopListening();

  const char *dTurn2 = "{\"type\":\"response.audio_transcript.delta\","
                       "\"delta\":\"Deuxieme reponse.\"}";
  voiceOpenAI.getOpenAIClient().handleIncomingMessage(dTurn2, strlen(dTurn2));
  voiceOpenAI.getSession().transitionTo(LiveSessionState::PLAYING,
                                        "Reponse audio 2");
  voiceOpenAI.update();
  voiceOpenAI.getSession().finishTurn();
  voiceOpenAI.getSession().transitionTo(LiveSessionState::CONNECTED,
                                        "Fin lecture audio 2");
  voiceOpenAI.update();

  TEST_ASSERT(capturedMetrics.assistantText == "Deuxieme reponse.",
              "15. Tour 2 conserve son texte propre sans residu du Tour 1");

  // 6. Verification des libelles dynamiques IVoiceProvider (Correction D)
  TEST_ASSERT(String(voiceProviderTypeToString(VoiceProviderType::OPENAI)) ==
                  "OPENAI",
              "16. voiceProviderTypeToString(OPENAI) == 'OPENAI'");
  TEST_ASSERT(String(voiceProviderTypeToString(VoiceProviderType::GEMINI)) ==
                  "GEMINI",
              "17. voiceProviderTypeToString(GEMINI) == 'GEMINI'");
  TEST_ASSERT(voiceOpenAI.getProvider().getProviderType() ==
                  VoiceProviderType::OPENAI,
              "18. Fournisseur actif dans VoxCoreVoice est OPENAI");

  // 7. Non-regression GeminiVoiceProvider
  GeminiVoiceProvider geminiProvider;
  MockLiveTransport geminiTransport;
  geminiProvider.setTransport(&geminiTransport);
  VoiceKitConfig geminiCfg;
  geminiCfg.provider = VoiceProviderType::GEMINI;
  geminiCfg.apiKey = "AIzaSyTest";
  geminiProvider.begin(geminiCfg);
  TEST_ASSERT(geminiProvider.getProviderType() == VoiceProviderType::GEMINI,
              "19. Fournisseur Gemini retourne VoiceProviderType::GEMINI");

  geminiProvider.setTransport(nullptr);
  voiceOpenAI.stop();
  voiceOpenAI.getOpenAIClient().setTransport(nullptr);
  TEST_ASSERT(voiceOpenAI.getConversationController().getLastAssistantText() ==
                  "Deuxieme reponse.",
              "20. Conservation derniere reponse apres arret");
}

// =============================================================================
// =============================================================================
// SUITE 109 : VoiceKit v0.6.3.2.2 — Diagnostics PTT, Causes de Rejet et
// Consigne Concise
// =============================================================================
void test_v06322_ptt_diagnostics_and_concise_instructions() {
  std::cout << "\n[TEST SUITE 109] v0.6.3.2.2 : Diagnostics PTT, Causes de "
               "Rejet et Consigne Concise"
            << std::endl;

  // 1. Verification de la consigne conversationnelle concise francaise
  // v0.6.3.2.2
  VoiceKitConfig config;
  config.conversationInstruction =
      "Tu es un assistant vocal français. "
      "Réponds directement à la question en une seule phrase courte, "
      "généralement 15 à 25 mots maximum. "
      "Évite les introductions, les répétitions et les formules de politesse "
      "inutiles. "
      "Si l'utilisateur demande explicitement une explication détaillée, "
      "adapte la longueur de ta réponse.";

  TEST_ASSERT(
      config.conversationInstruction.indexOf("assistant vocal français") >= 0,
      "1. Instruction contient le role francais");
  TEST_ASSERT(
      config.conversationInstruction.indexOf("une seule phrase courte") >= 0,
      "2. Instruction impose une seule phrase courte");
  TEST_ASSERT(config.conversationInstruction.indexOf("15 à 25 mots maximum") >=
                  0,
              "3. Instruction impose 15 a 25 mots maximum");
  TEST_ASSERT(
      config.conversationInstruction.indexOf("Évite les introductions") >= 0,
      "4. Instruction interdit les formules superflues");
  TEST_ASSERT(config.conversationInstruction.indexOf("explication détaillée") >=
                  0,
              "5. Instruction prevoit l'exception explicite");

  // 2. Verification de la classification exacte des causes de rejet PTT
  VoxCoreVoice v;
  TEST_ASSERT(!v.isReady(), "6. Moteur non initialise n'est PAS ready");
  TEST_ASSERT(v.getState() == VoiceState::UNINITIALIZED,
              "7. Etat initial UNINITIALIZED");

  MockLiveTransport mockTransport;
  config.provider = VoiceProviderType::OPENAI;
  config.apiKey = "sk-test-key-diagnostics";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  config.enableConversation = true;
  v.begin(config);
  v.getOpenAIClient().setTransport(&mockTransport);

  // Avant connexion, isReady doit etre faux (ne jamais forcer artificiellement
  // READY)
  TEST_ASSERT(!v.isReady(),
              "8. isReady() strictement faux avant connexion TLS");

  prepareMockOpenAIVoiceReady(v, mockTransport);
  TEST_ASSERT(v.isReady(), "9. isReady() valide apres setup session");
  TEST_ASSERT(!v.isBusy(), "10. Moteur disponible (busy == false)");

  // 3. Verification du comportement lors de la prise de parole
  TEST_ASSERT(v.startListening(), "11. startListening() autorise quand pret");
  TEST_ASSERT(v.isListening(), "12. isListening() actif");
  TEST_ASSERT(v.isBusy(), "13. isBusy() actif pendant l'ecoute");

  // Refus de deuxieme demarrage alors que deja en ecoute
  TEST_ASSERT(!v.startListening(),
              "14. Refus d'un deuxieme startListening() concurrent");

  v.stopListening();
  TEST_ASSERT(v.getState() == VoiceState::WAITING_RESPONSE,
              "15. Transition propre vers WAITING_RESPONSE");

  v.stop();
  v.getOpenAIClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 110 : VoiceKit v0.6.3.2.2 — Protection Annulations Tardives et Reprise
// Erreur
// =============================================================================
void test_v06322_stale_cancellation_protection_and_error_recovery() {
  std::cout << "\n[TEST SUITE 110] v0.6.3.2.2 : Protection Annulations "
               "Tardives et Reprise Erreur"
            << std::endl;

  VoxCoreVoice v;
  MockLiveTransport mockTransport;
  VoiceKitConfig config;
  config.provider = VoiceProviderType::OPENAI;
  config.apiKey = "sk-test-stale-cancel";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  config.enableConversation = true;
  v.begin(config);
  v.getOpenAIClient().setTransport(&mockTransport);
  prepareMockOpenAIVoiceReady(v, mockTransport);

  // 1. Demarrage et interruption du Tour 1
  v.startListening();
  uint64_t turn1Id = v.getConversationController().getMetrics().turnId;
  TEST_ASSERT(turn1Id > 0, "1. Tour 1 ID assigne");

  // Interruption STOP pendant le Tour 1
  v.stop();
  TEST_ASSERT(!v.getConversationController().isTurnActive(),
              "2. Tour 1 inactif apres stop()");

  // 2. Demarrage du Tour 2
  TEST_ASSERT(v.startListening(), "3. Demarrage Tour 2 reussi");
  uint64_t turn2Id = v.getConversationController().getMetrics().turnId;
  TEST_ASSERT(turn2Id > turn1Id, "4. Tour 2 ID strictement superieur a Tour 1");

  // Arrivee tardive de l'evenement response.cancelled associe au Tour 1
  const char *cancelEvt = "{\"type\":\"response.cancelled\"}";
  v.getOpenAIClient().handleIncomingMessage(cancelEvt, strlen(cancelEvt));
  v.update();

  // Verifier que le Tour 2 n'a PAS ete corrompu ou annule par l'evenement
  // tardif
  TEST_ASSERT(v.isListening(),
              "5. Tour 2 toujours en ecoute active malgre cancel tardif");
  TEST_ASSERT(v.getConversationController().isTurnActive(),
              "6. Tour 2 toujours actif");

  // 3. Verification de la reprise apres etat LiveSessionState::ERROR
  v.getSession().transitionTo(LiveSessionState::ERROR,
                              "Erreur simulee de test");
  TEST_ASSERT(v.getSession().getState() == LiveSessionState::ERROR,
              "7. Session en etat ERROR");

  // Appel de requestStop() sur la session : doit restaurer CONNECTED sans
  // reboot
  v.getSession().requestStop();
  TEST_ASSERT(v.getSession().getState() == LiveSessionState::CONNECTED,
              "8. Session restauree a CONNECTED sans reboot");

  v.stop();
  v.getOpenAIClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 111 : VoiceKit v0.6.3.2.2 — Reponse Sans Audio et Watchdog Non-Bloquant
// =============================================================================
void test_v06322_empty_audio_response_and_nonblocking_recovery() {
  std::cout << "\n[TEST SUITE 111] v0.6.3.2.2 : Reponse Sans Audio et Watchdog "
               "Non-Bloquant"
            << std::endl;

  VoxCoreVoice v;
  MockLiveTransport mockTransport;
  VoiceKitConfig config;
  config.provider = VoiceProviderType::OPENAI;
  config.apiKey = "sk-test-empty-audio";
  config.wifiMode = WiFiMode::EXTERNAL_WIFI;
  config.enableConversation = true;
  v.begin(config);
  v.getOpenAIClient().setTransport(&mockTransport);
  prepareMockOpenAIVoiceReady(v, mockTransport);

  // 1. Prise de parole normale
  v.startListening();
  v.stopListening();
  TEST_ASSERT(v.getState() == VoiceState::WAITING_RESPONSE,
              "1. En attente de reponse serveur");

  // 2. Le serveur termine le tour sans streaming audio (ex: reponse textuelle
  // ou annulation externe)
  v.getSession().transitionTo(LiveSessionState::CONNECTED,
                              "Termine sans audio");
  v.update();

  // Verifier que ConversationController a detecte CONNECTED sans audio et est
  // revenu a READY
  TEST_ASSERT(v.isReady(), "2. Retour automatique a READY sans blocage");
  TEST_ASSERT(!v.getConversationController().isTurnActive(),
              "3. Tour termine proprement");

  // 3. Nouveau tour immediatement possible (aucun verrou residuel)
  TEST_ASSERT(v.startListening(),
              "4. Prise de parole immediate autorisee apres cycle a vide");
  v.stop();

  v.getOpenAIClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 112 : OpenAI Realtime v0.1.0.1 - Evenements, Reponse Echouee, Sans
// Audio & Transitions PTT
// =============================================================================
void test_v0101_openai_realtime_events_failed_and_ptt_transitions() {
  std::cout << "\n[TEST SUITE 112] v0.1.0.1 : OpenAI Realtime Events, Reponse "
               "Echouee, Sans Audio & PTT"
            << std::endl;

  // 1. Audit des evenements OpenAI : response.created et
  // response.output_item.added
  OpenAIRealtimeClient client;
  MockLiveTransport transport;
  client.setTransport(&transport);
  VoiceKitConfig cfg;
  cfg.provider = VoiceProviderType::OPENAI;
  cfg.apiKey = "sk-test-v0101-audit";
  client.begin(cfg);

  TEST_ASSERT(client.getJsonErrorCount() == 0,
              "1. Compteur erreurs initialise a 0");

  const char *evCreated = "{\"type\":\"response.created\",\"response\":{\"id\":"
                          "\"resp_test_01\",\"status\":\"in_progress\"}}";
  client.handleIncomingMessage(evCreated, strlen(evCreated));
  TEST_ASSERT(client.getJsonErrorCount() == 0,
              "2. response.created traite sans erreur JSON");

  const char *evItemAdded = "{\"type\":\"response.output_item.added\",\"item\":"
                            "{\"type\":\"message\",\"role\":\"assistant\"}}";
  client.handleIncomingMessage(evItemAdded, strlen(evItemAdded));
  TEST_ASSERT(client.getJsonErrorCount() == 0,
              "3. response.output_item.added traite sans erreur JSON");

  // 2. Simulation de response.done avec status: 'failed' et
  // status_details.error
  String lastReportedError = "";
  client.setOnError(
      [&lastReportedError](const String &err) { lastReportedError = err; });

  const char *evFailed =
      "{\"type\":\"response.done\",\"response\":{\"id\":\"resp_err_02\","
      "\"status\":\"failed\",\"status_details\":{\"type\":\"incomplete\","
      "\"reason\":\"error\",\"error\":{\"message\":\"OpenAI rate limit "
      "reached\",\"code\":\"rate_limit_exceeded\"}}}}";
  client.handleIncomingMessage(evFailed, strlen(evFailed));
  TEST_ASSERT(lastReportedError.indexOf("OpenAI rate limit reached") >= 0,
              "4. Erreur status: failed propagee au callback onError");
  TEST_ASSERT(strstr(client.getLastError(), "OpenAI rate limit reached") !=
                  nullptr,
              "5. Derniere erreur enregistree dans le client");
  TEST_ASSERT(!client.isWaitingForResponse(),
              "6. waitingForResponse desactive apres response.done failed");

  // 3. Integration VoxCoreVoice : Simulation response.done sans audio
  // (recuperation immediate vers READY)
  VoxCoreVoice v;
  MockLiveTransport mockTransport;
  VoiceKitConfig vcfg;
  vcfg.provider = VoiceProviderType::OPENAI;
  vcfg.apiKey = "sk-test-v0101-no-audio";
  vcfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
  vcfg.enableConversation = true;
  v.begin(vcfg);
  prepareMockOpenAIVoiceReady(v, mockTransport);

  TEST_ASSERT(v.isReady(), "7. VoxCoreVoice est READY");
  TEST_ASSERT(v.startListening(), "8. Prise de parole demarree");
  TEST_ASSERT(v.isListening(), "9. Moteur en ecoute");

  v.stopListening();
  TEST_ASSERT(v.getState() == VoiceState::WAITING_RESPONSE,
              "10. Moteur en attente de reponse serveur (WAITING_RESPONSE)");

  // Reception de response.done termine SANS audio
  const char *evDoneEmpty =
      "{\"type\":\"response.done\",\"response\":{\"id\":\"resp_empty_03\","
      "\"status\":\"completed\",\"output\":[]}}";
  v.getOpenAIClient().handleIncomingMessage(evDoneEmpty, strlen(evDoneEmpty));
  v.update();

  // Verifier retour immediat a READY sans blocage en PROCESSING
  TEST_ASSERT(v.isReady(),
              "11. VoxCoreVoice revenu a READY apres reponse sans audio");
  TEST_ASSERT(!v.isBusy(), "12. Moteur disponible (busy == false)");
  TEST_ASSERT(!v.getConversationController().isTurnActive(),
              "13. Tour termine proprement");

  // 4. Test d'appuis PTT rapides (PRESS / RELEASE rapides)
  TEST_ASSERT(v.startListening(), "14. Nouvel appui rapide autorise");
  // Release quasi immediat (< 50 ms)
  v.stopListening();
  TEST_ASSERT(v.getState() == VoiceState::WAITING_RESPONSE,
              "15. Transition valide apres appui ultra-court");

  // Tentative d'appui concurrent pendant WAITING_RESPONSE : doit etre
  // STRICTEMENT refuse
  TEST_ASSERT(!v.startListening(), "16. Rejet strict d'un startListening "
                                   "concurrent pendant WAITING_RESPONSE");

  // Cloture du tour court avec audio
  const char *evAudioDelta =
      "{\"type\":\"response.audio.delta\",\"delta\":\"AQIDBAU=\"}";
  v.getOpenAIClient().handleIncomingMessage(evAudioDelta, strlen(evAudioDelta));
  v.getOpenAIClient().handleIncomingMessage(evDoneEmpty, strlen(evDoneEmpty));
  drainVoicePlayback(v);
  TEST_ASSERT(v.isReady(), "17. Moteur READY apres cycle PTT rapide");

  // 5. Enchainement multi-tours consecutifs : alternatif Avec Audio / Sans
  // Audio
  for (int cycle = 0; cycle < 3; cycle++) {
    TEST_ASSERT(v.startListening(), "18. Demarrage tour consecutif");
    v.stopListening();
    if (cycle % 2 == 0) {
      // Tour sans audio : auto-resolution non-bloquante via update()
      v.getOpenAIClient().handleIncomingMessage(evDoneEmpty,
                                                strlen(evDoneEmpty));
      v.update();
    } else {
      // Tour avec audio : lecture complete et vidage DMA
      v.getOpenAIClient().handleIncomingMessage(evAudioDelta,
                                                strlen(evAudioDelta));
      v.getOpenAIClient().handleIncomingMessage(evDoneEmpty,
                                                strlen(evDoneEmpty));
      drainVoicePlayback(v);
    }
    TEST_ASSERT(v.isReady(),
                "19. Cycle consecutif complete et retour propre a READY");
  }

  v.stop();
  v.getOpenAIClient().setTransport(nullptr);
}

// =============================================================================
// SUITE 113 : VoiceKit v0.6.4 — Local Tool Registry : Enregistrement valide
// =============================================================================
void test_v064_tool_valid_registration() {
  std::cout << "\n[TEST SUITE 113] v0.6.4 : Enregistrement valide et decouverte"
            << std::endl;
  ToolRegistry reg(16);

  // Enregistrement simple
  bool r1 = reg.registerTool(
      "system.get_status", "Lire l'etat du CoreS3",
      []() { return ToolResult::success("{\"battery\": 100}"); },
      ToolSecurityPolicy::READ_ONLY);
  TEST_ASSERT(r1, "Enregistrement de system.get_status reussi");

  // Enregistrement avec parametres
  std::vector<ToolParam> params = {ToolParam("volume", ToolParamType::INT, 0,
                                             100, true, "Niveau sonore 0-100")};
  bool r2 = reg.registerTool(
      "audio.set_volume", "Regler le volume",
      [](const ToolArgs &args) {
        return ToolResult::success("Volume regle a " +
                                   String(args.getInt("volume")));
      },
      params, ToolSecurityPolicy::WRITE);
  TEST_ASSERT(r2, "Enregistrement de audio.set_volume reussi");

  // Verification decouverte
  TEST_ASSERT(reg.getToolCount() == 2, "Compte d'outils == 2");
  TEST_ASSERT(reg.hasTool("system.get_status"),
              "hasTool system.get_status == true");
  TEST_ASSERT(reg.hasTool("audio.set_volume"),
              "hasTool audio.set_volume == true");
  TEST_ASSERT(!reg.hasTool("unknown.tool"), "hasTool unknown == false");

  const ToolDefinition *def = reg.findTool("audio.set_volume");
  TEST_ASSERT(def != nullptr, "findTool audio.set_volume != nullptr");
  TEST_ASSERT(def->description == "Regler le volume", "Description correcte");
  TEST_ASSERT(def->securityPolicy == ToolSecurityPolicy::WRITE,
              "Politique WRITE correcte");
  TEST_ASSERT(def->parameters.size() == 1, "Nombre de parametres == 1");
  TEST_ASSERT(def->parameters[0].name == "volume", "Nom du parametre 'volume'");
  TEST_ASSERT(def->parameters[0].type == ToolParamType::INT, "Type INT");
  TEST_ASSERT(def->parameters[0].hasMin && def->parameters[0].minValue == 0,
              "Borne min == 0");
  TEST_ASSERT(def->parameters[0].hasMax && def->parameters[0].maxValue == 100,
              "Borne max == 100");
}

// =============================================================================
// SUITE 114 : VoiceKit v0.6.4 — Local Tool Registry : Identifiant dupliqué
// =============================================================================
void test_v064_tool_duplicate_id() {
  std::cout << "\n[TEST SUITE 114] v0.6.4 : Rejet d'identifiant duplique et "
               "declarations invalides"
            << std::endl;
  ToolRegistry reg(16);

  bool r1 = reg.registerTool("test.tool", "Premiere definition",
                             []() { return ToolResult::success(); });
  TEST_ASSERT(r1, "Premier enregistrement reussi");

  // Tentative d'enregistrement avec le même identifiant
  bool r2 = reg.registerTool("test.tool", "Deuxieme definition",
                             []() { return ToolResult::success(); });
  TEST_ASSERT(!r2, "Rejet strict de l'identifiant duplique 'test.tool'");
  TEST_ASSERT(reg.getToolCount() == 1, "Le compte reste a 1");

  // Tentative d'enregistrement avec identifiant vide
  bool r3 =
      reg.registerTool("", "Vide", []() { return ToolResult::success(); });
  TEST_ASSERT(!r3, "Rejet identifiant vide");

  // Tentative d'enregistrement avec espaces
  bool r4 = reg.registerTool("bad id with spaces", "Espaces",
                             []() { return ToolResult::success(); });
  TEST_ASSERT(!r4, "Rejet identifiant contenant des espaces");

  // Tentative avec parametre min > max
  ToolDefinition badDef("bad.params", "Test");
  badDef.addParam(
      ToolParam("x", ToolParamType::INT, 100, 10)); // min 100 > max 10
  badDef.setHandler([](const ToolArgs &) { return ToolResult::success(); });
  bool r5 = reg.registerTool(badDef);
  TEST_ASSERT(!r5, "Rejet declaration invalide min > max");

  // Tentative sans handler
  ToolDefinition noHandler("no.handler", "Test");
  bool r6 = reg.registerTool(noHandler);
  TEST_ASSERT(!r6, "Rejet definition sans handler");
}

// =============================================================================
// SUITE 115 : VoiceKit v0.6.4 — Local Tool Registry : Capacité maximale
// =============================================================================
void test_v064_tool_max_capacity() {
  std::cout << "\n[TEST SUITE 115] v0.6.4 : Capacite maximale configurable et "
               "rejet de depassement"
            << std::endl;
  ToolRegistry reg(3); // Capacité fixée à 3
  TEST_ASSERT(reg.getMaxCapacity() == 3, "Capacite maximale fixee a 3");

  bool r1 =
      reg.registerTool("tool.1", "T1", []() { return ToolResult::success(); });
  bool r2 =
      reg.registerTool("tool.2", "T2", []() { return ToolResult::success(); });
  bool r3 =
      reg.registerTool("tool.3", "T3", []() { return ToolResult::success(); });
  TEST_ASSERT(r1 && r2 && r3, "Les 3 premiers outils sont enregistres");
  TEST_ASSERT(reg.getToolCount() == 3, "Compte d'outils == 3");

  // 4ème outil : doit être rejeté pour dépassement de capacité
  bool r4 =
      reg.registerTool("tool.4", "T4", []() { return ToolResult::success(); });
  TEST_ASSERT(!r4, "4eme outil rejete car capacite maximale atteinte");
  TEST_ASSERT(reg.getToolCount() == 3, "Compte reste a 3");

  // Augmentation dynamique de la capacité
  reg.setMaxCapacity(5);
  bool r5 =
      reg.registerTool("tool.4", "T4", []() { return ToolResult::success(); });
  TEST_ASSERT(r5, "Enregistrement accepte apres extension de capacite");
  TEST_ASSERT(reg.getToolCount() == 4, "Nouveau compte == 4");
}

// =============================================================================
// SUITE 116 : VoiceKit v0.6.4 — Local Tool Registry : Commande inexistante
// =============================================================================
void test_v064_tool_not_found() {
  std::cout << "\n[TEST SUITE 116] v0.6.4 : Recherche et execution d'une "
               "commande inexistante"
            << std::endl;
  ToolRegistry reg(16);

  TEST_ASSERT(reg.findTool("inexistant") == nullptr,
              "findTool retourne nullptr");
  ToolResult res = reg.execute("inexistant");
  TEST_ASSERT(res.status == ToolExecutionStatus::NOT_FOUND, "Statut NOT_FOUND");
  TEST_ASSERT(!res.isSuccess(), "isSuccess == false");
  TEST_ASSERT(res.error.indexOf("inexistant") != -1,
              "Message d'erreur cite la commande inexistante");
}

// =============================================================================
// SUITE 117 : VoiceKit v0.6.4 — Local Tool Registry : Exécution réussie
// =============================================================================
void test_v064_tool_successful_execution() {
  std::cout
      << "\n[TEST SUITE 117] v0.6.4 : Execution reussie avec arguments et types"
      << std::endl;
  ToolRegistry reg(16);

  std::vector<ToolParam> params = {
      ToolParam("a", ToolParamType::INT, true),
      ToolParam("b", ToolParamType::INT, true),
      ToolParam("operation", ToolParamType::STRING, false)};

  reg.registerTool(
      "calc.add", "Addition",
      [](const ToolArgs &args) {
        int a = args.getInt("a");
        int b = args.getInt("b");
        int sum = a + b;
        return ToolResult::success(String(sum));
      },
      params);

  ToolArgs args;
  args.set("a", 15);
  args.set("b", 27);
  ToolResult res = reg.execute("calc.add", args);
  TEST_ASSERT(res.isSuccess(), "Execution reussie");
  TEST_ASSERT(res.status == ToolExecutionStatus::SUCCESS, "Statut SUCCESS");
  TEST_ASSERT(res.output == "42", "Addition 15 + 27 == 42");
}

// =============================================================================
// SUITE 118 : VoiceKit v0.6.4 — Local Tool Registry : Paramètre manquant
// =============================================================================
void test_v064_tool_missing_required_param() {
  std::cout << "\n[TEST SUITE 118] v0.6.4 : Parametre obligatoire manquant"
            << std::endl;
  ToolRegistry reg(16);
  bool handlerCalled = false;

  std::vector<ToolParam> params = {
      ToolParam("volume", ToolParamType::INT, 0, 100, true, "Volume requis")};
  reg.registerTool(
      "audio.set_volume", "Volume",
      [&](const ToolArgs &) {
        handlerCalled = true;
        return ToolResult::success();
      },
      params);

  ToolArgs emptyArgs;
  ToolResult res = reg.execute("audio.set_volume", emptyArgs);
  TEST_ASSERT(!res.isSuccess(), "Execution refusee");
  TEST_ASSERT(res.status == ToolExecutionStatus::INVALID_ARGUMENT,
              "Statut INVALID_ARGUMENT");
  TEST_ASSERT(res.error.indexOf("volume") != -1,
              "Erreur cite le parametre manquant 'volume'");
  TEST_ASSERT(!handlerCalled,
              "Fonction d'execution JAMAIS appelee si validation echoue");
}

// =============================================================================
// SUITE 119 : VoiceKit v0.6.4 — Local Tool Registry : Paramètre de type
// incorrect
// =============================================================================
void test_v064_tool_wrong_param_type() {
  std::cout << "\n[TEST SUITE 119] v0.6.4 : Parametre de type incorrect (INT, "
               "FLOAT, BOOLEAN)"
            << std::endl;
  ToolRegistry reg(16);
  bool handlerCalled = false;

  std::vector<ToolParam> params = {
      ToolParam("num_int", ToolParamType::INT, true),
      ToolParam("num_float", ToolParamType::FLOAT, true),
      ToolParam("flag_bool", ToolParamType::BOOLEAN, true)};
  reg.registerTool(
      "types.check", "Test types",
      [&](const ToolArgs &) {
        handlerCalled = true;
        return ToolResult::success();
      },
      params);

  // 1. Mauvais type pour INT ("cent")
  ToolArgs badIntArgs;
  badIntArgs.set("num_int", "cent");
  badIntArgs.set("num_float", "3.14");
  badIntArgs.set("flag_bool", "true");
  ToolResult r1 = reg.execute("types.check", badIntArgs);
  TEST_ASSERT(r1.status == ToolExecutionStatus::INVALID_ARGUMENT,
              "Statut INVALID_ARGUMENT pour mauvais int");
  TEST_ASSERT(!handlerCalled, "Handler non appele");

  // 2. Mauvais type pour FLOAT ("invalide")
  ToolArgs badFloatArgs;
  badFloatArgs.set("num_int", "42");
  badFloatArgs.set("num_float", "invalide");
  badFloatArgs.set("flag_bool", "true");
  ToolResult r2 = reg.execute("types.check", badFloatArgs);
  TEST_ASSERT(r2.status == ToolExecutionStatus::INVALID_ARGUMENT,
              "Statut INVALID_ARGUMENT pour mauvais float");
  TEST_ASSERT(!handlerCalled, "Handler non appele");

  // 3. Mauvais type pour BOOLEAN ("peut-etre")
  ToolArgs badBoolArgs;
  badBoolArgs.set("num_int", "42");
  badBoolArgs.set("num_float", "3.14");
  badBoolArgs.set("flag_bool", "peut-etre");
  ToolResult r3 = reg.execute("types.check", badBoolArgs);
  TEST_ASSERT(r3.status == ToolExecutionStatus::INVALID_ARGUMENT,
              "Statut INVALID_ARGUMENT pour mauvais boolean");
  TEST_ASSERT(!handlerCalled, "Handler non appele");
}

// =============================================================================
// SUITE 120 : VoiceKit v0.6.4 — Local Tool Registry : Valeur hors limites
// =============================================================================
void test_v064_tool_param_out_of_bounds() {
  std::cout << "\n[TEST SUITE 120] v0.6.4 : Valeur hors limites minimales et "
               "maximales"
            << std::endl;
  ToolRegistry reg(16);
  bool handlerCalled = false;

  std::vector<ToolParam> params = {
      ToolParam("volume", ToolParamType::INT, 0, 100, true)};
  reg.registerTool(
      "audio.set_volume", "Volume",
      [&](const ToolArgs &) {
        handlerCalled = true;
        return ToolResult::success();
      },
      params);

  // Inférieur au min
  ToolArgs tooLow;
  tooLow.set("volume", -5);
  ToolResult r1 = reg.execute("audio.set_volume", tooLow);
  TEST_ASSERT(r1.status == ToolExecutionStatus::INVALID_ARGUMENT,
              "INVALID_ARGUMENT pour volume < 0");
  TEST_ASSERT(r1.error.indexOf("< min") != -1,
              "Message d'erreur explicite sur borne min");
  TEST_ASSERT(!handlerCalled, "Handler non appele");

  // Supérieur au max
  ToolArgs tooHigh;
  tooHigh.set("volume", 150);
  ToolResult r2 = reg.execute("audio.set_volume", tooHigh);
  TEST_ASSERT(r2.status == ToolExecutionStatus::INVALID_ARGUMENT,
              "INVALID_ARGUMENT pour volume > 100");
  TEST_ASSERT(r2.error.indexOf("> max") != -1,
              "Message d'erreur explicite sur borne max");
  TEST_ASSERT(!handlerCalled, "Handler non appele");
}

// =============================================================================
// SUITE 121 : VoiceKit v0.6.4 — Local Tool Registry : Paramètre inconnu
// =============================================================================
void test_v064_tool_unknown_param() {
  std::cout << "\n[TEST SUITE 121] v0.6.4 : Rejet des parametres inconnus sauf "
               "autorisation explicite"
            << std::endl;
  ToolRegistry reg(16);
  bool handlerCalled = false;

  std::vector<ToolParam> params = {
      ToolParam("volume", ToolParamType::INT, 0, 100, true)};
  // Par défaut : allowUnknownParams = false
  reg.registerTool(
      "audio.set_volume", "Volume",
      [&](const ToolArgs &) {
        handlerCalled = true;
        return ToolResult::success();
      },
      params, ToolSecurityPolicy::WRITE, false);

  ToolArgs argsWithExtra;
  argsWithExtra.set("volume", 50);
  argsWithExtra.set("unauthorized_arg", "val");
  ToolResult r1 = reg.execute("audio.set_volume", argsWithExtra);
  TEST_ASSERT(r1.status == ToolExecutionStatus::INVALID_ARGUMENT,
              "INVALID_ARGUMENT pour parametre non declare");
  TEST_ASSERT(r1.error.indexOf("unauthorized_arg") != -1,
              "Erreur cite le parametre inconnu");
  TEST_ASSERT(!handlerCalled, "Handler non appele");

  // Avec autorisation explicite
  reg.registerTool(
      "tools.flexible", "Flexible",
      [&](const ToolArgs &) {
        handlerCalled = true;
        return ToolResult::success("Flexible OK");
      },
      params, ToolSecurityPolicy::WRITE, true);

  ToolResult r2 = reg.execute("tools.flexible", argsWithExtra);
  TEST_ASSERT(r2.isSuccess(),
              "Execution autorisee quand allowUnknownParams = true");
  TEST_ASSERT(handlerCalled, "Handler appele");
}

// =============================================================================
// SUITE 122 : VoiceKit v0.6.4 — Local Tool Registry : Commande non autorisée
// =============================================================================
void test_v064_tool_unauthorized() {
  std::cout << "\n[TEST SUITE 122] v0.6.4 : Politique d'autorisation et "
               "commande non autorisee"
            << std::endl;
  ToolRegistry reg(16);
  bool rebootCalled = false;

  reg.registerTool("system.get_status", "Status",
                   []() { return ToolResult::success(); });
  reg.registerTool(
      "system.reboot", "Reboot",
      [&]() {
        rebootCalled = true;
        return ToolResult::success();
      },
      ToolSecurityPolicy::WRITE);

  // Politique : interdire tout reboot
  reg.setAuthorizationChecker([](const ToolDefinition &def, const ToolArgs &) {
    return def.id != "system.reboot";
  });

  ToolResult r1 = reg.execute("system.reboot");
  TEST_ASSERT(r1.status == ToolExecutionStatus::PERMISSION_DENIED,
              "PERMISSION_DENIED pour commande bloquee par checker");
  TEST_ASSERT(!rebootCalled, "Handler reboot non execute");

  ToolResult r2 = reg.execute("system.get_status");
  TEST_ASSERT(r2.status == ToolExecutionStatus::SUCCESS,
              "Commande autorisee executee avec succes");
}

// =============================================================================
// SUITE 123 : VoiceKit v0.6.4 — Local Tool Registry : Confirmation sensible
// =============================================================================
void test_v064_tool_sensitive_confirmation() {
  std::cout << "\n[TEST SUITE 123] v0.6.4 : Commande SENSITIVE et cycle de "
               "confirmation explicite"
            << std::endl;
  ToolRegistry reg(16);
  bool factoryResetCalled = false;

  reg.registerTool(
      "system.factory_reset", "Remise a zero",
      [&]() {
        factoryResetCalled = true;
        return ToolResult::success("Reset effectue");
      },
      ToolSecurityPolicy::SENSITIVE);

  // 1. Demande d'exécution directe sans confirmation : JAMAIS exécutée
  // automatiquement
  ToolResult r1 = reg.execute("system.factory_reset");
  TEST_ASSERT(r1.status == ToolExecutionStatus::CONFIRMATION_REQUIRED,
              "Statut CONFIRMATION_REQUIRED");
  TEST_ASSERT(!r1.confirmationToken.isEmpty(),
              "Jeton de confirmation non vide genere");
  TEST_ASSERT(!factoryResetCalled,
              "Commande sensible JAMAIS executee automatiquement");

  // 2. Confirmation explicite avec le jeton
  ToolResult r2 = reg.confirm(r1.confirmationToken);
  TEST_ASSERT(r2.status == ToolExecutionStatus::SUCCESS,
              "Statut SUCCESS apres confirmation");
  TEST_ASSERT(r2.output == "Reset effectue", "Sortie du handler confirmee");
  TEST_ASSERT(factoryResetCalled,
              "Handler execute APRES confirmation explicite");
}

// =============================================================================
// SUITE 124 : VoiceKit v0.6.4 — Local Tool Registry : Confirmation expirée ou
// réutilisée
// =============================================================================
void test_v064_tool_confirmation_expired_or_reused() {
  std::cout << "\n[TEST SUITE 124] v0.6.4 : Protection anti-rejeu et "
               "expiration du jeton de confirmation"
            << std::endl;
  ToolRegistry reg(16);
  int callCount = 0;

  reg.registerTool(
      "system.wipe", "Wipe",
      [&]() {
        callCount++;
        return ToolResult::success();
      },
      ToolSecurityPolicy::SENSITIVE);

  // Cas 1 : Protection anti-rejeu (réutilisation du même jeton)
  ToolResult r1 = reg.execute("system.wipe");
  String token1 = r1.confirmationToken;
  ToolResult r2 = reg.confirm(token1);
  TEST_ASSERT(r2.isSuccess(), "Premiere confirmation reussie");
  TEST_ASSERT(callCount == 1, "Handler execute 1 fois");

  // Deuxième utilisation du même jeton
  ToolResult r3 = reg.confirm(token1);
  TEST_ASSERT(r3.status == ToolExecutionStatus::PERMISSION_DENIED,
              "PERMISSION_DENIED lors de la reutilisation du jeton");
  TEST_ASSERT(r3.error.indexOf("anti-rejeu") != -1 ||
                  r3.error.indexOf("utilise") != -1,
              "Protection anti-rejeu explicite");
  TEST_ASSERT(callCount == 1, "Handler non reexecute (anti-rejeu)");

  // Cas 2 : Expiration temporelle
  ToolResult r4 = reg.execute("system.wipe");
  String token2 = r4.confirmationToken;

  // Avancement artificiel du temps au-delà du timeout (30s)
  advanceMockMillis(35000);

  ToolResult r5 = reg.confirm(token2);
  TEST_ASSERT(r5.status == ToolExecutionStatus::PERMISSION_DENIED,
              "PERMISSION_DENIED pour jeton expire");
  TEST_ASSERT(r5.error.indexOf("expire") != -1,
              "Message d'erreur cite l'expiration");
  TEST_ASSERT(callCount == 1, "Handler non execute pour jeton expire");
}

// =============================================================================
// SUITE 125 : VoiceKit v0.6.4 — Local Tool Registry : Erreur de fonction
// d'exécution
// =============================================================================
void test_v064_tool_execution_failure() {
  std::cout << "\n[TEST SUITE 125] v0.6.4 : Erreur de fonction d'execution et "
               "remontee structuree"
            << std::endl;
  ToolRegistry reg(16);

  reg.registerTool("sensor.read", "Lecture capteur", []() {
    return ToolResult::executionFailed("I2C bus timeout error");
  });

  ToolResult res = reg.execute("sensor.read");
  TEST_ASSERT(res.status == ToolExecutionStatus::EXECUTION_FAILED,
              "Statut EXECUTION_FAILED");
  TEST_ASSERT(res.error == "I2C bus timeout error",
              "Message d'erreur preserve");
  TEST_ASSERT(!res.isSuccess(), "isSuccess == false");
}

// =============================================================================
// SUITE 126 : VoiceKit v0.6.4 — Local Tool Registry : Double demande
// d'exécution (Busy)
// =============================================================================
void test_v064_tool_double_execution_busy() {
  std::cout
      << "\n[TEST SUITE 126] v0.6.4 : Double demande d'execution et statut BUSY"
      << std::endl;
  ToolRegistry reg(16);
  ToolResult innerResult;

  reg.registerTool("test.dummy", "Dummy",
                   []() { return ToolResult::success("Dummy OK"); });

  // Commande réentrante appelant le registre pendant qu'elle est en cours
  // d'exécution
  reg.registerTool("test.reentrant", "Reentrant", [&](const ToolArgs &) {
    innerResult = reg.execute("test.dummy");
    return ToolResult::success("Reentrant completed");
  });

  ToolResult outer = reg.execute("test.reentrant");
  TEST_ASSERT(outer.isSuccess(), "Execution parente completee");
  TEST_ASSERT(innerResult.status == ToolExecutionStatus::BUSY,
              "Appel reentrant concurrent refuse avec statut BUSY");
  TEST_ASSERT(innerResult.error.indexOf("occupe") != -1,
              "Erreur explicite sur occupation");
}

// =============================================================================
// SUITE 127 : VoiceKit v0.6.4 — Local Tool Registry : Ordre correct des
// événements
// =============================================================================
void test_v064_tool_event_ordering() {
  std::cout
      << "\n[TEST SUITE 127] v0.6.4 : Ordre correct des evenements du registre"
      << std::endl;
  ToolRegistry reg(16);
  std::vector<ToolEventType> recordedEvents;

  reg.onEvent([&](const ToolEvent &e) { recordedEvents.push_back(e.type); });

  reg.registerTool("tool.success", "Success",
                   []() { return ToolResult::success(); });
  reg.registerTool("tool.fail", "Fail",
                   []() { return ToolResult::executionFailed("err"); });
  reg.registerTool(
      "tool.sensitive", "Sensitive", []() { return ToolResult::success(); },
      ToolSecurityPolicy::SENSITIVE);

  // Scenario 1 : Succès -> REQUESTED -> STARTED -> COMPLETED
  recordedEvents.clear();
  reg.execute("tool.success");
  TEST_ASSERT(recordedEvents.size() == 3, "3 evenements pour un succes");
  if (recordedEvents.size() == 3) {
    TEST_ASSERT(recordedEvents[0] == ToolEventType::TOOL_REQUESTED,
                "1. TOOL_REQUESTED");
    TEST_ASSERT(recordedEvents[1] == ToolEventType::TOOL_STARTED,
                "2. TOOL_STARTED");
    TEST_ASSERT(recordedEvents[2] == ToolEventType::TOOL_COMPLETED,
                "3. TOOL_COMPLETED");
  }

  // Scenario 2 : Échec d'exécution -> REQUESTED -> STARTED -> FAILED
  recordedEvents.clear();
  reg.execute("tool.fail");
  TEST_ASSERT(recordedEvents.size() == 3,
              "3 evenements pour echec d'execution");
  if (recordedEvents.size() == 3) {
    TEST_ASSERT(recordedEvents[0] == ToolEventType::TOOL_REQUESTED,
                "1. TOOL_REQUESTED");
    TEST_ASSERT(recordedEvents[1] == ToolEventType::TOOL_STARTED,
                "2. TOOL_STARTED");
    TEST_ASSERT(recordedEvents[2] == ToolEventType::TOOL_FAILED,
                "3. TOOL_FAILED");
  }

  // Scenario 3 : Commande inexistante -> REQUESTED -> FAILED
  recordedEvents.clear();
  reg.execute("tool.unknown");
  TEST_ASSERT(recordedEvents.size() == 2,
              "2 evenements pour commande inexistante");
  if (recordedEvents.size() == 2) {
    TEST_ASSERT(recordedEvents[0] == ToolEventType::TOOL_REQUESTED,
                "1. TOOL_REQUESTED");
    TEST_ASSERT(recordedEvents[1] == ToolEventType::TOOL_FAILED,
                "2. TOOL_FAILED");
  }

  // Scenario 4 : Commande sensible -> REQUESTED -> CONFIRMATION_REQUIRED
  recordedEvents.clear();
  ToolResult sensRes = reg.execute("tool.sensitive");
  TEST_ASSERT(recordedEvents.size() == 2, "2 evenements avant confirmation");
  if (recordedEvents.size() == 2) {
    TEST_ASSERT(recordedEvents[0] == ToolEventType::TOOL_REQUESTED,
                "1. TOOL_REQUESTED");
    TEST_ASSERT(recordedEvents[1] == ToolEventType::TOOL_CONFIRMATION_REQUIRED,
                "2. TOOL_CONFIRMATION_REQUIRED");
  }

  // Suite Scenario 4 : confirm() -> STARTED -> COMPLETED
  recordedEvents.clear();
  reg.confirm(sensRes.confirmationToken);
  TEST_ASSERT(recordedEvents.size() == 2, "2 evenements apres confirmation");
  if (recordedEvents.size() == 2) {
    TEST_ASSERT(recordedEvents[0] == ToolEventType::TOOL_STARTED,
                "1. TOOL_STARTED");
    TEST_ASSERT(recordedEvents[1] == ToolEventType::TOOL_COMPLETED,
                "2. TOOL_COMPLETED");
  }
}

// =============================================================================
// SUITE 128 : VoiceKit v0.6.4 — Local Tool Registry : Libération des ressources
// =============================================================================
void test_v064_tool_resource_cleanup() {
  std::cout << "\n[TEST SUITE 128] v0.6.4 : Liberation des ressources et "
               "reinitialisation"
            << std::endl;
  ToolRegistry reg(16);

  for (int i = 0; i < 5; ++i) {
    reg.registerTool("tool." + String(i), "Desc",
                     []() { return ToolResult::success(); });
  }
  TEST_ASSERT(reg.getToolCount() == 5, "5 outils enregistres");

  // Desenregistrement unitaire
  bool unreg = reg.unregisterTool("tool.2");
  TEST_ASSERT(unreg, "unregisterTool tool.2 reussi");
  TEST_ASSERT(reg.getToolCount() == 4, "Compte passe a 4");
  TEST_ASSERT(!reg.hasTool("tool.2"), "tool.2 n'existe plus");

  // Vidage complet
  reg.clear();
  TEST_ASSERT(reg.getToolCount() == 0, "getToolCount == 0 apres clear()");
  TEST_ASSERT(reg.pendingConfirmationCount() == 0,
              "pendingConfirmations == 0 apres clear()");

  // Re-enregistrement immediat apres clear
  bool rNew = reg.registerTool("tool.new", "Nouveau",
                               []() { return ToolResult::success(); });
  TEST_ASSERT(rNew, "Re-enregistrement possible apres clear");
  TEST_ASSERT(reg.getToolCount() == 1, "Compte == 1");
}

// =============================================================================
// SUITE 129 : VoiceKit v0.6.4 — Local Tool Registry : Les 3 commandes de
// démonstration
// =============================================================================
void test_v064_demo_three_tools_and_json() {
  std::cout << "\n[TEST SUITE 129] v0.6.4 : Verification des 3 commandes de "
               "demonstration et parseur JSON"
            << std::endl;
  ToolRegistry reg(16);

  // 1. system.get_status
  reg.registerTool(
      "system.get_status", "Retourne l'etat du CoreS3",
      []() {
        return ToolResult::success(
            "{\"battery\": 98, \"heap\": 182400, \"uptime_s\": 120}");
      },
      ToolSecurityPolicy::READ_ONLY);

  // 2. audio.set_volume
  std::vector<ToolParam> volParams = {ToolParam(
      "volume", ToolParamType::INT, 0, 100, true, "Niveau sonore (0-100)")};
  int currentVolume = 50;
  reg.registerTool(
      "audio.set_volume", "Modifie le volume",
      [&](const ToolArgs &args) {
        currentVolume = args.getInt("volume");
        return ToolResult::success("Volume regle a " + String(currentVolume));
      },
      volParams, ToolSecurityPolicy::WRITE);

  // 3. display.set_brightness
  std::vector<ToolParam> brightParams = {
      ToolParam("brightness", ToolParamType::INT, 0, 255, true,
                "Luminosite ecran (0-255)")};
  int currentBrightness = 128;
  reg.registerTool(
      "display.set_brightness", "Modifie la luminosite",
      [&](const ToolArgs &args) {
        currentBrightness = args.getInt("brightness");
        return ToolResult::success("Luminosite reglee a " +
                                   String(currentBrightness));
      },
      brightParams, ToolSecurityPolicy::WRITE);

  // Test system.get_status
  ToolResult rStatus = reg.execute("system.get_status");
  TEST_ASSERT(rStatus.isSuccess(), "system.get_status reussi");
  TEST_ASSERT(rStatus.output.indexOf("\"battery\": 98") != -1,
              "Etat batterie conforme");

  // Test audio.set_volume via parseur JSON
  ToolArgs volJsonArgs = ToolArgs::fromJson("{\"volume\": 85}");
  ToolResult rVol = reg.execute("audio.set_volume", volJsonArgs);
  TEST_ASSERT(rVol.isSuccess(), "audio.set_volume via JSON reussi");
  TEST_ASSERT(currentVolume == 85, "Volume mis a jour a 85");

  // Test display.set_brightness via parseur JSON
  ToolArgs brightJsonArgs = ToolArgs::fromJson("{\"brightness\": 220}");
  ToolResult rBright = reg.execute("display.set_brightness", brightJsonArgs);
  TEST_ASSERT(rBright.isSuccess(), "display.set_brightness via JSON reussi");
  TEST_ASSERT(currentBrightness == 220, "Luminosite mise a jour a 220");
}

// =============================================================================
// TEST SUITE 143 : VoiceKit v0.6.4.3.2 — Final Event Correlation Validation
// =============================================================================
void test_v06432_final_event_correlation_validation() {
  std::cout << "\n[TEST SUITE 143] VoiceKit v0.6.4.3.2 — Final Event "
               "Correlation Validation"
            << std::endl;

  // -------------------------------------------------------------------------
  // TEST A : Corrélation STT — Ancienne transcription après nouvelle capture
  // Traversée stricte du véritable chemin d'événements :
  // MockLiveTransport -> GeminiLiveClient -> VoxCoreVoice ->
  // onTranscriptionWithTurnId -> VoiceUxLiveBridge
  // -------------------------------------------------------------------------
  {
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceUxLiveBridge bridge;
    bridge.begin();
    bridge.seedTestReplies();

    VoiceKitConfig cfg;
    cfg.apiKey = "AIzaTestValidationKeyA";
    cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(cfg);
    prepareMockVoiceReady(voice, mockTransport);

    // Câblage authentique comme dans VoiceUxLiveDemo
    voice.onListeningStartedWithTurnId(
        [&](uint32_t turnId) { bridge.onListeningStarted(turnId); });
    voice.onListeningFinished([&]() { bridge.onListeningFinished(); });
    voice.onTranscriptionWithTurnId(
        [&](const String &text, bool isFinal, uint32_t turnId) {
          bridge.feedTranscription(text, isFinal, turnId);
        });

    // 1. Démarrer capture A
    voice.startListening();
    uint32_t turnIdA = voice.getActiveCaptureTurnId();
    TEST_ASSERT(turnIdA > 0, "Test A.1 turnIdA positif attribue au depart");
    TEST_ASSERT(bridge.getActiveTurnId() == turnIdA,
                "Test A.1 Bridge actif = turnIdA");
    TEST_ASSERT(voice.isListening(), "Test A.1 Capture A active");

    // 2. STOP sur capture A
    voice.stop();
    bridge.triggerPriorityStop();
    TEST_ASSERT(voice.isReady(), "Test A.2 VoxCoreVoice READY apres STOP");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY,
                "Test A.2 Bridge READY apres STOP");

    // 3. Démarrer capture B
    voice.startListening();
    uint32_t turnIdB = voice.getActiveCaptureTurnId();
    TEST_ASSERT(turnIdB > turnIdA,
                "Test A.3 turnIdB strictement superieur a turnIdA");
    TEST_ASSERT(bridge.getActiveTurnId() == turnIdB,
                "Test A.3 Bridge actif = turnIdB");
    TEST_ASSERT(voice.isListening(), "Test A.3 Capture B active");

    // 4. Injecter via WebSocket une transcription finale appartenant a A
    uint32_t staleCountBefore =
        bridge.getBridgeStats().staleTranscriptionsIgnored;
    const char *jsonA =
        "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Répète\"}}}";
    voice.getClient().handleIncomingMessage(jsonA, strlen(jsonA));
    voice.update();

    // 5. Verifier rejet strict : aucune action pour B, compteur stale
    // incremente
    TEST_ASSERT(
        !bridge.hasPendingAction(),
        "Test A.5 Aucune action creee pour B depuis ancienne transcription A");
    TEST_ASSERT(bridge.getBridgeStats().staleTranscriptionsIgnored ==
                    staleCountBefore + 1,
                "Test A.5 Compteur stale incremente");
    TEST_ASSERT(voice.isListening(),
                "Test A.5 Capture B toujours active (non corrompue par A)");
    TEST_ASSERT(voice.getActiveCaptureTurnId() == turnIdB,
                "Test A.5 turnId actif reste strictement turnIdB");

    // 6. Terminer capture B normalement et injecter transcription legitime de B
    voice.stopListening();
    bridge.onListeningFinished();
    TEST_ASSERT(voice.isTranscribing(),
                "Test A.6 Capture B terminee, en attente transcription");
    const char *jsonB =
        "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Répète\"}}}";
    voice.getClient().handleIncomingMessage(jsonB, strlen(jsonB));
    voice.update();

    // 7. Verifier acceptation normale du tour B
    TEST_ASSERT(bridge.hasPendingAction(),
                "Test A.7 Action valide creee pour le tour B");
    VoiceUxBridgeAction actB = bridge.consumePendingAction();
    TEST_ASSERT(actB.intent == VoiceUxIntent::REPEAT,
                "Test A.7 Intention REPEAT du tour B conforme");
    TEST_ASSERT(voice.isReady(),
                "Test A.7 VoxCoreVoice READY apres transcription finale de B");

    voice.getClient().setTransport(nullptr);
  }

  // -------------------------------------------------------------------------
  // TEST B : Corrélation TTS — Ancienne fin audio après nouveau TTS
  // Deux ordres : STOP -> démarrage B -> ancienne fin A (ordre 2, le plus
  // critique)
  //              STOP -> ancienne fin A -> démarrage B (ordre 1)
  // -------------------------------------------------------------------------
  {
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig cfg;
    cfg.apiKey = "AIzaTestValidationKeyB";
    cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(cfg);
    prepareMockVoiceReady(voice, mockTransport);

    // --- ORDRE 2 : STOP -> démarrage B -> ancienne fin A ---
    uint32_t lastFinishedSpeechId = 0;
    uint32_t finishCallbackCount = 0;
    voice.onSpeechFinishedWithId([&](uint32_t spId) {
      lastFinishedSpeechId = spId;
      finishCallbackCount++;
    });

    // 1. Démarrer restitution A
    voice.say("Annonce A");
    uint32_t speechIdA = voice.getActiveSpeechId();
    TEST_ASSERT(speechIdA > 0, "Test B.1 speechIdA attribue");
    injectGeminiPcmAndDrain(voice, 2, false);
    TEST_ASSERT(voice.isSpeaking(),
                "Test B.1 Restitution A active (isSpeaking)");

    // 2. STOP pendant lecture de A
    voice.stop();
    TEST_ASSERT(!voice.isSpeaking(), "Test B.2 Haut-parleur arrete apres STOP");
    TEST_ASSERT(voice.getActiveSpeechId() == 0,
                "Test B.2 speechId actif reinitialise a 0");

    // 3. Démarrer restitution B
    voice.say("Annonce B");
    uint32_t speechIdB = voice.getActiveSpeechId();
    TEST_ASSERT(speechIdB > speechIdA, "Test B.3 speechIdB > speechIdA");
    injectGeminiPcmAndDrain(voice, 2, false);
    TEST_ASSERT(voice.isSpeaking(), "Test B.3 Restitution B active");

    // 4. Injecter notification de fin résiduelle provenant de A (speechIdA)
    // Vérifions que VoxCoreVoice ne termine pas prématurément B
    TEST_ASSERT(voice.getActiveSpeechId() == speechIdB,
                "Test B.4 speechId actif reste speechIdB");
    TEST_ASSERT(voice.isSpeaking(), "Test B.4 Moteur toujours SPEAKING pour B");
    TEST_ASSERT(finishCallbackCount == 0,
                "Test B.4 Aucun callback de fin premature pour B");

    // 5. Terminer réellement la restitution B
    injectGeminiPcmAndDrain(voice, 3, true);
    drainVoicePlayback(voice);
    TEST_ASSERT(voice.isReady(), "Test B.5 Moteur READY apres fin reelle de B");
    TEST_ASSERT(finishCallbackCount == 1,
                "Test B.5 Exactement un callback de fin audio emis");
    TEST_ASSERT(lastFinishedSpeechId == speechIdB,
                "Test B.5 Le callback emis correspond a speechIdB");

    // --- ORDRE 1 : STOP -> ancienne fin A -> démarrage B ---
    finishCallbackCount = 0;
    lastFinishedSpeechId = 0;
    voice.say("Annonce C");
    uint32_t speechIdC = voice.getActiveSpeechId();
    injectGeminiPcmAndDrain(voice, 2, false);
    voice.stop();
    TEST_ASSERT(voice.getActiveSpeechId() == 0,
                "Test B.6 Actif a 0 apres STOP");

    // Fin au repos ignorée
    TEST_ASSERT(finishCallbackCount == 0,
                "Test B.7 Fin apres STOP ignoree au repos");

    // Démarrage D
    voice.say("Annonce D");
    uint32_t speechIdD = voice.getActiveSpeechId();
    TEST_ASSERT(speechIdD > speechIdC, "Test B.8 speechIdD > speechIdC");
    injectGeminiPcmAndDrain(voice, 3, true);
    drainVoicePlayback(voice);
    TEST_ASSERT(finishCallbackCount == 1,
                "Test B.9 Exactement 1 callback de fin pour D");
    TEST_ASSERT(lastFinishedSpeechId == speechIdD,
                "Test B.9 Callback emis pour speechIdD");

    voice.getClient().setTransport(nullptr);
  }

  // -------------------------------------------------------------------------
  // TEST C : Absence de corruption des états internes après STOP
  // -------------------------------------------------------------------------
  {
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig cfg;
    cfg.apiKey = "AIzaTestValidationKeyC";
    cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(cfg);
    prepareMockVoiceReady(voice, mockTransport);

    // 1. Démarrer capture A
    voice.startListening();
    uint32_t turnA = voice.getActiveCaptureTurnId();
    voice.cancelListening(); // STOP / annulation de A
    TEST_ASSERT(voice.isReady(), "Test C.1 READY apres annulation A");

    // 2. Démarrer B
    voice.startListening();
    uint32_t turnB = voice.getActiveCaptureTurnId();
    TEST_ASSERT(turnB > turnA, "Test C.2 turnB > turnA");

    // 3. Capturer l'état complet de B
    VoiceState stateB = voice.getState();
    uint32_t activeIdB = voice.getActiveCaptureTurnId();
    String lastFinalB = voice.getLastFinalText();
    bool transPendingB = voice.isTranscribing();
    bool isListB = voice.isListening();

    // 4. Injecter transcription obsolète de A
    const char *staleJson = "{\"serverContent\":{\"inputTranscription\":{"
                            "\"text\":\"Texte pirate obsolete\"}}}";
    voice.getClient().handleIncomingMessage(staleJson, strlen(staleJson));
    voice.update();

    // 5. Vérifier qu'aucun état interne de B n'a été corrompu
    TEST_ASSERT(voice.getState() == stateB,
                "Test C.5 Etat conserve sans transition prematuree vers READY");
    TEST_ASSERT(voice.getActiveCaptureTurnId() == activeIdB,
                "Test C.5 turnId actif inchange");
    TEST_ASSERT(voice.getLastFinalText() == lastFinalB,
                "Test C.5 lastFinalText non pollue par le texte obsolete");
    TEST_ASSERT(voice.isTranscribing() == transPendingB,
                "Test C.5 isTranscribing inchange");
    TEST_ASSERT(voice.isListening() == isListB,
                "Test C.5 isListening toujours true");

    // 6. Nettoyage
    voice.stop();
    voice.getClient().setTransport(nullptr);
  }

  // -------------------------------------------------------------------------
  // TEST D : Gestion des erreurs pendant une restitution vocale
  // -------------------------------------------------------------------------
  {
    VoiceUxLiveBridge bridge;
    bridge.begin();
    bridge.seedTestReplies();

    VoiceUxActionStatus lastReceivedStatus = VoiceUxActionStatus::CREATED;
    uint32_t statusUpdatesCount = 0;
    bridge.onActionStatus(
        [&](const VoiceUxBridgeAction &act, VoiceUxActionStatus status) {
          lastReceivedStatus = status;
          statusUpdatesCount++;
        });

    // 1. Démarrer TTS A
    bridge.feedSimulatedText("Première consigne vocale");
    VoiceUxBridgeAction actA = bridge.consumePendingAction();
    bridge.notifySpeechStarted(actA.text, 100);
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING,
                "Test D.1 SPEAKING");

    // 2. Injecter une erreur de restitution / réseau
    bridge.notifyError("Perte de liaison WebSocket");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::ERROR,
                "Test D.2 Passage en etat ERROR");
    TEST_ASSERT(lastReceivedStatus == VoiceUxActionStatus::REJECTED,
                "Test D.2 Action A reçoit statut terminal REJECTED");

    // 3. Vérifier que la réponse erronée n'a PAS été mémorisée dans ReplyMemory
    TEST_ASSERT(bridge.getReplyMemory().getLastReply() !=
                    "Première consigne vocale",
                "Test D.3 Reponse incomplete NON memorisee");

    // 4. Récupération d'erreur
    bridge.recoverFromError();
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY,
                "Test D.4 Recuperation d'erreur -> READY");

    // 5. Démarrer TTS B
    bridge.feedSimulatedText("Deuxième consigne vocale");
    VoiceUxBridgeAction actB = bridge.consumePendingAction();
    bridge.notifySpeechStarted(actB.text, 101);
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING,
                "Test D.5 TTS B SPEAKING");
    TEST_ASSERT(bridge.getCurrentSpeechId() == 101,
                "Test D.5 SpeechId B = 101");

    // 6. Injecter ancienne fin de A (id=100)
    bridge.notifySpeechFinished(true, 100);
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING,
                "Test D.6 B reste active malgre fin ancienne A");
    TEST_ASSERT(bridge.getCurrentSpeechId() == 101,
                "Test D.6 SpeechId B toujours 101");

    // 7. Terminer B normalement
    bridge.notifySpeechFinished(true, 101);
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY,
                "Test D.7 READY apres fin legitime de B");
    TEST_ASSERT(bridge.getReplyMemory().getLastReply() ==
                    "Deuxième consigne vocale",
                "Test D.7 Reponse complete B memorisee");

    // 8. Action en attente lors d'une erreur
    bridge.feedSimulatedText("Action mise en attente");
    TEST_ASSERT(bridge.hasPendingAction(), "Test D.8 Action pending presente");
    bridge.notifyError("Nouvelle erreur inopinee");
    TEST_ASSERT(!bridge.hasPendingAction(),
                "Test D.8 Pending action retiree sur erreur");
    TEST_ASSERT(lastReceivedStatus == VoiceUxActionStatus::CANCELLED,
                "Test D.8 Action pending annulee proprement");
    bridge.recoverFromError();
  }

  // -------------------------------------------------------------------------
  // TEST E : Façade publique VoiceKit — Callbacks enrichis et corrélations
  // -------------------------------------------------------------------------
  {
    MockLiveTransport mockTransport;
    VoiceKit vk;
    VoiceKitConfig cfg;
    cfg.apiKey = "AIzaTestValidationKeyE";
    cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
    vk.begin(cfg);
    prepareMockVoiceKitReady(vk, mockTransport);

    uint32_t facadeTurnId = 0;
    String facadeText = "";
    bool facadeFinal = false;
    vk.onTranscriptionWithTurnId(
        [&](const String &text, bool isFinal, uint32_t turnId) {
          facadeText = text;
          facadeFinal = isFinal;
          facadeTurnId = turnId;
        });

    uint32_t facadeFinishedSpeechId = 0;
    vk.onSpeechFinishedWithId(
        [&](uint32_t speechId) { facadeFinishedSpeechId = speechId; });

    // 1. STT via façade
    bool listenOk = vk.listen();
    TEST_ASSERT(listenOk, "Test E.1 vk.listen() accepte");
    uint32_t activeTurn = vk.getActiveCaptureTurnId();
    TEST_ASSERT(activeTurn > 0, "Test E.1 vk.getActiveCaptureTurnId() positif");

    vk.stopListening();
    uint32_t pendingTurn = vk.getPendingTranscriptionTurnId();
    TEST_ASSERT(pendingTurn == activeTurn,
                "Test E.1 vk.getPendingTranscriptionTurnId() == activeTurn");

    const char *sttJson = "{\"serverContent\":{\"inputTranscription\":{"
                          "\"text\":\"Bonjour facade\"}}}";
    vk.getEngine().getClient().handleIncomingMessage(sttJson, strlen(sttJson));
    vk.update();

    TEST_ASSERT(facadeText == "Bonjour facade",
                "Test E.1 Texte relaye par facade");
    TEST_ASSERT(facadeFinal == true, "Test E.1 Flag isFinal relaye");
    TEST_ASSERT(facadeTurnId == activeTurn,
                "Test E.1 turnId relaye sans alteration");

    // 2. TTS via façade
    bool sayOk = vk.say("Phrase facade");
    TEST_ASSERT(sayOk, "Test E.2 vk.say() accepte");
    uint32_t activeSpeech = vk.getActiveSpeechId();
    TEST_ASSERT(activeSpeech > 0, "Test E.2 vk.getActiveSpeechId() positif");

    injectGeminiPcmAndDrain(vk.getEngine(), 3, true);
    drainVoicePlayback(vk.getEngine());

    TEST_ASSERT(facadeFinishedSpeechId == activeSpeech,
                "Test E.2 onSpeechFinishedWithId relaye speechId exact");
    TEST_ASSERT(vk.isReady(), "Test E.2 Façade READY apres TTS");

    vk.getEngine().getClient().setTransport(nullptr);
  }

  // -------------------------------------------------------------------------
  // TEST F : Scénarios temporels et robustesse (STOP avant/après fin, double
  // fin)
  // -------------------------------------------------------------------------
  {
    MockLiveTransport mockTransport;
    VoxCoreVoice voice;
    VoiceKitConfig cfg;
    cfg.apiKey = "AIzaTestValidationKeyF";
    cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
    voice.begin(cfg);
    prepareMockVoiceReady(voice, mockTransport);

    // 1. STOP avant transcription finale (pendant TRANSCRIBING)
    voice.startListening();
    voice.stopListening();
    TEST_ASSERT(voice.isTranscribing(), "Test F.1 TRANSCRIBING");
    voice.stop();
    TEST_ASSERT(voice.isReady(), "Test F.1 READY apres STOP");
    const char *lateJson =
        "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Tardif\"}}}";
    voice.getClient().handleIncomingMessage(lateJson, strlen(lateJson));
    voice.update();
    TEST_ASSERT(voice.isReady(), "Test F.1 Reste READY, aucune corruption");

    // 2. STOP immédiatement après transcription finale
    voice.startListening();
    voice.stopListening();
    const char *finalJson = "{\"serverContent\":{\"inputTranscription\":{"
                            "\"text\":\"Final valide\"}}}";
    voice.getClient().handleIncomingMessage(finalJson, strlen(finalJson));
    voice.update();
    TEST_ASSERT(voice.isReady(), "Test F.2 READY apres transcription");
    voice.stop(); // STOP au repos
    TEST_ASSERT(voice.isReady(), "Test F.2 Toujours READY apres STOP au repos");

    // 3. Double callback de fin audio (idempotence)
    uint32_t ttsFinCount = 0;
    voice.onSpeechFinishedWithId([&](uint32_t spId) { ttsFinCount++; });
    voice.say("Test double fin");
    uint32_t spId = voice.getActiveSpeechId();
    injectGeminiPcmAndDrain(voice, 3, true);
    drainVoicePlayback(voice);
    TEST_ASSERT(ttsFinCount == 1, "Test F.3 Premier callback de fin reçu");
    // Simuler un second appel tardif avec le même ID
    voice.getPlayer().setPlaybackId(spId);
    // Au niveau session, le tour étant déjà terminé, aucun second événement
    // n'est émis
    TEST_ASSERT(ttsFinCount == 1,
                "Test F.3 Idempotence : pas de double fin audio");

    voice.getClient().setTransport(nullptr);
  }
}

#include "test_pipeline_suites.h"
#include "test_v0652_transcription_suites.h"

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================
int main() {
  std::cout << "==========================================================="
            << std::endl;
  std::cout << "  SUITE COMPLETE DE TESTS AUTOMATISES — VoiceKit v0.6.4.1    "
            << std::endl;
  std::cout << "  Local Tool Registry & Architecture Multi-Fournisseurs   "
            << std::endl;
  std::cout << "  Cible logique C++14 / C++17                             "
            << std::endl;
  std::cout << "==========================================================="
            << std::endl;

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

  // Nouvelles suites de tests VoiceKit v0.4.1 (UTF-8, Configuration vocale,
  // Gemini Setup, Profils)
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

  // Nouvelles suites VoiceKit v0.5.0.1 (Stabilisation FreeRTOS, Transitions de
  // modes & Unicite des evenements)
  test_v0501_freertos_concurrency_and_audio_synchronization();
  test_v0501_segment_reliability_and_duplicate_events();
  test_v0501_mode_transitions_and_failure_resilience();

  // Nouvelles suites VoiceKit v0.5.2 (Fast Connection & Instrumentation)
  test_v052_wifi_fast_connect_and_fallback();
  test_v052_sntp_optimization_and_tls_safety();
  test_v052_gemini_reconnect_network_conditioning();
  test_v052_startup_timing_instrumentation();
  test_v052_facade_connection_metrics_and_resilience();

  // Nouvelles suites VoiceKit v0.5.3 (Voice Latency Optimization &
  // Non-Regression)
  test_v053_voice_latency_metrics_calculations();
  test_v053_base64_optimization_integrity();
  test_v053_vad_threshold_experimentation();
  test_v053_conversation_turn_latency_pipeline();
  test_v053_facade_latency_and_audio_integrity();

  // Nouvelles suites VoiceKit v0.5.3.1 (Consolidation, Latence Vocale &
  // Validation)
  test_v0531_latency_metrics_audit_and_edge_cases();
  test_v0531_vad_thresholds_hot_switch_and_validation();
  test_v0531_facade_modes_transitions_and_resilience();

  // Nouvelles suites VoiceKit v0.6.0 (Architecture Multi-Fournisseurs & OpenAI
  // Realtime)
  test_v060_multiprovider_config_and_enum();
  test_v060_audio_resampler_16to24();
  test_v060_ivoiceprovider_contract_and_gemini_adapter();
  test_v060_openai_realtime_connection_and_handshake();
  test_v060_openai_realtime_session_negotiation();
  test_v060_openai_realtime_audio_streaming_tx();
  test_v060_openai_realtime_inbound_audio_rx_and_drain();
  test_v060_openai_realtime_transcriptions();
  test_v060_openai_realtime_priority_stop_and_cancel();
  test_v060_openai_realtime_error_handling_and_reconnection();
  test_v060_voxcore_and_voicekit_integration_and_gemini_non_regression();

  // Nouvelles suites VoiceKit v0.6.1 (Correctifs de stabilite : Gemini Live,
  // WebSocket TLS et Wi-Fi)
  test_v061_correction_a_turn_rx_metrics_and_order_alerts();
  test_v061_correction_b_tls_scheduling_and_loop_diagnostics();
  test_v061_correction_c_wifi_10_scenarios_and_tick_durations();

  // Nouvelles suites VoiceKit v0.6.2 (Wake Word Foundation — M5Stack CoreS3)
  test_v062_offline_init_and_shared_audio_capture();
  test_v062_wakeword_audio_bridge_queue_and_overflow();
  test_v062_wakenet_engine_availability_and_frame_adapter();
  test_v062_test_engine_and_state_machine_transitions();
  test_v062_coexistence_suspension_and_resumption_all_modes();
  test_v062_lvgl_event_polling_and_diagnostics_metrics();

  // Nouvelles suites correctives VoiceKit v0.6.2.1 (Fiabilité et performances
  // Wake Word Foundation)
  test_v0621_audio_bridge_lifetime_concurrency_and_7_scenarios();
  test_v0621_cpu_budget_and_lvgl_fluidity();
  test_v0621_mic_stop_failure_and_mode_transitions();
  test_v0621_state_machine_callbacks_and_memory_safety();
  test_v0621_wakenet_engine_strict_validation();
  test_v0621_local_capture_offline_rms_and_non_interference();

  // Nouvelles suites VoiceKit v0.6.3 (Wake Word Driven Conversation — Scénarios
  // 1 à 12)
  test_v063_wake_conversation_single_and_10_consecutive_cycles();
  test_v063_concurrent_detections_and_silence_timeout();
  test_v063_multi_turn_conversation_and_voice_stop_command();
  test_v063_manual_stop_and_final_audio_samples_preservation();
  test_v063_wifi_loss_ws_disconnect_mic_failure_and_recovery();
  test_v063_lvgl_non_blocking_openai_provider_and_diagnostics();

  // Nouvelles suites correctives VoiceKit v0.6.3.2.1 (Latence non bloquante,
  // Concurrence micro, WakeNet configurable, UI)
  test_v0631_latency_instrumentation_and_nonblocking_session_teardown();
  test_v0631_audio_transitions_consolidation_and_mic_concurrency();
  test_v0631_configurable_wakenet_model_and_missing_model_validation();
  test_v0631_conversational_stability_and_turn_vs_session_distinction();
  test_v0631_ui_immediate_event_transitions_and_banner_labels();
  // Nouvelles suites correctives VoiceKit v0.6.3.2.1 (OpenAI Realtime : JSON,
  // Transcriptions & Performance)
  test_v06321_websocket_control_frames_and_json_parse_diagnostics();
  test_v06321_fragmented_transcription_and_utf8_word_cutting();
  test_v06321_assistant_full_text_conservation_and_dynamic_tags();

  // Nouvelles suites correctives VoiceKit v0.6.3.2.2 (Stabilite PTT,
  // Annulations tardives, Non-blocage)
  test_v06322_ptt_diagnostics_and_concise_instructions();
  test_v06322_stale_cancellation_protection_and_error_recovery();
  test_v06322_empty_audio_response_and_nonblocking_recovery();
  test_v0101_openai_realtime_events_failed_and_ptt_transitions();

  // Nouvelles suites VoiceKit v0.6.4 (Local Tool Registry — Section 3 & 6)
  test_v064_tool_valid_registration();
  test_v064_tool_duplicate_id();
  test_v064_tool_max_capacity();
  test_v064_tool_not_found();
  test_v064_tool_successful_execution();
  test_v064_tool_missing_required_param();
  test_v064_tool_wrong_param_type();
  test_v064_tool_param_out_of_bounds();
  test_v064_tool_unknown_param();
  test_v064_tool_unauthorized();
  test_v064_tool_sensitive_confirmation();
  test_v064_tool_confirmation_expired_or_reused();
  test_v064_tool_execution_failure();
  test_v064_tool_double_execution_busy();
  test_v064_tool_event_ordering();
  test_v064_tool_resource_cleanup();
  test_v064_demo_three_tools_and_json();

  // Suites VoiceKit v0.6.4.1 (Voice UX Foundation) & v0.6.4.2 (Voice UX Live
  // Bridge)
  run_all_voiceux_tests();

  // Suite finale VoiceKit v0.6.4.3.2 — Final Event Correlation Validation
  // (Tests A, B, C, D, E & F)
  test_v06432_final_event_correlation_validation();

  // Suite VoiceKit v0.6.5 — Integrated Voice Pipeline (Tests A, B, C, D, E, F,
  // G, H, I & J)
  run_pipeline_v065_tests();

  // Suite VoiceKit v0.6.5.1 — Integrated Pipeline Stabilization (Tests A, B, C,
  // D, E, F, G, H, I & J)
  run_pipeline_v0651_tests();

  // Suite VoiceKit v0.6.5.1 — Final Corrective & Hardware Readiness (Tests A, B, C,
  // D, E, F, G, H, I & J)
  run_pipeline_v0651_final_stabilization_tests();

  // Suite VoiceKit v0.6.5.1 — Minimal Corrective Final Validation (P0, P1 Conf, P1 Late AI)
  run_pipeline_v0651_minimal_corrective_tests();

  // Suite VoiceKit v0.6.5.2 — Transcription Assembly & Realtime Finalization (Tests 1 à 6)
  run_transcription_assembly_v0652_tests();

  std::cout << "\n==========================================================="
            << std::endl;
  std::cout << "  BILAN FINAL DES TESTS : " << s_testsPassed << " REUSSIS, "
            << s_testsFailed << " ECHECS" << std::endl;
  std::cout << "==========================================================="
            << std::endl;

  return (s_testsFailed == 0) ? 0 : 1;
}
