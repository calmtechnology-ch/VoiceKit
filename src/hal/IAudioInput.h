#pragma once
#include <cstdint>
#include <cstddef>

/**
 * @brief Interface d'abstraction matérielle pour l'acquisition audio (Microphone).
 * Format standard VoxCore / VoiceKit : PCM mono, 16 bits, 16 000 Hz.
 */
class IAudioInput {
public:
    virtual ~IAudioInput() = default;

    /**
     * @brief Initialise le périphérique de capture microphone.
     * @return true si l'initialisation matérielle a réussi.
     */
    virtual bool begin() = 0;

    /**
     * @brief Arrête et désactive le microphone (libère les ressources matérielles / DMA).
     */
    virtual void end() = 0;

    /**
     * @brief Vérifie si le sous-système microphone est en service.
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief Indique si une capture est en cours.
     * @return Nombre de tampons d'acquisition actifs (ex: 0, 1 ou 2 pour double-buffering DMA).
     */
    virtual int isRecording() const = 0;

    /**
     * @brief Enregistre ou arme un bloc d'échantillons PCM.
     * @param dest Pointeur de destination vers le tampon int16_t.
     * @param sampleCount Nombre d'échantillons à acquérir (ex: 512).
     * @param sampleRateHz Fréquence d'échantillonnage (défaut: 16000 Hz).
     * @param stereo False pour mono (recommandé), true pour stéréo.
     * @return true si la capture a été démarrée / armée avec succès.
     */
    virtual bool record(int16_t* dest, size_t sampleCount, uint32_t sampleRateHz = 16000, bool stereo = false) = 0;

    /**
     * @brief Arrête la capture en cours.
     */
    virtual void stop() = 0;

    /**
     * @brief Récupère le dernier message d'erreur éventuel.
     */
    virtual const char* getLastError() const = 0;
};
