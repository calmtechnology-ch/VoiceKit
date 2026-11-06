#include "VoiceIntentRouter.h"
#include "TextGate.h"

// Tables de correspondances exactes (termes en minuscules, sans accents, sans ponctuation)

static const char* const kStopCommands[] = {
    "stop",
    "arrete",
    "arreter",
    "arretez",
    "arrete toi",
    "arretez vous",
    "tais toi",
    "taisez vous",
    "silence",
    "quitte",
    "quitter",
    "annule",
    "annuler",
    "c est bon arrete",
    "stop c est bon",
    "c est bon"
};

static const char* const kPauseCommands[] = {
    "pause",
    "met en pause",
    "mets en pause",
    "mettre en pause",
    "mettez en pause",
    "fais une pause",
    "faire une pause",
    "faites une pause",
    "en pause",
    "attends",
    "attendez",
    "patiente",
    "patientez",
    "attends une seconde",
    "attends une minute",
    "attends un peu"
};

static const char* const kResumeCommands[] = {
    "continue",
    "continuer",
    "continuez",
    "reprends",
    "reprendre",
    "reprenez",
    "poursuis",
    "poursuivre",
    "poursuivez",
    "relance",
    "relancer",
    "relancez",
    "vas y",
    "vas y continue",
    "allez y",
    "reprends la lecture",
    "continue la lecture",
    "c est reparti"
};

static const char* const kRepeatCommands[] = {
    "repete",
    "repeter",
    "repetez",
    "redis",
    "redire",
    "redites",
    "redis moi",
    "redis le",
    "redis ca",
    "redis ta reponse",
    "redites votre reponse",
    "repete ta reponse",
    "repetez votre reponse",
    "repete la derniere reponse",
    "repetez la derniere reponse",
    "repete la reponse",
    "repetez la reponse",
    "repete ce que tu viens de dire",
    "repetez ce que vous venez de dire",
    "dis a nouveau",
    "dis le a nouveau",
    "encore une fois",
    "une fois de plus",
    "qu est ce que tu as dit",
    "quest ce que tu as dit",
    "qu as tu dit",
    "quavez vous dit",
    "qu avez vous dit",
    "qu est ce que vous avez dit"
};

static const char* const kPreviousCommands[] = {
    "reponse precedente",
    "la reponse precedente",
    "reviens a ta reponse precedente",
    "reviens a la reponse precedente",
    "reviens a votre reponse precedente",
    "reviens a la precedente",
    "revenir a la reponse precedente",
    "quelle etait la reponse precedente",
    "quelle etait ta reponse precedente",
    "la reponse d avant",
    "reponse d avant",
    "message precedent",
    "le message precedent",
    "reviens en arriere",
    "retour en arriere"
};

static const char* const kSlowerCommands[] = {
    "parle moins vite",
    "parler moins vite",
    "parlez moins vite",
    "moins vite",
    "plus lentement",
    "parle plus lentement",
    "parler plus lentement",
    "parlez plus lentement",
    "ralentis",
    "ralentissez",
    "baisse la vitesse"
};

static const char* const kRephraseCommands[] = {
    "reformule",
    "reformuler",
    "reformulez",
    "explique autrement",
    "expliquer autrement",
    "expliquez autrement",
    "dis le autrement",
    "dis autrement",
    "le autrement",
    "reformule ta reponse",
    "reformulez votre reponse",
    "je n ai pas compris",
    "je n ai rien compris",
    "pas compris",
    "en d autres termes",
    "autrement"
};

static const char* const kPolitePrefixes[] = {
    "s il te plait",
    "s il vous plait",
    "sil te plait",
    "sil vous plait",
    "stp",
    "svp",
    "est ce que tu peux",
    "est ce que vous pouvez",
    "tu peux",
    "vous pouvez",
    "peux tu",
    "pouvez vous",
    "pourrais tu",
    "pourriez vous",
    "tu pourrais",
    "vous pourriez",
    "veuille",
    "veuillez",
    "merci de",
    "dis moi",
    "dites moi"
};

static const char* const kPoliteSuffixes[] = {
    "s il te plait merci",
    "s il vous plait merci",
    "s il te plait",
    "s il vous plait",
    "sil te plait",
    "sil vous plait",
    "stp",
    "svp",
    "merci beaucoup",
    "merci"
};

VoiceIntentRouter::VoiceIntentRouter() {
}

String VoiceIntentRouter::stripPoliteWrappers(const String& normalized) {
    String cur = normalized;
    cur.trim();

    bool changed = true;
    while (changed && !cur.isEmpty()) {
        changed = false;

        // Tester les préfixes
        for (size_t i = 0; i < sizeof(kPolitePrefixes) / sizeof(kPolitePrefixes[0]); ++i) {
            const char* pref = kPolitePrefixes[i];
            size_t prefLen = strlen(pref);
            if (cur.length() == prefLen && cur == pref) {
                // Le mot de politesse est seul dans la phrase : ne pas le vider complètement ici
                break;
            }
            if (cur.startsWith(pref) && cur.length() > prefLen && cur[prefLen] == ' ') {
                cur = cur.substring(prefLen + 1);
                cur.trim();
                changed = true;
                break;
            }
        }

        // Tester les suffixes
        for (size_t i = 0; i < sizeof(kPoliteSuffixes) / sizeof(kPoliteSuffixes[0]); ++i) {
            const char* suff = kPoliteSuffixes[i];
            size_t suffLen = strlen(suff);
            if (cur.length() == suffLen && cur == suff) {
                break;
            }
            if (cur.endsWith(suff) && cur.length() > suffLen && cur[cur.length() - suffLen - 1] == ' ') {
                cur = cur.substring(0, cur.length() - suffLen - 1);
                cur.trim();
                changed = true;
                break;
            }
        }
    }

    return cur;
}

bool VoiceIntentRouter::matchesExact(const String& phrase, const char* const* candidates, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (phrase == candidates[i]) {
            return true;
        }
    }
    return false;
}

VoiceUxIntent VoiceIntentRouter::route(const String& text) const {
    if (text.isEmpty()) {
        return VoiceUxIntent::NONE;
    }

    // Normalisation lexicale complète (minuscules, sans accents, sans ponctuation)
    String normalized = TextGate::normalizeForAnalysis(text);
    if (normalized.isEmpty()) {
        return VoiceUxIntent::NONE;
    }

    // Retrait des formules de politesse en préfixe ou suffixe
    String core = stripPoliteWrappers(normalized);
    if (core.isEmpty()) {
        return VoiceUxIntent::NONE;
    }

    // Évaluation déterministe avec correspondance exacte sur les listes bornées

    // 1. STOP
    if (matchesExact(core, kStopCommands, sizeof(kStopCommands) / sizeof(kStopCommands[0]))) {
        return VoiceUxIntent::STOP;
    }

    // 2. PAUSE
    if (matchesExact(core, kPauseCommands, sizeof(kPauseCommands) / sizeof(kPauseCommands[0]))) {
        return VoiceUxIntent::PAUSE;
    }

    // 3. RESUME
    if (matchesExact(core, kResumeCommands, sizeof(kResumeCommands) / sizeof(kResumeCommands[0]))) {
        return VoiceUxIntent::RESUME;
    }

    // 4. REPEAT
    if (matchesExact(core, kRepeatCommands, sizeof(kRepeatCommands) / sizeof(kRepeatCommands[0]))) {
        return VoiceUxIntent::REPEAT;
    }

    // 5. PREVIOUS
    if (matchesExact(core, kPreviousCommands, sizeof(kPreviousCommands) / sizeof(kPreviousCommands[0]))) {
        return VoiceUxIntent::PREVIOUS;
    }

    // 6. SLOWER
    if (matchesExact(core, kSlowerCommands, sizeof(kSlowerCommands) / sizeof(kSlowerCommands[0]))) {
        return VoiceUxIntent::SLOWER;
    }

    // 7. REPHRASE
    if (matchesExact(core, kRephraseCommands, sizeof(kRephraseCommands) / sizeof(kRephraseCommands[0]))) {
        return VoiceUxIntent::REPHRASE;
    }

    // Aucune commande conversationnelle reconnue -> phrase ordinaire
    return VoiceUxIntent::NONE;
}
