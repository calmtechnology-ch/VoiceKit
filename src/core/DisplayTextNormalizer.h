#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

#ifdef ARDUINO
#include <Arduino.h>
#elif __has_include("native_mock/Arduino.h")
#include "native_mock/Arduino.h"
#elif __has_include("test/native_mock/Arduino.h")
#include "test/native_mock/Arduino.h"
#elif __has_include("tests/native_mock/Arduino.h")
#include "tests/native_mock/Arduino.h"
#else
#include <Arduino.h>
#endif

// =============================================================================
// DisplayTextNormalizer : Normalisation pour écrans matériels à polices ASCII
// VoiceKit v0.4.1
// 
// IMPORTANT ARCHITECTURE :
// Cet utilitaire est STRICTEMENT dédié aux afficheurs graphiques (ex: LCD, TFT, LVGL)
// ne disposant que d'une table de caractères ASCII standard 7-bits (0..127).
// Il ne doit JAMAIS être inséré dans le pipeline audio, le transport WebSocket,
// les requêtes Gemini Live ni les transcriptions vocales de VoiceKit, qui opèrent
// en UTF-8 pur intégral de bout en bout pour garantir la prononciation correcte.
// =============================================================================

class DisplayTextNormalizer {
public:
    static std::string normalize(const std::string& input) {
        std::string output;
        output.reserve(input.size());

        size_t i = 0;
        const size_t len = input.size();
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(input.data());

        while (i < len) {
            uint8_t b0 = bytes[i];

            // 1. ASCII standard (1 octet : 0x00..0x7F)
            if ((b0 & 0x80) == 0) {
                output.push_back(static_cast<char>(b0));
                i++;
                continue;
            }

            // 2. Sequence UTF-8 2 octets (0xC0..0xDF)
            if ((b0 & 0xE0) == 0xC0) {
                if (i + 1 < len) {
                    uint8_t b1 = bytes[i + 1];
                    if ((b1 & 0xC0) == 0x80) {
                        uint32_t cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
                        const char* repl = mapCodepoint(cp);
                        if (repl) {
                            output.append(repl);
                        } else {
                            // Non accentue : conserver la sequence UTF-8 originale intacte
                            output.push_back(static_cast<char>(b0));
                            output.push_back(static_cast<char>(b1));
                        }
                        i += 2;
                        continue;
                    }
                }
            }
            // 3. Sequence UTF-8 3 octets (0xE0..0xEF)
            else if ((b0 & 0xF0) == 0xE0) {
                if (i + 2 < len) {
                    uint8_t b1 = bytes[i + 1];
                    uint8_t b2 = bytes[i + 2];
                    if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
                        uint32_t cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
                        const char* repl = mapCodepoint(cp);
                        if (repl) {
                            output.append(repl);
                        } else {
                            output.push_back(static_cast<char>(b0));
                            output.push_back(static_cast<char>(b1));
                            output.push_back(static_cast<char>(b2));
                        }
                        i += 3;
                        continue;
                    }
                }
            }
            // 4. Sequence UTF-8 4 octets (0xF0..0xF7)
            else if ((b0 & 0xF8) == 0xF0) {
                if (i + 3 < len) {
                    uint8_t b1 = bytes[i + 1];
                    uint8_t b2 = bytes[i + 2];
                    uint8_t b3 = bytes[i + 3];
                    if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
                        output.push_back(static_cast<char>(b0));
                        output.push_back(static_cast<char>(b1));
                        output.push_back(static_cast<char>(b2));
                        output.push_back(static_cast<char>(b3));
                        i += 4;
                        continue;
                    }
                }
            }

            // Octet isole ou mal forme : conserver tel quel sans corruption
            output.push_back(static_cast<char>(b0));
            i++;
        }

        return output;
    }

    static String normalize(const String& input) {
        std::string inStr(input.c_str());
        std::string outStr = normalize(inStr);
        return String(outStr.c_str());
    }

private:
    static const char* mapCodepoint(uint32_t cp) {
        switch (cp) {
            // Lettres minuscules 'a'
            case 0x00E0: // à
            case 0x00E1: // á
            case 0x00E2: // â
            case 0x00E3: // ã
            case 0x00E4: // ä
            case 0x00E5: // å
                return "a";

            // Lettres majuscules 'A'
            case 0x00C0: // À
            case 0x00C1: // Á
            case 0x00C2: // Â
            case 0x00C3: // Ã
            case 0x00C4: // Ä
            case 0x00C5: // Å
                return "A";

            // Lettres minuscules 'e'
            case 0x00E8: // è
            case 0x00E9: // é
            case 0x00EA: // ê
            case 0x00EB: // ë
                return "e";

            // Lettres majuscules 'E'
            case 0x00C8: // È
            case 0x00C9: // É
            case 0x00CA: // Ê
            case 0x00CB: // Ë
                return "E";

            // Lettres minuscules 'i'
            case 0x00EC: // ì
            case 0x00ED: // í
            case 0x00EE: // î
            case 0x00EF: // ï
                return "i";

            // Lettres majuscules 'I'
            case 0x00CC: // Ì
            case 0x00CD: // Í
            case 0x00CE: // Î
            case 0x00CF: // Ï
                return "I";

            // Lettres minuscules 'o'
            case 0x00F2: // ò
            case 0x00F3: // ó
            case 0x00F4: // ô
            case 0x00F5: // õ
            case 0x00F6: // ö
                return "o";

            // Lettres majuscules 'O'
            case 0x00D2: // Ò
            case 0x00D3: // Ó
            case 0x00D4: // Ô
            case 0x00D5: // Õ
            case 0x00D6: // Ö
                return "O";

            // Lettres minuscules 'u'
            case 0x00F9: // ù
            case 0x00FA: // ú
            case 0x00FB: // û
            case 0x00FC: // ü
                return "u";

            // Lettres majuscules 'U'
            case 0x00D9: // Ù
            case 0x00DA: // Ú
            case 0x00DB: // Û
            case 0x00DC: // Ü
                return "U";

            // Cédille
            case 0x00E7: // ç
                return "c";
            case 0x00C7: // Ç
                return "C";

            // Ligatures
            case 0x0153: // œ
                return "oe";
            case 0x0152: // Œ
                return "OE";
            case 0x00E6: // æ
                return "ae";
            case 0x00C6: // Æ
                return "AE";

            // Tilde espagnol
            case 0x00F1: // ñ
                return "n";
            case 0x00D1: // Ñ
                return "N";

            // Apostrophe typographique
            case 0x2018: // ‘
            case 0x2019: // ’
                return "'";

            // Espace insecable
            case 0x00A0:
                return " ";

            default:
                return nullptr;
        }
    }
};
