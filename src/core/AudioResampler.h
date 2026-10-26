#pragma once
#include <cstdint>
#include <cstddef>

/**
 * @class AudioResampler
 * @brief Rééchantillonneur audio haute performance pour VoiceKit v0.6.0.
 * 
 * Permet de convertir les flux audio entre les différentes fréquences d'échantillonnage :
 * - 16 kHz (Microphone ES7210 / M5Stack CoreS3) vers 24 kHz (Format natif OpenAI Realtime PCM16).
 * - Algorithme d'interpolation linéaire optimisé en arithmétique entière 32 bits (zéro division flottante).
 * - Extrapolation d'ordre 1 aux frontières sans discontinuité de phase ni artefact audible.
 * - Fonctionnement déterministe, sans allocation dynamique et adapté aux contraintes temps réel strictes.
 */
class AudioResampler {
public:
    /**
     * @brief Rééchantillonne un tampon PCM 16 kHz mono vers 24 kHz mono (rapport 2:3).
     * 
     * Pour chaque bloc de 512 échantillons à 16 kHz (~32 ms), produit exactement 768 échantillons à 24 kHz (~32 ms).
     * 
     * @param inSamples Pointeur vers le tableau d'échantillons d'entrée 16 kHz.
     * @param inCount Nombre d'échantillons d'entrée (doit être pair, ex: 512).
     * @param outSamples Pointeur vers le tampon de destination 24 kHz.
     * @param outCapacity Capacité maximale du tampon de destination en échantillons (>= inCount * 3 / 2).
     * @return Nombre d'échantillons rééchantillonnés écrits dans outSamples (0 si paramètre invalide).
     */
    static size_t resample16to24(const int16_t* inSamples, size_t inCount,
                                 int16_t* outSamples, size_t outCapacity);

    static size_t calculateOutputSamples(size_t inCount) { return (inCount * 3) / 2; }

    static size_t resample(const int16_t* inSamples, size_t inCount,
                           int16_t* outSamples, size_t outCapacity) {
        return resample16to24(inSamples, inCount, outSamples, outCapacity);
    }
};

using AudioResampler16to24 = AudioResampler;
