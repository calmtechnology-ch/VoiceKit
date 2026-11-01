#pragma once

#include <Arduino.h>
#include <M5GFX.h>
#include <vector>

/**
 * @file FrenchUI.h
 * @brief Utilitaires de rendu et de formatage multi-lignes pour texte francais UTF-8
 *        sur ecran M5Stack CoreS3 (320x240 M5GFX).
 *
 * VoiceKit v0.6.3.1 :
 * - Decoupage strict des lignes respectant les mots entiers (word-wrap).
 * - Preservation absolue des frontieres de codepoints UTF-8 (zero coupure d'octet).
 * - Troncature propre avec points de suspension sans sequelle d'octet tronque.
 * - Rendu haute precision des accents francais (e, e, e, e, a, a, c, i, i, o, u, u, u),
 *   majuscules accentuees (E, E, E, A, C) et typographie (« », ', -, ...)
 *   sans necessiter d'embarquer des polices CJK volumineuses (> 1.5 Mo).
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
     * @brief Tronque une chaine UTF-8 a au plus maxBytes en garantissant l'integrite du dernier codepoint.
     */
    static String truncateUtf8(const String& str, size_t maxBytes, const String& ellipsis = "...") {
        if (str.length() <= maxBytes) return str;
        size_t elen = ellipsis.length();
        if (maxBytes <= elen) return ellipsis.substring(0, maxBytes);
        size_t limit = maxBytes - elen;
        size_t idx = limit;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(str.c_str());
        while (idx > 0 && !isUtf8Start(p[idx])) {
            idx--;
        }
        return str.substring(0, idx) + ellipsis;
    }

    /**
     * @brief Decoupe un texte UTF-8 en plusieurs lignes sans couper de mots ni de caracteres multi-octets.
     */
    static std::vector<String> wrapUtf8Words(const String& text, size_t maxCharsPerLine, size_t maxLines) {
        std::vector<String> lines;
        if (text.isEmpty()) return lines;

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

        String curLine = "";
        size_t curGlyphs = 0;

        for (size_t w = 0; w < words.size(); ++w) {
            const String& word = words[w];
            size_t wGlyphs = 0;
            for (size_t b = 0; b < word.length(); ) {
                size_t clen = utf8CharLen(static_cast<uint8_t>(word[b]));
                b += (clen > 0) ? clen : 1;
                wGlyphs++;
            }

            if (curGlyphs == 0) {
                curLine = word;
                curGlyphs = wGlyphs;
            } else if (curGlyphs + 1 + wGlyphs <= maxCharsPerLine) {
                curLine += " " + word;
                curGlyphs += 1 + wGlyphs;
            } else {
                if (lines.size() + 1 >= maxLines) {
                    // Derniere ligne autorisee atteinte : ajout propre de suspension
                    curLine = truncateUtf8(curLine + " " + word, maxCharsPerLine, "...");
                    lines.push_back(curLine);
                    return lines;
                }
                lines.push_back(curLine);
                curLine = word;
                curGlyphs = wGlyphs;
            }
        }

        if (!curLine.isEmpty() && lines.size() < maxLines) {
            lines.push_back(curLine);
        }
        return lines;
    }

    /**
     * @brief Affiche une ligne de texte francais UTF-8 en restituant les accents et glyphes.
     */
    static void drawFrenchLine(M5GFX& display, int x, int y, const char* utf8, uint16_t fg, uint16_t bg) {
        display.setTextSize(1);
        display.setTextColor(fg, bg);

        int cx = x;
        char chBuf[2] = {0, 0};
        const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8);

        while (*p) {
            if (*p < 0x80) {
                // Caractere ASCII standard
                chBuf[0] = static_cast<char>(*p++);
                display.drawString(chBuf, cx, y);
                cx += display.textWidth(chBuf);
            } else if (p[0] == 0xC3 && p[1] != 0) {
                // Bloc Latin-1 supplement (accents francais majeurs)
                uint8_t c2 = p[1];
                p += 2;
                bool isUpper = (c2 >= 0x80 && c2 <= 0x9F);
                char baseChar = ' ';
                int accentType = 0; // 1: aigu, 2: grave, 3: circonflexe, 4: trema, 5: cedille

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
                    // Accent aigu (/)
                    display.drawLine(ax - 1, ay, ax + 2, ay - 2, fg);
                } else if (accentType == 2) {
                    // Accent grave (\)
                    display.drawLine(ax + 1, ay, ax - 2, ay - 2, fg);
                } else if (accentType == 3) {
                    // Accent circonflexe (^)
                    display.drawLine(ax - 2, ay, ax, ay - 2, fg);
                    display.drawLine(ax, ay - 2, ax + 2, ay, fg);
                } else if (accentType == 4) {
                    // Trema (..)
                    display.drawPixel(ax - 2, ay - 1, fg);
                    display.drawPixel(ax + 2, ay - 1, fg);
                } else if (accentType == 5) {
                    // Cedille (,) sous la lettre
                    int cy = y + 7;
                    display.drawLine(ax, cy, ax - 1, cy + 2, fg);
                }
                cx += w;
            } else if (p[0] == 0xC2 && (p[1] == 0xAB || p[1] == 0xBB)) {
                // Guillemets francais « »
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
                    // Tiret cadratin (—)
                    p += 3;
                    int cy = y + 4;
                    display.drawFastHLine(cx, cy, 8, fg);
                    cx += 10;
                } else if (p[2] == 0x99) {
                    // Apostrophe typographique (’)
                    p += 3;
                    chBuf[0] = '\'';
                    display.drawString(chBuf, cx, y);
                    cx += display.textWidth(chBuf);
                } else if (p[2] == 0xA6) {
                    // Points de suspension (…)
                    p += 3;
                    display.drawString("...", cx, y);
                    cx += display.textWidth("...");
                } else {
                    p += 3;
                }
            } else if (p[0] == 0xC5 && (p[1] == 0x92 || p[1] == 0x93)) {
                // Ligature oe / OE
                bool isUpper = (p[1] == 0x92);
                p += 2;
                display.drawString(isUpper ? "OE" : "oe", cx, y);
                cx += display.textWidth(isUpper ? "OE" : "oe");
            } else {
                // Octet inconnu ou non supporte : avance securisee
                p++;
            }
        }
    }

    /**
     * @brief Affiche un texte dans un cadre avec retour a la ligne automatique et effacement de zone.
     */
    static void drawCardContent(M5GFX& display, int boxX, int boxY, int boxW, int boxH,
                                const String& text, const String& placeholder,
                                uint16_t fgText, uint16_t fgPlaceholder, uint16_t bg, uint16_t borderCol,
                                size_t maxCharsPerLine = 46, size_t maxLines = 3) {
        // Redessine le fond de la carte pour effacer l'ancien contenu
        display.fillRoundRect(boxX, boxY, boxW, boxH, 4, bg);
        display.drawRoundRect(boxX, boxY, boxW, boxH, 4, borderCol);

        int textX = boxX + 6;
        int textY = boxY + 6;
        int lineSpacing = 13;

        if (text.isEmpty()) {
            display.setTextColor(fgPlaceholder, bg);
            drawFrenchLine(display, textX, textY, placeholder.c_str(), fgPlaceholder, bg);
            return;
        }

        std::vector<String> lines = wrapUtf8Words(text, maxCharsPerLine, maxLines);
        for (size_t i = 0; i < lines.size() && i < maxLines; ++i) {
            drawFrenchLine(display, textX, textY + (i * lineSpacing), lines[i].c_str(), fgText, bg);
        }
    }
};

