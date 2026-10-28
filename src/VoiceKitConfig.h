#pragma once
#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include "hal/IAudioOutput.h"
#include "hal/IAudioInput.h"
#include "core/IWakeWordEngine.h"

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
 * @brief Fournisseurs d'IA vocale en temps réel pris en charge (VoiceKit v0.6.0).
 */
enum class VoiceProviderType : uint8_t {
    GEMINI = 0,   ///< Google Gemini Live API (fournisseur par défaut)
    OPENAI = 1    ///< OpenAI Realtime API (WebSocket TLS direct)
};

inline const char* voiceProviderTypeToString(VoiceProviderType p) {
    switch (p) {
        case VoiceProviderType::GEMINI: return "GEMINI";
        case VoiceProviderType::OPENAI: return "OPENAI";
        default:                        return "GEMINI";
    }
}
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
 * @brief Métriques détaillées d'instrumentation de la latence vocale d'un tour (v0.5.3 — Latency Optimization).
 * 
 * Mesure avec rigueur chaque étape du pipeline vocal entre la parole et la restitution.
 * Toute valeur non observée reste à 0 et les durées dérivées correspondantes retournent -1 (N/D),
 * garantissant de ne jamais inventer une durée artificielle de 0 ms.
 * Distingue le début du transfert DMA (firstAudioPlayedMs) de la sortie acoustique audible.
 */
struct VoiceLatencyMetrics {
    // 7 Instants temporels observés (en millisecondes depuis le boot, 0 = non observé / N/D)
    uint32_t speechEndDetectedMs = 0;    ///< Instant détection fin de parole par VAD local (0 si bouton manuel)
    uint32_t micStoppedMs = 0;           ///< Instant fin effective de capture microphone
    uint32_t activityEndSentMs = 0;      ///< Instant envoi notification fin d'activité (activityEnd) à Gemini
    uint32_t firstResponseReceivedMs = 0;///< Instant premier événement de réponse serveur reçu (texte ou audio)
    uint32_t firstAudioReceivedMs = 0;   ///< Instant premier paquet PCM de réponse reçu
    uint32_t firstAudioPlayedMs = 0;     ///< Instant premier bloc audio remis au périphérique matériel DMA (playRaw)
    uint32_t playbackFinishedMs = 0;     ///< Instant fin effective de restitution audio (vidage + drain)

    // Horodatage du début du tour pour calcul de la durée globale
    uint32_t turnStartedMs = 0;          ///< Instant de début du tour vocal (0 si non observé)

    // Indicateurs d'observation explicites
    bool hasSpeechEndDetected() const { return speechEndDetectedMs > 0; }
    bool hasMicStopped() const { return micStoppedMs > 0; }
    bool hasActivityEndSent() const { return activityEndSentMs > 0; }
    bool hasFirstResponseReceived() const { return firstResponseReceivedMs > 0; }
    bool hasFirstAudioReceived() const { return firstAudioReceivedMs > 0; }
    bool hasFirstAudioPlayed() const { return firstAudioPlayedMs > 0; }
    bool hasPlaybackFinished() const { return playbackFinishedMs > 0; }

    // 6 Durées dérivées calculées (-1 si non mesuré ou non disponible)
    
    // 1. Fin de parole -> fin de capture
    int32_t getSpeechEndToMicStoppedMs() const {
        if (speechEndDetectedMs == 0 || micStoppedMs == 0 || micStoppedMs < speechEndDetectedMs) {
            return -1;
        }
        return static_cast<int32_t>(micStoppedMs - speechEndDetectedMs);
    }

    // 2. Fin de capture -> notification Gemini
    int32_t getMicStoppedToActivityEndMs() const {
        if (micStoppedMs == 0 || activityEndSentMs == 0 || activityEndSentMs < micStoppedMs) {
            return -1;
        }
        return static_cast<int32_t>(activityEndSentMs - micStoppedMs);
    }

    // 3. Notification Gemini -> premier audio reçu
    int32_t getActivityEndToFirstAudioMs() const {
        if (activityEndSentMs == 0 || firstAudioReceivedMs == 0 || firstAudioReceivedMs < activityEndSentMs) {
            return -1;
        }
        return static_cast<int32_t>(firstAudioReceivedMs - activityEndSentMs);
    }

    // Notification Gemini -> premier événement de réponse (texte ou audio)
    int32_t getActivityEndToFirstResponseMs() const {
        if (activityEndSentMs == 0 || firstResponseReceivedMs == 0 || firstResponseReceivedMs < activityEndSentMs) {
            return -1;
        }
        return static_cast<int32_t>(firstResponseReceivedMs - activityEndSentMs);
    }

    // 4. Premier audio reçu -> début restitution (remise DMA)
    int32_t getFirstAudioToPlaybackMs() const {
        if (firstAudioReceivedMs == 0 || firstAudioPlayedMs == 0 || firstAudioPlayedMs < firstAudioReceivedMs) {
            return -1;
        }
        return static_cast<int32_t>(firstAudioPlayedMs - firstAudioReceivedMs);
    }

    // 5. Fin de parole -> début restitution (nécessite VAD local speechEndDetectedMs)
    int32_t getSpeechEndToPlaybackMs() const {
        if (speechEndDetectedMs == 0 || firstAudioPlayedMs == 0 || firstAudioPlayedMs < speechEndDetectedMs) {
            return -1;
        }
        return static_cast<int32_t>(firstAudioPlayedMs - speechEndDetectedMs);
    }

    // Latence de réponse perçue par l'utilisateur :
    // - si VAD local actif : depuis fin de parole détectée (speechEndDetectedMs)
    // - si mode manuel (bouton) : depuis arrêt microphone (micStoppedMs)
    int32_t getResponseLatencyMs() const {
        uint32_t startEvent = (speechEndDetectedMs > 0) ? speechEndDetectedMs : micStoppedMs;
        if (startEvent == 0 || firstAudioPlayedMs == 0 || firstAudioPlayedMs < startEvent) {
            return -1;
        }
        return static_cast<int32_t>(firstAudioPlayedMs - startEvent);
    }

    // 6. Durée totale du tour vocal
    int32_t getTotalTurnDurationMs() const {
        uint32_t startRef = (turnStartedMs > 0) ? turnStartedMs : ((speechEndDetectedMs > 0) ? speechEndDetectedMs : micStoppedMs);
        if (startRef == 0 || playbackFinishedMs == 0 || playbackFinishedMs < startRef) {
            return -1;
        }
        return static_cast<int32_t>(playbackFinishedMs - startRef);
    }

    void reset() {
        speechEndDetectedMs = 0;
        micStoppedMs = 0;
        activityEndSentMs = 0;
        firstResponseReceivedMs = 0;
        firstAudioReceivedMs = 0;
        firstAudioPlayedMs = 0;
        playbackFinishedMs = 0;
        turnStartedMs = 0;
    }
};

/**
 * @brief Métriques diagnostiques d'un tour conversationnel (v0.4.0 / v0.5.3).
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
    VoiceLatencyMetrics latency; ///< Métriques détaillées d'étapes de latence (v0.5.3)
};

/**
 * @brief Métriques instrumentées de connectivité et temps de démarrage (v0.5.2 — Fast Connection).
 */
struct ConnectionMetrics {
    // 5 Temps d'instrumentation requis (en millisecondes)
    uint32_t wifiConnectMs = 0;       ///< Démarrage Wi-Fi -> IP obtenue (t_wifi)
    uint32_t sntpSyncMs = 0;          ///< Début SNTP -> horloge valide (t_sntp, 0 si déjà synchronisée)
    uint32_t wsConnectMs = 0;         ///< Début connexion Gemini -> WebSocket connecté (t_ws)
    uint32_t setupCompleteMs = 0;     ///< WebSocket connecté -> confirmation setupComplete (t_setup)
    uint32_t totalReadyMs = 0;        ///< Démarrage total (begin) -> VoiceKit::isReady() (t_ready)

    // Compteurs opérationnels et fiabilité
    uint32_t wifiAttempts = 0;        ///< Nombre de tentatives Wi-Fi
    uint32_t wifiReconnects = 0;      ///< Nombre de reconnexions Wi-Fi réussies après perte
    uint32_t geminiAttempts = 0;      ///< Nombre de tentatives de connexion Gemini Live
    uint32_t geminiReconnects = 0;    ///< Nombre de reconnexions Gemini Live
    uint32_t connectionFailures = 0;  ///< Nombre d'échecs de connexion observés

    // Indicateurs d'optimisation
    bool fastConnectEnabled = true;   ///< Connexion rapide BSSID/canal autorisée
    bool fastConnectAttempted = false;///< Vrai si la connexion rapide BSSID/canal a été tentée
    bool fastConnectSuccess = false;  ///< Vrai si la connexion rapide a abouti avec succès
    bool sntpSkipped = false;         ///< Vrai si SNTP a été immédiat car l'horloge système était déjà valide
    uint8_t channel = 0;              ///< Canal Wi-Fi utilisé (1..14)
    char bssidStr[18] = "";           ///< BSSID au format standard "XX:XX:XX:XX:XX:XX"

    // Diagnostics FreeRTOS, TLS, RAM interne et boucles (v0.6.1 — Corrections B & C)
    int8_t wsTaskCore = -1;                      ///< Cœur FreeRTOS observé pour la tâche WebSocket (-1 si N/D)
    uint8_t wsTaskPriority = 0;                  ///< Priorité FreeRTOS active de la tâche WebSocket
    uint8_t wsHandshakeTaskPriority = 1;         ///< Priorité FreeRTOS configurée pendant le handshake TLS
    uint8_t wsSteadyTaskPriority = 2;            ///< Priorité FreeRTOS configurée en régime établi (connecté)
    int8_t loopTaskCore = -1;                    ///< Cœur FreeRTOS de la boucle principale applicative
    uint8_t loopTaskPriority = 0;                ///< Priorité FreeRTOS de la boucle principale applicative
    uint32_t lastLoopGapMs = 0;                  ///< Dernier intervalle observé entre deux appels à update() (ms)
    uint32_t maxLoopGapMs = 0;                   ///< Durée maximale d'interruption apparente de la boucle principale (ms)
    uint32_t maxLoopGapDuringTlsMs = 0;          ///< Durée maximale d'interruption apparente pendant la connexion TLS (ms)
    uint32_t lastUpdateDurationUs = 0;           ///< Durée du dernier appel à VoiceKit::update() / VoxCoreVoice::update() (µs)
    uint32_t maxUpdateDurationUs = 0;            ///< Durée maximale d'un appel à update() (µs)
    uint32_t lastWifiTickDurationUs = 0;         ///< Durée du dernier appel à WiFiManager::tick() (µs)
    uint32_t maxWifiTickDurationUs = 0;          ///< Durée maximale d'un appel à WiFiManager::tick() (µs)
    uint32_t freeInternalHeapDuringTls = 0;      ///< RAM interne libre mesurée pendant/après le handshake TLS (octets)
    uint32_t minFreeInternalHeapDuringTls = 0;   ///< Minimum historique de RAM interne libre pendant TLS (octets)
    uint32_t largestInternalBlockDuringTls = 0;  ///< Plus grand bloc mémoire interne disponible pendant TLS (octets)
};

/**
 * @brief Configuration d'initialisation de VoxCore VoiceKit v0.6.0.
 */
struct VoiceKitConfig {
    // Fournisseur d'IA vocale (v0.6.0) : GEMINI (par défaut) ou OPENAI
    VoiceProviderType provider = VoiceProviderType::GEMINI;

    // Cle d'API (Google Gemini ou OpenAI selon le fournisseur actif)
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

    // Optimisations de connexion rapide Wi-Fi (v0.5.2)
    bool enableFastConnect = true;             ///< Active la connexion rapide au dernier BSSID/canal connu (défaut: true)
    uint32_t fastConnectTimeoutMs = 3500;      ///< Délai max pour tentative rapide avant repli automatique vers scan normal (ms)

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

    // Seuil de silence VAD pour fin de parole en ms (défaut : 1200 ms, réglable à 800, 1000, 1200 ms)
    uint32_t vadSilenceDurationMs = 1200;

    // Mode conversationnel bidirectionnel natif (Mode B v0.4.0)
    bool enableConversation = false;
    String conversationInstruction = VOICEKIT_DEFAULT_CONVERSATION_INSTRUCTION;

    // Fondation Wake Word locale (v0.6.2 / v0.6.2.1 — optionnelle, désactivée par défaut)
    bool enableWakeWord = false;                                    ///< Active le détecteur au démarrage si true
    WakeWordEngineType wakeWordEngine = WakeWordEngineType::WAKENET;///< Moteur ciblé (WAKENET par défaut, ou TEST)
    String wakeWordPhrase = "Bonjour ESP";                          ///< Mot de réveil cible
    bool wakeWordAutoResume = true;                                 ///< Reprise automatique après fin de conversation/TTS
    uint32_t wakeWordResumeDelayMs = 250;                           ///< Délai d'extinction acoustique avant réarmement (ms)
    size_t wakeWordMaxFramesPerUpdate = 2;                          ///< Nombre max de trames PCM traitées par appel à update() (fluidité LVGL)
    uint32_t wakeWordProcessingBudgetUs = 5000;                     ///< Budget max de traitement Wake Word par appel à update() en µs (0 = illimité)

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

