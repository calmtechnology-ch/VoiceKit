#pragma once

#include <Arduino.h>
#include <vector>
#include <memory>
#include <functional>
#include "ToolEvents.h"
#include "ToolDefinition.h"
#include "ToolValidator.h"
#include "ToolExecutor.h"

/**
 * @class ToolRegistry
 * @brief Registre de commandes et d'outils locaux pour VoiceKit v0.6.4.
 * 
 * Permet aux applications ESP32 / M5Stack CoreS3 d'enregistrer, découvrir,
 * valider et exécuter des commandes locales de façon totalement indépendante
 * de Gemini Live et OpenAI Realtime, sans nécessiter d'initialiser le moteur vocal.
 * 
 * Caractéristiques & Garanties :
 * - Capacité paramétrable (16 commandes par défaut).
 * - Rejet strict des doublons et des déclarations invalides.
 * - Validation complète des types, bornes et paramètres obligatoires avant tout appel.
 * - Rejet des paramètres non déclarés (sauf autorisation explicite).
 * - Sécurité à 3 niveaux (READ_ONLY, WRITE, SENSITIVE).
 * - Commandes sensibles avec jeton de confirmation explicite, expiration et protection anti-rejeu.
 * - Émission ordonnée d'événements (TOOL_REQUESTED, STARTED, COMPLETED, FAILED, CONFIRMATION_REQUIRED).
 * - Exécution isolée dans un contexte contrôlé, avec protection anti-réentrance.
 * - Support de tâches différées non bloquantes pour les opérations matérielles longues.
 */
class ToolRegistry {
public:
    static constexpr size_t DEFAULT_MAX_CAPACITY = 16;
    static constexpr size_t DEFAULT_MAX_PENDING_CONFIRMATIONS = 8;
    static constexpr uint32_t DEFAULT_CONFIRMATION_TIMEOUT_MS = 30000;

    using AuthorizationChecker = std::function<bool(const ToolDefinition& def, const ToolArgs& args)>;

    struct PendingConfirmation {
        String token;
        String requestId;
        String toolId;
        uint32_t toolGeneration = 0;
        ToolArgs args;
        uint32_t createdAtMs = 0;
        uint32_t timeoutMs = DEFAULT_CONFIRMATION_TIMEOUT_MS;
        bool used = false;
    };

    explicit ToolRegistry(size_t maxCapacity = DEFAULT_MAX_CAPACITY);
    ~ToolRegistry();

    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;

    // =========================================================================
    // Enregistrement
    // =========================================================================

    /**
     * @brief Enregistre une définition d'outil complète.
     * @param def Définition de l'outil.
     * @return true si enregistrement accepté, false si doublon, invalide ou capacité atteinte.
     */
    bool registerTool(const ToolDefinition& def);

    /**
     * @brief Surcharge ergonomique sans arguments (ex: system.get_status).
     */
    bool registerTool(const String& id,
                      const String& description,
                      std::function<ToolResult()> handler,
                      ToolSecurityPolicy policy = ToolSecurityPolicy::READ_ONLY);

    /**
     * @brief Surcharge ergonomique avec arguments et paramètres optionnels.
     */
    bool registerTool(const String& id,
                      const String& description,
                      ToolHandler handler,
                      const std::vector<ToolParam>& params = {},
                      ToolSecurityPolicy policy = ToolSecurityPolicy::READ_ONLY,
                      bool allowUnknownParams = false);

    /**
     * @brief Désenregistre une commande par son identifiant.
     */
    bool unregisterTool(const String& id);

    /**
     * @brief Réinitialise et vide l'intégralité du registre et des confirmations.
     */
    void clear();

    // =========================================================================
    // Découverte
    // =========================================================================

    size_t getToolCount() const;
    bool hasTool(const String& id) const;
    const ToolDefinition* findTool(const String& id) const;
    std::vector<String> getToolIds() const;

    size_t getMaxCapacity() const { return _maxCapacity; }
    void setMaxCapacity(size_t maxCapacity);

    // =========================================================================
    // Exécution et Confirmation
    // =========================================================================

    /**
     * @brief Exécute une commande par son identifiant.
     * @param id Identifiant de la commande.
     * @param args Arguments d'exécution.
     * @param confirmationToken Jeton pour exécuter une commande sensible préalablement autorisée.
     * @return Résultat structuré ToolResult.
     */
    ToolResult execute(const String& id,
                       const ToolArgs& args = ToolArgs(),
                       const String& confirmationToken = "");

    /**
     * @brief Confirme et exécute une requête sensible en attente via son jeton.
     * @param confirmationToken Jeton de confirmation émis lors de execute().
     * @return Résultat structuré ToolResult de l'exécution confirmée.
     */
    ToolResult confirm(const String& confirmationToken);

    /**
     * @brief Annule toutes les requêtes en attente et les tâches différées.
     */
    void cancelPending();

    /**
     * @brief Indique si une commande est en cours d'exécution.
     */
    bool isBusy() const;

    /**
     * @brief Traitement régulier non bloquant (purge expirations, tâches différées).
     */
    void update();

    // =========================================================================
    // Sécurité et Autorisation
    // =========================================================================

    void setAuthorizationChecker(AuthorizationChecker checker) {
        _authChecker = std::move(checker);
    }

    void setConfirmationTimeoutMs(uint32_t timeoutMs) {
        _confirmationTimeoutMs = timeoutMs;
    }

    uint32_t getConfirmationTimeoutMs() const {
        return _confirmationTimeoutMs;
    }

    size_t getMaxPendingConfirmations() const {
        return _maxPendingConfirmations;
    }

    void setMaxPendingConfirmations(size_t maxPending) {
        _maxPendingConfirmations = maxPending;
    }

    size_t pendingConfirmationCount() const {
        return _pendingConfirmations.size();
    }

    // =========================================================================
    // Gestion des Événements
    // =========================================================================

    void onEvent(ToolEventCallback cb) {
        _globalEventCallback = std::move(cb);
    }

    void onEvent(ToolEventType type, ToolEventCallback cb) {
        _typedEventCallbacks[static_cast<size_t>(type)] = std::move(cb);
    }

    // =========================================================================
    // Personnalisation de l'Exécuteur
    // =========================================================================

    void setExecutor(std::shared_ptr<IToolExecutor> executor) {
        _customExecutor = std::move(executor);
    }

    IToolExecutor& getExecutor() {
        return _customExecutor ? *_customExecutor : _defaultExecutor;
    }

private:
    void emitEvent(ToolEventType type, const String& toolId, const String& requestId, const String& msg);
    String generateRequestId();
    String generateConfirmationToken();
    void purgeExpiredConfirmations();

    size_t _maxCapacity;
    size_t _maxPendingConfirmations;
    uint32_t _confirmationTimeoutMs;
    std::vector<ToolDefinition> _tools;
    std::vector<PendingConfirmation> _pendingConfirmations;

    ToolExecutor _defaultExecutor;
    std::shared_ptr<IToolExecutor> _customExecutor;
    AuthorizationChecker _authChecker;

    ToolEventCallback _globalEventCallback;
    ToolEventCallback _typedEventCallbacks[5];

    uint32_t _requestCounter = 0;
    uint32_t _tokenCounter = 0;
    uint32_t _toolGenerationCounter = 0;
};

