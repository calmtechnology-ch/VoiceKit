#pragma once
#include <Arduino.h>
#include <cstdint>

// =============================================================================
// Machine a etats du Dialogue VoiceHub v0.2.0
// Transitions principales :
// STARTING -> WAIT_WIFI -> READY -> RECORDING -> PROCESSING -> (CONVERSATION/WINDOWS) -> SPEAKING -> READY
// Etats annexes :
// WINDOWS_COMMAND (requete locale Node.js vers Windows)
// COMPILING (compilation en cours sur Windows)
// STOPPING (interruption propre en cours)
// ERROR (defaillance reseau, memoire ou audio)
// =============================================================================

enum class DialogueState {
    STARTING,        // Initialisation LCD, audio, configuration
    WAIT_WIFI,       // Connexion Wi-Fi et synchronisation NTP requise pour TLS
    READY,           // Pret a ecouter (appui maintenu sur PARLER)
    RECORDING,       // Enregistrement micro en cours (16 kHz mono 16 bits)
    PROCESSING,      // Envoi base64 a Gemini et attente de la reponse
    WINDOWS_COMMAND, // Requete HTTP locale vers le serveur Windows Node.js
    COMPILING,       // Compilation PlatformIO en cours sur Windows
    SPEAKING,        // Synthese Google TTS et lecture sur haut-parleur M5CoreS3
    STOPPING,        // Annulation en cours (STOP demande par l'utilisateur)
    ERROR            // Erreur signalee, retour propre a READY
};

inline const char* dialogueStateToString(DialogueState s) {
    switch (s) {
        case DialogueState::STARTING:        return "DEMARRAGE";
        case DialogueState::WAIT_WIFI:       return "WIFI / NTP";
        case DialogueState::READY:           return "PRET";
        case DialogueState::RECORDING:       return "ECOUTE...";
        case DialogueState::PROCESSING:      return "GEMINI REFLECHIT...";
        case DialogueState::WINDOWS_COMMAND: return "COMMANDE WINDOWS";
        case DialogueState::COMPILING:       return "COMPILATION EN COURS";
        case DialogueState::SPEAKING:        return "PARLE...";
        case DialogueState::STOPPING:        return "ARRET...";
        case DialogueState::ERROR:           return "ERREUR";
        default:                             return "INCONNU";
    }
}

// Mode vocal VoxCore v0.1.1.0 (Gemini Live temps réel exclusif)
enum class VoiceMode {
    LIVE  // Mode LIVE temps réel unique : streaming PCM 16 kHz -> Gemini Live WS -> audio PCM 24 kHz
};

inline const char* voiceModeToString(VoiceMode m) {
    return "LIVE";
}

// Statut matériel audio et transition I2S (VoxCore v0.1.0.2)
enum class AudioHardwareStatus {
    SUCCESS = 0,          // Succès complet
    RECOVERABLE_ERROR,    // Échec récupérable
    TIMEOUT,              // Timeout d'attente
    DEVICE_BUSY,          // Matériel toujours occupé
    PERMANENT_FAULT       // Défaillance matérielle permanente
};

inline const char* audioHardwareStatusToString(AudioHardwareStatus s) {
    switch (s) {
        case AudioHardwareStatus::SUCCESS:           return "SUCCESS";
        case AudioHardwareStatus::RECOVERABLE_ERROR: return "RECOVERABLE_ERROR";
        case AudioHardwareStatus::TIMEOUT:           return "TIMEOUT";
        case AudioHardwareStatus::DEVICE_BUSY:       return "DEVICE_BUSY";
        case AudioHardwareStatus::PERMANENT_FAULT:   return "PERMANENT_FAULT";
        default:                                     return "UNKNOWN";
    }
}

// Machine à états dédiée à la session Gemini Live (v0.4.0)
enum class LiveSessionState {
    DISCONNECTED,     // Déconnecté du WebSocket Gemini Live
    CONNECTING,       // Connexion TLS et initialisation protocole en cours
    CONNECTED,        // Connecté et prêt (session active, contexte préservé)
    LISTENING,        // Microphone actif et streaming des blocs PCM 16 kHz vers Gemini
    WAITING_RESPONSE, // Attente de la première réponse audio Gemini Live
    PLAYING,          // Restitution audio PCM 24 kHz sur haut-parleur AW88298
    INTERRUPTING,     // Interruption par STOP en cours
    RECONNECTING,     // Reconnexion automatique en cours après déconnexion
    ERROR             // Erreur signalée sur la session Live
};

inline const char* liveSessionStateToString(LiveSessionState s) {
    switch (s) {
        case LiveSessionState::DISCONNECTED:     return "DISCONNECTED";
        case LiveSessionState::CONNECTING:       return "CONNECTING";
        case LiveSessionState::CONNECTED:        return "CONNECTED";
        case LiveSessionState::LISTENING:        return "LISTENING";
        case LiveSessionState::WAITING_RESPONSE: return "WAITING_RESPONSE";
        case LiveSessionState::PLAYING:          return "PLAYING";
        case LiveSessionState::INTERRUPTING:     return "INTERRUPTING";
        case LiveSessionState::RECONNECTING:     return "RECONNECTING";
        case LiveSessionState::ERROR:            return "ERROR";
        default:                                 return "UNKNOWN";
    }
}

// Intentions vocales supportées par VoiceHub
enum class IntentType {
    NONE,             // Question générale / conversation
    PROJECTS_LIST,    // Lister les projets PlatformIO
    PROJECTS_COMPILE, // Compiler un projet PlatformIO
    PROJECTS_STATUS,  // Consulter l'état de la compilation
    UNKNOWN           // Intention non reconnue ou invalide
};

inline const char* intentTypeToString(IntentType intent) {
    switch (intent) {
        case IntentType::NONE:             return "none";
        case IntentType::PROJECTS_LIST:    return "projects.list";
        case IntentType::PROJECTS_COMPILE: return "projects.compile";
        case IntentType::PROJECTS_STATUS:  return "projects.status";
        default:                           return "unknown";
    }
}

inline IntentType stringToIntentType(const String& str) {
    if (str == "projects.list")    return IntentType::PROJECTS_LIST;
    if (str == "projects.compile") return IntentType::PROJECTS_COMPILE;
    if (str == "projects.status")  return IntentType::PROJECTS_STATUS;
    if (str == "none")             return IntentType::NONE;
    return IntentType::UNKNOWN;
}

struct GeminiResult {
    bool success = false;
    String transcription;  // Transcription de la voix de l'utilisateur
    String response;       // Reponse generee par Gemini (mode conversation)
    String mode = "conversation"; // "conversation" ou "command"
    String intent = "none";       // "projects.list", "projects.compile", "projects.status", "none"
    IntentType intentType = IntentType::NONE;
    String project;        // Nom du projet si applicable (ex: "Sensor")
    String errorMessage;   // Message d'erreur en cas d'echec
    int httpCode = 0;      // Code HTTP
    uint32_t geminiDurationMs = 0; // Durée traitement Gemini en ms (v0.3.0)
};

// Mesures de latence et d'interaction VoiceHub v0.3.0
struct InteractionMetrics {
    uint32_t recordDurationMs = 0;           // Durée effective de l'enregistrement micro
    uint32_t prepDurationMs = 0;             // Durée préparation WAV + Base64
    uint32_t geminiDurationMs = 0;           // Durée de traitement / appel Gemini REST
    uint32_t intentDurationMs = 0;           // Durée de résolution d'intention
    uint32_t winDurationMs = 0;              // Durée requête serveur Windows (PlatformIO)
    uint32_t ttsFirstAudioDurationMs = 0;     // Durée avant premier son Google TTS
    uint32_t totalInteractionDurationMs = 0; // Durée totale de l'interaction
    bool speechDetected = false;             // VAD : début de parole détecté
    bool silenceDetected = false;            // VAD : fin de parole par silence détectée
    int32_t vadEnergy = 0;                   // Niveau d'énergie audio calculé
};

// Instantane d'etat transmis a l'interface utilisateur (UI)
struct DialogueSnapshot {
    DialogueState state = DialogueState::STARTING;
    bool wifiConnected = false;
    bool timeSynced = false;
    bool pcConnected = false;       // Indicateur de disponibilite du serveur Windows Node.js
    unsigned volume = 65;
    String userText;
    String assistantText;
    String activeProject;           // Nom du projet PlatformIO concerne
    String resultSummary;           // Resume du resultat de compilation
    String statusMessage;
    char errorDetail[64] = {};

    // Données d'application génériques (VoxCore v0.2.0)
    String appName = "VoxCore";     // Nom de l'application affiché dans l'en-tête UI
    String appBadgeText;            // Badge personnalisé (ex: "PC: OK", ou vide si aucun badge requis)
    String welcomeHint;             // Indication personnalisée sur l'écran d'accueil
    String extraInfo;               // Information supplémentaire contextuelle

    // Données de contexte et métriques VoiceHub
    InteractionMetrics metrics;
    size_t contextExchangeCount = 0;
    String contextActiveProject;
    String contextState = "aucun";  // "actif", "expiré", "aucun"
    bool speechDetected = false;
    bool silenceDetected = false;

    // Métriques et état Gemini Live (VoxCore v0.1.1.0)
    VoiceMode voiceMode = VoiceMode::LIVE;
    LiveSessionState liveSessionState = LiveSessionState::DISCONNECTED;
    bool liveWsConnected = false;
    uint32_t liveChunksSent = 0;
    uint32_t liveChunksReceived = 0;
    uint32_t liveTurnDurationMs = 0;
    uint32_t liveFirstAudioLatencyMs = 0;
    size_t liveQueueSize = 0;
    uint32_t liveDisconnectCount = 0;
    char liveLastError[64] = {};
    String liveInputTranscription;
    uint32_t liveAvgRms = 0;
    String liveTranscriptionStatus;
};

// =============================================================================
// Structure d'événement de télémétrie normalisée pour VoiceHub v0.3.0 & v0.4.0
// Schéma : eventId, timestamp, deviceId, source, type, state, level, etc.
// Aucune clé secrète ni donnée audio brute.
// =============================================================================
struct TelemetryEvent {
    String eventId;
    String timestamp;
    String deviceId = "M5CoreS3-VoiceHub";
    String source = "M5CoreS3";
    String type = "dialogue_state";
    String state = "READY";
    String level = "INFO";
    String transcription;
    String intent;
    String project;
    String message;
    int wifiRssi = 0;
    uint32_t freeHeap = 0;
    bool hasMetadata = false;

    // Métriques et contexte étendu v0.3.0
    uint32_t recordDurationMs = 0;
    uint32_t prepDurationMs = 0;
    uint32_t geminiDurationMs = 0;
    uint32_t intentDurationMs = 0;
    uint32_t winDurationMs = 0;
    uint32_t ttsFirstAudioDurationMs = 0;
    uint32_t totalInteractionDurationMs = 0;
    bool speechDetected = false;
    bool silenceDetected = false;
    int vadThreshold = 0;
    String contextState;
    int contextExchangeCount = -1;
    String contextActiveProject;

    // Métriques et état Gemini Live v0.4.0 & v0.4.1.1
    String voiceMode = "REST";
    String liveWsState;
    String liveSessionState;
    int liveChunksSent = -1;
    int liveChunksReceived = -1;
    int liveTurnDurationMs = -1;
    int liveFirstAudioDurationMs = -1;
    int liveQueueSize = -1;
    int liveDisconnectCount = -1;
    String liveLastError;
    int liveBlocksAcquired = -1;
    int liveBlocksCompleted = -1;
    int liveBytesSent = -1;
    int liveAvgRms = -1;
    int liveBlocksDropped = -1;
    bool liveActivityStartConfirmed = false;
    bool liveActivityEndConfirmed = false;
    String liveTranscriptionStatus;
    String liveAssistantText;

    String toJson() const {
        String json = "{";
        json += "\"eventId\":\"" + escapeJson(eventId) + "\",";
        json += "\"timestamp\":\"" + escapeJson(timestamp) + "\",";
        json += "\"deviceId\":\"" + escapeJson(deviceId) + "\",";
        json += "\"source\":\"" + escapeJson(source) + "\",";
        json += "\"type\":\"" + escapeJson(type) + "\",";
        json += "\"state\":\"" + escapeJson(state) + "\",";
        json += "\"level\":\"" + escapeJson(level) + "\"";
        if (!transcription.isEmpty()) {
            json += ",\"transcription\":\"" + escapeJson(transcription) + "\"";
        }
        if (!intent.isEmpty()) {
            json += ",\"intent\":\"" + escapeJson(intent) + "\"";
        }
        if (!project.isEmpty()) {
            json += ",\"project\":\"" + escapeJson(project) + "\"";
        }
        if (!message.isEmpty()) {
            json += ",\"message\":\"" + escapeJson(message) + "\"";
        }
        if (hasMetadata) {
            json += ",\"metadata\":{";
            json += "\"wifiRssi\":" + String(wifiRssi) + ",";
            json += "\"freeHeap\":" + String(freeHeap);
            if (recordDurationMs > 0) json += ",\"recordDurationMs\":" + String(recordDurationMs);
            if (prepDurationMs > 0) json += ",\"prepDurationMs\":" + String(prepDurationMs);
            if (geminiDurationMs > 0) json += ",\"geminiDurationMs\":" + String(geminiDurationMs);
            if (intentDurationMs > 0) json += ",\"intentDurationMs\":" + String(intentDurationMs);
            if (winDurationMs > 0) json += ",\"winDurationMs\":" + String(winDurationMs);
            if (ttsFirstAudioDurationMs > 0) json += ",\"ttsFirstAudioDurationMs\":" + String(ttsFirstAudioDurationMs);
            if (totalInteractionDurationMs > 0) json += ",\"totalInteractionDurationMs\":" + String(totalInteractionDurationMs);
            if (speechDetected) json += ",\"speechDetected\":true";
            if (silenceDetected) json += ",\"silenceDetected\":true";
            if (vadThreshold > 0) json += ",\"vadThreshold\":" + String(vadThreshold);
            if (!contextState.isEmpty()) json += ",\"contextState\":\"" + escapeJson(contextState) + "\"";
            if (contextExchangeCount >= 0) json += ",\"contextExchangeCount\":" + String(contextExchangeCount);
            if (!contextActiveProject.isEmpty()) json += ",\"contextActiveProject\":\"" + escapeJson(contextActiveProject) + "\"";
            if (!voiceMode.isEmpty()) json += ",\"voiceMode\":\"" + escapeJson(voiceMode) + "\"";
            if (!liveWsState.isEmpty()) json += ",\"liveWsState\":\"" + escapeJson(liveWsState) + "\"";
            if (!liveSessionState.isEmpty()) json += ",\"liveSessionState\":\"" + escapeJson(liveSessionState) + "\"";
            if (liveChunksSent >= 0) json += ",\"liveChunksSent\":" + String(liveChunksSent);
            if (liveChunksReceived >= 0) json += ",\"liveChunksReceived\":" + String(liveChunksReceived);
            if (liveTurnDurationMs >= 0) json += ",\"liveTurnDurationMs\":" + String(liveTurnDurationMs);
            if (liveFirstAudioDurationMs >= 0) json += ",\"liveFirstAudioDurationMs\":" + String(liveFirstAudioDurationMs);
            if (liveQueueSize >= 0) json += ",\"liveQueueSize\":" + String(liveQueueSize);
            if (liveDisconnectCount >= 0) json += ",\"liveDisconnectCount\":" + String(liveDisconnectCount);
            if (!liveLastError.isEmpty()) json += ",\"liveLastError\":\"" + escapeJson(liveLastError) + "\"";
            if (liveBlocksAcquired >= 0) json += ",\"liveBlocksAcquired\":" + String(liveBlocksAcquired);
            if (liveBlocksCompleted >= 0) json += ",\"liveBlocksCompleted\":" + String(liveBlocksCompleted);
            if (liveBytesSent >= 0) json += ",\"liveBytesSent\":" + String(liveBytesSent);
            if (liveAvgRms >= 0) json += ",\"liveAvgRms\":" + String(liveAvgRms);
            if (liveBlocksDropped >= 0) json += ",\"liveBlocksDropped\":" + String(liveBlocksDropped);
            if (liveActivityStartConfirmed) json += ",\"liveActivityStartConfirmed\":true";
            if (liveActivityEndConfirmed) json += ",\"liveActivityEndConfirmed\":true";
            if (!liveTranscriptionStatus.isEmpty()) json += ",\"liveTranscriptionStatus\":\"" + escapeJson(liveTranscriptionStatus) + "\"";
            if (!liveAssistantText.isEmpty()) json += ",\"liveAssistantText\":\"" + escapeJson(liveAssistantText) + "\"";
            json += "}";
        }
        json += "}";
        return json;
    }

private:
    static String escapeJson(const String& in) {
        String out = "";
        out.reserve(in.length() + 8);
        for (size_t i = 0; i < in.length(); ++i) {
            char c = in[i];
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += " ";
            else if (c == '\r') out += "";
            else if (c == '\t') out += " ";
            else if ((uint8_t)c < 32) { /* ignorer les caractères de contrôle */ }
            else out += c;
        }
        return out;
    }
};
