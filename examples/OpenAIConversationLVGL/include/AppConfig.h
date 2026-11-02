/**
 * @file AppConfig.h
 * @brief Constantes et parametres de configuration pour OpenAIConversationLVGL v0.1.0.
 */

#pragma once

#include <stdint.h>
#include <lvgl.h>

namespace AppConfig {

// Metadonnees de l'application
constexpr const char* APP_NAME = "OpenAIConversationLVGL";
constexpr const char* APP_VERSION = "v0.1.0.1";
constexpr const char* OPENAI_MODEL = "gpt-realtime-2.1-mini";
constexpr const char* OPENAI_VOICE = "alloy";
constexpr const char* OPENAI_LANGUAGE = "fr-FR";
constexpr uint8_t DEFAULT_VOLUME = 70;

// Instruction conversationnelle concise (Section 12)
constexpr const char* CONVERSATION_INSTRUCTION =
    "Tu es un assistant vocal français. "
    "Réponds directement en une phrase courte, "
    "généralement 15 à 25 mots maximum. "
    "Évite les introductions, répétitions "
    "et formules de politesse inutiles. "
    "Donne davantage de détails uniquement "
    "si l'utilisateur le demande.";

// Dimensions physiques ecran M5Stack CoreS3
constexpr int32_t SCREEN_WIDTH = 320;
constexpr int32_t SCREEN_HEIGHT = 240;

// Dimensions du buffer partiel LVGL (Section 10) : 320 x 20 pixels en RGB565 (12 800 octets)
constexpr int32_t BUFFER_LINES = 20;
constexpr size_t BUFFER_PIXELS = SCREEN_WIDTH * BUFFER_LINES;

// Zones graphiques
constexpr int32_t HEADER_Y = 0;
constexpr int32_t HEADER_HEIGHT = 30;

constexpr int32_t ORB_CENTER_X = 160;
constexpr int32_t ORB_CENTER_Y = 72;
constexpr int32_t ORB_BOX_SIZE = 74;

constexpr int32_t STATE_TEXT_Y = 110;
constexpr int32_t STATE_TEXT_HEIGHT = 22;

constexpr int32_t USER_CARD_X = 10;
constexpr int32_t USER_CARD_Y = 136;
constexpr int32_t USER_CARD_W = 300;
constexpr int32_t USER_CARD_H = 28;

constexpr int32_t ASST_CARD_X = 10;
constexpr int32_t ASST_CARD_Y = 168;
constexpr int32_t ASST_CARD_W = 300;
constexpr int32_t ASST_CARD_H = 28;

constexpr int32_t MAIN_BTN_X = 10;
constexpr int32_t MAIN_BTN_Y = 200;
constexpr int32_t MAIN_BTN_W = 300;
constexpr int32_t MAIN_BTN_H = 35;

// Couleurs Dark Mode Premium (24-bit RGB)
constexpr uint32_t COLOR_BG_HEX          = 0x080E14; // Bleu nuit profond
constexpr uint32_t COLOR_CARD_BG_HEX     = 0x0D1522; // Fond cartes sombre contraste
constexpr uint32_t COLOR_CARD_BORDER_HEX = 0x1E3C4E; // Bordure ardoise
constexpr uint32_t COLOR_TEXT_PRI_HEX    = 0xFFFFFF; // Blanc pur
constexpr uint32_t COLOR_TEXT_SEC_HEX    = 0xDEDBDE; // Argent clair doux
constexpr uint32_t COLOR_TEXT_MUTED_HEX  = 0x6B7D8D; // Gris-bleu estompe

constexpr uint32_t COLOR_CYAN_HEX        = 0x00D2D3; // PRET (Cyan respiration)
constexpr uint32_t COLOR_CORAL_HEX       = 0xFF6B6B; // ECOUTE (Corail pulsation)
constexpr uint32_t COLOR_PURPLE_HEX      = 0x9B59B6; // ANALYSE (Violet doux)
constexpr uint32_t COLOR_TURQUOISE_HEX   = 0x26C6DA; // REPONSE (Turquoise ondes)
constexpr uint32_t COLOR_RED_HEX         = 0xE74C3C; // ERREUR (Rouge alerte)
constexpr uint32_t COLOR_STOP_HEX        = 0xE67E22; // Action STOP

// Timeouts et surveillances de securite non-bloquantes (Section 9)
constexpr uint32_t MIC_TIMEOUT_MS = 15000;         // Duree max capture continue (15 s)
constexpr uint32_t PROCESSING_TIMEOUT_MS = 15000;  // Duree max attente reponse serveur (15 s)
constexpr uint32_t STOPPED_RECOVERY_MS = 1000;     // Delai transition automatique STOPPED -> READY (1 s)
constexpr uint32_t ERROR_RECOVERY_MS = 15000;      // Delai tentative reprise post-erreur si Wi-Fi (15 s)

// Cadence des diagnostics periodiques
constexpr uint32_t DIAG_INTERVAL_MS = 10000;

// Frequence d'animation cible de l'orbe : ~18 fps (~55 ms)
constexpr uint32_t ORB_FRAME_INTERVAL_MS = 55;

} // namespace AppConfig

