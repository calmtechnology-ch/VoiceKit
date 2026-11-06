#pragma once

#include <Arduino.h>
#include <vector>

/**
 * @struct ReplyEntry
 * @brief Entrée individuelle mémorisée dans ReplyMemory.
 */
struct ReplyEntry {
    String text;
    uint32_t timestampMs = 0;
};

/**
 * @class ReplyMemory
 * @brief Mémoire locale, légère et bornée des dernières réponses de l'assistant.
 *        VoiceKit v0.6.4.1 — Voice UX Foundation
 * 
 * Rôles :
 * - Mémoriser en RAM uniquement les N dernières réponses complètes de l'assistant (5 par défaut).
 * - Éviter toute écriture Flash ou base de données.
 * - Supporter la consultation de la dernière réponse et de la réponse précédente.
 * - Permettre le parcours chronologique inversé (du plus récent au plus ancien).
 * - Borner strictement la taille de chaque réponse (1024 octets par défaut).
 * - Protéger l'intégrité UTF-8 en cas de troncature (aucun point de code scindé).
 * - Fonctionner de manière autonome sans initialiser le moteur vocal ni le réseau.
 */
class ReplyMemory {
public:
    static constexpr size_t DEFAULT_CAPACITY = 5;
    static constexpr size_t DEFAULT_MAX_REPLY_BYTES = 1024;

    explicit ReplyMemory(size_t capacity = DEFAULT_CAPACITY,
                         size_t maxReplyBytes = DEFAULT_MAX_REPLY_BYTES);
    ~ReplyMemory() = default;

    /**
     * @brief Ajoute une réponse complète à la mémoire.
     * @param reply Texte complet de la réponse.
     * @return true si ajoutée, false si texte vide.
     */
    bool addReply(const String& reply);

    /**
     * @brief Récupère la dernière réponse mémorisée (la plus récente).
     */
    String getLastReply() const;
    bool getLastReply(String& out) const;

    /**
     * @brief Récupère la réponse précédente (avant-dernière).
     */
    String getPreviousReply() const;
    bool getPreviousReply(String& out) const;

    /**
     * @brief Consulte une réponse par son décalage (0 = plus récente, 1 = précédente, etc.).
     */
    String getReplyAt(size_t indexFromLatest) const;
    bool getReplyAt(size_t indexFromLatest, String& out) const;

    /**
     * @brief Vide intégralement la mémoire.
     */
    void clear();

    /**
     * @brief Nombre de réponses actuellement mémorisées.
     */
    size_t getCount() const;

    /**
     * @brief Capacité maximale de réponses.
     */
    size_t getCapacity() const;

    /**
     * @brief Modifie la capacité maximale (tronque les plus anciennes si nécessaire).
     */
    void setCapacity(size_t capacity);

    /**
     * @brief Longueur maximale en octets d'une réponse mémorisée.
     */
    size_t getMaxReplyBytes() const;

    /**
     * @brief Indique si la mémoire est vide.
     */
    bool isEmpty() const;

    /**
     * @brief Indique si la mémoire a atteint sa capacité maximale.
     */
    bool isFull() const;

    /**
     * @brief Tronque proprement une chaîne UTF-8 sans scinder de caractère multioctet.
     */
    static String truncateUtf8(const String& input, size_t maxBytes);

private:
    size_t _capacity;
    size_t _maxReplyBytes;
    std::vector<ReplyEntry> _entries;
};

