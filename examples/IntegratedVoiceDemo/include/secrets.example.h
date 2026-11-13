#pragma once

// =============================================================================
// VoiceKit v0.6.5.2 — IntegratedVoiceDemo Configuration des Identifiants
// Copiez ce fichier sous le nom "secrets.h" et renseignez vos identifiants réels.
// =============================================================================

#define WIFI_SSID     "VOTRE_SSID_WIFI"
#define WIFI_PASSWORD "VOTRE_MOT_DE_PASSE_WIFI"

// Clé API Google Gemini Live (utilisée par défaut si présente)
#define GEMINI_API_KEY "VOTRE_CLE_API_GEMINI"

// Clé API OpenAI Realtime (utilisée si sélectionnée ou si clé Gemini absente)
#define OPENAI_API_KEY "VOTRE_CLE_API_OPENAI"

// Choix optionnel du fournisseur vocal IA (décommenter pour forcer) :
// 0 = Google Gemini Live
// 1 = OpenAI Realtime
// #define VOICE_PROVIDER 0

