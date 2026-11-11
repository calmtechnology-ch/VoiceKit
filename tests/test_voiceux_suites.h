#pragma once

#include "core/voiceux/VoiceUxTypes.h"
#include "core/voiceux/TextGate.h"
#include "core/voiceux/VoiceIntentRouter.h"
#include "core/voiceux/ReplyMemory.h"
#include "core/voiceux/VoiceUxController.h"
#include "core/voiceux/VoiceUxLiveBridge.h"
#include "core/Utf8Utils.h"
#include <iostream>

// =============================================================================
// SUITE 130 : TextGate — Filtrage déterministe et rejet des hésitations
// =============================================================================
inline void test_v0641_textgate_validation() {
    std::cout << "\n[TEST SUITE 130] v0.6.4.1 : TextGate (Vide, Hesitations, Commandes courtes, Accents & Incomplet)" << std::endl;

    TextGate gate;
    String norm;
    String reason;

    // 1. Chaîne vide
    TEST_ASSERT(gate.evaluate("", &norm, &reason) == TextGateDecision::IGNORE, "Chaine vide -> IGNORE");
    TEST_ASSERT(norm == "", "Chaine vide -> norm vide");

    // 2. Espaces et tabulations uniquement
    TEST_ASSERT(gate.evaluate("   \t  \r\n  ", &norm, &reason) == TextGateDecision::IGNORE, "Espaces seuls -> IGNORE");

    // 3. Ponctuation seule
    TEST_ASSERT(gate.evaluate("... ??? !!! ,,, ;;;", &norm, &reason) == TextGateDecision::IGNORE, "Ponctuation seule -> IGNORE");

    // 4. Hésitations isolées simples
    TEST_ASSERT(gate.evaluate("euh", &norm, &reason) == TextGateDecision::IGNORE, "euh -> IGNORE");
    TEST_ASSERT(gate.evaluate("heu", &norm, &reason) == TextGateDecision::IGNORE, "heu -> IGNORE");
    TEST_ASSERT(gate.evaluate("hum", &norm, &reason) == TextGateDecision::IGNORE, "hum -> IGNORE");
    TEST_ASSERT(gate.evaluate("hmm", &norm, &reason) == TextGateDecision::IGNORE, "hmm -> IGNORE");
    TEST_ASSERT(gate.evaluate("hm", &norm, &reason) == TextGateDecision::IGNORE, "hm -> IGNORE");

    // 5. Hésitations avec casse et ponctuation
    TEST_ASSERT(gate.evaluate("Euh...", &norm, &reason) == TextGateDecision::IGNORE, "Euh... -> IGNORE");
    TEST_ASSERT(gate.evaluate("  HEU !? ", &norm, &reason) == TextGateDecision::IGNORE, "HEU !? -> IGNORE");
    TEST_ASSERT(gate.evaluate("Hum...", &norm, &reason) == TextGateDecision::IGNORE, "Hum... -> IGNORE");
    TEST_ASSERT(gate.evaluate("Hmm !!!", &norm, &reason) == TextGateDecision::IGNORE, "Hmm !!! -> IGNORE");

    // 6. Suite d'hésitations multiples
    TEST_ASSERT(gate.evaluate("euh... hum... heu", &norm, &reason) == TextGateDecision::IGNORE, "Suite d'hesitations -> IGNORE");

    // 7. Commandes courtes valides
    TEST_ASSERT(gate.evaluate("oui", &norm, &reason) == TextGateDecision::ACCEPT, "oui -> ACCEPT");
    TEST_ASSERT(gate.evaluate("non", &norm, &reason) == TextGateDecision::ACCEPT, "non -> ACCEPT");
    TEST_ASSERT(gate.evaluate("stop", &norm, &reason) == TextGateDecision::ACCEPT, "stop -> ACCEPT");
    TEST_ASSERT(gate.evaluate("pause", &norm, &reason) == TextGateDecision::ACCEPT, "pause -> ACCEPT");
    TEST_ASSERT(gate.evaluate("répète", &norm, &reason) == TextGateDecision::ACCEPT, "répète -> ACCEPT");

    // 8. Phrases complètes contenant une hésitation (l'hésitation ne doit pas invalider la phrase !)
    TEST_ASSERT(gate.evaluate("Euh quelle heure est-il ?", &norm, &reason) == TextGateDecision::ACCEPT, "Phrase avec hesitation au debut -> ACCEPT");
    TEST_ASSERT(gate.evaluate("Je pense euh que c'est une bonne idée", &norm, &reason) == TextGateDecision::ACCEPT, "Phrase avec hesitation au milieu -> ACCEPT");
    TEST_ASSERT(gate.evaluate("Répète euh s'il te plaît", &norm, &reason) == TextGateDecision::ACCEPT, "Commande avec hesitation interne -> ACCEPT");

    // 9. Énoncés potentiellement incomplets (informatif)
    TEST_ASSERT(gate.evaluate("Je voudrais...", &norm, &reason) == TextGateDecision::INCOMPLETE, "Points de suspension finaux -> INCOMPLETE");
    TEST_ASSERT(gate.evaluate("Il fait beau mais", &norm, &reason) == TextGateDecision::INCOMPLETE, "Dernier mot conjonction 'mais' -> INCOMPLETE");
    TEST_ASSERT(gate.evaluate("Dis-moi si", &norm, &reason) == TextGateDecision::INCOMPLETE, "Dernier mot 'si' -> INCOMPLETE");
    TEST_ASSERT(gate.evaluate("Donne-moi le", &norm, &reason) == TextGateDecision::INCOMPLETE, "Dernier mot 'le' -> INCOMPLETE");

    // 10. Texte UTF-8 français valide
    String frText = "À bientôt, où sont les élèves français ?";
    TEST_ASSERT(gate.evaluate(frText, &norm, &reason) == TextGateDecision::ACCEPT, "UTF-8 francais complexe -> ACCEPT");
    TEST_ASSERT(norm == "a bientot ou sont les eleves francais", "Normalisation sans accent conforme");
}

// =============================================================================
// SUITE 131 : VoiceIntentRouter — Intentions conversationnelles & Faux positifs
// =============================================================================
inline void test_v0641_intent_router_recognition_and_false_positives() {
    std::cout << "\n[TEST SUITE 131] v0.6.4.1 : VoiceIntentRouter (Intentions, Politesse, Casse, Accents & Faux positifs)" << std::endl;

    VoiceIntentRouter router;

    // 1. REPEAT
    TEST_ASSERT(router.route("Répète") == VoiceUxIntent::REPEAT, "Répète -> REPEAT");
    TEST_ASSERT(router.route("repete") == VoiceUxIntent::REPEAT, "repete -> REPEAT");
    TEST_ASSERT(router.route("Redis ta réponse") == VoiceUxIntent::REPEAT, "Redis ta réponse -> REPEAT");
    TEST_ASSERT(router.route("Répète ce que tu viens de dire") == VoiceUxIntent::REPEAT, "Répète ce que tu viens de dire -> REPEAT");
    TEST_ASSERT(router.route("Peux-tu répéter ?") == VoiceUxIntent::REPEAT, "Peux-tu répéter ? -> REPEAT");
    TEST_ASSERT(router.route("Répète s'il te plaît") == VoiceUxIntent::REPEAT, "Répète s'il te plaît -> REPEAT");
    TEST_ASSERT(router.route("S'il vous plaît, répétez la dernière réponse") == VoiceUxIntent::REPEAT, "S'il vous plaît répétez... -> REPEAT");
    TEST_ASSERT(router.route("Dis à nouveau") == VoiceUxIntent::REPEAT, "Dis à nouveau -> REPEAT");
    TEST_ASSERT(router.route("Encore une fois !") == VoiceUxIntent::REPEAT, "Encore une fois -> REPEAT");

    // 2. PREVIOUS
    TEST_ASSERT(router.route("Réponse précédente") == VoiceUxIntent::PREVIOUS, "Réponse précédente -> PREVIOUS");
    TEST_ASSERT(router.route("reponse precedente") == VoiceUxIntent::PREVIOUS, "reponse precedente -> PREVIOUS");
    TEST_ASSERT(router.route("Reviens à ta réponse précédente") == VoiceUxIntent::PREVIOUS, "Reviens à ta réponse précédente -> PREVIOUS");
    TEST_ASSERT(router.route("Quelle était la réponse précédente ?") == VoiceUxIntent::PREVIOUS, "Quelle était la réponse précédente -> PREVIOUS");
    TEST_ASSERT(router.route("La réponse d'avant") == VoiceUxIntent::PREVIOUS, "La réponse d'avant -> PREVIOUS");
    TEST_ASSERT(router.route("Message précédent") == VoiceUxIntent::PREVIOUS, "Message précédent -> PREVIOUS");
    TEST_ASSERT(router.route("Reviens en arrière s'il te plaît") == VoiceUxIntent::PREVIOUS, "Reviens en arrière s'il te plaît -> PREVIOUS");

    // 3. STOP
    TEST_ASSERT(router.route("Arrête") == VoiceUxIntent::STOP, "Arrête -> STOP");
    TEST_ASSERT(router.route("arrete") == VoiceUxIntent::STOP, "arrete -> STOP");
    TEST_ASSERT(router.route("STOP") == VoiceUxIntent::STOP, "STOP -> STOP");
    TEST_ASSERT(router.route("Arrête-toi !") == VoiceUxIntent::STOP, "Arrête-toi ! -> STOP");
    TEST_ASSERT(router.route("Tais-toi") == VoiceUxIntent::STOP, "Tais-toi -> STOP");
    TEST_ASSERT(router.route("Silence") == VoiceUxIntent::STOP, "Silence -> STOP");
    TEST_ASSERT(router.route("Arrête s'il te plaît merci") == VoiceUxIntent::STOP, "Arrête s'il te plaît merci -> STOP");
    TEST_ASSERT(router.route("Annule") == VoiceUxIntent::STOP, "Annule -> STOP");
    TEST_ASSERT(router.route("C'est bon arrête") == VoiceUxIntent::STOP, "C'est bon arrête -> STOP");

    // 4. PAUSE
    TEST_ASSERT(router.route("Mets en pause") == VoiceUxIntent::PAUSE, "Mets en pause -> PAUSE");
    TEST_ASSERT(router.route("Pause") == VoiceUxIntent::PAUSE, "Pause -> PAUSE");
    TEST_ASSERT(router.route("Fais une pause") == VoiceUxIntent::PAUSE, "Fais une pause -> PAUSE");
    TEST_ASSERT(router.route("Patiente") == VoiceUxIntent::PAUSE, "Patiente -> PAUSE");
    TEST_ASSERT(router.route("Attends une minute") == VoiceUxIntent::PAUSE, "Attends une minute -> PAUSE");
    TEST_ASSERT(router.route("Peux-tu faire une pause ?") == VoiceUxIntent::PAUSE, "Peux-tu faire une pause -> PAUSE");

    // 5. RESUME
    TEST_ASSERT(router.route("Continue") == VoiceUxIntent::RESUME, "Continue -> RESUME");
    TEST_ASSERT(router.route("Reprends") == VoiceUxIntent::RESUME, "Reprends -> RESUME");
    TEST_ASSERT(router.route("Reprends la lecture") == VoiceUxIntent::RESUME, "Reprends la lecture -> RESUME");
    TEST_ASSERT(router.route("Poursuis") == VoiceUxIntent::RESUME, "Poursuis -> RESUME");
    TEST_ASSERT(router.route("Relance") == VoiceUxIntent::RESUME, "Relance -> RESUME");
    TEST_ASSERT(router.route("Vas-y continue") == VoiceUxIntent::RESUME, "Vas-y continue -> RESUME");

    // 6. SLOWER
    TEST_ASSERT(router.route("Parle moins vite") == VoiceUxIntent::SLOWER, "Parle moins vite -> SLOWER");
    TEST_ASSERT(router.route("Moins vite") == VoiceUxIntent::SLOWER, "Moins vite -> SLOWER");
    TEST_ASSERT(router.route("Plus lentement") == VoiceUxIntent::SLOWER, "Plus lentement -> SLOWER");
    TEST_ASSERT(router.route("Ralentis") == VoiceUxIntent::SLOWER, "Ralentis -> SLOWER");
    TEST_ASSERT(router.route("Peux-tu parler plus lentement s'il te plaît ?") == VoiceUxIntent::SLOWER, "Peux-tu parler plus lentement... -> SLOWER");

    // 7. REPHRASE
    TEST_ASSERT(router.route("Reformule") == VoiceUxIntent::REPHRASE, "Reformule -> REPHRASE");
    TEST_ASSERT(router.route("Explique autrement") == VoiceUxIntent::REPHRASE, "Explique autrement -> REPHRASE");
    TEST_ASSERT(router.route("Dis-le autrement") == VoiceUxIntent::REPHRASE, "Dis-le autrement -> REPHRASE");
    TEST_ASSERT(router.route("Je n'ai pas compris") == VoiceUxIntent::REPHRASE, "Je n'ai pas compris -> REPHRASE");
    TEST_ASSERT(router.route("Reformule ta réponse") == VoiceUxIntent::REPHRASE, "Reformule ta réponse -> REPHRASE");
    TEST_ASSERT(router.route("Autrement") == VoiceUxIntent::REPHRASE, "Autrement -> REPHRASE");

    // 8. Faux positifs critiques (stricte conformité au cahier des charges)
    TEST_ASSERT(router.route("Pourquoi la voiture s'arrête-t-elle ?") == VoiceUxIntent::NONE, "Faux positif 'voiture s'arrête' -> NONE");
    TEST_ASSERT(router.route("Peux-tu répéter cette expérience scientifique ?") == VoiceUxIntent::NONE, "Faux positif 'répéter cette expérience' -> NONE");
    TEST_ASSERT(router.route("Continue cette équation") == VoiceUxIntent::NONE, "Faux positif 'continue cette équation' -> NONE");
    TEST_ASSERT(router.route("Arrête la minuterie") == VoiceUxIntent::NONE, "Faux positif 'arrête la minuterie' -> NONE");
    TEST_ASSERT(router.route("Arrête de faire du bruit") == VoiceUxIntent::NONE, "Faux positif 'arrête de faire du bruit' -> NONE");
    TEST_ASSERT(router.route("Je veux faire une pause déjeuner") == VoiceUxIntent::NONE, "Faux positif 'pause déjeuner' -> NONE");
    TEST_ASSERT(router.route("C'est une réponse précédente que j'ai lue hier") == VoiceUxIntent::NONE, "Faux positif 'réponse précédente lue hier' -> NONE");

    // 9. Commandes ordinaires et phrases libres
    TEST_ASSERT(router.route("Bonjour ESP") == VoiceUxIntent::NONE, "Bonjour ESP -> NONE");
    TEST_ASSERT(router.route("Quelle heure est-il ?") == VoiceUxIntent::NONE, "Quelle heure est-il -> NONE");
    TEST_ASSERT(router.route("Allume la lumière de la cuisine") == VoiceUxIntent::NONE, "Allume la lumière -> NONE");
    TEST_ASSERT(router.route("Quel temps fera-t-il demain à Marseille ?") == VoiceUxIntent::NONE, "Météo -> NONE");
}

// =============================================================================
// SUITE 132 : ReplyMemory — Cycle de vie, éviction FIFO & intégrité UTF-8
// =============================================================================
inline void test_v0641_reply_memory_lifecycle_and_utf8_truncation() {
    std::cout << "\n[TEST SUITE 132] v0.6.4.1 : ReplyMemory (Capacite 5, FIFO, Recul, Clear, Troncature UTF-8)" << std::endl;

    ReplyMemory mem(5, 128); // Capacité 5, max 128 octets pour les tests

    // 1. Mémoire initialement vide
    TEST_ASSERT(mem.isEmpty(), "Memoire vide au demarrage");
    TEST_ASSERT(mem.getCount() == 0, "Compte initial nul");
    TEST_ASSERT(mem.getCapacity() == 5, "Capacite conforme 5");
    TEST_ASSERT(mem.getLastReply() == "", "Derniere reponse vide");
    TEST_ASSERT(mem.getPreviousReply() == "", "Reponse precedente vide");

    // 2. Rejet des chaînes vides ou espaces
    TEST_ASSERT(!mem.addReply(""), "Rejet chaine vide");
    TEST_ASSERT(!mem.addReply("   \r\n "), "Rejet espaces seuls");
    TEST_ASSERT(mem.getCount() == 0, "Compte reste nul apres rejets");

    // 3. Ajout de 3 réponses consécutives
    TEST_ASSERT(mem.addReply("Reponse 1 : Il fait 20 degres."), "Ajout Reponse 1");
    TEST_ASSERT(mem.getCount() == 1, "Compte = 1");
    TEST_ASSERT(mem.getLastReply() == "Reponse 1 : Il fait 20 degres.", "getLastReply() == R1");
    TEST_ASSERT(mem.getPreviousReply() == "", "getPreviousReply() == vide car 1 seul element");

    TEST_ASSERT(mem.addReply("Reponse 2 : L'humidite est de 45%."), "Ajout Reponse 2");
    TEST_ASSERT(mem.getCount() == 2, "Compte = 2");
    TEST_ASSERT(mem.getLastReply() == "Reponse 2 : L'humidite est de 45%.", "getLastReply() == R2");
    TEST_ASSERT(mem.getPreviousReply() == "Reponse 1 : Il fait 20 degres.", "getPreviousReply() == R1");

    TEST_ASSERT(mem.addReply("Reponse 3 : Vent modere a 15 km/h."), "Ajout Reponse 3");
    TEST_ASSERT(mem.getCount() == 3, "Compte = 3");
    TEST_ASSERT(mem.getLastReply() == "Reponse 3 : Vent modere a 15 km/h.", "getLastReply() == R3");
    TEST_ASSERT(mem.getPreviousReply() == "Reponse 2 : L'humidite est de 45%.", "getPreviousReply() == R2");
    TEST_ASSERT(mem.getReplyAt(2) == "Reponse 1 : Il fait 20 degres.", "getReplyAt(2) == R1");

    // 4. Remplissage jusqu'à saturation (5 réponses)
    mem.addReply("Reponse 4 : Pression 1013 hPa.");
    mem.addReply("Reponse 5 : Indice UV 3.");
    TEST_ASSERT(mem.getCount() == 5, "Capacite atteinte (5)");
    TEST_ASSERT(mem.isFull(), "isFull() == true");
    TEST_ASSERT(mem.getLastReply() == "Reponse 5 : Indice UV 3.", "getLastReply() == R5");
    TEST_ASSERT(mem.getPreviousReply() == "Reponse 4 : Pression 1013 hPa.", "getPreviousReply() == R4");

    // 5. Éviction FIFO de la plus ancienne (R1 doit être évincée lors de l'ajout de R6)
    mem.addReply("Reponse 6 : Nuit etoilee.");
    TEST_ASSERT(mem.getCount() == 5, "Compte maintenu a 5 apres debordement");
    TEST_ASSERT(mem.getLastReply() == "Reponse 6 : Nuit etoilee.", "Derniere reponse = R6");
    TEST_ASSERT(mem.getPreviousReply() == "Reponse 5 : Indice UV 3.", "Precedente = R5");
    TEST_ASSERT(mem.getReplyAt(0) == "Reponse 6 : Nuit etoilee.", "Index 0 = R6");
    TEST_ASSERT(mem.getReplyAt(1) == "Reponse 5 : Indice UV 3.", "Index 1 = R5");
    TEST_ASSERT(mem.getReplyAt(2) == "Reponse 4 : Pression 1013 hPa.", "Index 2 = R4");
    TEST_ASSERT(mem.getReplyAt(3) == "Reponse 3 : Vent modere a 15 km/h.", "Index 3 = R3");
    TEST_ASSERT(mem.getReplyAt(4) == "Reponse 2 : L'humidite est de 45%.", "Index 4 = R2 (R1 a ete evince)");
    TEST_ASSERT(mem.getReplyAt(5) == "", "Index 5 hors bornes -> vide");

    // 6. Troncature et protection UTF-8
    // Phrase longue contenant des accents français
    String longAccented = "Cet été à Paris, les élèves découvrent l'architecture étonnante de Notre-Dame et écoutent un récit extraordinaire sous le soleil brillant.";
    String truncated = ReplyMemory::truncateUtf8(longAccented, 30);
    TEST_ASSERT(truncated.length() <= 30, "Longueur tronquee <= 30 octets");
    TEST_ASSERT(Utf8Utils::isValidUtf8(truncated), "Chaine tronquee strictement conforme RFC 3629 UTF-8");

    // Cas d'un caractère UTF-8 2-octets à la frontière exacte
    // "café" a 5 octets ('c', 'a', 'f', 0xC3, 0xA9)
    String cafe = "café";
    String cut4 = ReplyMemory::truncateUtf8(cafe, 4); // couper au milieu du 'é'
    TEST_ASSERT(cut4 == "caf", "Troncature frontière 2-octets -> 'caf' sans corrompre le 'é'");
    TEST_ASSERT(Utf8Utils::isValidUtf8(cut4), "'caf' est UTF-8 valide");

    // 7. Effacement de la mémoire
    mem.clear();
    TEST_ASSERT(mem.isEmpty(), "Memoire vide apres clear()");
    TEST_ASSERT(mem.getCount() == 0, "Compte nul apres clear()");
    TEST_ASSERT(mem.getLastReply() == "", "getLastReply() vide apres clear()");
}

// =============================================================================
// SUITE 133 : VoiceUxController — Façade, Événements & Neutralité
// =============================================================================
inline void test_v0641_voiceux_controller_facade_and_events() {
    std::cout << "\n[TEST SUITE 133] v0.6.4.1 : VoiceUxController (Façade, Callbacks, Neutralite audio, Statut)" << std::endl;

    VoiceUxController ux;
    TEST_ASSERT(ux.begin(), "ux.begin() reussi");

    // 1. Doit être désactivé par défaut (stricte neutralité pour les projets existants)
    TEST_ASSERT(!ux.isEnabled(), "Desactive par defaut");

    // 2. Traitement d'un texte quand désactivé : pass-through neutre
    VoiceUxResult resDis = ux.processFinalText("Répète");
    TEST_ASSERT(!resDis.handledLocally, "Desactive -> handledLocally = false");
    TEST_ASSERT(resDis.intent == VoiceUxIntent::NONE, "Desactive -> intent = NONE");

    // 3. Activation du module
    ux.setEnabled(true);
    TEST_ASSERT(ux.isEnabled(), "Module active");

    // 4. Enregistrement d'un callback d'événement
    int eventCount = 0;
    VoiceUxResult capturedResult;
    ux.onResult([&](const VoiceUxResult& res) {
        eventCount++;
        capturedResult = res;
    });

    // 5. Test rejet d'une hésitation
    VoiceUxResult resHes = ux.processFinalText("Euh...");
    TEST_ASSERT(resHes.gateDecision == TextGateDecision::IGNORE, "Euh... -> IGNORE");
    TEST_ASSERT(resHes.handledLocally, "Hesitation isolee handledLocally = true");
    TEST_ASSERT(resHes.intent == VoiceUxIntent::NONE, "Hesitation -> intent NONE");
    TEST_ASSERT(eventCount == 1, "Callback emis pour hesitation");

    // 6. Test commande locale REPEAT sans réponse en mémoire
    VoiceUxResult resRepEmpty = ux.processFinalText("Répète");
    TEST_ASSERT(resRepEmpty.gateDecision == TextGateDecision::ACCEPT, "Répète -> ACCEPT");
    TEST_ASSERT(resRepEmpty.intent == VoiceUxIntent::REPEAT, "Répète -> REPEAT");
    TEST_ASSERT(resRepEmpty.handledLocally, "REPEAT handledLocally = true");
    TEST_ASSERT(resRepEmpty.recallText == "", "Memoire vide -> recallText vide");
    TEST_ASSERT(eventCount == 2, "Callback emis pour REPEAT");

    // 7. Mémorisation d'une réponse de l'assistant
    TEST_ASSERT(ux.rememberReply("La température actuelle est de 22 degrés."), "rememberReply reussi");
    TEST_ASSERT(ux.getReplyMemory().getCount() == 1, "1 reponse en memoire");

    // 8. Test REPEAT avec mémoire active
    VoiceUxResult resRepFull = ux.processFinalText("Répète s'il te plaît");
    TEST_ASSERT(resRepFull.intent == VoiceUxIntent::REPEAT, "Répète s'il te plaît -> REPEAT");
    TEST_ASSERT(resRepFull.recallText == "La température actuelle est de 22 degrés.", "recallText contient la reponse");

    // 9. Mémorisation d'une 2e réponse et test PREVIOUS
    ux.rememberReply("L'humidité relative est de 48 pour cent.");
    TEST_ASSERT(ux.getReplyMemory().getCount() == 2, "2 reponses en memoire");

    VoiceUxResult resPrev = ux.processFinalText("Réponse précédente");
    TEST_ASSERT(resPrev.intent == VoiceUxIntent::PREVIOUS, "Réponse précédente -> PREVIOUS");
    TEST_ASSERT(resPrev.recallText == "La température actuelle est de 22 degrés.", "PREVIOUS renvoie l'avant-derniere");

    // 10. Test commande STOP
    VoiceUxResult resStop = ux.processFinalText("Arrête");
    TEST_ASSERT(resStop.intent == VoiceUxIntent::STOP, "Arrête -> STOP");
    TEST_ASSERT(resStop.handledLocally, "STOP -> handledLocally = true");

    // 11. Test phrase ordinaire (sans intention locale)
    VoiceUxResult resNormal = ux.processFinalText("Quelle est la météo pour demain ?");
    TEST_ASSERT(resNormal.gateDecision == TextGateDecision::ACCEPT, "Question normale -> ACCEPT");
    TEST_ASSERT(resNormal.intent == VoiceUxIntent::NONE, "Question normale -> NONE");
    TEST_ASSERT(!resNormal.handledLocally, "handledLocally = false (a envoyer au LLM)");

    // 12. Vérification des statistiques
    const VoiceUxStats& stats = ux.getStats();
    TEST_ASSERT(stats.processedCount >= 6, "Compteur processedCount coherent");
    TEST_ASSERT(stats.ignoredCount == 1, "Compteur ignoredCount = 1");
    TEST_ASSERT(stats.acceptedCount >= 5, "Compteur acceptedCount >= 5");
    TEST_ASSERT(stats.recognizedIntentCount >= 4, "Compteur recognizedIntentCount >= 4");
}

// =============================================================================
// SUITE 134 : VoiceUxController — Performance, Déterminisme & Non-Régression
// =============================================================================
inline void test_v0641_performance_metrics_and_stability() {
    std::cout << "\n[TEST SUITE 134] v0.6.4.1 : Performance (Temps microsecondes, 100 requetes, Stabilite RAM)" << std::endl;

    VoiceUxController ux;
    ux.begin();
    ux.setEnabled(true);

    // Mémoriser quelques réponses
    ux.rememberReply("Reponse 1 sur la physique quantique.");
    ux.rememberReply("Reponse 2 sur les equations differentielles.");
    ux.rememberReply("Reponse 3 sur la chimie organique.");

    // Mesurer le temps d'analyse de commandes représentatives
    uint32_t t0 = micros();
    VoiceUxResult r1 = ux.processFinalText("Répète");
    uint32_t dt1 = micros() - t0;
    TEST_ASSERT(r1.intent == VoiceUxIntent::REPEAT, "Répète reconnu");
    TEST_ASSERT(dt1 < 5000, "Temps REPEAT < 5 ms");

    t0 = micros();
    VoiceUxResult r2 = ux.processFinalText("Pourquoi la voiture s'arrête-t-elle ?");
    uint32_t dt2 = micros() - t0;
    TEST_ASSERT(r2.intent == VoiceUxIntent::NONE, "Faux positif évité");
    TEST_ASSERT(dt2 < 5000, "Temps phrase longue < 5 ms");

    t0 = micros();
    VoiceUxResult r3 = ux.processFinalText("Euh...");
    uint32_t dt3 = micros() - t0;
    TEST_ASSERT(r3.gateDecision == TextGateDecision::IGNORE, "Hésitation ignorée");
    TEST_ASSERT(dt3 < 2000, "Temps hésitation < 2 ms");

    // Stabilité sur 100 requêtes successives variées
    const char* testCorpus[] = {
        "Répète",
        "Arrête",
        "Mets en pause",
        "Continue",
        "Euh...",
        "Pourquoi la voiture s'arrête-t-elle ?",
        "Parle moins vite s'il te plaît",
        "Réponse précédente",
        "Peux-tu répéter cette expérience scientifique ?",
        "Quelle heure est-il ?",
        "Reformule autrement",
        "Arrête la minuterie",
        "Continue cette équation",
        "Hum",
        "Bonjour le monde"
    };
    size_t corpusSize = sizeof(testCorpus) / sizeof(testCorpus[0]);

    ux.resetStats();
    for (int i = 0; i < 100; ++i) {
        const char* phrase = testCorpus[i % corpusSize];
        ux.processFinalText(phrase);
        ux.update();
    }

    const VoiceUxStats& s = ux.getStats();
    TEST_ASSERT(s.processedCount == 100, "100 requetes traitees");
    TEST_ASSERT(s.acceptedCount + s.ignoredCount == 100, "Bilan d'integrite des etats respecte (100 = accepted + ignored)");
    TEST_ASSERT(s.recognizedIntentCount > 0, "Intentions locales reconnues sur le corpus");

    std::cout << "  [METRIQUES REELLES] Latence traitement unitaire : REPEAT=" << dt1 
              << " us, Phrase complexe=" << dt2 
              << " us, Hesitation=" << dt3 << " us" << std::endl;
}

inline void test_v0642_livebridge_stt_and_intents() {
    std::cout << "\n[TEST SUITE 135] v0.6.4.2 : VoiceUxLiveBridge (Transmission STT, Filtrage interim & REPEAT/PREVIOUS/STOP)" << std::endl;

    VoiceUxLiveBridge bridge;
    TEST_ASSERT(bridge.begin(), "Initialisation du pont VoiceUxLiveBridge");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Etat initial = READY");

    // 1. Rejet des transcriptions intermediaires (interim)
    bridge.onListeningStarted();
    bool dispatched = bridge.feedTranscription("Répète", false);
    TEST_ASSERT(!dispatched, "Transcription interim -> aucune action dispatchee");
    TEST_ASSERT(!bridge.hasPendingAction(), "hasPendingAction = false");
    TEST_ASSERT(bridge.getInterimText() == "Répète", "Texte interim enregistre pour l'UI");
    TEST_ASSERT(bridge.getBridgeStats().partialTranscriptionsIgnored == 1, "Compteur de transcriptions partielles incremente");

    // 2. REPEAT avec mémoire vide
    bridge.onListeningFinished();
    dispatched = bridge.feedTranscription("Répète", true);
    TEST_ASSERT(dispatched, "Transcription finale REPEAT -> action dispatchee");
    TEST_ASSERT(bridge.hasPendingAction(), "hasPendingAction = true");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::ACTION_PENDING, "Etat ACTION_PENDING");

    VoiceUxBridgeAction act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::TTS_SPEAK, "Type d'action TTS_SPEAK");
    TEST_ASSERT(act.intent == VoiceUxIntent::REPEAT, "Intention REPEAT");
    TEST_ASSERT(act.isFeedback == true, "Reponse memoire vide marquee isFeedback");
    TEST_ASSERT(act.text == "Aucune réponse à répéter.", "Message de retour sur memoire vide conforme");
    TEST_ASSERT(!bridge.hasPendingAction(), "Action consommee");

    // 3. Remplissage des reponses deterministes de test prescrites
    bridge.seedTestReplies();
    TEST_ASSERT(bridge.getReplyMemory().getCount() == 2, "2 reponses de test enregistrees");

    // 4. REPEAT avec memoire disponible
    bridge.onListeningStarted();
    bridge.onListeningFinished();
    dispatched = bridge.feedTranscription("Répète", true);
    TEST_ASSERT(dispatched, "REPEAT avec memoire dispatche");
    act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::TTS_SPEAK, "Action TTS_SPEAK");
    TEST_ASSERT(act.intent == VoiceUxIntent::REPEAT, "Intention REPEAT");
    TEST_ASSERT(act.isRecall == true, "Marquage isRecall = true");
    TEST_ASSERT(act.text == "L'humidité relative est de quarante-huit pour cent.", "Derniere reponse correctement rappelee");

    // 5. PREVIOUS avec plusieurs reponses
    bridge.onListeningStarted();
    bridge.onListeningFinished();
    dispatched = bridge.feedTranscription("Réponse précédente", true);
    TEST_ASSERT(dispatched, "PREVIOUS dispatche");
    act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::TTS_SPEAK, "Action TTS_SPEAK");
    TEST_ASSERT(act.intent == VoiceUxIntent::PREVIOUS, "Intention PREVIOUS");
    TEST_ASSERT(act.isRecall == true, "Marquage isRecall = true");
    TEST_ASSERT(act.text == "La température actuelle est de vingt-deux degrés.", "Avant-derniere reponse rappelee");

    // 6. PREVIOUS avec une seule reponse
    bridge.clearMemory();
    bridge.rememberReply("Seule et unique réponse.");
    TEST_ASSERT(bridge.getReplyMemory().getCount() == 1, "Une seule reponse en memoire");

    bridge.onListeningStarted();
    bridge.onListeningFinished();
    dispatched = bridge.feedTranscription("Réponse précédente", true);
    TEST_ASSERT(dispatched, "PREVIOUS avec 1 reponse dispatche");
    act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::TTS_SPEAK, "Action TTS_SPEAK");
    TEST_ASSERT(act.isFeedback == true, "Marque isFeedback");
    TEST_ASSERT(act.text == "Aucune réponse précédente disponible.", "Message d'info sur absence de precedente conforme");

    // 7. Commande STOP vocale
    bridge.onListeningStarted();
    bridge.onListeningFinished();
    dispatched = bridge.feedTranscription("Arrête", true);
    TEST_ASSERT(dispatched, "STOP vocal dispatche");
    act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::STOP, "Action STOP");
    TEST_ASSERT(act.intent == VoiceUxIntent::STOP, "Intention STOP");

    // 8. Commandes diagnostiques SLOWER et REPHRASE (pas d'action TTS)
    bridge.onListeningStarted();
    bridge.onListeningFinished();
    dispatched = bridge.feedTranscription("Parle moins vite", true);
    TEST_ASSERT(!dispatched, "SLOWER -> aucune action materielle");
    TEST_ASSERT(!bridge.hasPendingAction(), "hasPendingAction = false");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Retour a READY apres diagnostic");
}

inline void test_v0642_livebridge_pause_resume() {
    std::cout << "\n[TEST SUITE 136] v0.6.4.2 : VoiceUxLiveBridge (Gestion de la pause, commandes en pause et reprise)" << std::endl;

    VoiceUxLiveBridge bridge;
    bridge.begin();

    // 1. Commande vocale PAUSE
    bridge.onListeningStarted();
    bridge.onListeningFinished();
    bool dispatched = bridge.feedTranscription("Mets en pause", true);
    TEST_ASSERT(dispatched, "PAUSE vocale dispatchee");
    VoiceUxBridgeAction act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::PAUSE, "Action PAUSE");

    bridge.setPaused(true);
    TEST_ASSERT(bridge.isPaused(), "Bridge est en pause");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::PAUSED, "Etat = PAUSED");

    // 2. En mode PAUSED : les demandes conversationnelles ordinaires sont ignorees
    bridge.onListeningStarted();
    bridge.onListeningFinished();
    dispatched = bridge.feedTranscription("Quel temps fait-il ?", true);
    TEST_ASSERT(!dispatched, "Demande ordinaire en pause rejetee");
    TEST_ASSERT(!bridge.hasPendingAction(), "Aucune action creee en pause");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::PAUSED, "Reste en PAUSED");

    // 3. En mode PAUSED : la reprise vocale RESUME est autorisee
    bridge.onListeningStarted();
    bridge.onListeningFinished();
    dispatched = bridge.feedTranscription("Continue", true);
    TEST_ASSERT(dispatched, "RESUME vocal en pause autorise");
    act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::RESUME, "Action RESUME");

    bridge.setPaused(false);
    TEST_ASSERT(!bridge.isPaused(), "Bridge n'est plus en pause");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Etat revenu a READY");

    // 4. Reprise par bouton tactile ou serie
    bridge.setPaused(true);
    TEST_ASSERT(bridge.isPaused(), "Pause tactile activee");
    bridge.setPaused(false);
    TEST_ASSERT(!bridge.isPaused(), "Reprise tactile effectuee");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Etat READY");
}

inline void test_v0642_livebridge_concurrency_and_turn_safety() {
    std::cout << "\n[TEST SUITE 137] v0.6.4.2 : VoiceUxLiveBridge (Securite, concurrence, dedoublonnage, evenements tardifs & annulations)" << std::endl;

    VoiceUxLiveBridge bridge;
    bridge.begin();
    bridge.seedTestReplies();

    // 1. Routage d'une phrase ordinaire non-locale (FORWARD_AI)
    bridge.onListeningStarted();
    bridge.onListeningFinished();
    bool dispatched = bridge.feedTranscription("Quelle est la capitale de la France ?", true);
    TEST_ASSERT(dispatched, "Phrase ordinaire traitee");
    VoiceUxBridgeAction act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::FORWARD_AI, "Action FORWARD_AI");
    TEST_ASSERT(act.text == "Quelle est la capitale de la France ?", "Texte original transmis intact");

    // 2. Rejet des hesitations isolees (TextGate IGNORE)
    bridge.onListeningStarted();
    bridge.onListeningFinished();
    dispatched = bridge.feedTranscription("Euh...", true);
    TEST_ASSERT(!dispatched, "Hesitation isolee -> rejet");
    TEST_ASSERT(!bridge.hasPendingAction(), "Pas d'action pour hesitation isolee");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Etat reste READY");

    // 3. Dedoublonnage des transcriptions finales pour un meme tour
    TEST_ASSERT(bridge.onListeningStarted(), "Demarrage capture");
    uint32_t turn = bridge.getActiveTurnId();
    bridge.onListeningFinished();

    dispatched = bridge.feedTranscription("Répète", true, turn);
    TEST_ASSERT(dispatched, "Premiere transcription finale du tour acceptee");
    TEST_ASSERT(bridge.hasPendingAction(), "Action en attente");

    // Deuxieme appel final pour le meme tour
    dispatched = bridge.feedTranscription("Répète", true, turn);
    TEST_ASSERT(!dispatched, "Deuxieme transcription finale du tour rejetee (dedoublonnage)");
    TEST_ASSERT(bridge.getBridgeStats().duplicateTranscriptionsIgnored == 1, "Compteur doublon incremente");

    act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::TTS_SPEAK, "Action consommee");

    // 4. Evenement tardif d'un ancien tour rejete
    dispatched = bridge.feedTranscription("Répète", true, turn - 1);
    TEST_ASSERT(!dispatched, "Evenement tardif turnId anterieur rejete");
    TEST_ASSERT(bridge.getBridgeStats().staleTranscriptionsIgnored == 1, "Compteur stale incremente");

    // 5. STOP prioritaire et annulation de tour
    TEST_ASSERT(bridge.onListeningStarted(), "Nouvelle ecoute");
    uint32_t cancelledTurn = bridge.getActiveTurnId();
    bridge.triggerPriorityStop();
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "STOP prioritaire remet l'etat a READY");

    // Une transcription arrivant pour ce tour annule doit etre rejetee
    dispatched = bridge.feedTranscription("Répète", true, cancelledTurn);
    TEST_ASSERT(!dispatched, "Transcription d'un tour annule rejetee");

    // 6. Annulation explicite d'une action mise en attente
    bridge.feedSimulatedText("Répète");
    TEST_ASSERT(bridge.hasPendingAction(), "Action simulee en attente");
    bridge.cancelPendingAction();
    TEST_ASSERT(!bridge.hasPendingAction(), "Action annulee avec succes");
}

inline void test_v0642_livebridge_state_machine_tts_cycle_and_resilience() {
    std::cout << "\n[TEST SUITE 138] v0.6.4.2 : VoiceUxLiveBridge (Machine a etats, cycle TTS complet, gestion des erreurs & resilience)" << std::endl;

    VoiceUxLiveBridge bridge;
    bridge.begin();
    bridge.seedTestReplies();

    // 1. Cycle complet de la machine a etats
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "1. READY");
    TEST_ASSERT(bridge.canStartListening(), "Peut demarrer ecoute");

    TEST_ASSERT(bridge.onListeningStarted(), "Appui PARLER -> demarrage ecoute");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::LISTENING, "2. LISTENING");
    TEST_ASSERT(!bridge.canStartListening(), "Refus de seconde capture concurrente");

    bridge.onListeningFinished();
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::TRANSCRIBING, "3. TRANSCRIBING");

    bridge.feedTranscription("Répète", true);
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::ACTION_PENDING, "4. ACTION_PENDING");

    VoiceUxBridgeAction act = bridge.consumePendingAction();
    TEST_ASSERT(bridge.canSpeak(), "Peut demarrer restitution TTS");

    bridge.notifySpeechStarted(act.text);
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING, "5. SPEAKING");
    TEST_ASSERT(!bridge.canStartListening(), "Microphone interdit pendant la restitution TTS");

    bridge.notifySpeechFinished(true);
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "6. Retour a READY");
    TEST_ASSERT(bridge.canStartListening(), "Microphone a nouveau autorise");

    // 2. Regle memoire : restitution interrompue non memorisee vs reponse terminee memorisee
    bridge.clearMemory();
    TEST_ASSERT(bridge.getReplyMemory().isEmpty(), "Memoire vide");

    // Simulation reponse interrompue
    bridge.notifySpeechStarted("Phrase qui va être interrompue");
    bridge.triggerPriorityStop(); // Interruption STOP
    TEST_ASSERT(bridge.getReplyMemory().isEmpty(), "Reponse interrompue NON memorisee");
    TEST_ASSERT(bridge.getBridgeStats().ttsCancelledCount >= 1, "Compteur TTS annule incremente");

    // Simulation reponse complete d'un assistant
    bridge.notifySpeechStarted("La lune est un satellite naturel.");
    bridge.notifySpeechFinished(true);
    TEST_ASSERT(bridge.getReplyMemory().getCount() == 1, "Reponse complete bien memorisee");
    TEST_ASSERT(bridge.getReplyMemory().getLastReply() == "La lune est un satellite naturel.", "Contenu exact mémorise");

    // 3. Simulation de perte de connexion et recuperation non bloquante
    bridge.notifyError("Connexion WebSocket perdue (1006)");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::ERROR, "Etat ERROR");
    TEST_ASSERT(bridge.getLastError().indexOf("WebSocket") >= 0, "Message d'erreur sauvegarde");
    TEST_ASSERT(!bridge.hasPendingAction(), "Action pending videe apres erreur");

    bridge.recoverFromError();
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Recuperation -> READY");
    TEST_ASSERT(bridge.getLastError() == "", "Derniere erreur effacee");

    // 4. Verification de non double restitution TTS
    TEST_ASSERT(bridge.getBridgeStats().ttsDispatchedCount > 0, "Statistiques TTS enregistrees");
}

// =============================================================================
// SUITE 139 : VoiceKit v0.6.4.3 — ToolRegistry : Sécurité, Jetons immuables & Robustesse
// =============================================================================
inline void test_v0643_tool_registry_security_and_stabilization() {
    std::cout << "\n[TEST SUITE 139] v0.6.4.3 : ToolRegistry (Immuabilite, Rejeu, Auth, Busy & Capacite)" << std::endl;

    // 1. Confirmation valide
    {
        ToolRegistry reg(16);
        bool rebootDone = false;
        reg.registerTool("system.reboot", "Redemarrer", [&]() {
            rebootDone = true;
            return ToolResult::success("Redemarrage en cours");
        }, ToolSecurityPolicy::SENSITIVE);

        ToolResult rInit = reg.execute("system.reboot");
        TEST_ASSERT(rInit.status == ToolExecutionStatus::CONFIRMATION_REQUIRED, "Confirmation requise");
        TEST_ASSERT(!rInit.confirmationToken.isEmpty(), "Jeton genere");
        TEST_ASSERT(!rebootDone, "Commande non executee avant confirmation");

        ToolResult rConf = reg.confirm(rInit.confirmationToken);
        TEST_ASSERT(rConf.isSuccess(), "Confirmation reussie");
        TEST_ASSERT(rebootDone, "Commande executee apres confirmation");
    }

    // 2. Confirmation expiree
    {
        ToolRegistry reg(16);
        reg.registerTool("system.reboot", "Redemarrer", []() { return ToolResult::success(); }, ToolSecurityPolicy::SENSITIVE);
        ToolResult rInit = reg.execute("system.reboot");
        advanceMockMillis(35000);
        ToolResult rExp = reg.confirm(rInit.confirmationToken);
        TEST_ASSERT(rExp.status == ToolExecutionStatus::PERMISSION_DENIED, "Jeton expire refuse");
        TEST_ASSERT(rExp.error.indexOf("expire") != -1, "Erreur cite expiration");
    }

    // 3. Reutilisation d'un jeton (protection anti-rejeu)
    {
        ToolRegistry reg(16);
        int runs = 0;
        reg.registerTool("system.reboot", "Redemarrer", [&]() { runs++; return ToolResult::success(); }, ToolSecurityPolicy::SENSITIVE);
        ToolResult rInit = reg.execute("system.reboot");
        ToolResult r1 = reg.confirm(rInit.confirmationToken);
        TEST_ASSERT(r1.isSuccess(), "Premier confirm ok");
        TEST_ASSERT(runs == 1, "Execute 1 fois");

        ToolResult r2 = reg.confirm(rInit.confirmationToken);
        TEST_ASSERT(r2.status == ToolExecutionStatus::PERMISSION_DENIED, "Deuxieme confirm refuse");
        TEST_ASSERT(r2.error.indexOf("anti-rejeu") != -1 || r2.error.indexOf("utilise") != -1, "Erreur anti-rejeu");
        TEST_ASSERT(runs == 1, "Non reexecute");
    }

    // 4. Jeton d'une autre commande
    {
        ToolRegistry reg(16);
        reg.registerTool("tool.a", "Tool A", []() { return ToolResult::success(); }, ToolSecurityPolicy::SENSITIVE);
        reg.registerTool("tool.b", "Tool B", []() { return ToolResult::success(); }, ToolSecurityPolicy::SENSITIVE);

        ToolResult rA = reg.execute("tool.a");
        ToolResult rB = reg.execute("tool.b", ToolArgs(), rA.confirmationToken);
        TEST_ASSERT(rB.status == ToolExecutionStatus::PERMISSION_DENIED, "Jeton outil A refuse pour outil B");
        TEST_ASSERT(rB.error.indexOf("non associe") != -1 || rB.error.indexOf("autre outil") != -1, "Message jeton non associe");
    }

    // 5. Modification des arguments apres demande de confirmation
    {
        ToolRegistry reg(16);
        ToolParam p("drive", ToolParamType::STRING, true);
        reg.registerTool("storage.format", "Formatage", [](const ToolArgs& args) {
            return ToolResult::success("Formate " + args.getString("drive"));
        }, {p}, ToolSecurityPolicy::SENSITIVE);

        ToolArgs initialArgs;
        initialArgs.set("drive", "SD");
        ToolResult rInit = reg.execute("storage.format", initialArgs);
        TEST_ASSERT(rInit.status == ToolExecutionStatus::CONFIRMATION_REQUIRED, "Formatage sensible");
        String token = rInit.confirmationToken;

        ToolArgs tamperedArgs;
        tamperedArgs.set("drive", "FLASH_SYSTEM");
        ToolResult rTampered = reg.execute("storage.format", tamperedArgs, token);
        TEST_ASSERT(rTampered.status == ToolExecutionStatus::PERMISSION_DENIED, "Arguments modifies rejetes");
        TEST_ASSERT(rTampered.error.indexOf("Arguments non conformes") != -1, "Message arguments non conformes");

        // Mais confirm(token) avec les arguments initiaux preserves doit reussir !
        ToolResult rLegit = reg.confirm(token);
        TEST_ASSERT(rLegit.isSuccess(), "Confirmation legitime reussie");
        TEST_ASSERT(rLegit.output == "Formate SD", "Arguments initiaux preserves executes");
    }

    // 6. Suppression d'une commande avant confirmation
    {
        ToolRegistry reg(16);
        reg.registerTool("temp.tool", "Outil temporaire", []() { return ToolResult::success(); }, ToolSecurityPolicy::SENSITIVE);
        ToolResult rInit = reg.execute("temp.tool");
        String token = rInit.confirmationToken;

        reg.unregisterTool("temp.tool");
        ToolResult rConf = reg.confirm(token);
        TEST_ASSERT(rConf.status == ToolExecutionStatus::NOT_FOUND, "NOT_FOUND apres unregister");
    }

    // 7. Modification des autorisations entre demande et confirmation
    {
        ToolRegistry reg(16);
        bool allowAction = true;
        reg.setAuthorizationChecker([&](const ToolDefinition&, const ToolArgs&) {
            return allowAction;
        });

        reg.registerTool("net.reset", "Reset", []() { return ToolResult::success(); }, ToolSecurityPolicy::SENSITIVE);
        ToolResult rInit = reg.execute("net.reset");
        String token = rInit.confirmationToken;

        // Revocation de l'autorisation
        allowAction = false;
        ToolResult rConf = reg.confirm(token);
        TEST_ASSERT(rConf.status == ToolExecutionStatus::PERMISSION_DENIED, "Refus apres revocation d'autorisation");
    }

    // 8. Executeur occupe lors de la confirmation
    {
        class BusyMockExecutor : public IToolExecutor {
        public:
            bool busy = true;
            bool cancelled = false;
            ToolResult execute(const ToolDefinition& def, const ToolArgs& args) override {
                return def.handler ? def.handler(args) : ToolResult::success();
            }
            bool isBusy() const override { return busy; }
            void update() override {}
            void cancelDeferred() override { cancelled = true; }
        };

        ToolRegistry reg(16);
        auto mockExec = std::make_shared<BusyMockExecutor>();
        reg.setExecutor(mockExec);

        reg.registerTool("sys.action", "Action", []() { return ToolResult::success("Fini"); }, ToolSecurityPolicy::SENSITIVE);
        ToolResult rInit = reg.execute("sys.action");
        String token = rInit.confirmationToken;

        // Executeur occupe -> confirmation retourne BUSY sans detruire le jeton
        ToolResult rBusy = reg.confirm(token);
        TEST_ASSERT(rBusy.status == ToolExecutionStatus::BUSY, "Statut BUSY remonte");

        // L'executeur redevient disponible -> confirmation doit fonctionner
        mockExec->busy = false;
        ToolResult rRetry = reg.confirm(token);
        TEST_ASSERT(rRetry.isSuccess(), "Confirmation reussie apres liberation de l'executeur");
        TEST_ASSERT(rRetry.output == "Fini", "Output conforme");

        // Verification de cancelDeferred sur custom executor
        reg.cancelPending();
        TEST_ASSERT(mockExec->cancelled, "cancelDeferred appele sur custom executor");
    }

    // 9. Confirmation repetee apres erreur d'execution
    {
        ToolRegistry reg(16);
        int attempts = 0;
        reg.registerTool("hardware.burn", "Burn", [&]() {
            attempts++;
            return ToolResult::executionFailed("Erreur materielle I2C");
        }, ToolSecurityPolicy::SENSITIVE);

        ToolResult rInit = reg.execute("hardware.burn");
        String token = rInit.confirmationToken;

        // Premiere execution -> retourne EXECUTION_FAILED
        ToolResult rExec = reg.confirm(token);
        TEST_ASSERT(rExec.status == ToolExecutionStatus::EXECUTION_FAILED, "Erreur retournee");
        TEST_ASSERT(attempts == 1, "Handler execute 1 fois");

        // Tentative de rejouer le jeton apres erreur -> DOIT ETRE REJETEE (anti-rejeu)
        ToolResult rSecond = reg.confirm(token);
        TEST_ASSERT(rSecond.status == ToolExecutionStatus::PERMISSION_DENIED, "Jeton consomme malgre l'erreur d'execution");
        TEST_ASSERT(attempts == 1, "Handler NON reexecute");
    }

    // 10. File de confirmations bornee (anti-DoS RAM)
    {
        ToolRegistry reg(16);
        reg.setMaxPendingConfirmations(3);
        TEST_ASSERT(reg.getMaxPendingConfirmations() == 3, "Capacite confirmee");
        reg.registerTool("cmd.s", "S", []() { return ToolResult::success(); }, ToolSecurityPolicy::SENSITIVE);

        ToolResult r1 = reg.execute("cmd.s");
        ToolResult r2 = reg.execute("cmd.s");
        ToolResult r3 = reg.execute("cmd.s");
        TEST_ASSERT(r1.status == ToolExecutionStatus::CONFIRMATION_REQUIRED, "Req 1 ok");
        TEST_ASSERT(r2.status == ToolExecutionStatus::CONFIRMATION_REQUIRED, "Req 2 ok");
        TEST_ASSERT(r3.status == ToolExecutionStatus::CONFIRMATION_REQUIRED, "Req 3 ok");

        // 4eme confirmation alors que max = 3 -> BUSY
        ToolResult r4 = reg.execute("cmd.s");
        TEST_ASSERT(r4.status == ToolExecutionStatus::BUSY, "File saturee rejetee avec BUSY");
        TEST_ASSERT(r4.error.indexOf("saturee") != -1 || r4.error.indexOf("Trop de confirmations") != -1, "Message d'erreur saturation");
    }
}

// =============================================================================
// SUITE 140 : VoiceKit v0.6.4.3 — VoiceUxLiveBridge : Cycle TTS, Métadonnées, États & Statuts
// =============================================================================
inline void test_v0643_voiceux_livebridge_stabilization_and_edge_cases() {
    std::cout << "\n[TEST SUITE 140] v0.6.4.3 : VoiceUxLiveBridge (Metadonnees TTS, Deduplication, Refus, STOP & Statuts)" << std::endl;

    VoiceUxLiveBridge bridge;
    bridge.begin();

    // 1. Sequence 1 & 2 : STT -> REPEAT -> TTS -> READY & Reponse repetee non re-enregistree
    {
        bridge.clearMemory();
        bridge.seedTestReplies();
        TEST_ASSERT(bridge.getReplyMemory().getCount() == 2, "2 reponses pre-chargees");
        String initialR0 = bridge.getReplyMemory().getLastReply();

        TEST_ASSERT(bridge.onListeningStarted(), "Demarrage ecoute");
        bridge.onListeningFinished();

        bool ok = bridge.feedTranscription("Répète", true);
        TEST_ASSERT(ok, "Transcription REPEAT dispatchee");
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::ACTION_PENDING, "ACTION_PENDING");

        VoiceUxBridgeAction act = bridge.consumePendingAction();
        TEST_ASSERT(act.type == VoiceUxBridgeActionType::TTS_SPEAK, "Type TTS_SPEAK");
        TEST_ASSERT(act.isRecall == true, "Action marquee isRecall");
        TEST_ASSERT(act.text == initialR0, "Texte conforme");

        // Verification des metadonnees actives dans le pont
        TEST_ASSERT(bridge.getActiveAction().isRecall == true, "getActiveAction().isRecall preserve");

        bridge.notifySpeechStarted(act.text);
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING, "SPEAKING");
        TEST_ASSERT(bridge.isCurrentSpeechRecall() == true, "isCurrentSpeechRecall preserve pendant la restitution");
        TEST_ASSERT(bridge.getCurrentSpokenText() == initialR0, "getCurrentSpokenText preserve");

        bridge.notifySpeechFinished(true);
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Retour a READY");

        // GARANTIE P1 : La memoire conversationnelle N'A PAS ete polluee par la repetition !
        TEST_ASSERT(bridge.getReplyMemory().getCount() == 2, "Compte ReplyMemory inchange (2)");
        TEST_ASSERT(bridge.getReplyMemory().getLastReply() == initialR0, "Derniere reponse inchangee");
    }

    // 2. Sequence 3 : Feedback systeme sans pollution de l'historique
    {
        bridge.clearMemory();
        TEST_ASSERT(bridge.getReplyMemory().isEmpty(), "Memoire vide");

        bridge.feedSimulatedText("Répète");
        VoiceUxBridgeAction act = bridge.consumePendingAction();
        TEST_ASSERT(act.isFeedback == true, "Action marquee isFeedback");
        TEST_ASSERT(act.text == "Aucune réponse à répéter.", "Message de feedback");

        bridge.notifySpeechStarted(act.text);
        TEST_ASSERT(bridge.isCurrentSpeechFeedback() == true, "isCurrentSpeechFeedback actif");

        bridge.notifySpeechFinished(true);
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "READY apres feedback");
        TEST_ASSERT(bridge.getReplyMemory().isEmpty(), "Memoire toujours vide apres feedback systeme");
    }

    // 3. Sequence 4 : STOP a chaque etape sans blocage
    {
        // STOP pendant LISTENING
        bridge.onListeningStarted();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::LISTENING, "LISTENING");
        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "READY apres STOP en LISTENING");
        TEST_ASSERT(bridge.canStartListening(), "Peut ecouter");

        // STOP pendant TRANSCRIBING
        bridge.onListeningStarted();
        bridge.onListeningFinished();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::TRANSCRIBING, "TRANSCRIBING");
        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "READY apres STOP en TRANSCRIBING");
        TEST_ASSERT(bridge.canStartListening(), "Peut ecouter");

        // STOP pendant ACTION_PENDING
        bridge.feedSimulatedText("Quelle heure est-il ?");
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::ACTION_PENDING, "ACTION_PENDING");
        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "READY apres STOP en ACTION_PENDING");
        TEST_ASSERT(!bridge.hasPendingAction(), "Action annulee");
    }

    // 4. Sequence 5 & 10 : STT -> Erreur reseau -> Recuperation & Nouvelle ecoute
    {
        bridge.onListeningStarted();
        bridge.notifyError("Connexion WebSocket terminee inattendue (1006)");
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::ERROR, "ERROR");
        TEST_ASSERT(!bridge.canStartListening(), "Ecoute interdite en ERROR");

        bridge.recoverFromError();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "READY apres recuperation");
        TEST_ASSERT(bridge.canStartListening(), "Ecoute re-autorisee");

        // Nouvelle ecoute immediate sans residu d'erreur
        TEST_ASSERT(bridge.onListeningStarted(), "Nouvelle ecoute reussie");
        bridge.onListeningFinished();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::TRANSCRIBING, "TRANSCRIBING");
        bridge.triggerPriorityStop();
    }

    // 5. Sequence 6 : TTS refuse (say() echoue) -> Recuperation deterministe
    {
        bridge.feedSimulatedText("Bonjour");
        VoiceUxBridgeAction act = bridge.consumePendingAction();

        // Simulation refus du moteur vocal
        bridge.notifySpeechRefused("say() refuse : buffer DAC plein");
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Retour a READY apres refus TTS");
        TEST_ASSERT(bridge.canStartListening(), "Microphone accessible apres refus TTS");
    }

    // 6. Sequence 7 : STOP pendant restitution TTS
    {
        bridge.clearMemory();
        bridge.notifySpeechStarted("Reponse longue qui sera interrompue par l'utilisateur.");
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING, "SPEAKING");

        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "READY apres STOP pendant SPEAKING");
        TEST_ASSERT(bridge.getReplyMemory().getLastReply() != "Reponse longue qui sera interrompue par l'utilisateur.", "Interrompue non memorisee");
        TEST_ASSERT(bridge.canStartListening(), "Micro disponible");
    }

    // 7. Sequence 8 : Deux transcriptions finales identiques pour un meme tour
    {
        bridge.onListeningStarted();
        uint32_t tid = bridge.getActiveTurnId();
        bridge.onListeningFinished();

        TEST_ASSERT(bridge.feedTranscription("Répète", true, tid), "Premiere transcription acceptee");
        TEST_ASSERT(!bridge.feedTranscription("Répète", true, tid), "Deuxieme transcription finale dupliquee rejetee");
        bridge.consumePendingAction();
    }

    // 8. Sequence 9 : Ancienne transcription recue apres annulation
    {
        bridge.onListeningStarted();
        uint32_t tCancelled = bridge.getActiveTurnId();
        bridge.triggerPriorityStop();

        // Transcription arrivant avec l'ancien turnId annule
        TEST_ASSERT(!bridge.feedTranscription("Répète", true, tCancelled), "Transcription tardive de tour annule rejetee");
    }

    // 9. Sequence 11 : Suivi fin des statuts de l'action
    {
        std::vector<VoiceUxActionStatus> statuses;
        bridge.onActionStatus([&](const VoiceUxBridgeAction&, VoiceUxActionStatus st) {
            statuses.push_back(st);
        });

        bridge.feedSimulatedText("Répète");
        TEST_ASSERT(!statuses.empty() && statuses.back() == VoiceUxActionStatus::CREATED, "Status CREATED");

        VoiceUxBridgeAction act = bridge.consumePendingAction();
        TEST_ASSERT(statuses.back() == VoiceUxActionStatus::DISPATCHED, "Status DISPATCHED");

        bridge.notifySpeechStarted(act.text);
        TEST_ASSERT(statuses.back() == VoiceUxActionStatus::ACCEPTED, "Status ACCEPTED");

        bridge.notifySpeechFinished(true);
        TEST_ASSERT(statuses.back() == VoiceUxActionStatus::COMPLETED, "Status COMPLETED");
    }
}

// =============================================================================
// SUITE 141 : VoiceKit v0.6.4.3.1 — Sécurité Événementielle Corrective
//             (Scénarios A, B, C, D, E & STOP multi-états)
// =============================================================================
inline void test_v06431_corrective_event_safety() {
    std::cout << "\n[TEST SUITE 141] v0.6.4.3.1 : Securite Evenementielle Corrective (Scenarios A, B, C, D, E & STOP)" << std::endl;

    // -------------------------------------------------------------------------
    // Scénario A : STOP puis transcription tardive (sans identifiant & avec identifiant)
    // -------------------------------------------------------------------------
    {
        VoiceUxLiveBridge bridge;
        bridge.begin();
        bridge.seedTestReplies();

        // A.1. Sans identifiant (turnId == 0)
        TEST_ASSERT(bridge.onListeningStarted(), "A1.1 Demarrage ecoute");
        bridge.onListeningFinished();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::TRANSCRIBING, "A1.2 Etat TRANSCRIBING");

        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "A1.3 STOP remet a READY");

        // Injection d'une transcription finale tardive sans turnId (turnId == 0)
        bool lateDispatched = bridge.feedTranscription("Répète", true);
        TEST_ASSERT(!lateDispatched, "A1.4 Transcription tardive sans turnId rejetee apres STOP");
        TEST_ASSERT(!bridge.hasPendingAction(), "A1.5 Aucune action creee pour transcription tardive");
        TEST_ASSERT(bridge.getBridgeStats().staleTranscriptionsIgnored >= 1, "A1.6 Compteur stale incremente");

        // Nouvelle écoute autorisée et opérationnelle
        TEST_ASSERT(bridge.onListeningStarted(), "A1.7 Nouvelle ecoute autorisee apres STOP");
        bridge.onListeningFinished();
        bool newDispatched = bridge.feedTranscription("Répète", true);
        TEST_ASSERT(newDispatched, "A1.8 Nouvelle ecoute traite la transcription normalement");
        TEST_ASSERT(bridge.hasPendingAction(), "A1.9 Action pending valide disponible");
        bridge.consumePendingAction();

        // A.2. Avec identifiant explicite de l'ancien tour
        TEST_ASSERT(bridge.onListeningStarted(), "A2.1 Demarrage ecoute avec turnId");
        uint32_t oldTurn = bridge.getActiveTurnId();
        bridge.onListeningFinished();
        bridge.triggerPriorityStop();

        lateDispatched = bridge.feedTranscription("Répète", true, oldTurn);
        TEST_ASSERT(!lateDispatched, "A2.2 Transcription tardive avec oldTurn rejetee");
        TEST_ASSERT(!bridge.hasPendingAction(), "A2.3 Aucune action creee");

        TEST_ASSERT(bridge.onListeningStarted(), "A2.4 Nouvelle capture");
        uint32_t newTurn = bridge.getActiveTurnId();
        TEST_ASSERT(newTurn > oldTurn, "A2.5 Nouveau turnId superieur a l'ancien");
        bridge.onListeningFinished();
        newDispatched = bridge.feedTranscription("Répète", true, newTurn);
        TEST_ASSERT(newDispatched, "A2.6 Nouvelle ecoute avec newTurn acceptee");
        bridge.consumePendingAction();
    }

    // -------------------------------------------------------------------------
    // Scénario B : Double transcription pour un même tour
    // -------------------------------------------------------------------------
    {
        VoiceUxLiveBridge bridge;
        bridge.begin();
        bridge.seedTestReplies();

        TEST_ASSERT(bridge.onListeningStarted(), "B.1 Demarrage ecoute");
        bridge.onListeningFinished();

        bool first = bridge.feedTranscription("Répète", true);
        TEST_ASSERT(first, "B.2 Premiere transcription finale acceptee");
        TEST_ASSERT(bridge.hasPendingAction(), "B.3 Action en attente presente");

        // Deuxième transcription finale identique reçue pour le même tour
        bool second = bridge.feedTranscription("Répète", true);
        TEST_ASSERT(!second, "B.4 Deuxieme transcription finale rejetee strictement");
        TEST_ASSERT(bridge.getBridgeStats().duplicateTranscriptionsIgnored >= 1, "B.5 Compteur doublons incremente");

        VoiceUxBridgeAction act = bridge.consumePendingAction();
        TEST_ASSERT(act.type == VoiceUxBridgeActionType::TTS_SPEAK, "B.6 Action unique TTS_SPEAK");
        TEST_ASSERT(!bridge.hasPendingAction(), "B.7 Aucune deuxieme action creee");
    }

    // -------------------------------------------------------------------------
    // Scénario C : Ancien événement audio TTS après STOP et nouvelle restitution
    // -------------------------------------------------------------------------
    {
        VoiceUxLiveBridge bridge;
        bridge.begin();

        // 1. Démarrer un premier TTS
        bridge.feedSimulatedText("Première réponse vocale");
        VoiceUxBridgeAction act1 = bridge.consumePendingAction();
        bridge.notifySpeechStarted(act1.text);
        uint32_t ttsId1 = bridge.getCurrentSpeechId();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING, "C.1 TTS 1 en cours");
        TEST_ASSERT(ttsId1 > 0, "C.2 ttsId1 genere");

        // 2. Interrompre avec STOP
        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "C.3 READY apres STOP");
        TEST_ASSERT(bridge.getReplyMemory().getLastReply() != "Première réponse vocale", "C.4 Reponse interrompue NON memorisee");

        // 3. Démarrer une nouvelle restitution TTS
        bridge.feedSimulatedText("Deuxième réponse vocale");
        VoiceUxBridgeAction act2 = bridge.consumePendingAction();
        bridge.notifySpeechStarted(act2.text);
        uint32_t ttsId2 = bridge.getCurrentSpeechId();
        TEST_ASSERT(ttsId2 > ttsId1, "C.5 ttsId2 distinct et superieur");
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING, "C.6 TTS 2 en cours");
        TEST_ASSERT(bridge.getCurrentSpokenText() == "Deuxième réponse vocale", "C.7 Texte courant = TTS 2");

        // 4. Simuler une notification tardive de fin du premier TTS
        bridge.notifySpeechFinished(true, ttsId1);

        // 5. Vérifier que la nouvelle restitution reste active
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING, "C.8 TTS 2 reste SPEAKING apres ancien evenement");
        TEST_ASSERT(bridge.getCurrentSpeechId() == ttsId2, "C.9 speechId actif reste ttsId2");
        TEST_ASSERT(bridge.getCurrentSpokenText() == "Deuxième réponse vocale", "C.10 Texte reste TTS 2");

        // 6. Terminer réellement la nouvelle restitution
        bridge.notifySpeechFinished(true, ttsId2);
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "C.11 READY apres fin reelle TTS 2");
        TEST_ASSERT(bridge.getReplyMemory().getLastReply() == "Deuxième réponse vocale", "C.12 TTS 2 complete bien memorisee");
    }

    // -------------------------------------------------------------------------
    // Scénario D : Commande sensible remplacée dans ToolRegistry
    // -------------------------------------------------------------------------
    {
        ToolRegistry reg(16);
        int runsOld = 0;
        int runsNew = 0;

        // 1. Enregistrer un outil sensible initial
        reg.registerTool("door.unlock", "Deverrouiller porte ancienne", [&]() {
            runsOld++;
            return ToolResult::success("Ancienne porte ouverte");
        }, ToolSecurityPolicy::SENSITIVE);

        // 2. Demander une confirmation
        ToolResult rInit = reg.execute("door.unlock");
        TEST_ASSERT(rInit.status == ToolExecutionStatus::CONFIRMATION_REQUIRED, "D.1 Confirmation requise");
        String oldToken = rInit.confirmationToken;
        TEST_ASSERT(!oldToken.isEmpty(), "D.2 Jeton emis");

        // 3. Supprimer cet outil
        bool unreg = reg.unregisterTool("door.unlock");
        TEST_ASSERT(unreg, "D.3 Outil supprime");

        // Confirmation immédiate après suppression -> NOT_FOUND
        ToolResult rUnregConf = reg.confirm(oldToken);
        TEST_ASSERT(rUnregConf.status == ToolExecutionStatus::NOT_FOUND, "D.4 Outil supprime -> NOT_FOUND");

        // 4. Réenregistrer un autre outil sous le même identifiant
        bool reReg = reg.registerTool("door.unlock", "Nouvelle serrure securisee", [&]() {
            runsNew++;
            return ToolResult::success("Nouvelle serrure deverrouillee");
        }, ToolSecurityPolicy::SENSITIVE);
        TEST_ASSERT(reReg, "D.5 Nouvel outil reenregistre sous le meme ID");

        // 5. Tenter d'utiliser l'ancien jeton sur la nouvelle définition
        ToolResult rStale = reg.confirm(oldToken);

        // 6. Vérifier le refus absolu (PERMISSION_DENIED car commande réenregistrée)
        TEST_ASSERT(rStale.status == ToolExecutionStatus::PERMISSION_DENIED, "D.6 Ancien jeton refuse sur nouvelle definition");
        TEST_ASSERT(runsOld == 0, "D.7 Ancien handler non execute");
        TEST_ASSERT(runsNew == 0, "D.8 Nouveau handler non execute par l'ancien jeton");

        // 7. Vérifier qu'une nouvelle confirmation fonctionne parfaitement
        ToolResult rNewReq = reg.execute("door.unlock");
        TEST_ASSERT(rNewReq.status == ToolExecutionStatus::CONFIRMATION_REQUIRED, "D.9 Nouvelle demande requiert confirmation");
        String newToken = rNewReq.confirmationToken;
        TEST_ASSERT(newToken != oldToken, "D.10 Nouveau jeton unique et different");

        ToolResult rNewConf = reg.confirm(newToken);
        TEST_ASSERT(rNewConf.isSuccess(), "D.11 Nouvelle confirmation reussie");
        TEST_ASSERT(rNewConf.output == "Nouvelle serrure deverrouillee", "D.12 Sortie conforme du nouvel outil");
        TEST_ASSERT(runsNew == 1, "D.13 Nouveau handler execute exactement 1 fois");
    }

    // -------------------------------------------------------------------------
    // Scénario E : Cohérence des statuts d'action et cycle de vie STOP
    // -------------------------------------------------------------------------
    {
        VoiceUxLiveBridge bridge;
        bridge.begin();
        bridge.seedTestReplies();

        // E.1. Transition complète nominale : CREATED -> DISPATCHED -> ACCEPTED -> COMPLETED
        {
            std::vector<VoiceUxActionStatus> statuses;
            bridge.onActionStatus([&](const VoiceUxBridgeAction&, VoiceUxActionStatus st) {
                statuses.push_back(st);
            });

            bridge.feedSimulatedText("Répète");
            TEST_ASSERT(statuses.size() == 1 && statuses[0] == VoiceUxActionStatus::CREATED, "E1.1 Status CREATED");

            VoiceUxBridgeAction act = bridge.consumePendingAction();
            TEST_ASSERT(statuses.size() == 2 && statuses[1] == VoiceUxActionStatus::DISPATCHED, "E1.2 Status DISPATCHED");

            bridge.notifySpeechStarted(act.text);
            TEST_ASSERT(statuses.size() == 3 && statuses[2] == VoiceUxActionStatus::ACCEPTED, "E1.3 Status ACCEPTED");

            bridge.notifySpeechFinished(true);
            TEST_ASSERT(statuses.size() == 4 && statuses[3] == VoiceUxActionStatus::COMPLETED, "E1.4 Status COMPLETED");
        }

        // E.2. Action refusée : CREATED -> DISPATCHED -> REJECTED
        {
            std::vector<VoiceUxActionStatus> statuses;
            bridge.onActionStatus([&](const VoiceUxBridgeAction&, VoiceUxActionStatus st) {
                statuses.push_back(st);
            });

            bridge.feedSimulatedText("Quelle est la météo ?");
            VoiceUxBridgeAction act = bridge.consumePendingAction();
            TEST_ASSERT(act.type == VoiceUxBridgeActionType::FORWARD_AI, "E2.1 Type FORWARD_AI");

            // Dans v0.6.4.3.1, l'action non transmise à l'IA reçoit REJECTED (non COMPLETED)
            bridge.notifyActionRejected("Transmission IA differee");
            TEST_ASSERT(statuses.size() == 3 && statuses[2] == VoiceUxActionStatus::REJECTED, "E2.2 Status REJECTED pour action non executee");
        }

        // E.3. Action annulée par STOP : CREATED -> DISPATCHED -> CANCELLED
        {
            std::vector<VoiceUxActionStatus> statuses;
            bridge.onActionStatus([&](const VoiceUxBridgeAction&, VoiceUxActionStatus st) {
                statuses.push_back(st);
            });

            bridge.feedSimulatedText("Répète");
            VoiceUxBridgeAction act = bridge.consumePendingAction();
            bridge.notifySpeechStarted(act.text);
            bridge.triggerPriorityStop(); // STOP pendant la restitution
            TEST_ASSERT(statuses.back() == VoiceUxActionStatus::CANCELLED, "E3.1 Status CANCELLED pour restitution interrompue");
        }

        // E.4. Commande vocale STOP elle-même : Statut de succès COMPLETED garanti
        {
            VoiceUxLiveBridge stopBridge;
            stopBridge.begin();

            std::vector<VoiceUxActionStatus> statuses;
            stopBridge.onActionStatus([&](const VoiceUxBridgeAction&, VoiceUxActionStatus st) {
                statuses.push_back(st);
            });

            stopBridge.onListeningStarted();
            stopBridge.onListeningFinished();
            stopBridge.feedTranscription("Arrête", true);
            TEST_ASSERT(statuses.size() == 1 && statuses[0] == VoiceUxActionStatus::CREATED, "E4.1 STOP CREATED");

            VoiceUxBridgeAction act = stopBridge.consumePendingAction();
            TEST_ASSERT(act.type == VoiceUxBridgeActionType::STOP, "E4.2 Action STOP");
            TEST_ASSERT(statuses.size() == 2 && statuses[1] == VoiceUxActionStatus::DISPATCHED, "E4.3 STOP DISPATCHED");

            // Execution de STOP par la boucle applicative (comme dans main.cpp)
            stopBridge.triggerPriorityStop();
            stopBridge.notifyActionCompleted();

            TEST_ASSERT(statuses.back() == VoiceUxActionStatus::COMPLETED, "E4.4 Commande STOP executee avec succes = COMPLETED");
            TEST_ASSERT(stopBridge.getState() == VoiceUxBridgeState::READY, "E4.5 Etat READY apres commande STOP");
        }
    }

    // -------------------------------------------------------------------------
    // Scénario F : STOP dans les 7 états de la machine et réarmement
    // -------------------------------------------------------------------------
    {
        VoiceUxLiveBridge bridge;
        bridge.begin();
        bridge.seedTestReplies();

        // 1. STOP pendant READY
        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "F.1 READY apres STOP en READY");
        TEST_ASSERT(bridge.canStartListening(), "F.1bis Ecoute possible");

        // 2. STOP pendant LISTENING
        bridge.onListeningStarted();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::LISTENING, "F.2 LISTENING actif");
        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "F.2 READY apres STOP en LISTENING");
        TEST_ASSERT(bridge.canStartListening(), "F.2bis Ecoute possible");

        // 3. STOP pendant TRANSCRIBING
        bridge.onListeningStarted();
        bridge.onListeningFinished();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::TRANSCRIBING, "F.3 TRANSCRIBING actif");
        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "F.3 READY apres STOP en TRANSCRIBING");
        TEST_ASSERT(bridge.canStartListening(), "F.3bis Ecoute possible");

        // 4. STOP pendant ACTION_PENDING
        bridge.feedSimulatedText("Répète");
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::ACTION_PENDING, "F.4 ACTION_PENDING actif");
        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "F.4 READY apres STOP en ACTION_PENDING");
        TEST_ASSERT(!bridge.hasPendingAction(), "F.4 Action annulee");
        TEST_ASSERT(bridge.canStartListening(), "F.4bis Ecoute possible");

        // 5. STOP pendant SPEAKING
        bridge.feedSimulatedText("Répète");
        VoiceUxBridgeAction act = bridge.consumePendingAction();
        bridge.notifySpeechStarted(act.text);
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING, "F.5 SPEAKING actif");
        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "F.5 READY apres STOP en SPEAKING");
        TEST_ASSERT(bridge.canStartListening(), "F.5bis Ecoute possible");

        // 6. STOP pendant PAUSED
        bridge.setPaused(true);
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::PAUSED, "F.6 PAUSED actif");
        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::PAUSED, "F.6 Reste PAUSED apres STOP");
        bridge.setPaused(false);
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "F.6 Sortie de pause");

        // 7. STOP pendant une ERREUR récupérable
        bridge.notifyError("Erreur temporaire de transport");
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::ERROR, "F.7 ERROR actif");
        bridge.recoverFromError();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "F.7 READY apres recoverFromError()");
        TEST_ASSERT(bridge.canStartListening(), "F.7bis Ecoute possible apres recuperation");

        // Validation finale : nouvelle interaction complète après la série de STOPs
        TEST_ASSERT(bridge.onListeningStarted(), "F.8 Nouvelle interaction post-STOP demarree");
        bridge.onListeningFinished();
        TEST_ASSERT(bridge.feedTranscription("Répète", true), "F.8 Transcription traitee");
        VoiceUxBridgeAction finalAct = bridge.consumePendingAction();
        bridge.notifySpeechStarted(finalAct.text);
        bridge.notifySpeechFinished(true);
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "F.8 Interaction terminee avec succes");
    }
}

// =============================================================================
// TEST SUITE 142 : VoiceKit v0.6.4.3.2 — Corrélation Réelle des Événements Asynchrones
// =============================================================================
inline void test_v06432_corrective_event_correlation() {
    std::cout << "\n[TEST SUITE 142] v0.6.4.3.2 : Correlation Reelle des Evenements Asynchrones (Tests A, B, C, D, E & F)" << std::endl;

    // -------------------------------------------------------------------------
    // Test A — Ancienne transcription après nouvelle capture
    // -------------------------------------------------------------------------
    {
        VoiceUxLiveBridge bridge;
        bridge.begin();
        bridge.seedTestReplies();

        // 1. Démarrer le tour A
        uint32_t turnA = 10;
        TEST_ASSERT(bridge.onListeningStarted(turnA), "Test A.1 Demarrage tour A");
        TEST_ASSERT(bridge.getActiveTurnId() == turnA, "Test A.1 turnId actif = turnA (10)");
        TEST_ASSERT(bridge.getSttPhase() == VoiceUxSttPhase::LISTENING, "Test A.1 Phase = LISTENING");

        // 2. Terminer ou annuler le tour A
        bridge.onListeningFinished();
        TEST_ASSERT(bridge.getSttPhase() == VoiceUxSttPhase::TRANSCRIBING, "Test A.2 Phase = TRANSCRIBING");
        bridge.triggerPriorityStop(); // Annulation explicite (STOP) du tour A
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Test A.2 READY apres annulation tour A");

        // 3. Démarrer le tour B (nouvelle capture)
        uint32_t turnB = 11;
        TEST_ASSERT(bridge.onListeningStarted(turnB), "Test A.3 Demarrage tour B");
        TEST_ASSERT(bridge.getActiveTurnId() == turnB, "Test A.3 turnId actif = turnB (11)");
        bridge.onListeningFinished();
        TEST_ASSERT(bridge.getSttPhase() == VoiceUxSttPhase::TRANSCRIBING, "Test A.3 Phase B = TRANSCRIBING");

        // 4. Émettre tardivement une transcription du tour A
        // 5. Vérifier que cette transcription conserve son origine A (turnId == turnA == 10)
        uint32_t staleCountBefore = bridge.getBridgeStats().staleTranscriptionsIgnored;
        bool resA = bridge.feedTranscription("Répète", true, turnA);

        // 6. Vérifier son rejet
        TEST_ASSERT(!resA, "Test A.6 Transcription retardee tour A rejetee strictement");
        TEST_ASSERT(!bridge.hasPendingAction(), "Test A.6 Aucune action creee pour l'ancienne transcription");
        TEST_ASSERT(bridge.getBridgeStats().staleTranscriptionsIgnored == staleCountBefore + 1, "Test A.6 Compteur stale incremente");

        // 7. Émettre une transcription valide du tour B (turnId == turnB == 11)
        bool resB = bridge.feedTranscription("Répète", true, turnB);

        // 8. Vérifier son acceptation
        TEST_ASSERT(resB, "Test A.8 Transcription valide tour B acceptee");
        TEST_ASSERT(bridge.hasPendingAction(), "Test A.8 Action pending creee pour le tour B");
        TEST_ASSERT(bridge.getSttPhase() == VoiceUxSttPhase::COMPLETED, "Test A.8 Phase = COMPLETED");
        VoiceUxBridgeAction actB = bridge.consumePendingAction();
        TEST_ASSERT(actB.intent == VoiceUxIntent::REPEAT, "Test A.8 Intention REPEAT du tour B conforme");
    }

    // -------------------------------------------------------------------------
    // Test B — Ancien événement TTS après nouveau TTS
    // -------------------------------------------------------------------------
    {
        VoiceUxLiveBridge bridge;
        bridge.begin();

        // 1. Démarrer TTS A
        uint32_t ttsA_id = 20;
        bridge.feedSimulatedText("Première restitution vocale A");
        VoiceUxBridgeAction actA = bridge.consumePendingAction();
        bridge.notifySpeechStarted(actA.text, ttsA_id);
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING, "Test B.1 TTS A SPEAKING");
        TEST_ASSERT(bridge.getCurrentSpeechId() == ttsA_id, "Test B.1 Speech ID = 20");

        // 2. Interrompre TTS A par STOP
        bridge.triggerPriorityStop();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Test B.2 READY apres STOP");
        TEST_ASSERT(!bridge.isCurrentSpeechRecall(), "Test B.2 Non recall");

        // 3. Démarrer TTS B
        uint32_t ttsB_id = 21;
        bridge.feedSimulatedText("Deuxième restitution vocale B");
        VoiceUxBridgeAction actB = bridge.consumePendingAction();
        bridge.notifySpeechStarted(actB.text, ttsB_id);
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING, "Test B.3 TTS B SPEAKING");
        TEST_ASSERT(bridge.getCurrentSpeechId() == ttsB_id, "Test B.3 Speech ID = 21");
        TEST_ASSERT(bridge.getCurrentSpokenText() == "Deuxième restitution vocale B", "Test B.3 Texte = TTS B");

        // 4. Émettre une fin retardée du TTS A (id=20)
        bridge.notifySpeechFinished(true, ttsA_id);

        // 5. Vérifier que TTS B reste actif
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING, "Test B.5 TTS B reste actif malgre fin retardee A");
        TEST_ASSERT(bridge.getCurrentSpeechId() == ttsB_id, "Test B.5 Speech ID toujours 21");
        TEST_ASSERT(bridge.getCurrentSpokenText() == "Deuxième restitution vocale B", "Test B.5 Texte toujours TTS B");

        // 6. Terminer TTS B (id=21)
        bridge.notifySpeechFinished(true, ttsB_id);

        // 7. Vérifier une unique terminaison correcte
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Test B.7 READY apres fin reelle TTS B");
        TEST_ASSERT(bridge.getReplyMemory().getLastReply() == "Deuxième restitution vocale B", "Test B.7 TTS B memorise");
    }

    // -------------------------------------------------------------------------
    // Test C — Transcription sans capture
    // -------------------------------------------------------------------------
    {
        VoiceUxLiveBridge bridge;
        // 1. Initialiser VoiceUxLiveBridge
        TEST_ASSERT(bridge.begin(), "Test C.1 Initialisation bridge");
        TEST_ASSERT(bridge.getSttPhase() == VoiceUxSttPhase::IDLE, "Test C.1 Phase initiale = IDLE");

        // 2. Injecter une transcription réelle finale sans démarrer une capture
        uint32_t staleBefore = bridge.getBridgeStats().staleTranscriptionsIgnored;
        bool resDirect = bridge.feedTranscription("Répète", true);

        // 3. Vérifier son rejet
        TEST_ASSERT(!resDirect, "Test C.3 Transcription reelle sans capture rejetee strictement");
        TEST_ASSERT(!bridge.hasPendingAction(), "Test C.3 Aucune action creee sans capture");
        TEST_ASSERT(bridge.getBridgeStats().staleTranscriptionsIgnored == staleBefore + 1, "Test C.3 Compteur stale incremente");

        // 4. Démarrer une capture
        TEST_ASSERT(bridge.onListeningStarted(), "Test C.4 Demarrage capture");
        TEST_ASSERT(bridge.getSttPhase() == VoiceUxSttPhase::LISTENING, "Test C.4 Phase = LISTENING");

        // 5. Terminer correctement la capture
        bridge.onListeningFinished();
        TEST_ASSERT(bridge.getSttPhase() == VoiceUxSttPhase::TRANSCRIBING, "Test C.5 Phase = TRANSCRIBING");

        // 6. Injecter la transcription finale légitime
        bridge.seedTestReplies();
        bool resValid = bridge.feedTranscription("Répète", true, bridge.getActiveTurnId());

        // 7. Vérifier son acceptation
        TEST_ASSERT(resValid, "Test C.7 Transcription legitime acceptee");
        TEST_ASSERT(bridge.hasPendingAction(), "Test C.7 Action disponible");
        TEST_ASSERT(bridge.getSttPhase() == VoiceUxSttPhase::COMPLETED, "Test C.7 Phase = COMPLETED");
        bridge.consumePendingAction();
    }

    // -------------------------------------------------------------------------
    // Test D — Erreur pendant TTS
    // -------------------------------------------------------------------------
    {
        VoiceUxLiveBridge bridge;
        bridge.begin();

        std::vector<VoiceUxActionStatus> recordedStatuses;
        bridge.onActionStatus([&](const VoiceUxBridgeAction&, VoiceUxActionStatus st) {
            recordedStatuses.push_back(st);
        });

        // 1. Démarrer une restitution
        bridge.feedSimulatedText("Phrase pour test erreur");
        VoiceUxBridgeAction act = bridge.consumePendingAction();
        bridge.notifySpeechStarted(act.text, 50);
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::SPEAKING, "Test D.1 SPEAKING");

        // 2. Injecter une erreur
        bridge.notifyError("I2S DMA underrun");
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::ERROR, "Test D.2 Etat = ERROR");

        // 3. Vérifier un unique statut terminal (REJECTED)
        int terminalCount = 0;
        VoiceUxActionStatus finalStatus = VoiceUxActionStatus::CREATED;
        for (auto st : recordedStatuses) {
            if (st == VoiceUxActionStatus::COMPLETED || st == VoiceUxActionStatus::REJECTED || st == VoiceUxActionStatus::CANCELLED) {
                terminalCount++;
                finalStatus = st;
            }
        }
        TEST_ASSERT(terminalCount == 1, "Test D.3 Exactement 1 statut terminal emis pour l'action");
        TEST_ASSERT(finalStatus == VoiceUxActionStatus::REJECTED, "Test D.3 Statut terminal = REJECTED");

        // 4. Vérifier l'absence de mémorisation incorrecte
        TEST_ASSERT(bridge.getReplyMemory().getLastReply() != "Phrase pour test erreur", "Test D.4 Reponse en erreur NON memorisee");

        // 5. Récupérer après erreur
        bridge.recoverFromError();
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Test D.5 READY apres recoverFromError");

        // 6. Démarrer une nouvelle restitution
        recordedStatuses.clear();
        bridge.feedSimulatedText("Nouvelle phrase nominale");
        VoiceUxBridgeAction act2 = bridge.consumePendingAction();
        bridge.notifySpeechStarted(act2.text, 51);
        bridge.notifySpeechFinished(true, 51);

        // 7. Vérifier le fonctionnement nominal
        TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Test D.7 Retour a READY apres restitution nominale");
        TEST_ASSERT(bridge.getReplyMemory().getLastReply() == "Nouvelle phrase nominale", "Test D.7 Reponse nominale memorisee");
        TEST_ASSERT(!recordedStatuses.empty() && recordedStatuses.back() == VoiceUxActionStatus::COMPLETED, "Test D.7 Statut final COMPLETED");
    }

    // -------------------------------------------------------------------------
    // Test E — STOP concurrent avec fin audio
    // -------------------------------------------------------------------------
    {
        // E.1. Ordre 1 : STOP puis notification de fin retardée
        {
            VoiceUxLiveBridge bridge;
            bridge.begin();

            std::vector<VoiceUxActionStatus> statuses;
            bridge.onActionStatus([&](const VoiceUxBridgeAction&, VoiceUxActionStatus st) {
                statuses.push_back(st);
            });

            bridge.feedSimulatedText("Restitution concurrent 1");
            VoiceUxBridgeAction act = bridge.consumePendingAction();
            uint32_t spkId = 60;
            bridge.notifySpeechStarted(act.text, spkId);

            // STOP intervient pendant la restitution
            bridge.triggerPriorityStop();
            TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Test E.1.1 READY apres STOP");

            // Notification de fin retardée du pilote audio arrive après STOP
            bridge.notifySpeechFinished(true, spkId);

            // Vérifier absence de double terminaison
            int termCount = 0;
            for (auto st : statuses) {
                if (st == VoiceUxActionStatus::COMPLETED || st == VoiceUxActionStatus::CANCELLED || st == VoiceUxActionStatus::REJECTED) {
                    termCount++;
                }
            }
            TEST_ASSERT(termCount == 1, "Test E.1.2 Exactement un statut terminal (CANCELLED) malgre notification tardive");
            TEST_ASSERT(statuses.back() == VoiceUxActionStatus::CANCELLED, "Test E.1.3 Dernier statut = CANCELLED");
            TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Test E.1.4 Etat reste READY coherent");
        }

        // E.2. Ordre 2 : Notification de fin puis STOP immédiat
        {
            VoiceUxLiveBridge bridge;
            bridge.begin();

            std::vector<VoiceUxActionStatus> statuses;
            bridge.onActionStatus([&](const VoiceUxBridgeAction&, VoiceUxActionStatus st) {
                statuses.push_back(st);
            });

            bridge.feedSimulatedText("Restitution concurrent 2");
            VoiceUxBridgeAction act = bridge.consumePendingAction();
            uint32_t spkId = 70;
            bridge.notifySpeechStarted(act.text, spkId);

            // Fin normale déclarée
            bridge.notifySpeechFinished(true, spkId);
            TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Test E.2.1 READY apres fin");

            // STOP concurrent immédiat
            bridge.triggerPriorityStop();
            TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::READY, "Test E.2.2 Reste READY apres STOP post-fin");

            int termCount = 0;
            for (auto st : statuses) {
                if (st == VoiceUxActionStatus::COMPLETED || st == VoiceUxActionStatus::CANCELLED || st == VoiceUxActionStatus::REJECTED) {
                    termCount++;
                }
            }
            TEST_ASSERT(termCount == 1, "Test E.2.3 Exactement un statut terminal (COMPLETED), pas de double terminaison");
            TEST_ASSERT(statuses.back() == VoiceUxActionStatus::COMPLETED, "Test E.2.4 Statut reste COMPLETED");
        }
    }

    // -------------------------------------------------------------------------
    // Test F — Propagation et attribution réelle depuis VoxCoreVoice
    // -------------------------------------------------------------------------
    {
        MockLiveTransport mockTransport;
        VoxCoreVoice voice;
        VoiceKitConfig cfg;
        cfg.apiKey = "TEST_KEY_CORRELATION";
        cfg.wifiMode = WiFiMode::EXTERNAL_WIFI;
        TEST_ASSERT(voice.begin(cfg), "Test F.1 Initialisation VoxCoreVoice");

        WiFi.setConnected(true);
        voice.getClient().setTransport(&mockTransport);
        voice.getClient().connect();
        const char* setupJson = "{\"setupComplete\":{}}";
        voice.getClient().handleIncomingMessage(setupJson, strlen(setupJson));
        voice.update();

        // Vérification des identifiants et corrélations STT
        uint32_t detectedTurnId = 0;
        voice.onListeningStartedWithTurnId([&](uint32_t turnId) {
            detectedTurnId = turnId;
        });

        uint32_t transTurnId = 0;
        String transText = "";
        bool transIsFinal = false;
        voice.onTranscriptionWithTurnId([&](const String& text, bool isFinal, uint32_t turnId) {
            transText = text;
            transIsFinal = isFinal;
            transTurnId = turnId;
        });

        // 1. Démarrer écoute
        TEST_ASSERT(voice.startListening(), "Test F.2 startListening reussi");
        TEST_ASSERT(detectedTurnId > 0, "Test F.2 detectedTurnId attribue");
        TEST_ASSERT(voice.getActiveCaptureTurnId() == detectedTurnId, "Test F.2 getActiveCaptureTurnId coherent");

        // 2. Arrêter écoute -> passe en attente transcription
        TEST_ASSERT(voice.stopListening(), "Test F.3 stopListening reussi");
        TEST_ASSERT(voice.getPendingTranscriptionTurnId() == detectedTurnId, "Test F.3 getPendingTranscriptionTurnId coherent");

        // 3. Simuler une transcription reçue depuis le serveur
        const char* transMsg = "{\"serverContent\":{\"inputTranscription\":{\"text\":\"Bonjour tout le monde\"}}}";
        voice.getClient().handleIncomingMessage(transMsg, strlen(transMsg));
        voice.update();
        TEST_ASSERT(transText == "Bonjour tout le monde", "Test F.4 Transcription reçue");
        TEST_ASSERT(transTurnId == detectedTurnId, "Test F.4 originTurnId correctement correle a l'operation");

        // 4. STOP invalide les tours actifs
        voice.stop();
        TEST_ASSERT(voice.getActiveCaptureTurnId() == 0, "Test F.5 activeCaptureTurnId remis a 0 apres stop");
        TEST_ASSERT(voice.getPendingTranscriptionTurnId() == 0, "Test F.5 pendingTranscriptionTurnId remis a 0 apres stop");

        // 5. TTS et attribution speechId
        uint32_t finishedSpeechId = 0;
        voice.onSpeechFinishedWithId([&](uint32_t speechId) {
            finishedSpeechId = speechId;
        });

        TEST_ASSERT(voice.getActiveSpeechId() == 0, "Test F.6 speechId actif 0 au repos");
        voice.say("Test vocal correlation");
        uint32_t spId = voice.getActiveSpeechId();
        TEST_ASSERT(spId > 0, "Test F.7 say() a attribue un speechId positif");
        voice.stop();
        TEST_ASSERT(voice.getActiveSpeechId() == 0, "Test F.8 stop() nettoie le speechId actif");
    }
}

inline void run_all_voiceux_tests() {
    test_v0641_textgate_validation();
    test_v0641_intent_router_recognition_and_false_positives();
    test_v0641_reply_memory_lifecycle_and_utf8_truncation();
    test_v0641_voiceux_controller_facade_and_events();
    test_v0641_performance_metrics_and_stability();
    test_v0642_livebridge_stt_and_intents();
    test_v0642_livebridge_pause_resume();
    test_v0642_livebridge_concurrency_and_turn_safety();
    test_v0642_livebridge_state_machine_tts_cycle_and_resilience();
    test_v0643_tool_registry_security_and_stabilization();
    test_v0643_voiceux_livebridge_stabilization_and_edge_cases();
    test_v06431_corrective_event_safety();
    test_v06432_corrective_event_correlation();
}

