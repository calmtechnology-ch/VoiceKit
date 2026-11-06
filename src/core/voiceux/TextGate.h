#pragma once

#include <Arduino.h>
#include <vector>
#include "VoiceUxTypes.h"

/**
 * @class TextGate
 * @brief Filtre déterministe de transcriptions textuelles pour Voice UX.
 *        VoiceKit v0.6.4.1 — Voice UX Foundation
 * 
 * Rôles :
 * - Filtrer les entrées sans contenu sémantique utile (vides, espaces, bruits).
 * - Rejeter les hésitations isolées (« euh », « heu », « hum », « hmm »).
 * - Accepter les commandes courtes valides (« oui », « non », « stop », « répète »).
 * - Normaliser la ponctuation et les espaces uniquement pour l'analyse lexicale.
 * - Conserver scrupuleusement la transcription originale.
 * - Ne pas rejeter une phrase contenant une hésitation si un propos utile existe.
 * - Détecter à titre informatif les énoncés incomplets (suspension, conjonction orpheline).
 * - Garantir un temps d'exécution déterministe sans blocage.
 */
class TextGate {
public:
    TextGate();
    ~TextGate() = default;

    /**
     * @brief Évalue une transcription textuelle.
     * @param rawText Texte brut issu de la reconnaissance vocale ou d'une simulation.
     * @param outNormalized Pointeur optionnel pour récupérer le texte normalisé.
     * @param outReason Pointeur optionnel pour récupérer le motif de la décision.
     * @return TextGateDecision ACCEPT, IGNORE ou INCOMPLETE.
     */
    TextGateDecision evaluate(const String& rawText,
                             String* outNormalized = nullptr,
                             String* outReason = nullptr) const;

    /**
     * @brief Normalise une chaîne pour l'analyse : minuscules, suppression ponctuation,
     *        suppression des accents français, compactage des espaces multiples.
     *        Ne modifie jamais le texte original de l'appelant.
     */
    static String normalizeForAnalysis(const String& input);

    /**
     * @brief Indique si un mot unique normalisé correspond à une hésitation connue.
     */
    static bool isHesitationWord(const String& word);

    /**
     * @brief Indique si une chaîne contient uniquement des hésitations.
     */
    static bool isOnlyHesitations(const String& normalized);

    /**
     * @brief Détecte si un énoncé se termine de façon manifestement incomplète (informatif).
     */
    static bool isPotentiallyIncomplete(const String& rawText, const String& normalized);

    /**
     * @brief Découpe une chaîne en mots individuels séparés par des espaces.
     */
    static std::vector<String> splitWords(const String& str);
};

