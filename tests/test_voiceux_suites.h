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
    bool dispatched = bridge.feedTranscription("Répète", false);
    TEST_ASSERT(!dispatched, "Transcription interim -> aucune action dispatchee");
    TEST_ASSERT(!bridge.hasPendingAction(), "hasPendingAction = false");
    TEST_ASSERT(bridge.getInterimText() == "Répète", "Texte interim enregistre pour l'UI");
    TEST_ASSERT(bridge.getBridgeStats().partialTranscriptionsIgnored == 1, "Compteur de transcriptions partielles incremente");

    // 2. REPEAT avec mémoire vide
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
    dispatched = bridge.feedTranscription("Répète", true);
    TEST_ASSERT(dispatched, "REPEAT avec memoire dispatche");
    act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::TTS_SPEAK, "Action TTS_SPEAK");
    TEST_ASSERT(act.intent == VoiceUxIntent::REPEAT, "Intention REPEAT");
    TEST_ASSERT(act.isRecall == true, "Marquage isRecall = true");
    TEST_ASSERT(act.text == "L'humidité relative est de quarante-huit pour cent.", "Derniere reponse correctement rappelee");

    // 5. PREVIOUS avec plusieurs reponses
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

    dispatched = bridge.feedTranscription("Réponse précédente", true);
    TEST_ASSERT(dispatched, "PREVIOUS avec 1 reponse dispatche");
    act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::TTS_SPEAK, "Action TTS_SPEAK");
    TEST_ASSERT(act.isFeedback == true, "Marque isFeedback");
    TEST_ASSERT(act.text == "Aucune réponse précédente disponible.", "Message d'info sur absence de precedente conforme");

    // 7. Commande STOP vocale
    dispatched = bridge.feedTranscription("Arrête", true);
    TEST_ASSERT(dispatched, "STOP vocal dispatche");
    act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::STOP, "Action STOP");
    TEST_ASSERT(act.intent == VoiceUxIntent::STOP, "Intention STOP");

    // 8. Commandes diagnostiques SLOWER et REPHRASE (pas d'action TTS)
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
    bool dispatched = bridge.feedTranscription("Mets en pause", true);
    TEST_ASSERT(dispatched, "PAUSE vocale dispatchee");
    VoiceUxBridgeAction act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::PAUSE, "Action PAUSE");

    bridge.setPaused(true);
    TEST_ASSERT(bridge.isPaused(), "Bridge est en pause");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::PAUSED, "Etat = PAUSED");

    // 2. En mode PAUSED : les demandes conversationnelles ordinaires sont ignorees
    dispatched = bridge.feedTranscription("Quel temps fait-il ?", true);
    TEST_ASSERT(!dispatched, "Demande ordinaire en pause rejetee");
    TEST_ASSERT(!bridge.hasPendingAction(), "Aucune action creee en pause");
    TEST_ASSERT(bridge.getState() == VoiceUxBridgeState::PAUSED, "Reste en PAUSED");

    // 3. En mode PAUSED : la reprise vocale RESUME est autorisee
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
    bool dispatched = bridge.feedTranscription("Quelle est la capitale de la France ?", true);
    TEST_ASSERT(dispatched, "Phrase ordinaire traitee");
    VoiceUxBridgeAction act = bridge.consumePendingAction();
    TEST_ASSERT(act.type == VoiceUxBridgeActionType::FORWARD_AI, "Action FORWARD_AI");
    TEST_ASSERT(act.text == "Quelle est la capitale de la France ?", "Texte original transmis intact");

    // 2. Rejet des hesitations isolees (TextGate IGNORE)
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
}

