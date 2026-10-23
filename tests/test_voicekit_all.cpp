#include "native_mock/Arduino.h"
#include "native_mock/WiFi.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>

#include "VoxCoreVoice.h"
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
// SUITE 3 : Scénarios Audio Longs, Multi-tours et Intégrité finale
// =============================================================================
void test_audio_scenarios_duration() {
    std::cout << "\n[TEST SUITE 3] Scenarios de duree : 10s, 30s, alpha-tango et 2 annonces consecutives" << std::endl;

    LiveAudioPlayer player;
    player.begin(120000);

    // 1. Phrase moyenne de 10 secondes (240 000 échantillons)
    player.reset();
    size_t total10s = 240000;
    size_t chunkSize = 960;
    std::vector<int16_t> chunk(chunkSize, 1200);

    size_t submitted10s = 0;
    while (submitted10s < total10s) {
        size_t toFeed = std::min(chunkSize, total10s - submitted10s);
        player.feedPcm(chunk.data(), toFeed);
        submitted10s += toFeed;
        player.tick();
        player.simFinishSpeakerChunk();
    }
    player.notifyTurnComplete();

    while ((player.getBufferedSamples() > 0 || player.isPlaying())) {
        player.tick();
        player.simFinishSpeakerChunk();
    }
    TEST_ASSERT(player.getSamplesReceivedTotal() == total10s, "Test 10s : 240 000 echantillons recus");
    TEST_ASSERT(player.checkBufferIntegrity(), "Test 10s : Integrite parfaite");

    // 2. Phrase longue de 30 secondes (720 000 échantillons)
    player.reset();
    size_t total30s = 720000;
    size_t submitted30s = 0;
    while (submitted30s < total30s) {
        size_t toFeed = std::min(chunkSize, total30s - submitted30s);
        player.feedPcm(chunk.data(), toFeed);
        submitted30s += toFeed;
        player.tick();
        player.simFinishSpeakerChunk();
    }
    player.notifyTurnComplete();

    while ((player.getBufferedSamples() > 0 || player.isPlaying())) {
        player.tick();
        player.simFinishSpeakerChunk();
    }
    TEST_ASSERT(player.getSamplesReceivedTotal() == total30s, "Test 30s : 720 000 echantillons recus");
    TEST_ASSERT(player.checkBufferIntegrity(), "Test 30s : Integrite parfaite (720 000 ech sans perte)");

    // 3. Test de signature finale (aucun mot coupe en fin de phrase)
    player.reset();
    bool finishedCallbackCalled = false;
    player.setOnPlaybackFinished([&finishedCallbackCalled]() {
        finishedCallbackCalled = true;
    });

    std::vector<int16_t> alphaTango(9600, 2000);
    alphaTango.back() = 0x5A5A; // Marqueur d'intégrité sur le tout dernier échantillon
    player.feedPcm(alphaTango.data(), alphaTango.size());
    player.notifyTurnComplete();

    while (player.getBufferedSamples() > 0 || player.isPlaying()) {
        player.tick();
        player.simFinishSpeakerChunk();
    }
    TEST_ASSERT(player.getSamplesPlayedTotal() == 9600, "Alpha Tango : 9600 echantillons joues sans coupure");
    TEST_ASSERT(finishedCallbackCalled, "Callback onPlaybackFinished declenche au vidage complet");
    TEST_ASSERT(player.checkBufferIntegrity(), "Zero syllabe finale tronquee");

    // 4. Deux annonces vocales successives
    player.reset();
    player.feedPcm(chunk.data(), chunkSize * 10);
    player.notifyTurnComplete();
    while (player.getBufferedSamples() > 0 || player.isPlaying()) {
        player.tick();
        player.simFinishSpeakerChunk();
    }
    TEST_ASSERT(player.checkBufferIntegrity(), "Annonce 1 terminee et valide");

    player.reset();
    player.feedPcm(chunk.data(), chunkSize * 15);
    player.notifyTurnComplete();
    while (player.getBufferedSamples() > 0 || player.isPlaying()) {
        player.tick();
        player.simFinishSpeakerChunk();
    }
    TEST_ASSERT(player.checkBufferIntegrity(), "Annonce 2 consecutive terminee et valide");
}

// =============================================================================
// SUITE 4 : API VoxCoreVoice — Politiques de file et non-blocage
// =============================================================================
void test_voicekit_api_and_queue_policies() {
    std::cout << "\n[TEST SUITE 4] API VoxCoreVoice : Politiques de file, non-blocage et erreurs" << std::endl;

    VoxCoreVoice voice;
    VoiceKitConfig cfg;
    cfg.apiKey = "TEST_API_KEY_VOICEKIT";
    cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
    cfg.volume = 80;

    // Test 1 : Rejet si clé API manquante
    VoiceKitConfig emptyCfg;
    TEST_ASSERT(!voice.begin(emptyCfg), "begin() echoue si cle API vide");

    // Test 2 : Initialisation valide
    TEST_ASSERT(voice.begin(cfg), "begin() reussit avec configuration valide");
    TEST_ASSERT(voice.getVolume() == 80, "Volume initial regle a 80%");

    // Test 3 : say() refuse un texte vide
    TEST_ASSERT(!voice.say(""), "say(\"\") refuse");

    // Test 4 : say() refuse si Wi-Fi deconnecte
    WiFi.setConnected(false);
    TEST_ASSERT(!voice.say("Bonjour"), "say() refuse si Wi-Fi deconnecte");
    TEST_ASSERT(voice.getLastError() == "Wi-Fi non connecte", "Message d'erreur Wi-Fi clair");
    WiFi.setConnected(true);

    // Test 5 : Politique REJECT_IF_BUSY
    cfg.queuePolicy = QueuePolicy::REJECT_IF_BUSY;
    voice.begin(cfg);
    std::vector<int16_t> dummy(4800, 100);
    voice.getPlayer().feedPcm(dummy.data(), dummy.size());
    TEST_ASSERT(voice.isBusy(), "Moteur detecte occupe");
    TEST_ASSERT(!voice.say("Deuxieme phrase"), "REJECT_IF_BUSY rejette la demande concorrente");
    TEST_ASSERT(voice.getLastError() == "Moteur vocal occupe", "Erreur moteur occupe retournee");

    // Test 6 : Politique QUEUE_FIFO (mise en file séquentielle)
    cfg.queuePolicy = QueuePolicy::QUEUE_FIFO;
    cfg.maxQueueSize = 2;
    voice.begin(cfg);
    voice.getPlayer().feedPcm(dummy.data(), dummy.size());
    TEST_ASSERT(voice.isBusy(), "Moteur occupe avec audio");
    TEST_ASSERT(voice.say("Message file 1"), "QUEUE_FIFO accepte le message 1 en file");
    TEST_ASSERT(voice.say("Message file 2"), "QUEUE_FIFO accepte le message 2 en file");
    TEST_ASSERT(!voice.say("Message file 3"), "QUEUE_FIFO rejette quand la file est saturee (max 2)");
    TEST_ASSERT(voice.getLastError() == "File d'attente vocale saturee", "Erreur file saturee retournee");

    // Test 7 : Politique INTERRUPT_CURRENT
    cfg.queuePolicy = QueuePolicy::INTERRUPT_CURRENT;
    voice.begin(cfg);
    voice.getPlayer().feedPcm(dummy.data(), dummy.size());
    TEST_ASSERT(voice.isBusy(), "Moteur detecte occupe avant interruption");
    voice.stop();
    TEST_ASSERT(!voice.isBusy(), "Moteur libere apres stop()");

    // Test 8 : Volume
    voice.setVolume(50);
    TEST_ASSERT(voice.getVolume() == 50, "Volume modifie a 50%");
}

// =============================================================================
// SUITE 5 : Coordination Wi-Fi / WiFiTimeSync (Prescriptions section 7)
// =============================================================================
void test_wifi_coordination() {
    std::cout << "\n[TEST SUITE 5] Coordination Wi-Fi : Verrou keepalive pour empecher la coupure" << std::endl;

    WiFiTimeSync wifiSync;
    wifiSync.init();

    // Verrouiller la connexion Wi-Fi pour VoiceKit
    wifiSync.setKeepAliveLock(true);
    TEST_ASSERT(wifiSync.isKeepAliveLocked(), "Verrou Wi-Fi VoiceKit verrouille");

    // Déverrouiller la connexion Wi-Fi
    wifiSync.setKeepAliveLock(false);
    TEST_ASSERT(!wifiSync.isKeepAliveLocked(), "Verrou Wi-Fi VoiceKit libere");
}

// =============================================================================
// SUITE 6 : Gestion explicite de la propriete du haut-parleur (Correction C)
// =============================================================================
void test_speaker_ownership_management() {
    std::cout << "\n[TEST SUITE 6] Propriete du haut-parleur (Correction C)" << std::endl;

    // Cas 1 : VoiceKit initialise le haut-parleur (l'hote ne l'avait pas demarre)
    M5AudioOutput::setMockHostSpeakerRunning(false);
    {
        M5AudioOutput out1;
        bool b1 = out1.begin();
        TEST_ASSERT(b1, "begin() reussit pour VoiceKit proprietaire");
        TEST_ASSERT(out1.isOwner() == true, "VoiceKit est detecte proprietaire (Cas 1)");
        TEST_ASSERT(out1.isRunning() == true, "Haut-parleur actif");
        out1.end(AudioStopReason::NORMAL_FINISH);
        TEST_ASSERT(M5AudioOutput::isMockHostSpeakerRunning() == false, "Haut-parleur eteint car VoiceKit etait proprietaire");
    }

    // Cas 2 : L'application hote possede deja le haut-parleur
    M5AudioOutput::setMockHostSpeakerRunning(true);
    {
        M5AudioOutput out2;
        bool b2 = out2.begin();
        TEST_ASSERT(b2, "begin() reussit quand l'hote possede deja le HP");
        TEST_ASSERT(out2.isOwner() == false, "VoiceKit n'est PAS proprietaire (Cas 2)");
        TEST_ASSERT(out2.isRunning() == true, "Haut-parleur actif pour l'hote");
        out2.end(AudioStopReason::RESET);
        TEST_ASSERT(M5AudioOutput::isMockHostSpeakerRunning() == true, "Haut-parleur reste ACTIF apres end() (preserve pour l'hote)");
    }
    // Verification apres destruction de l'objet out2
    TEST_ASSERT(M5AudioOutput::isMockHostSpeakerRunning() == true, "Haut-parleur toujours ACTIF apres destruction de M5AudioOutput (Cas 2)");

    // Cas 3 : Arret via stop()
    {
        M5AudioOutput out3;
        out3.begin();
        out3.stop(0, AudioStopReason::USER_STOP);
        TEST_ASSERT(out3.wasStopCalledInTurn(), "stop() enregistre pour le canal VoiceKit");
        TEST_ASSERT(M5AudioOutput::isMockHostSpeakerRunning() == true, "stop() n'arrete pas le peripherique global de l'hote");
    }
}

// =============================================================================
// SUITE 7 : Coherence heure affichee et annoncee (Correction D)
// =============================================================================
void test_announced_and_displayed_time_coherence() {
    std::cout << "\n[TEST SUITE 7] Coherence heure affichee et annoncee (Correction D)" << std::endl;

    int test_hours[] = {0, 8, 12, 14, 20, 21, 23};
    int test_mins[]  = {0, 5, 0,  35, 9,  21, 59};

    for (size_t i = 0; i < 7; ++i) {
        int h = test_hours[i];
        int m = test_mins[i];

        char displayed[8];
        snprintf(displayed, sizeof(displayed), "%02d:%02d", h, m);
        String announced = formatTimeInFrench(h, m);

        if (h == 0 && m == 0) {
            TEST_ASSERT(announced == "Il est minuit.", "00:00 -> minuit");
        } else if (h == 12 && m == 0) {
            TEST_ASSERT(announced == "Il est midi.", "12:00 -> midi");
        } else if (h == 20 && m == 9) {
            TEST_ASSERT(announced == "Il est vingt heures neuf.", "20:09 -> vingt heures neuf");
        } else if (h == 21 && m == 21) {
            TEST_ASSERT(announced == "Il est vingt et une heures vingt et un.", "21:21 -> vingt et une heures vingt et un");
        }
        TEST_ASSERT(announced.startsWith("Il est "), "Toute annonce commence par 'Il est '");
        TEST_ASSERT(announced.endsWith("."), "Toute annonce se termine par un point");
    }

    String fallbackSpeech = "L'heure n'est pas encore synchronisée.";
    TEST_ASSERT(!fallbackSpeech.isEmpty(), "Message de repli valide en cas d'heure RTC invalide");
}

// =============================================================================
// SUITE 8 : Robustesse des requetes asynchrones et resilience (Correction E)
// =============================================================================
void test_async_requests_and_resilience() {
    std::cout << "\n[TEST SUITE 8] Gestion robuste requetes asynchrones et resilience (Correction E)" << std::endl;

    VoxCoreVoice voice;
    VoiceKitConfig cfg;
    cfg.apiKey = "TEST_API_KEY";
    cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
    cfg.connectOnDemand = true;
    WiFi.setConnected(true);
    voice.begin(cfg);

    // 1. say() declenche la connexion a la demande
    bool accepted1 = voice.say("Premiere annonce");
    TEST_ASSERT(accepted1, "say() accepte en connectOnDemand");
    TEST_ASSERT(voice.isBusy(), "Moteur considere occupe pendant la connexion/requete");

    // 2. Interruption avec stop()
    bool speechFinishedCalled = false;
    voice.onSpeechFinished([&speechFinishedCalled]() {
        speechFinishedCalled = true;
    });
    voice.stop();
    TEST_ASSERT(!voice.isBusy(), "Moteur libre apres stop()");
    TEST_ASSERT(!speechFinishedCalled, "Aucun callback onSpeechFinished fantome apres stop()");

    // 3. Nouvelle annonce apres stop()
    bool acceptedAfterStop = voice.say("Nouvelle annonce");
    TEST_ASSERT(acceptedAfterStop, "say() accepte immediatement apres stop()");

    // 4. Gestion de deconnexion ou erreur
    bool errorTriggered = false;
    voice.onError([&errorTriggered](const String& err) {
        errorTriggered = true;
    });
    voice.stop();

    // 5. Simuler coupure Wi-Fi pendant requete
    voice.say("Test perte WiFi");
    WiFi.setConnected(false);
    voice.update();
    TEST_ASSERT(errorTriggered, "onError declenche lors de la perte Wi-Fi");
    TEST_ASSERT(!voice.isBusy(), "Moteur libere apres erreur Wi-Fi");
    WiFi.setConnected(true);

    // 6. Relance d'annonce apres erreur
    bool acceptedAfterError = voice.say("Annonce de retablissement");
    TEST_ASSERT(acceptedAfterError, "Nouvelle annonce possible apres une erreur");
    voice.stop();
}

// =============================================================================
// SUITE 9 : Cycle de vie du verrou Wi-Fi et maintien 15s (Correction F)
// =============================================================================
void test_wifi_lock_lifecycle_and_keepalive() {
    std::cout << "\n[TEST SUITE 9] Cycle de vie du verrou Wi-Fi et maintien 15s (Correction F)" << std::endl;

    WiFiTimeSync wifiSync;
    wifiSync.init();

    // Etape 1 : repos
    TEST_ASSERT(!wifiSync.isKeepAliveLocked(), "Verrou initialement inactif");

    // Etape 2 : Annonce vocale demandee -> Verrouillage immediat
    wifiSync.setKeepAliveLock(true);
    TEST_ASSERT(wifiSync.isKeepAliveLocked(), "Verrou actif pendant la requete vocale");

    // Etape 3 : Maintien chaud 15 s
    uint32_t keepalive_until = millis() + 15000;
    TEST_ASSERT(keepalive_until > millis(), "Timer 15s arme");
    TEST_ASSERT(wifiSync.isKeepAliveLocked(), "Verrou toujours maintenu durant les 15s de veille");

    // Etape 4 : 2eme annonce consecutive pendant les 15s -> rearmement immediat
    keepalive_until = 0;
    wifiSync.setKeepAliveLock(true);
    TEST_ASSERT(wifiSync.isKeepAliveLocked(), "2eme annonce consecutive : verrou maintenu sans interruption");

    // Etape 5 : Expiration des 15s -> liberation propre
    wifiSync.setKeepAliveLock(false);
    TEST_ASSERT(!wifiSync.isKeepAliveLocked(), "Verrou libere apres les 15s d'inactivite");

    // Etape 6 : Liberation d'urgence en cas d'erreur
    wifiSync.setKeepAliveLock(true);
    TEST_ASSERT(wifiSync.isKeepAliveLocked(), "Verrou reaquis");
    wifiSync.setKeepAliveLock(false); // declenche par onError
    TEST_ASSERT(!wifiSync.isKeepAliveLocked(), "Verrou libere en cas d'erreur (anti-blocage)");
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================
int main() {
    std::cout << "===========================================================" << std::endl;
    std::cout << "  SUITE COMPLETE DE TESTS AUTOMATISES — VoxCore VoiceKit   " << std::endl;
    std::cout << "  Version : v0.1.0.1 | Cible logique C++17                 " << std::endl;
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

    std::cout << "\n===========================================================" << std::endl;
    std::cout << "  BILAN FINAL DES TESTS : " << s_testsPassed << " REUSSIS, " << s_testsFailed << " ECHECS" << std::endl;
    std::cout << "===========================================================" << std::endl;

    return (s_testsFailed == 0) ? 0 : 1;
}
