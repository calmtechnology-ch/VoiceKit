/**
 * @file FrenchFonts.h
 * @brief Declarations des polices vectorielles Segoe UI avec support complet UTF-8 francais pour LVGL 9.
 *
 * Glyphes inclus :
 * - ASCII standard (0x20-0x7F)
 * - Accents et minuscules francaises : e, e, e, e, a, a, c, i, i, o, u, u, u
 * - Majuscules accentuees : E, E, E, A, C
 * - Ligatures : oe, OE
 * - Typographie francaise et ponctuation : <<, >>, ', ..., --, --, o, EUR
 */

#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(font_french_12);
LV_FONT_DECLARE(font_french_14);
LV_FONT_DECLARE(font_french_bold_16);

#ifdef __cplusplus
}
#endif

