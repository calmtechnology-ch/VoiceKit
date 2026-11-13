#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include "VoiceKitConfig.h"

/**
 * @brief Version du schéma de persistance NVS des profils vocaux (VoiceKit v0.6.6).
 */
static constexpr uint16_t VOICEKIT_PROFILE_SCHEMA_VERSION = 1;

/**
 * @brief Espace de noms NVS par défaut pour les profils vocaux (sans clés API).
 */
static constexpr const char* VOICEKIT_PROFILE_NVS_NAMESPACE = "vk_vprof";

/**
 * @brief Nombre maximal d'emplacements de profils favoris en NVS.
 */
static constexpr uint8_t VOICEKIT_MAX_FAVORITE_PROFILES = 4;

/**
 * @brief Nombre maximal de voix ou langues personnalisées enregistrables dynamiquement sans réallocation.
 */
static constexpr size_t VOICEKIT_MAX_CUSTOM_VOICES = 16;
static constexpr size_t VOICEKIT_MAX_CUSTOM_LANGUAGES = 8;

/**
 * @enum VoiceGender
 * @brief Catégorie vocale perçue d'une voix prédéfinie (VoiceKit v0.6.6).
 *
 * REMARQUE IMPORTANTE : Le genre vocal est une classification indicative de filtrage
 * dans le catalogue VoiceKit, et non un paramètre transmis aux API Gemini Live ou OpenAI Realtime.
 */
enum class VoiceGender : uint8_t {
    ANY = 0,       ///< Indifférent / toutes les voix
    FEMININE = 1,  ///< Voix perçue comme féminine
    MASCULINE = 2, ///< Voix perçue comme masculine
    NEUTRAL = 3    ///< Voix perçue comme neutre / ambivalente
};

inline const char* voiceGenderToString(VoiceGender g) {
    switch (g) {
        case VoiceGender::FEMININE:  return "feminine";
        case VoiceGender::MASCULINE: return "masculine";
        case VoiceGender::NEUTRAL:   return "neutral";
        case VoiceGender::ANY:
        default:                     return "any";
    }
}

inline const char* voiceGenderToFrenchLabel(VoiceGender g) {
    switch (g) {
        case VoiceGender::FEMININE:  return "Féminin";
        case VoiceGender::MASCULINE: return "Masculin";
        case VoiceGender::NEUTRAL:   return "Neutre";
        case VoiceGender::ANY:
        default:                     return "Toutes";
    }
}

inline VoiceGender stringToVoiceGender(const String& str) {
    String s = str;
    s.toLowerCase();
    s.trim();
    if (s == "feminine" || s == "female" || s == "feminin" || s == "féminin" || s == "f") {
        return VoiceGender::FEMININE;
    }
    if (s == "masculine" || s == "male" || s == "masculin" || s == "m") {
        return VoiceGender::MASCULINE;
    }
    if (s == "neutral" || s == "neutre" || s == "n") {
        return VoiceGender::NEUTRAL;
    }
    return VoiceGender::ANY;
}

/**
 * @struct VoiceProfile
 * @brief Profil vocal universel multi-fournisseurs (VoiceKit v0.6.6).
 *
 * Contient exclusivement les paramètres vocaux et linguistiques.
 * RÈGLE DE SÉCURITÉ : Ne contient jamais aucune clé API ni secret réseau.
 */
struct VoiceProfile {
    VoiceProviderType provider = VoiceProviderType::GEMINI;
    String language = "fr-FR";
    String voiceId = VOICEKIT_DEFAULT_VOICE;
    VoiceGender preferredGender = VoiceGender::ANY;
    VoiceStyle style = VoiceStyle::NATURAL;
    VoiceTone tone = VoiceTone::NEUTRAL;
    String accent = "";

    bool hasSameAcousticConfig(const VoiceProfile& other) const {
        return provider == other.provider &&
               voiceId == other.voiceId &&
               language.equalsIgnoreCase(other.language) &&
               style == other.style &&
               tone == other.tone &&
               accent == other.accent;
    }

    bool operator==(const VoiceProfile& other) const {
        return hasSameAcousticConfig(other) && preferredGender == other.preferredGender;
    }

    bool operator!=(const VoiceProfile& other) const {
        return !(*this == other);
    }
};

/**
 * @struct VoiceDescriptor
 * @brief Description statique ou enregistrée d'une voix de fournisseur.
 */
struct VoiceDescriptor {
    const char* id = "";
    VoiceProviderType provider = VoiceProviderType::GEMINI;
    VoiceGender gender = VoiceGender::NEUTRAL; ///< Classification indicative uniquement
    const char* displayName = "";
    const char* traitsFr = "";
};

/**
 * @struct LanguageDescriptor
 * @brief Description d'une langue supportée avec code BCP-47 et phrase d'essai native.
 */
struct LanguageDescriptor {
    const char* bcp47 = "";
    const char* iso639_1 = "";
    const char* displayNameFr = "";
    const char* directiveLabelFr = "";
    const char* samplePhrase = "";
};

/**
 * @class VoiceProfileManager
 * @brief Gestionnaire centralisé des profils vocaux universels (VoiceKit v0.6.6).
 *
 * Responsabilités :
 * - Catalogue extensible des voix Gemini Live et OpenAI Realtime avec catégorie vocale indicative.
 * - Filtrage sans allocation dynamique répétée par fournisseur et par genre perçu.
 * - Validation stricte des identifiants de voix par fournisseur et des codes de langue BCP-47.
 * - Fourniture des profils par défaut par fournisseur et de phrases d'essai multilingues.
 * - Sauvegarde / restauration sécurisée sur NVS (`Preferences` sur ESP32-S3) avec version de schéma
 *   et somme de contrôle CRC32, sans jamais stocker de clé API, avec repli sûr en cas de corruption.
 */
class VoiceProfileManager {
public:
    VoiceProfileManager();
    ~VoiceProfileManager() = default;

    // =========================================================================
    // Catalogue de voix et filtrage par genre perçu
    // =========================================================================

    /**
     * @brief Retourne le catalogue complet des voix intégrées (tous fournisseurs).
     */
    static const VoiceDescriptor* getBuiltinVoices(size_t& outCount);

    /**
     * @brief Remplit un tableau fourni par l'appelant avec les voix compatibles (zéro allocation tas).
     * @param provider Fournisseur cible (GEMINI ou OPENAI).
     * @param genderFilter Filtre de genre perçu (ANY retourne toutes les voix du fournisseur).
     * @param outBuffer Tampon de sortie fourni par l'appelant.
     * @param maxCount Capacité maximale du tampon.
     * @return Nombre de voix écrites dans outBuffer.
     */
    size_t getVoicesForProvider(VoiceProviderType provider,
                                VoiceGender genderFilter,
                                VoiceDescriptor* outBuffer,
                                size_t maxCount) const;

    /**
     * @brief Surcharge pratique retournant la liste des voix compatibles.
     */
    std::vector<VoiceDescriptor> getVoicesForProvider(VoiceProviderType provider,
                                                      VoiceGender genderFilter = VoiceGender::ANY) const;

    /**
     * @brief Recherche le descripteur d'une voix pour un fournisseur donné.
     * @return Pointeur vers le descripteur ou nullptr si la voix n'existe pas pour ce fournisseur.
     */
    const VoiceDescriptor* findVoice(VoiceProviderType provider, const String& voiceId) const;

    /**
     * @brief Vérifie qu'un identifiant de voix est valide pour le fournisseur indiqué.
     */
    bool isValidVoiceForProvider(VoiceProviderType provider, const String& voiceId) const;

    /**
     * @brief Retourne la catégorie vocale indicative d'une voix (ANY si inconnue).
     */
    VoiceGender getVoiceGender(VoiceProviderType provider, const String& voiceId) const;

    /**
     * @brief Enregistre une voix supplémentaire dans le catalogue extensible (sans réallocation en boucle).
     */
    bool registerCustomVoice(const char* id,
                             VoiceProviderType provider,
                             VoiceGender gender,
                             const char* displayName = nullptr,
                             const char* traitsFr = nullptr);

    void clearCustomVoices();

    // =========================================================================
    // Catalogue multilingue et validation BCP-47
    // =========================================================================

    /**
     * @brief Retourne le catalogue des langues prédéfinies (fr-FR, fr-CH, en-US, en-GB, de-DE, it-IT, es-ES).
     */
    static const LanguageDescriptor* getBuiltinLanguages(size_t& outCount);

    size_t getSupportedLanguages(LanguageDescriptor* outBuffer, size_t maxCount) const;
    std::vector<LanguageDescriptor> getSupportedLanguages() const;

    const LanguageDescriptor* findLanguage(const String& bcp47) const;

    /**
     * @brief Vérifie la validité syntaxique d'un code de langue BCP-47 (ex: fr-FR, fr-CH, en-US, pt-BR, ja-JP).
     */
    static bool isValidLanguageCode(const String& bcp47);

    /**
     * @brief Extrait le sous-code primaire ISO-639-1 à 2 lettres (ex: "fr-CH" -> "fr", "en-GB" -> "en")
     *        utilisable par les moteurs de transcription d'entrée (ex: Whisper-1).
     *        Retourne une chaîne vide si le code n'a pas de sous-tag primaire à 2 lettres.
     */
    static String extractIso639_1(const String& bcp47);

    /**
     * @brief Retourne une phrase d'essai vocale adaptée à la langue sélectionnée.
     */
    String getSamplePhraseForLanguage(const String& bcp47) const;

    bool registerCustomLanguage(const char* bcp47,
                                const char* iso639_1,
                                const char* displayNameFr,
                                const char* directiveLabelFr = nullptr,
                                const char* samplePhrase = nullptr);

    // =========================================================================
    // Profils par défaut, construction et validation atomique
    // =========================================================================

    /**
     * @brief Retourne un profil valide par défaut adapté au fournisseur choisi.
     *        GEMINI -> voix "Aoede", langue "fr-FR"
     *        OPENAI -> voix "alloy", langue "fr-FR"
     */
    static VoiceProfile getDefaultProfile(VoiceProviderType provider = VoiceProviderType::GEMINI);

    /**
     * @brief Retourne l'identifiant de voix par défaut pour un fournisseur et un filtre de genre.
     */
    String getDefaultVoiceForProvider(VoiceProviderType provider,
                                      VoiceGender preferredGender = VoiceGender::ANY) const;

    /**
     * @brief Valide intégralement un profil vocal (fournisseur, appartenance de la voix, code langue BCP-47).
     * @param profile Profil à contrôler.
     * @param outError Reçoit le motif précis en cas d'invalidité.
     * @return true si le profil est valide et cohérent, false sinon.
     */
    bool validateProfile(const VoiceProfile& profile, String* outError = nullptr) const;

    /**
     * @brief Extrait un VoiceProfile depuis un VoiceKitConfig existant.
     */
    static VoiceProfile fromConfig(const VoiceKitConfig& config,
                                   VoiceGender preferredGender = VoiceGender::ANY);

    /**
     * @brief Applique les champs d'un VoiceProfile dans un VoiceKitConfig (sans toucher aux clés API).
     */
    static void applyToConfig(const VoiceProfile& profile, VoiceKitConfig& config);

    // =========================================================================
    // Persistance NVS (Preferences ESP32-S3 + simulation native de test)
    // =========================================================================

    /**
     * @brief Sauvegarde le profil actif en mémoire non volatile (NVS).
     *        Ne sauvegarde JAMAIS aucune clé API.
     */
    bool saveActiveProfile(const VoiceProfile& profile, const char* nvsNamespace = "vk_vprof");

    /**
     * @brief Charge le profil actif depuis la NVS.
     *        Si aucune sauvegarde n'existe, si la version de schéma est incompatible,
     *        ou si les données sont corrompues / invalides, retourne un profil par défaut valide
     *        sans jamais bloquer le démarrage.
     * @param outProfile Reçoit le profil chargé (ou le profil par défaut de repli).
     * @param fallbackProvider Fournisseur par défaut utilisé en cas de repli.
     * @param outRestoredFromNvs Optionnel : mis à true uniquement si des données NVS saines ont été restaurées.
     */
    bool loadActiveProfile(VoiceProfile& outProfile,
                           VoiceProviderType fallbackProvider = VoiceProviderType::GEMINI,
                           bool* outRestoredFromNvs = nullptr,
                           const char* nvsNamespace = "vk_vprof");

    /**
     * @brief Indique si un profil actif valide est présent en NVS.
     */
    bool hasSavedActiveProfile(const char* nvsNamespace = "vk_vprof") const;

    /**
     * @brief Efface le profil actif sauvegardé en NVS et revient au profil par défaut.
     */
    bool resetActiveProfile(const char* nvsNamespace = "vk_vprof");

    /**
     * @brief Sauvegarde un profil favori dans l'un des emplacements (0 .. VOICEKIT_MAX_FAVORITE_PROFILES - 1).
     */
    bool saveFavoriteProfile(uint8_t slot, const VoiceProfile& profile, const char* nvsNamespace = "vk_vprof");

    /**
     * @brief Charge un profil favori depuis l'emplacement indiqué.
     */
    bool loadFavoriteProfile(uint8_t slot, VoiceProfile& outProfile, const char* nvsNamespace = "vk_vprof");

    bool hasFavoriteProfile(uint8_t slot, const char* nvsNamespace = "vk_vprof") const;
    bool clearFavoriteProfile(uint8_t slot, const char* nvsNamespace = "vk_vprof");

    const String& getLastError() const { return _lastError; }

    /**
     * @brief Calcule le CRC32 d'intégrité d'un profil vocal sérialisé.
     */
    static uint32_t computeProfileCrc32(uint16_t schemaVersion, const VoiceProfile& profile);

#if !defined(ESP32)
    // Hooks réservés aux tests unitaires natifs sur hôte (simulation redémarrage, corruption NVS, version schéma)
    static void resetMockNvsForTesting();
    static void injectCorruptedMockNvsForTesting(const char* nvsNamespace, uint16_t rawSchema, const char* rawBlob, uint32_t rawCrc);
    static String getRawMockNvsDumpForTesting(const char* nvsNamespace);
#endif

private:
    struct CustomVoiceStorage {
        bool used = false;
        char id[32] = "";
        VoiceProviderType provider = VoiceProviderType::GEMINI;
        VoiceGender gender = VoiceGender::NEUTRAL;
        char displayName[32] = "";
        char traitsFr[48] = "";
    };

    struct CustomLanguageStorage {
        bool used = false;
        char bcp47[16] = "";
        char iso639_1[8] = "";
        char displayNameFr[32] = "";
        char directiveLabelFr[40] = "";
        char samplePhrase[128] = "";
    };

    CustomVoiceStorage _customVoices[VOICEKIT_MAX_CUSTOM_VOICES];
    size_t _customVoiceCount = 0;

    CustomLanguageStorage _customLanguages[VOICEKIT_MAX_CUSTOM_LANGUAGES];
    size_t _customLanguageCount = 0;

    mutable String _lastError = "";
};
