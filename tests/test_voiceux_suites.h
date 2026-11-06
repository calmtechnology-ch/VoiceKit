#pragma once

#include "core/voiceux/VoiceUxTypes.h"
#include "core/voiceux/TextGate.h"
#include "core/voiceux/VoiceIntentRouter.h"
#include "core/voiceux/ReplyMemory.h"
#include "core/voiceux/VoiceUxController.h"
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

inline void run_all_voiceux_tests() {
    test_v0641_textgate_validation();
    test_v0641_intent_router_recognition_and_false_positives();
    test_v0641_reply_memory_lifecycle_and_utf8_truncation();
    test_v0641_voiceux_controller_facade_and_events();
    test_v0641_performance_metrics_and_stability();
}

