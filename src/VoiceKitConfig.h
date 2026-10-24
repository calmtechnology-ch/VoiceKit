#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include "hal/IAudioOutput.h"
#include "hal/IAudioInput.h"

/**
 * @brief Mode de gestion du reseau Wi-Fi.
 */
enum class WiFiMode : uint8_t {
    MANAGED_WIFI = 0,   ///< VoiceKit gere la connexion Wi-Fi et la synchronisation NTP
    EXTERNAL_WIFI = 1   ///< L'application hote gere entierement le Wi-Fi et NTP
};

/**
 * @brief Politique de traitement lorsque le moteur est deja en train de parler.
 */
enum class QueuePolicy : uint8_t {
    REJECT_IF_BUSY = 0,   ///< Rejette immediatement la nouvelle demande si occupe (defaut)
    QUEUE_FIFO = 1,       ///< Enfile la demande dans une file bornee (maxQueueSize)
    INTERRUPT_CURRENT = 2 ///< Interrompt la parole en cours et commence immediatement
};

/**
 * @brief Etats operationnels de VoxCoreVoice.
 */
enum class VoiceState : uint8_t {
    UNINITIALIZED = 0,
    IDLE,
    CONNECTING_WIFI,
    CONNECTING_SERVICE,
    READY,
    GENERATING,
    WAITING_RESPONSE = GENERATING,
    PROCESSING = GENERATING,  ///< Traitement / réflexion Gemini Live en cours (mode conversation v0.4.0)
    SPEAKING,
    LISTENING,        ///< Acquisition microphone et streaming en cours (STT / Conversation)
    TRANSCRIBING,     ///< Acquisition terminée, attente de la transcription finale Gemini (STT)
    ERROR_STATE,
    STOPPED,          ///< Interruption volontaire (STOP)
    DISCONNECTED      ///< Déconnexion réseau / service
};

inline const char* voiceStateToString(VoiceState state) {
    switch (state) {
        case VoiceState::UNINITIALIZED:      return "NON_INITIALISE";
        case VoiceState::IDLE:               return "EN_REPOS";
        case VoiceState::CONNECTING_WIFI:    return "CONNEXION_WIFI";
        case VoiceState::CONNECTING_SERVICE: return "CONNEXION_SERVICE";
        case VoiceState::READY:              return "PRET";
        case VoiceState::GENERATING:         return "REFLEXION_EN_COURS";
        case VoiceState::SPEAKING:           return "RESTITUTION_EN_COURS";
        case VoiceState::LISTENING:          return "ECOUTE_EN_COURS";
        case VoiceState::TRANSCRIBING:       return "TRANSCRIPTION_EN_COURS";
        case VoiceState::ERROR_STATE:        return "ERREUR";
        case VoiceState::STOPPED:            return "STOPPE";
        case VoiceState::DISCONNECTED:       return "DECONNECTE";
        default:                             return "INCONNU";
    }
}

/**
 * @brief Style d'élocution vocale (v0.4.1).
 */
enum class VoiceStyle : uint8_t {
    NATURAL = 0,       ///< Style naturel, fluide et spontané (défaut)
    PROFESSIONAL,      ///< Style professionnel, posé et clair
    EXPRESSIVE,        ///< Style expressif, vivant et nuancé
    NARRATIVE          ///< Style narratif, immersif et conteur
};

/**
 * @brief Tonalité vocale (v0.4.1).
 */
enum class VoiceTone : uint8_t {
    NEUTRAL = 0,       ///< Tonalité neutre et sobre (défaut)
    CALM,              ///< Tonalité calme, douce et apaisante
    WARM,              ///< Tonalité chaleureuse, accueillante et bienveillante
    CHEERFUL,          ///< Tonalité enjouée, dynamique et souriante
    SERIOUS            ///< Tonalité sérieuse, formelle et rigoureuse
};

inline const char* voiceStyleToString(VoiceStyle style) {
    switch (style) {
        case VoiceStyle::NATURAL:      return "natural";
        case VoiceStyle::PROFESSIONAL: return "professional";
        case VoiceStyle::EXPRESSIVE:   return "expressive";
        case VoiceStyle::NARRATIVE:    return "narrative";
        default:                       return "natural";
    }
}

inline VoiceStyle stringToVoiceStyle(const String& str) {
    String s = str;
    s.toLowerCase();
    s.trim();
    if (s == "professional") return VoiceStyle::PROFESSIONAL;
    if (s == "expressive")   return VoiceStyle::EXPRESSIVE;
    if (s == "narrative")    return VoiceStyle::NARRATIVE;
    return VoiceStyle::NATURAL;
}

inline const char* voiceToneToString(VoiceTone tone) {
    switch (tone) {
        case VoiceTone::NEUTRAL:  return "neutral";
        case VoiceTone::CALM:     return "calm";
        case VoiceTone::WARM:     return "warm";
        case VoiceTone::CHEERFUL: return "cheerful";
        case VoiceTone::SERIOUS:  return "serious";
        default:                  return "neutral";
    }
}

inline VoiceTone stringToVoiceTone(const String& str) {
    String s = str;
    s.toLowerCase();
    s.trim();
    if (s == "calm")     return VoiceTone::CALM;
    if (s == "warm")     return VoiceTone::WARM;
    if (s == "cheerful") return VoiceTone::CHEERFUL;
    if (s == "serious")  return VoiceTone::SERIOUS;
    return VoiceTone::NEUTRAL;
}

/**
 * @brief Wrapper transparent acceptant aussi bien l'énumérateur VoiceStyle qu'une chaîne C-string ou String.
 */
struct VoiceStyleWrapper {
    VoiceStyle val = VoiceStyle::NATURAL;
    VoiceStyleWrapper() = default;
    VoiceStyleWrapper(VoiceStyle s) : val(s) {}
    VoiceStyleWrapper(const char* s) : val(stringToVoiceStyle(s ? s : "")) {}
    VoiceStyleWrapper(const String& s) : val(stringToVoiceStyle(s)) {}
    VoiceStyleWrapper& operator=(VoiceStyle s) { val = s; return *this; }
    VoiceStyleWrapper& operator=(const char* s) { val = stringToVoiceStyle(s ? s : ""); return *this; }
    VoiceStyleWrapper& operator=(const String& s) { val = stringToVoiceStyle(s); return *this; }
    operator VoiceStyle() const { return val; }
    bool operator==(VoiceStyle other) const { return val == other; }
    bool operator!=(VoiceStyle other) const { return val != other; }
    bool operator==(const char* other) const { return val == stringToVoiceStyle(other ? other : ""); }
    bool operator!=(const char* other) const { return !(*this == other); }
    const char* c_str() const { return voiceStyleToString(val); }
    String toString() const { return String(voiceStyleToString(val)); }
};

/**
 * @brief Wrapper transparent acceptant aussi bien l'énumérateur VoiceTone qu'une chaîne C-string ou String.
 */
struct VoiceToneWrapper {
    VoiceTone val = VoiceTone::NEUTRAL;
    VoiceToneWrapper() = default;
    VoiceToneWrapper(VoiceTone t) : val(t) {}
    VoiceToneWrapper(const char* t) : val(stringToVoiceTone(t ? t : "")) {}
    VoiceToneWrapper(const String& t) : val(stringToVoiceTone(t)) {}
    VoiceToneWrapper& operator=(VoiceTone t) { val = t; return *this; }
    VoiceToneWrapper& operator=(const char* t) { val = stringToVoiceTone(t ? t : ""); return *this; }
    VoiceToneWrapper& operator=(const String& t) { val = stringToVoiceTone(t); return *this; }
    operator VoiceTone() const { return val; }
    bool operator==(VoiceTone other) const { return val == other; }
    bool operator!=(VoiceTone other) const { return val != other; }
    bool operator==(const char* other) const { return val == stringToVoiceTone(other ? other : ""); }
    bool operator!=(const char* other) const { return !(*this == other); }
    const char* c_str() const { return voiceToneToString(val); }
    String toString() const { return String(voiceToneToString(val)); }
};

#ifndef VOICEKIT_DEFAULT_MODEL
#define VOICEKIT_DEFAULT_MODEL "models/gemini-3.8-live"
#endif

#ifndef VOICEKIT_DEFAULT_VOICE
#define VOICEKIT_DEFAULT_VOICE "Aoede" // Voix supportees : Aoede, Charon, Fenrir, Kore, Puck
#endif

#define VOICEKIT_DEFAULT_TTS_INSTRUCTION \
    "Tu es un moteur de synthese vocale TTS pur en francais pour un banc d'essai audio. " \
    "Ton unique role est de prononcer oralement et tres exactement le texte demande mot pour mot. " \
    "Ne formule aucun commentaire, aucune reponse conversationnelle, aucun mot d'introduction ni de conclusion. " \
    "Prononce uniquement et tres exactement le texte fourni par l'utilisateur."

#define VOICEKIT_DEFAULT_STT_INSTRUCTION \
    "Tu es un transcripteur vocal de haute precision en temps reel pour de longs textes et discours. " \
    "Ton unique mission est de transcrire fidelement et mot a mot l'integralite des propos prononces par " \
    "l'utilisateur en francais ou dans sa langue, sans jamais ajouter d'introduction ni de conclusion, " \
    "sans commenter, sans repondre aux questions dictees, et sans generer de son audio superflu. " \
    "Reste rigoureusement silencieux sur le plan sonore."

#define VOICEKIT_DEFAULT_CONVERSATION_INSTRUCTION \
    "Tu es un assistant vocal bienveillant, intelligent et concis. " \
    "Reponds toujours en francais direct et naturel en une ou deux phrases maximum."

/**
 * @brief Métriques diagnostiques d'un tour conversationnel (v0.4.0).
 */
struct ConversationMetrics {
    uint64_t turnId = 0;
    uint32_t micCaptureDurationMs = 0;
    uint32_t audioBlocksSent = 0;
    uint32_t firstAudioLatencyMs = 0;
    uint32_t pcmBlocksReceived = 0;
    uint32_t droppedSamples = 0;
    uint32_t totalTurnDurationMs = 0;
    String userText = "";
    String assistantText = "";
};

/**
 * @brief Configuration d'initialisation de VoxCore VoiceKit v0.4.1.
 */
struct VoiceKitConfig {
    // Cle d'API Google Gemini
    String apiKey = "";

    // Modele et voix Gemini Live centralises
    String model = VOICEKIT_DEFAULT_MODEL;
    String voice = VOICEKIT_DEFAULT_VOICE;

    // Configuration vocale et linguistique (v0.4.1)
    String language = "fr-FR";                 ///< Code de langue BCP-47 (défaut : fr-FR)
    VoiceStyleWrapper style = VoiceStyle::NATURAL;   ///< Style d'élocution (natural, professional, expressive, narrative)
    VoiceToneWrapper tone = VoiceTone::NEUTRAL;      ///< Tonalité vocale (neutral, calm, warm, cheerful, serious)
    String accent = "";                        ///< Orientation de prononciation / accent optionnel

    // Instructions systeme strictes pour TTS mot a mot fidele
    String systemInstruction = VOICEKIT_DEFAULT_TTS_INSTRUCTION;

    // Prefixe de consigne pour reproduction mot a mot (TTS)
    String promptPrefix = "Prononce exactement et uniquement ce texte : ";

    // Mode Wi-Fi
    WiFiMode wifiMode = WiFiMode::EXTERNAL_WIFI;
    String wifiSSID = "";
    String wifiPassword = "";

    // Volume initial du haut-parleur (0 - 100 %)
    uint8_t volume = 65;

    // Politique de gestion des requetes concurrentes
    QueuePolicy queuePolicy = QueuePolicy::REJECT_IF_BUSY;
    size_t maxQueueSize = 4;

    // Sortie audio optionnelle (utilise M5AudioOutput par defaut)
    IAudioOutput* audioOutput = nullptr;

    // Entree audio optionnelle (utilise M5AudioInput par defaut)
    IAudioInput* audioInput = nullptr;

    // Mute du haut-parleur pendant l'écoute STT (évite l'effet larsen acoustique)
    bool muteSpeakerDuringSTT = true;

    // Timeout maximal d'attente de la transcription finale Gemini (en ms)
    uint32_t sttFinalTimeoutMs = 6000;

    // Mode conversationnel bidirectionnel natif (Mode B v0.4.0)
    bool enableConversation = false;
    String conversationInstruction = VOICEKIT_DEFAULT_CONVERSATION_INSTRUCTION;

    // Champs de compatibilite ascendante pour applications hotes autonomes
    bool hardwareAlreadyInitialized = true;
    bool connectOnDemand = false;
    uint32_t autoDisconnectIdleMs = 0;

    /**
     * @brief Formate les directives vocales (langue, style, ton, accent) en consignes textuelles.
     */
    String buildVoiceDirectives() const {
        String directives;
        // 1. Langue d'expression
        String lang = language;
        lang.trim();
        if (!lang.isEmpty()) {
            directives += "Langue d'expression : ";
            if (lang.equalsIgnoreCase("fr-FR") || lang.equalsIgnoreCase("fr")) {
                directives += "francais (fr-FR).";
            } else if (lang.equalsIgnoreCase("en-US") || lang.equalsIgnoreCase("en-GB") || lang.equalsIgnoreCase("en")) {
                directives += "anglais (" + lang + ").";
            } else if (lang.equalsIgnoreCase("de-DE") || lang.equalsIgnoreCase("de")) {
                directives += "allemand (de-DE).";
            } else if (lang.equalsIgnoreCase("es-ES") || lang.equalsIgnoreCase("es")) {
                directives += "espagnol (es-ES).";
            } else if (lang.equalsIgnoreCase("it-IT") || lang.equalsIgnoreCase("it")) {
                directives += "italien (it-IT).";
            } else {
                directives += lang + ".";
            }
        }

        // 2. Style d'élocution
        if (!directives.isEmpty()) directives += " ";
        directives += "Style d'elocution : ";
        switch (static_cast<VoiceStyle>(style)) {
            case VoiceStyle::PROFESSIONAL: directives += "professionnel, pose et clair."; break;
            case VoiceStyle::EXPRESSIVE:   directives += "expressif, vivant et nuance."; break;
            case VoiceStyle::NARRATIVE:    directives += "narratif, immersif et conteur."; break;
            case VoiceStyle::NATURAL:
            default:                       directives += "naturel, fluide et spontane."; break;
        }

        // 3. Tonalité vocale
        directives += " Tonalite vocale : ";
        switch (static_cast<VoiceTone>(tone)) {
            case VoiceTone::CALM:     directives += "calme, douce et posee."; break;
            case VoiceTone::WARM:     directives += "chaleureuse, accueillante et bienveillante."; break;
            case VoiceTone::CHEERFUL: directives += "enjouee, joyeuse et souriante."; break;
            case VoiceTone::SERIOUS:  directives += "serieuse, formelle et rigoureuse."; break;
            case VoiceTone::NEUTRAL:
            default:                  directives += "neutre et sobre."; break;
        }

        // 4. Accent optionnel
        String acc = accent;
        acc.trim();
        if (!acc.isEmpty()) {
            directives += " Prononciation et accentuation souhaitee : " + acc + " (a reproduire selon les capacites du modele).";
        }

        directives.trim();
        return directives;
    }

    /**
     * @brief Construit l'instruction système effective en combinant strictement l'instruction métier
     *        (TTS, STT ou conversation) avec les consignes vocales (langue, style, ton, accent).
     */
    String buildEffectiveInstruction(const String& baseInstruction) const {
        String effective = baseInstruction;
        effective.trim();
        String dir = buildVoiceDirectives();
        if (!dir.isEmpty() && effective.indexOf("[Directives vocales]") < 0) {
            if (!effective.isEmpty()) {
                effective += " [Directives vocales] " + dir;
            } else {
                effective = dir;
            }
        }
        return effective;
    }
};

