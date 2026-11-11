#pragma once

#include <Arduino.h>
#include <vector>
#include "../tools/ToolRegistry.h"
#include "../tools/ToolDefinition.h"

/**
 * @struct VoiceCommandMatch
 * @brief Résultat de l'analyse et de la correspondance d'une phrase avec une commande locale.
 *        VoiceKit v0.6.5 — Integrated Voice Pipeline
 */
struct VoiceCommandMatch {
    bool matched = false;           ///< Vrai si une correspondance locale valide a été identifiée
    String toolId;                  ///< Identifiant de la commande locale (ex: "system.get_status")
    ToolArgs args;                  ///< Arguments extraits ou par défaut
    String matchedPattern;          ///< Motif de correspondance ayant validé l'intention
    String originalText;            ///< Phrase d'origine non modifiée
    String normalizedText;          ///< Phrase après normalisation (minuscules, sans accent, sans ponctuation)
};

/**
 * @struct VoiceCommandPattern
 * @brief Association entre un motif textuel normalisé et un outil local.
 */
struct VoiceCommandPattern {
    String toolId;
    std::vector<String> patterns;   ///< Formulations acceptées
    ToolArgs defaultArgs;           ///< Arguments par défaut associés
};

/**
 * @class VoiceCommandDispatcher
 * @brief Composant assurant la liaison déterministe entre les phrases reconnues et les outils locaux.
 *        VoiceKit v0.6.5 — Section 6
 * 
 * Rôles :
 * - Normaliser rigoureusement le texte français (casse, accents UTF-8, ponctuation, espaces).
 * - Supprimer les formules de politesse courantes (« s'il te plaît », « merci », « peux-tu », etc.).
 * - Comparer de façon déterministe aux motifs enregistrés (plusieurs formulations possibles).
 * - Extraire les paramètres numériques simples (ex: niveau de luminosité, température, etc.).
 * - Valider l'existence de l'outil dans ToolRegistry : aucun outil non enregistré ne peut être ciblé.
 * - Respecter scrupuleusement les politiques de sécurité (READ_ONLY, WRITE, SENSITIVE).
 */
class VoiceCommandDispatcher {
public:
    VoiceCommandDispatcher();
    explicit VoiceCommandDispatcher(const ToolRegistry& registry);
    ~VoiceCommandDispatcher() = default;

    /**
     * @brief Associe un registre d'outils pour la validation des commandes existantes.
     */
    void setRegistry(const ToolRegistry* registry);

    /**
     * @brief Enregistre une correspondance avec plusieurs formulations autorisées.
     * @param toolId Identifiant de la commande locale enregistrée dans ToolRegistry.
     * @param patterns Liste des phrases ou formulations équivalentes.
     * @param defaultArgs Arguments par défaut optionnels.
     * @return true si l'enregistrement a réussi, false si toolId vide ou aucun motif.
     */
    bool registerMapping(const String& toolId, const std::vector<String>& patterns, const ToolArgs& defaultArgs = ToolArgs());

    /**
     * @brief Surcharge ergonomique pour un motif unique.
     */
    bool registerMapping(const String& toolId, const String& pattern, const ToolArgs& defaultArgs = ToolArgs());

    /**
     * @brief Désenregistre toutes les correspondances associées à un outil.
     */
    bool unregisterMapping(const String& toolId);

    /**
     * @brief Réinitialise et vide l'intégralité des motifs de correspondance.
     */
    void clear();

    /**
     * @brief Retourne le nombre d'outils ayant au moins une correspondance enregistrée.
     */
    size_t getMappingCount() const;

    /**
     * @brief Recherche une correspondance pour une phrase transcrite.
     * @param rawText Transcription brute issue du STT ou d'une simulation.
     * @return VoiceCommandMatch Résultat de l'analyse (matched = true si reconnue et autorisée).
     */
    VoiceCommandMatch match(const String& rawText) const;

    /**
     * @brief Normalise un texte français (minuscules, suppression des accents UTF-8 et ponctuation).
     */
    static String normalize(const String& text);

    /**
     * @brief Supprime les formules de politesse et préfixes conversationnels courants.
     */
    static String stripPoliteWrappers(const String& normalizedText);

    /**
     * @brief Tente d'extraire une valeur numérique (entière et réelle) d'une chaîne.
     */
    static bool extractNumber(const String& text, int& outInt, double& outDouble);

private:
    const ToolRegistry* _registry = nullptr;
    std::vector<VoiceCommandPattern> _patterns;
};

