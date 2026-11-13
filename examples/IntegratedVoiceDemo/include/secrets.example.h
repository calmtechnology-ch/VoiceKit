#pragma once

// =============================================================================
// VoiceKit v0.6.5.2 — IntegratedVoiceDemo Modèle de Configuration (Template)
// ATTENTION : Ce fichier est uniquement un modèle d'exemple.
// Pour configurer votre application, copiez ce fichier sous le nom "secrets.h"
// dans ce même répertoire et renseignez vos identifiants réels.
// Le fichier "secrets.h" est ignoré par Git et ne doit jamais être commité.
// =============================================================================

// =============================================================================
// 1. CONFIGURATION RÉSEAU WI-FI
// =============================================================================
#define WIFI_SSID     "VOTRE_SSID_WIFI"
#define WIFI_PASSWORD "VOTRE_MOT_DE_PASSE_WIFI"

// =============================================================================
// 2. SÉLECTION EXPLICITE DU FOURNISSEUR IA VOCAL
//    (Décommentez une seule ligne pour choisir le moteur actif)
// =============================================================================
#define USE_GEMINI      // <-- Google Gemini Live (Actif par défaut)
// #define USE_OPENAI   // <-- OpenAI Realtime (Décommenter pour basculer sur OpenAI)

// =============================================================================
// 3. CLÉS D'AUTHENTIFICATION API
// =============================================================================

// Clé API Google Gemini Live
#define GEMINI_KEY    "VOTRE_CLE_API_GEMINI"
#define GEMINI_API_KEY GEMINI_KEY

// Clé API OpenAI Realtime
#define OPENAI_API_KEY "VOTRE_CLE_API_OPENAI"
