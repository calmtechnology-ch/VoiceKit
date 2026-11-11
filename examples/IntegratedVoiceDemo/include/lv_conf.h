/**
 * @file lv_conf.h
 * @brief Configuration LVGL 9 pour M5Stack CoreS3 (320x240) - IntegratedVoiceDemo v0.6.5
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_COLOR_DEPTH 16

/*=========================
   MEMORY SETTINGS
 *=========================*/
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    /* 32 KB pool mémoire pour les widgets LVGL */
    #define LV_MEM_SIZE (32U * 1024U)
    #define LV_MEM_POOL_EXPAND_SIZE 0
    #define LV_MEM_ADR 0
#endif

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD 33   /* ~30 FPS */

/*====================
   DRAW UNIT SETTINGS
 *====================*/
#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW
    #define LV_DRAW_SW_DRAW_UNIT_CNT 1
    #define LV_DRAW_SW_COMPLEX 1
#endif

/*====================
   FONT SETTINGS
 *====================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1

#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   WIDGETS
 *====================*/
#define LV_USE_OBJ 1
#define LV_USE_BUTTON 1
#define LV_USE_LABEL 1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_LABEL_LONG_TXT_HINT 1
#define LV_USE_ARC 1

/*====================
   THEME
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/*====================
   LOGGING & DEBUG
 *====================*/
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#endif /* LV_CONF_H */

