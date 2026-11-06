#pragma once

#include <Arduino.h>
#include <functional>
#include "VoiceUxTypes.h"
#include "TextGate.h"
#include "VoiceIntentRouter.h"
#include "ReplyMemory.h"

/**
 * @class VoiceUxController
 * @brief Contrôleur et façade unique pour la fondation Voice UX locale.
 *        VoiceKit v0.6.4.1 — Voice UX Foundation
 * 
 * Rôles :
 * - Orchestrer de manière totalement autonome le filtrage de transcriptions (TextGate),
 *   la détection d'intentions locales en français (VoiceIntentRouter) et la mémoire
 *   des réponses de l'assistant (ReplyMemory).
 * - Offrir une API simple, thread-safe et non-bloquante pour analyser les transcriptions.
 * - Ne dépendre d'aucun matériel audio, réseau ou modèle d'IA externe.
 * - Désactivé par défaut pour garantir une stricte neutralité sur les projets existants.
 * - Ne jamais commander directement le matériel audio (say, stop, listen) :
 *   génère des événements structurés et laisse l'application maîtresse décider des actions.
 */
class VoiceUxController {
public:
    using ResultCallback = std::function<void(const VoiceUxResult& result)>;

    VoiceUxController();
    ~VoiceUxController() = default;

    /**
     * @brief Initialise le sous-système Voice UX.
     * @return true si l'initialisation réussit.
     */
    bool begin();

    /**
     * @brief Active ou désactive le sous-système Voice UX.
     * @param enabled true pour activer, false pour désactiver (mode pass-through neutre).
     */
    void setEnabled(bool enabled);

    /**
     * @brief Indique si le sous-système est actif.
     */
    bool isEnabled() const;

    /**
     * @brief Analyse une transcription finale.
     * @param rawText Transcription issue d'un fournisseur STT ou d'une simulation.
     * @return VoiceUxResult Bilan complet de l'analyse.
     */
    VoiceUxResult processFinalText(const String& rawText);

    /**
     * @brief Mémorise une réponse complète générée par l'assistant.
     * @param reply Texte complet de la réponse.
     * @return true si mémorisée, false si vide ou module inactif.
     */
    bool rememberReply(const String& reply);

    /**
     * @brief Méthode de maintenance périodique non-bloquante.
     */
    void update();

    /**
     * @brief Définit le callback recevant chaque résultat d'analyse.
     */
    void onResult(ResultCallback cb);

    /**
     * @brief Récupère le dernier résultat d'analyse produit.
     */
    const VoiceUxResult& getLastResult() const;

    /**
     * @brief Accès direct au filtre TextGate.
     */
    TextGate& getTextGate();
    const TextGate& getTextGate() const;

    /**
     * @brief Accès direct au routeur d'intentions.
     */
    VoiceIntentRouter& getIntentRouter();
    const VoiceIntentRouter& getIntentRouter() const;

    /**
     * @brief Accès direct à la mémoire de réponses.
     */
    ReplyMemory& getReplyMemory();
    const ReplyMemory& getReplyMemory() const;

    /**
     * @brief Récupère les métriques statistiques de traitement.
     */
    const VoiceUxStats& getStats() const;

    /**
     * @brief Réinitialise les compteurs de statistiques.
     */
    void resetStats();

private:
    bool _enabled;
    TextGate _gate;
    VoiceIntentRouter _router;
    ReplyMemory _memory;
    VoiceUxResult _lastResult;
    VoiceUxStats _stats;
    ResultCallback _callback;
};

