#pragma once

#include <Arduino.h>
#include <M5GFX.h>
#include <vector>

/**
 * @file FrenchUI.h
 * @brief Utilitaires de rendu et de formatage multi-lignes pour texte francais UTF-8
 *        sur ecran M5Stack CoreS3 (320x240 M5GFX) pour VoiceKit v0.6.3.2.
 *
 * Améliorations v0.6.3.2 :
 * - Calcul du retour à la ligne basé sur la LARGEUR RÉELLE EN PIXELS (display.textWidth)
 *   et non plus sur un nombre arbitraire de caractères.
 * - Mesure pixel exacte prenant en compte les glyphes accentués et ligatures françaises.
 * - Troncature propre aux frontières de codepoints avec points de suspension (...).
 * - Maintien intégral de la transcription en mémoire (la carte n'affiche que ce qui tient
 *   mais la mémoire conserve la chaîne complète).
 * - Dessin haute précision des accents français (é, è, ê, ë, à, â, ç, î, ï, ô, ù, û, ü),
 *   majuscules accentuées (É, È, Ê, À, Ç), ligatures (œ, Œ) et typographie (« », ’, —, …).
 * - Centrage horizontal automatique pour les textes d'état.
 */
class FrenchUI {
public:
    static inline bool isUtf8Start(uint8_t b) {
        return (b & 0xC0) != 0x80;
    }

    static inline size_t utf8CharLen(uint8_t b) {
        if (b < 0x80) return 1;
        if ((b & 0xE0) == 0xC0) return 2;
        if ((b & 0xF0) == 0xE0) return 3;
        if ((b & 0xF8) == 0xF0) return 4;
        return 1;
    }

    /**
     * @brief Mesure la largeur exacte en pixels d'une chaîne UTF-8 avec accents français.
     */
    static int measureUtf8PixelWidth(M5GFX& display, const String& str) {
        if (str.isEmpty()) return 0;
        int totalWidth = 0;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(str.c_str());
        char chBuf[2] = {0, 0};

        while (*p) {
            if (*p < 0x80) {
                chBuf[0] = static_cast<char>(*p++);
                totalWidth += display.textWidth(chBuf);
            } else if (p[0] == 0xC3 && p[1] != 0) {
                uint8_t c2 = p[1];
                p += 2;
                bool isUpper = (c2 >= 0x80 && c2 <= 0x9F);
                char baseChar = 'e';
                switch (c2) {
                    case 0x89: case 0xA9: baseChar = isUpper ? 'E' : 'e'; break;
                    case 0x88: case 0xA8: baseChar = isUpper ? 'E' : 'e'; break;
                    case 0x8A: case 0xAA: baseChar = isUpper ? 'E' : 'e'; break;
                    case 0x8B: case 0xAB: baseChar = isUpper ? 'E' : 'e'; break;
                    case 0x80: case 0xA0: baseChar = isUpper ? 'A' : 'a'; break;
                    case 0x82: case 0xA2: baseChar = isUpper ? 'A' : 'a'; break;
                    case 0x87: case 0xA7: baseChar = isUpper ? 'C' : 'c'; break;
                    case 0x8E: case 0xAE: baseChar = isUpper ? 'I' : 'i'; break;
                    case 0x8F: case 0xAF: baseChar = isUpper ? 'I' : 'i'; break;
                    case 0x94: case 0xB4: baseChar = isUpper ? 'O' : 'o'; break;
                    case 0x99: case 0xB9: baseChar = isUpper ? 'U' : 'u'; break;
                    case 0x9B: case 0xBB: baseChar = isUpper ? 'U' : 'u'; break;
                    case 0x9C: case 0xBC: baseChar = isUpper ? 'U' : 'u'; break;
                    default: baseChar = isUpper ? 'E' : 'e'; break;
                }
                chBuf[0] = baseChar;
                totalWidth += display.textWidth(chBuf);
            } else if (p[0] == 0xC2 && (p[1] == 0xAB || p[1] == 0xBB)) {
                p += 2;
                totalWidth += 9;
            } else if (p[0] == 0xE2 && p[1] == 0x80) {
                if (p[2] == 0x94) {
                    p += 3;
                    totalWidth += 10;
                } else if (p[2] == 0x99 || p[2] == 0x98) {
                    p += 3;
                    chBuf[0] = '\'';
                    totalWidth += display.textWidth(chBuf);
                } else if (p[2] == 0x9C || p[2] == 0x9D) {
                    p += 3;
                    chBuf[0] = '"';
                    totalWidth += display.textWidth(chBuf);
                } else if (p[2] == 0xA6) {
                    p += 3;
                    totalWidth += display.textWidth("...");
                } else {
                    p += 3;
                }
            } else if (p[0] == 0xC5 && (p[1] == 0x92 || p[1] == 0x93)) {
                bool isUpper = (p[1] == 0x92);
                p += 2;
                totalWidth += display.textWidth(isUpper ? "OE" : "oe");
            } else {
                p++;
            }
        }
        return totalWidth;
    }

    /**
     * @brief Tronque proprement une chaîne UTF-8 pour qu'elle tienne dans maxPixelWidth avec des points de suspension.
     */
    static String truncateUtf8ToPixels(M5GFX& display, const String& str, int maxPixelWidth, const String& ellipsis = "...") {
        if (measureUtf8PixelWidth(display, str) <= maxPixelWidth) return str;
        int elWidth = measureUtf8PixelWidth(display, ellipsis);
        if (maxPixelWidth <= elWidth) return ellipsis;

        int targetWidth = maxPixelWidth - elWidth;
        String result = "";
        size_t i = 0;
        size_t len = str.length();
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(str.c_str());

        while (i < len) {
            size_t clen = utf8CharLen(bytes[i]);
            if (clen == 0 || i + clen > len) break;
            String glyph = str.substring(i, i + clen);
            int gWidth = measureUtf8PixelWidth(display, glyph);
            if (measureUtf8PixelWidth(display, result) + gWidth > targetWidth) {
                break;
            }
            result += glyph;
            i += clen;
        }
        return result + ellipsis;
    }

    /**
     * @brief Découpe un texte UTF-8 en plusieurs lignes selon la LARGEUR RÉELLE EN PIXELS.
     */
    static std::vector<String> wrapUtf8WordsByPixels(M5GFX& display, const String& text, int maxPixelWidth, size_t maxLines) {
        std::vector<String> lines;
        if (text.isEmpty() || maxPixelWidth <= 0 || maxLines == 0) return lines;

        // 1. Découpage en mots UTF-8
        std::vector<String> words;
        size_t i = 0;
        size_t len = text.length();
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(text.c_str());

        while (i < len) {
            while (i < len && bytes[i] == ' ') i++;
            if (i >= len) break;
            size_t start = i;
            while (i < len && bytes[i] != ' ') {
                size_t clen = utf8CharLen(bytes[i]);
                i += (clen > 0) ? clen : 1;
            }
            words.push_back(text.substring(start, i));
        }

        if (words.empty()) return lines;

        int spaceWidth = display.textWidth(" ");
        String curLine = "";
        int curLineWidth = 0;

        for (size_t w = 0; w < words.size(); ++w) {
            const String& word = words[w];
            int wWidth = measureUtf8PixelWidth(display, word);

            if (curLineWidth == 0) {
                if (wWidth <= maxPixelWidth) {
                    curLine = word;
                    curLineWidth = wWidth;
                } else {
                    // Mot unique plus large que la ligne
                    if (lines.size() + 1 >= maxLines) {
                        lines.push_back(truncateUtf8ToPixels(display, word, maxPixelWidth));
                        return lines;
                    } else {
                        lines.push_back(truncateUtf8ToPixels(display, word, maxPixelWidth));
                        curLine = "";
                        curLineWidth = 0;
                    }
                }
            } else if (curLineWidth + spaceWidth + wWidth <= maxPixelWidth) {
                curLine += " " + word;
                curLineWidth += spaceWidth + wWidth;
            } else {
                // Dépassement de la ligne courante
                if (lines.size() + 1 >= maxLines) {
                    // Dernière ligne autorisée : troncature élégante avec le reste
                    String remainder = curLine + " " + word;
                    for (size_t rest = w + 1; rest < words.size(); ++rest) {
                        remainder += " " + words[rest];
                    }
                    lines.push_back(truncateUtf8ToPixels(display, remainder, maxPixelWidth));
                    return lines;
                }
                lines.push_back(curLine);
                if (wWidth <= maxPixelWidth) {
                    curLine = word;
                    curLineWidth = wWidth;
                } else {
                    curLine = truncateUtf8ToPixels(display, word, maxPixelWidth);
                    curLineWidth = measureUtf8PixelWidth(display, curLine);
                }
            }
        }

        if (!curLine.isEmpty() && lines.size() < maxLines) {
            lines.push_back(curLine);
        }
        return lines;
    }

    /**
     * @brief Affiche une ligne de texte français UTF-8 en restituant géométriquement les accents.
     */
    static void drawFrenchLine(M5GFX& display, int x, int y, const char* utf8, uint16_t fg, uint16_t bg) {
        display.setTextSize(1);
        display.setTextColor(fg, bg);

        int cx = x;
        char chBuf[2] = {0, 0};
        const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8);

        while (*p) {
            if (*p < 0x80) {
                chBuf[0] = static_cast<char>(*p++);
                display.drawString(chBuf, cx, y);
                cx += display.textWidth(chBuf);
            } else if (p[0] == 0xC3 && p[1] != 0) {
                uint8_t c2 = p[1];
                p += 2;
                bool isUpper = (c2 >= 0x80 && c2 <= 0x9F);
                char baseChar = ' ';
                int accentType = 0; // 1: aigu, 2: grave, 3: circonflexe, 4: tréma, 5: cédille

                switch (c2) {
                    case 0x89: case 0xA9: baseChar = isUpper ? 'E' : 'e'; accentType = 1; break; // é, É
                    case 0x88: case 0xA8: baseChar = isUpper ? 'E' : 'e'; accentType = 2; break; // è, È
                    case 0x8A: case 0xAA: baseChar = isUpper ? 'E' : 'e'; accentType = 3; break; // ê, Ê
                    case 0x8B: case 0xAB: baseChar = isUpper ? 'E' : 'e'; accentType = 4; break; // ë, Ë
                    case 0x80: case 0xA0: baseChar = isUpper ? 'A' : 'a'; accentType = 2; break; // à, À
                    case 0x82: case 0xA2: baseChar = isUpper ? 'A' : 'a'; accentType = 3; break; // â, Â
                    case 0x87: case 0xA7: baseChar = isUpper ? 'C' : 'c'; accentType = 5; break; // ç, Ç
                    case 0x8E: case 0xAE: baseChar = isUpper ? 'I' : 'i'; accentType = 3; break; // î, Î
                    case 0x8F: case 0xAF: baseChar = isUpper ? 'I' : 'i'; accentType = 4; break; // ï, Ï
                    case 0x94: case 0xB4: baseChar = isUpper ? 'O' : 'o'; accentType = 3; break; // ô, Ô
                    case 0x99: case 0xB9: baseChar = isUpper ? 'U' : 'u'; accentType = 2; break; // ù, Ù
                    case 0x9B: case 0xBB: baseChar = isUpper ? 'U' : 'u'; accentType = 3; break; // û, Û
                    case 0x9C: case 0xBC: baseChar = isUpper ? 'U' : 'u'; accentType = 4; break; // ü, Ü
                    default: baseChar = isUpper ? 'E' : 'e'; break;
                }

                chBuf[0] = baseChar;
                display.drawString(chBuf, cx, y);
                int w = display.textWidth(chBuf);
                int ax = cx + (w / 2);
                int ay = y + (isUpper ? 0 : 1);

                if (accentType == 1) {
                    display.drawLine(ax - 1, ay, ax + 2, ay - 2, fg);
                } else if (accentType == 2) {
                    display.drawLine(ax + 1, ay, ax - 2, ay - 2, fg);
                } else if (accentType == 3) {
                    display.drawLine(ax - 2, ay, ax, ay - 2, fg);
                    display.drawLine(ax, ay - 2, ax + 2, ay, fg);
                } else if (accentType == 4) {
                    display.drawPixel(ax - 2, ay - 1, fg);
                    display.drawPixel(ax + 2, ay - 1, fg);
                } else if (accentType == 5) {
                    int cy = y + 7;
                    display.drawLine(ax, cy, ax - 1, cy + 2, fg);
                }
                cx += w;
            } else if (p[0] == 0xC2 && (p[1] == 0xAB || p[1] == 0xBB)) {
                bool isLeft = (p[1] == 0xAB);
                p += 2;
                int cy = y + 4;
                int d = 2;
                for (int k = 0; k < 2; ++k) {
                    int gx = cx + k * 4;
                    if (isLeft) {
                        display.drawLine(gx + d, cy - d, gx, cy, fg);
                        display.drawLine(gx, cy, gx + d, cy + d, fg);
                    } else {
                        display.drawLine(gx, cy - d, gx + d, cy, fg);
                        display.drawLine(gx + d, cy, gx, cy + d, fg);
                    }
                }
                cx += 9;
            } else if (p[0] == 0xE2 && p[1] == 0x80) {
                if (p[2] == 0x94) {
                    p += 3;
                    int cy = y + 4;
                    display.drawFastHLine(cx, cy, 8, fg);
                    cx += 10;
                } else if (p[2] == 0x99 || p[2] == 0x98) {
                    p += 3;
                    chBuf[0] = '\'';
                    display.drawString(chBuf, cx, y);
                    cx += display.textWidth(chBuf);
                } else if (p[2] == 0x9C || p[2] == 0x9D) {
                    p += 3;
                    chBuf[0] = '"';
                    display.drawString(chBuf, cx, y);
                    cx += display.textWidth(chBuf);
                } else if (p[2] == 0xA6) {
                    p += 3;
                    display.drawString("...", cx, y);
                    cx += display.textWidth("...");
                } else {
                    p += 3;
                }
            } else if (p[0] == 0xC5 && (p[1] == 0x92 || p[1] == 0x93)) {
                bool isUpper = (p[1] == 0x92);
                p += 2;
                display.drawString(isUpper ? "OE" : "oe", cx, y);
                cx += display.textWidth(isUpper ? "OE" : "oe");
            } else {
                p++;
            }
        }
    }

    /**
     * @brief Affiche une ligne de texte français centrée horizontalement autour de centerX.
     */
    static void drawCenteredFrenchLine(M5GFX& display, int centerX, int y, const char* utf8, uint16_t fg, uint16_t bg) {
        int w = measureUtf8PixelWidth(display, utf8);
        int x = centerX - (w / 2);
        drawFrenchLine(display, x, y, utf8, fg, bg);
    }
};
