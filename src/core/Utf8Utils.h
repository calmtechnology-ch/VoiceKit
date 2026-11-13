#pragma once
#include <Arduino.h>
#include <string>
#include <cstdint>
#include <cstddef>

/**
 * @class Utf8Utils
 * @brief Utilitaires de validation, assainissement et échappement JSON pour UTF-8.
 *        VoiceKit v0.4.1
 * 
 * Principes stricts :
 * 1. Préservation intégrale des caractères accentués français, ligatures (œ, æ),
 *    apostrophes typographiques (’), et guillemets («, »).
 * 2. Zéro translittération ni suppression arbitraire d'accents.
 * 3. Validation stricte selon RFC 3629 (rejet des surrogates UTF-16 et overlong).
 * 4. Les textes UTF-8 valides ne subissent AUCUNE altération.
 * 5. Échappement JSON centralisé et conforme RFC 8259 pour tout envoi réseau.
 */
class Utf8Utils {
public:
    /**
     * @brief Vérifie si un tampon mémoire forme une séquence UTF-8 strictement valide.
     * @param data Pointeur vers les octets.
     * @param len Longueur en octets.
     * @return true si la séquence est 100% conforme RFC 3629.
     */
    static bool isValidUtf8(const char* data, size_t len) {
        if (!data) return len == 0;
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
        size_t i = 0;

        while (i < len) {
            uint8_t b0 = bytes[i];

            // 1 octet ASCII standard : 0x00 .. 0x7F
            if (b0 <= 0x7F) {
                i++;
                continue;
            }

            // Octet de continuation isolé ou invalide en début de séquence
            if (b0 < 0xC2 || b0 > 0xF4) {
                return false;
            }

            // 2 octets : 0xC2..0xDF 0x80..0xBF
            if (b0 <= 0xDF) {
                if (i + 1 >= len) return false;
                uint8_t b1 = bytes[i + 1];
                if ((b1 & 0xC0) != 0x80) return false;
                i += 2;
                continue;
            }

            // 3 octets : 0xE0..0xEF
            if (b0 <= 0xEF) {
                if (i + 2 >= len) return false;
                uint8_t b1 = bytes[i + 1];
                uint8_t b2 = bytes[i + 2];

                if ((b2 & 0xC0) != 0x80) return false;

                if (b0 == 0xE0) {
                    // Rejet overlong : b1 doit être >= 0xA0
                    if (b1 < 0xA0 || b1 > 0xBF) return false;
                } else if (b0 == 0xED) {
                    // Rejet surrogates UTF-16 (U+D800..U+DFFF) : b1 doit être <= 0x9F
                    if (b1 < 0x80 || b1 > 0x9F) return false;
                } else {
                    if ((b1 & 0xC0) != 0x80) return false;
                }

                i += 3;
                continue;
            }

            // 4 octets : 0xF0..0xF4
            if (b0 <= 0xF4) {
                if (i + 3 >= len) return false;
                uint8_t b1 = bytes[i + 1];
                uint8_t b2 = bytes[i + 2];
                uint8_t b3 = bytes[i + 3];

                if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return false;

                if (b0 == 0xF0) {
                    // Rejet overlong : b1 doit être >= 0x90
                    if (b1 < 0x90 || b1 > 0xBF) return false;
                } else if (b0 == 0xF4) {
                    // Rejet points de code > U+10FFFF : b1 doit être <= 0x8F
                    if (b1 < 0x80 || b1 > 0x8F) return false;
                } else {
                    if ((b1 & 0xC0) != 0x80) return false;
                }

                i += 4;
                continue;
            }

            return false;
        }

        return true;
    }

    static bool isValidUtf8(const String& s) {
        return isValidUtf8(s.c_str(), s.length());
    }

    /**
     * @brief Assainit une chaîne en validant chaque séquence UTF-8.
     *        Si la chaîne est déjà valide, elle est retournée sans aucune modification.
     *        Si un octet mal formé ou tronqué est détecté, seul cet octet est remplacé
     *        par le caractère de substitution officiel Unicode U+FFFD (0xEF 0xBF 0xBD).
     */
    static String sanitizeUtf8(const String& input) {
        if (isValidUtf8(input)) {
            return input; // Préservation absolue des textes valides
        }

        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(input.c_str());
        const size_t len = input.length();
        String output;
        output.reserve(len + 8);

        size_t i = 0;
        while (i < len) {
            uint8_t b0 = bytes[i];

            if (b0 <= 0x7F) {
                output += static_cast<char>(b0);
                i++;
                continue;
            }

            if (b0 >= 0xC2 && b0 <= 0xDF && (i + 1 < len)) {
                uint8_t b1 = bytes[i + 1];
                if ((b1 & 0xC0) == 0x80) {
                    output += static_cast<char>(b0);
                    output += static_cast<char>(b1);
                    i += 2;
                    continue;
                }
            } else if (b0 >= 0xE0 && b0 <= 0xEF && (i + 2 < len)) {
                uint8_t b1 = bytes[i + 1];
                uint8_t b2 = bytes[i + 2];
                bool ok = ((b2 & 0xC0) == 0x80);
                if (b0 == 0xE0) {
                    ok = ok && (b1 >= 0xA0 && b1 <= 0xBF);
                } else if (b0 == 0xED) {
                    ok = ok && (b1 >= 0x80 && b1 <= 0x9F);
                } else {
                    ok = ok && ((b1 & 0xC0) == 0x80);
                }
                if (ok) {
                    output += static_cast<char>(b0);
                    output += static_cast<char>(b1);
                    output += static_cast<char>(b2);
                    i += 3;
                    continue;
                }
            } else if (b0 >= 0xF0 && b0 <= 0xF4 && (i + 3 < len)) {
                uint8_t b1 = bytes[i + 1];
                uint8_t b2 = bytes[i + 2];
                uint8_t b3 = bytes[i + 3];
                bool ok = ((b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80);
                if (b0 == 0xF0) {
                    ok = ok && (b1 >= 0x90 && b1 <= 0xBF);
                } else if (b0 == 0xF4) {
                    ok = ok && (b1 >= 0x80 && b1 <= 0x8F);
                } else {
                    ok = ok && ((b1 & 0xC0) == 0x80);
                }
                if (ok) {
                    output += static_cast<char>(b0);
                    output += static_cast<char>(b1);
                    output += static_cast<char>(b2);
                    output += static_cast<char>(b3);
                    i += 4;
                    continue;
                }
            }

            // Octet mal formé : remplacement par U+FFFD (0xEF 0xBF 0xBD)
            output += "\xEF\xBF\xBD";
            i++;
        }

        return output;
    }

    /**
     * @brief Tronque une chaîne UTF-8 à au plus maxBytes octets sans couper
     *        au milieu d'un point de code Unicode multi-octets.
     *        Le résultat est toujours strictement conforme RFC 3629.
     */
    static String truncateUtf8(const String& input, size_t maxBytes) {
        if (input.length() <= maxBytes) {
            return input;
        }
        if (maxBytes == 0) {
            return "";
        }

        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(input.c_str());
        size_t cut = maxBytes;

        // Si cut tombe sur un octet de continuation (0x80..0xBF), reculer jusqu'à l'octet de début
        while (cut > 0 && (bytes[cut] & 0xC0) == 0x80) {
            cut--;
        }

        // Si bytes[cut] est un octet de tête multioctet (0xC0..0xFF)
        if (cut < input.length() && (bytes[cut] & 0x80) != 0) {
            size_t seqLen = 1;
            uint8_t b0 = bytes[cut];
            if ((b0 & 0xE0) == 0xC0) seqLen = 2;
            else if ((b0 & 0xF0) == 0xE0) seqLen = 3;
            else if ((b0 & 0xF8) == 0xF0) seqLen = 4;

            if (cut + seqLen > maxBytes) {
                // La séquence ne peut pas être complète dans maxBytes -> couper avant
            } else {
                cut += seqLen;
            }
        }

        String truncated = input.substring(0, cut);
        return sanitizeUtf8(truncated);
    }

    /**
     * @brief Échappement JSON centralisé et conforme RFC 8259 pour tous les champs textuels.
     *        - Conserve intégralement les caractères UTF-8 valides (> 0x7F).
     *        - Échappe les caractères de contrôle ASCII (< 0x20) en séquences standard ou \u00XX.
     *        - Échappe les guillemets (") et les barres obliques inverses (\).
     *        - Assure l'assainissement préalable si des octets corrompus sont présents.
     */
    static String escapeJson(const String& s) {
        String clean = sanitizeUtf8(s);
        String out;
        out.reserve(clean.length() + 16);

        const size_t len = clean.length();
        for (size_t i = 0; i < len; ++i) {
            char c = clean[i];
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: {
                    uint8_t u = static_cast<uint8_t>(c);
                    if (u < 32) {
                        char hexBuf[8];
                        snprintf(hexBuf, sizeof(hexBuf), "\\u%04x", u);
                        out += hexBuf;
                    } else {
                        // Octets ASCII standard (>= 32) et octets de continuation UTF-8 (>= 128)
                        out += c;
                    }
                    break;
                }
            }
        }
        return out;
    }

    /**
     * @brief Calcule le nombre de points de code Unicode d'une chaîne UTF-8 valide.
     */
    static size_t unicodeLength(const String& s) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(s.c_str());
        const size_t len = s.length();
        size_t count = 0;
        for (size_t i = 0; i < len; ++i) {
            // Un point de code commence par tout octet qui n'est pas un octet de continuation (0x80..0xBF)
            if ((bytes[i] & 0xC0) != 0x80) {
                count++;
            }
        }
        return count;
    }
};
