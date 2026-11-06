#pragma once

#include <Arduino.h>
#include <vector>
#include "VoiceUxTypes.h"

/**
 * @class VoiceIntentRouter
 * @brief Routeur local d'intentions conversationnelles en français.
 *        VoiceKit v0.6.4.1 — Voice UX Foundation
 * 
 * Rôles :
 * - Identifier de façon 100% locale, déterministe et sans IA les commandes conversationnelles.
 * - Reconnaître les 7 intentions majeures : REPEAT, PREVIOUS, STOP, PAUSE, RESUME, SLOWER, REPHRASE.
 * - Tolérer les formules de politesse courantes en préfixe ou suffixe (« s'il te plaît », « merci », etc.).
 * - Rejeter strictement les faux positifs (phrases avec complément d'objet, questions complexes).
 * - Garantir un temps d'exécution en microsecondes adapté à l'ESP32-S3.
 */
class VoiceIntentRouter {
public:
    VoiceIntentRouter();
    ~VoiceIntentRouter() = default;

    /**
     * @brief Détermine l'intention conversationnelle d'une transcription.
     * @param text Transcription (brute ou normalisée).
     * @return VoiceUxIntent Intention reconnue ou VoiceUxIntent::NONE.
     */
    VoiceUxIntent route(const String& text) const;

    /**
     * @brief Retire les formules de politesse et préfixes/suffixes conversationnels connus.
     */
    static String stripPoliteWrappers(const String& normalized);

private:
    static bool matchesExact(const String& phrase, const char* const* candidates, size_t count);
};

