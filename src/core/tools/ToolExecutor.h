#pragma once

#include <Arduino.h>
#include <functional>
#include <atomic>
#include <deque>
#include "ToolDefinition.h"

/**
 * @class IToolExecutor
 * @brief Interface commune d'exécution pour les outils de VoiceKit.
 * Garantit que l'exécution se déroule dans un contexte applicatif contrôlé,
 * jamais sous interruption, callback audio ou tâche réseau critique.
 */
class IToolExecutor {
public:
    virtual ~IToolExecutor() = default;

    /**
     * @brief Exécute une commande de façon contrôlée.
     * @param def Définition de l'outil validée.
     * @param args Arguments validés.
     * @return Résultat structuré ToolResult.
     */
    virtual ToolResult execute(const ToolDefinition& def, const ToolArgs& args) = 0;

    /**
     * @brief Indique si l'exécuteur est déjà en cours de traitement d'une commande.
     */
    virtual bool isBusy() const = 0;

    /**
     * @brief Traitement régulier non bloquant pour les tâches différées.
     */
    virtual void update() = 0;

    /**
     * @brief Annule toutes les tâches différées en attente sur cet exécuteur.
     */
    virtual void cancelDeferred() {}
};

/**
 * @class ToolExecutor
 * @brief Implémentation par défaut de IToolExecutor avec protection anti-réentrance,
 * gestion des échecs d'exécution et ordonnancement de tâches différées non bloquantes.
 * 
 * NOTE D'ARCHITECTURE (VoiceKit v0.6.4.3 — Option A) :
 * Les tâches différées enfilées via enqueueDeferred() sont exécutées de façon synchrone
 * et séquentielle lors des appels périodiques à update() (dans la boucle loop()).
 * Afin de préserver la réactivité du système audio et éviter tout blocage de la boucle
 * principale sur ESP32-S3, les handlers différés doivent être courts et coopératifs
 * (durée recommandée < 10 à 20 ms).
 */
class ToolExecutor : public IToolExecutor {
public:
    using DeferredCompletionCallback = std::function<void(const ToolResult& result)>;
    using DeferredTask = std::function<ToolResult()>;

    struct DeferredJob {
        uint32_t id = 0;
        String toolId;
        DeferredTask task;
        DeferredCompletionCallback onComplete;
    };

    ToolExecutor() = default;
    ~ToolExecutor() override {
        cancelDeferred();
    }

    ToolExecutor(const ToolExecutor&) = delete;
    ToolExecutor& operator=(const ToolExecutor&) = delete;

    /**
     * @brief Exécute l'outil avec protection contre la réentrance (double demande).
     */
    ToolResult execute(const ToolDefinition& def, const ToolArgs& args) override {
        if (_isBusy.exchange(true)) {
            return ToolResult::busy("Executeur occupe : une execution est deja en cours");
        }

        struct BusyGuard {
            std::atomic<bool>& flag;
            ~BusyGuard() { flag.store(false); }
        } guard{_isBusy};

        if (!def.handler) {
            return ToolResult::executionFailed("Fonction d'execution inexistante");
        }

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
        try {
            ToolResult res = def.handler(args);
            return res;
        } catch (const std::exception& e) {
            return ToolResult::executionFailed(String("Exception durant l'execution : ") + e.what());
        } catch (...) {
            return ToolResult::executionFailed("Exception inconnue durant l'execution");
        }
#else
        return def.handler(args);
#endif
    }

    bool isBusy() const override {
        return _isBusy.load() || !_deferredQueue.empty();
    }

    /**
     * @brief Enfile une opération matérielle longue pour exécution différée non bloquante.
     */
    bool enqueueDeferred(const String& toolId, DeferredTask task, DeferredCompletionCallback onComplete = nullptr) {
        if (!task) return false;
        DeferredJob job;
        job.id = ++_nextJobId;
        job.toolId = toolId;
        job.task = std::move(task);
        job.onComplete = std::move(onComplete);
        _deferredQueue.push_back(std::move(job));
        return true;
    }

    /**
     * @brief Exécute une tranche de traitement différé (à appeler dans update / loop).
     */
    void update() override {
        if (_deferredQueue.empty()) return;
        if (_isBusy.exchange(true)) return;

        DeferredJob job = std::move(_deferredQueue.front());
        _deferredQueue.pop_front();

        ToolResult res;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
        try {
            res = job.task();
        } catch (const std::exception& e) {
            res = ToolResult::executionFailed(String("Exception differee : ") + e.what());
        } catch (...) {
            res = ToolResult::executionFailed("Exception differee inconnue");
        }
#else
        res = job.task();
#endif

        _isBusy.store(false);

        if (job.onComplete) {
            job.onComplete(res);
        }
    }

    /**
     * @brief Annule et vide les tâches différées en attente.
     */
    void cancelDeferred() override {
        _deferredQueue.clear();
    }

    size_t pendingDeferredCount() const {
        return _deferredQueue.size();
    }

private:
    std::atomic<bool> _isBusy{false};
    std::deque<DeferredJob> _deferredQueue;
    uint32_t _nextJobId = 0;
};

