#pragma once

// =============================================================================
// VoiceKit v0.6.5.2 — IntegratedVoiceDemo Configuration des Identifiants
// Copiez ce fichier sous le nom "secrets.h" et renseignez vos identifiants réels.
// =============================================================================

// =============================================================================
// 1. CONFIGURATION RÉSEAU WI-FI
// =============================================================================
#define WIFI_SSID     "VOTRE_SSID_WIFI"
#define WIFI_PASSWORD "VOTRE_MOT_DE_PASSE_WIFI"

// =============================================================================
// 2. SÉLECTION FACILE DU FOURNISSEUR IA VOCAL
//    (Décommentez une seule ligne pour choisir le moteur actif)
// =============================================================================
#define USE_GEMINI      // <-- Google Gemini Live (Actif par défaut)
// #define USE_OPENAI   // <-- OpenAI Realtime (Décommenter pour basculer sur OpenAI)

// =============================================================================
// 3. CLÉS D'AUTHENTIFICATION API
// =============================================================================

// Clé API Google Gemini Live (débutant par AQ...)
#define GEMINI_KEY    "VOTRE_CLE_API_GEMINI"
#define GEMINI_API_KEY GEMINI_KEY

// Clé API OpenAI Realtime (débutant par sk-proj-...)
#define OPENAI_API_KEY "VOTRE_CLE_API_OPENAI"
