#pragma once
#include <cstdint>
#include <cstddef>

/**
 * @brief Motifs d'arrêt de la restitution audio pour traçabilité et diagnostic v0.2.1.3.5.
 */
enum class AudioStopReason : uint8_t {
    NONE = 0,
    NORMAL_FINISH,
    USER_STOP,
    GEMINI_INTERRUPT,
    HARDWARE_ERROR,
    NEW_TURN,
    RESET
};

inline const char* audioStopReasonToString(AudioStopReason reason) {
    switch (reason) {
        case AudioStopReason::NONE:             return "AUCUN";
        case AudioStopReason::NORMAL_FINISH:    return "FIN_NORMALE";
        case AudioStopReason::USER_STOP:        return "STOP_UTILISATEUR";
        case AudioStopReason::GEMINI_INTERRUPT: return "INTERRUPTION_GEMINI";
        case AudioStopReason::HARDWARE_ERROR:   return "ERREUR_MATERIELLE";
        case AudioStopReason::NEW_TURN:         return "NOUVEAU_TOUR";
        case AudioStopReason::RESET:            return "REINITIALISATION";
        default:                                return "INCONNU";
    }
}

/**
 * @brief Interface d'abstraction matérielle pour la restitution audio (Haut-parleur / Codec).
 * Supporte le streaming PCM direct (ex: 24 kHz Gemini Live mono 16 bits)
 * et le flux MP3 décodé (ex: 44.1 kHz Google TTS).
 */
class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;

    /**
     * @brief Initialise le pilote audio et le codec de sortie.
     * @return true si l'initialisation a réussi.
     */
    virtual bool begin() = 0;

    /**
     * @brief Arrête le pilote audio et libère les canaux DMA.
     */
    virtual void end(AudioStopReason reason = AudioStopReason::RESET) = 0;

    /**
     * @brief Indique si le périphérique de sortie est en état de marche.
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief Soumet un bloc d'échantillons PCM bruts pour restitution au haut-parleur.
     * 
     * CONTRAT D'ACCEPTATION STRICT v0.4.1.3 :
     * - Retourne true si le bloc a été accepté par le pilote matériel (mis en file DMA).
     *   Un retour true atteste que le pilote prend en charge le bloc, sans prétendre
     *   que le son a déjà été physiquement émis.
     * - Retourne false si le bloc a été refusé (ex: file matérielle saturée, transition).
     * 
     * RÈGLE CRITIQUE D'INTÉGRITÉ DU FLUX :
     * Un bloc refusé (retour false) ne doit JAMAIS être retiré du tampon logiciel appelant.
     * L'appelant doit le conserver intact et le soumettre à nouveau au cycle suivant.
     * 
     * @param samples Pointeur vers les échantillons int16_t (mono ou stéréo entrelacé).
     * @param sampleCount Nombre d'échantillons.
     * @param sampleRateHz Fréquence d'échantillonnage en Hz (ex: 24000 pour Gemini Live, 44100 pour TTS).
     * @param stereo Indique si le flux est stéréo (true) ou mono (false).
     * @param channel Numéro de canal virtuel ou matériel (défaut: 0).
     * @return true si accepté, false si refusé.
     */
    virtual bool playRaw(const int16_t* samples, size_t sampleCount, 
                         uint32_t sampleRateHz = 24000, bool stereo = false, 
                         uint8_t channel = 0) = 0;

    /**
     * @brief Indique le nombre de tampons audio en cours de restitution.
     * @param channel Numéro de canal (défaut: 0).
     * @return Nombre de tampons actifs dans la file matérielle (0 si au repos).
     */
    virtual int isPlaying(uint8_t channel = 0) const = 0;

    /**
     * @brief Arrête immédiatement la restitution en cours et vide la file matérielle.
     * @param channel Numéro de canal (défaut: 0).
     * @param reason Motif explicite de l'arrêt (pour diagnostic et traçabilité v0.2.1.3.5).
     */
    virtual void stop(uint8_t channel = 0, AudioStopReason reason = AudioStopReason::NORMAL_FINISH) = 0;

    /**
     * @brief Réinitialise les métriques d'appels matériels pour un nouveau tour de dialogue.
     */
    virtual void resetTurnMetrics() {}

    /**
     * @brief Fonctions d'inspection du cycle de vie matériel pour le bilan AUDIO END CHECK.
     */
    virtual bool wasStopCalledInTurn() const { return false; }
    virtual bool wasEndCalledInTurn() const { return false; }
    virtual uint32_t getLastStopTimeMs() const { return 0; }
    virtual uint32_t getLastEndTimeMs() const { return 0; }
    virtual AudioStopReason getLastStopReason() const { return AudioStopReason::NONE; }
    virtual AudioStopReason getLastEndReason() const { return AudioStopReason::NONE; }
    virtual uint32_t getLastPlayRawAcceptedTimeMs() const { return 0; }
    virtual uint32_t getStopCallCountInTurn() const { return 0; }
    virtual uint32_t getEndCallCountInTurn() const { return 0; }
    virtual size_t getSpeakerStopCount() const { return getStopCallCountInTurn(); }
    virtual size_t getSpeakerEndCount() const { return getEndCallCountInTurn(); }
    virtual uint32_t getBeginCallCountInTurn() const { return 0; }
    virtual uint32_t getPlayRawCallCountInTurn() const { return 0; }
    virtual size_t getSamplesAcceptedInTurn() const { return 0; }

    /**
     * @brief Règle le volume général en pourcentage (0 à 100%).
     */
    virtual void setVolume(unsigned volumePercent) = 0;

    /**
     * @brief Règle le volume spécifique d'un canal (0 à 255).
     */
    virtual void setChannelVolume(uint8_t channel, uint8_t volume255) = 0;

    /**
     * @brief Retourne le volume actuel en pourcentage (0 à 100%).
     */
    virtual unsigned getVolume() const = 0;

    /**
     * @brief Retourne la durée maximale théorique (en ms) de vidange des tampons matériels DMA.
     * Représente le délai physique nécessaire pour que les échantillons acceptés par playRaw()
     * soient intégralement convertis et émis par le haut-parleur une fois la file logicielle vide.
     * Par défaut 0 (pour simulation hôte / tests unitaires).
     */
    virtual uint32_t getHardwareDrainMs() const { return 0; }

    /**
     * @brief Type de fonction de rappel notifiant la libération matérielle d'un buffer audio source.
     */
    using BufferReleaseCallback = void (*)(void* args, const void* data, uint8_t channel);

    /**
     * @brief Enregistre la fonction de rappel appelée lorsque le pilote matériel a fini de lire un buffer.
     * Permet à l'appelant de réutiliser ou de recharger le buffer sans risque d'écrasement mémoire.
     */
    virtual void setBufferReleaseCallback(void* args, BufferReleaseCallback cb) {
        (void)args;
        (void)cb;
    }
};
