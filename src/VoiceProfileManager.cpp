#include "VoiceProfileManager.h"
#include "core/Utf8Utils.h"
#include <cstring>
#include <cctype>

#if defined(ESP32)
#include <Preferences.h>
#else
#include <map>
#include <string>
#endif

namespace {

// =============================================================================
// Catalogue statique des voix prédéfinies (classification de genre indicative)
// =============================================================================
static const VoiceDescriptor kBuiltinVoices[] = {
    // --- Google Gemini Live API ---
    { "Aoede",   VoiceProviderType::GEMINI, VoiceGender::FEMININE,  "Aoede",   "Fluide, naturelle" },
    { "Kore",    VoiceProviderType::GEMINI, VoiceGender::FEMININE,  "Kore",    "Ferme, claire" },
    { "Leda",    VoiceProviderType::GEMINI, VoiceGender::FEMININE,  "Leda",    "Vive, lumineuse" },
    { "Autonoe", VoiceProviderType::GEMINI, VoiceGender::FEMININE,  "Autonoe", "Douce, posée" },
    { "Puck",    VoiceProviderType::GEMINI, VoiceGender::MASCULINE, "Puck",    "Enjouée, rythmée" },
    { "Charon",  VoiceProviderType::GEMINI, VoiceGender::MASCULINE, "Charon",  "Informative, posée" },
    { "Fenrir",  VoiceProviderType::GEMINI, VoiceGender::MASCULINE, "Fenrir",  "Énergique, affirmée" },
    { "Orus",    VoiceProviderType::GEMINI, VoiceGender::MASCULINE, "Orus",    "Grave, structurée" },
    { "Zephyr",  VoiceProviderType::GEMINI, VoiceGender::NEUTRAL,   "Zephyr",  "Claire, équilibrée" },
    { "Umbriel", VoiceProviderType::GEMINI, VoiceGender::NEUTRAL,   "Umbriel", "Calme, sobre" },

    // --- OpenAI Realtime API ---
    { "alloy",   VoiceProviderType::OPENAI, VoiceGender::NEUTRAL,   "alloy",   "Polyvalente, équilibrée" },
    { "ash",     VoiceProviderType::OPENAI, VoiceGender::MASCULINE, "ash",     "Claire, directe" },
    { "ballad",  VoiceProviderType::OPENAI, VoiceGender::NEUTRAL,   "ballad",  "Mélodique, posée" },
    { "coral",   VoiceProviderType::OPENAI, VoiceGender::FEMININE,  "coral",   "Chaleureuse, expressive" },
    { "echo",    VoiceProviderType::OPENAI, VoiceGender::MASCULINE, "echo",    "Douce, articulée" },
    { "sage",    VoiceProviderType::OPENAI, VoiceGender::FEMININE,  "sage",    "Calme, réfléchie" },
    { "shimmer", VoiceProviderType::OPENAI, VoiceGender::FEMININE,  "shimmer", "Lumineuse, limpide" },
    { "verse",   VoiceProviderType::OPENAI, VoiceGender::MASCULINE, "verse",   "Narrative, vivante" },
    { "marin",   VoiceProviderType::OPENAI, VoiceGender::FEMININE,  "marin",   "Naturelle, fluide" },
    { "cedar",   VoiceProviderType::OPENAI, VoiceGender::MASCULINE, "cedar",   "Chaleureuse, assurée" }
};

static constexpr size_t kBuiltinVoiceCount = sizeof(kBuiltinVoices) / sizeof(kBuiltinVoices[0]);

// =============================================================================
// Catalogue statique des langues de base (BCP-47)
// =============================================================================
static const LanguageDescriptor kBuiltinLanguages[] = {
    {
        "fr-FR", "fr", "Français", "francais (fr-FR)",
        "Bonjour, ceci est un essai de mon profil vocal."
    },
    {
        "fr-CH", "fr", "Français suisse", "francais suisse (fr-CH)",
        "Bonjour depuis la Suisse, ceci est un essai de mon profil vocal."
    },
    {
        "en-US", "en", "Anglais américain", "anglais (en-US)",
        "Hello, this is a live test of my voice profile."
    },
    {
        "en-GB", "en", "Anglais britannique", "anglais (en-GB)",
        "Good afternoon, this is a test of my voice profile."
    },
    {
        "de-DE", "de", "Allemand", "allemand (de-DE)",
        "Guten Tag, dies ist ein Test meines Sprachprofils."
    },
    {
        "it-IT", "it", "Italien", "italien (it-IT)",
        "Buongiorno, questo è un test del mio profilo vocale."
    },
    {
        "es-ES", "es", "Espagnol", "espagnol (es-ES)",
        "Hola, esta es una prueba de mi perfil de voz."
    }
};

static constexpr size_t kBuiltinLanguageCount = sizeof(kBuiltinLanguages) / sizeof(kBuiltinLanguages[0]);

static uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int b = 0; b < 8; ++b) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static uint32_t crc32String(uint32_t crc, const String& s) {
    const uint8_t sep = 0x1F;
    crc = crc32Update(crc, reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
    crc = crc32Update(crc, &sep, 1);
    return crc;
}

#if !defined(ESP32)
struct MockNvsSlotRecord {
    bool present = false;
    uint16_t schema = 0;
    uint8_t provider = 0;
    uint8_t gender = 0;
    uint8_t style = 0;
    uint8_t tone = 0;
    std::string language;
    std::string voiceId;
    std::string accent;
    uint32_t crc = 0;
    std::string rawCorruptBlob;
};

static std::map<std::string, std::map<std::string, MockNvsSlotRecord>> g_mockNvsStore;
#endif

static String makeKeyPrefix(const char* basePrefix) {
    return String(basePrefix ? basePrefix : "act");
}

} // namespace

VoiceProfileManager::VoiceProfileManager() = default;

const VoiceDescriptor* VoiceProfileManager::getBuiltinVoices(size_t& outCount) {
    outCount = kBuiltinVoiceCount;
    return kBuiltinVoices;
}

size_t VoiceProfileManager::getVoicesForProvider(VoiceProviderType provider,
                                                 VoiceGender genderFilter,
                                                 VoiceDescriptor* outBuffer,
                                                 size_t maxCount) const {
    if (!outBuffer || maxCount == 0) {
        return 0;
    }

    size_t written = 0;
    for (size_t i = 0; i < kBuiltinVoiceCount && written < maxCount; ++i) {
        const VoiceDescriptor& vd = kBuiltinVoices[i];
        if (vd.provider != provider) {
            continue;
        }
        if (genderFilter != VoiceGender::ANY && vd.gender != genderFilter) {
            continue;
        }
        outBuffer[written++] = vd;
    }

    for (size_t i = 0; i < _customVoiceCount && written < maxCount; ++i) {
        if (!_customVoices[i].used || _customVoices[i].provider != provider) {
            continue;
        }
        if (genderFilter != VoiceGender::ANY && _customVoices[i].gender != genderFilter) {
            continue;
        }
        VoiceDescriptor vd;
        vd.id = _customVoices[i].id;
        vd.provider = _customVoices[i].provider;
        vd.gender = _customVoices[i].gender;
        vd.displayName = _customVoices[i].displayName;
        vd.traitsFr = _customVoices[i].traitsFr;
        outBuffer[written++] = vd;
    }

    return written;
}

std::vector<VoiceDescriptor> VoiceProfileManager::getVoicesForProvider(VoiceProviderType provider,
                                                                       VoiceGender genderFilter) const {
    VoiceDescriptor buf[kBuiltinVoiceCount + VOICEKIT_MAX_CUSTOM_VOICES];
    size_t count = getVoicesForProvider(provider, genderFilter, buf, kBuiltinVoiceCount + VOICEKIT_MAX_CUSTOM_VOICES);
    return std::vector<VoiceDescriptor>(buf, buf + count);
}

const VoiceDescriptor* VoiceProfileManager::findVoice(VoiceProviderType provider, const String& voiceId) const {
    if (voiceId.isEmpty()) {
        return nullptr;
    }
    for (size_t i = 0; i < kBuiltinVoiceCount; ++i) {
        if (kBuiltinVoices[i].provider == provider && voiceId == kBuiltinVoices[i].id) {
            return &kBuiltinVoices[i];
        }
    }
    return nullptr;
}

bool VoiceProfileManager::isValidVoiceForProvider(VoiceProviderType provider, const String& voiceId) const {
    if (voiceId.isEmpty()) {
        return false;
    }
    if (findVoice(provider, voiceId) != nullptr) {
        return true;
    }
    for (size_t i = 0; i < _customVoiceCount; ++i) {
        if (_customVoices[i].used &&
            _customVoices[i].provider == provider &&
            voiceId == _customVoices[i].id) {
            return true;
        }
    }
    return false;
}

VoiceGender VoiceProfileManager::getVoiceGender(VoiceProviderType provider, const String& voiceId) const {
    const VoiceDescriptor* vd = findVoice(provider, voiceId);
    if (vd) {
        return vd->gender;
    }
    for (size_t i = 0; i < _customVoiceCount; ++i) {
        if (_customVoices[i].used &&
            _customVoices[i].provider == provider &&
            voiceId == _customVoices[i].id) {
            return _customVoices[i].gender;
        }
    }
    return VoiceGender::ANY;
}

bool VoiceProfileManager::registerCustomVoice(const char* id,
                                              VoiceProviderType provider,
                                              VoiceGender gender,
                                              const char* displayName,
                                              const char* traitsFr) {
    if (!id || id[0] == '\0') {
        _lastError = "Identifiant de voix personnalisee vide";
        return false;
    }
    if (isValidVoiceForProvider(provider, String(id))) {
        return true; // Déjà présente
    }
    if (_customVoiceCount >= VOICEKIT_MAX_CUSTOM_VOICES) {
        _lastError = "Capacite maximale du catalogue de voix personnalisees atteinte";
        return false;
    }

    CustomVoiceStorage& slot = _customVoices[_customVoiceCount++];
    slot.used = true;
    strncpy(slot.id, id, sizeof(slot.id) - 1);
    slot.id[sizeof(slot.id) - 1] = '\0';
    slot.provider = provider;
    slot.gender = gender;

    const char* disp = (displayName && displayName[0] != '\0') ? displayName : id;
    strncpy(slot.displayName, disp, sizeof(slot.displayName) - 1);
    slot.displayName[sizeof(slot.displayName) - 1] = '\0';

    const char* tr = (traitsFr && traitsFr[0] != '\0') ? traitsFr : "Voix personnalisée";
    strncpy(slot.traitsFr, tr, sizeof(slot.traitsFr) - 1);
    slot.traitsFr[sizeof(slot.traitsFr) - 1] = '\0';
    return true;
}

void VoiceProfileManager::clearCustomVoices() {
    for (size_t i = 0; i < VOICEKIT_MAX_CUSTOM_VOICES; ++i) {
        _customVoices[i] = CustomVoiceStorage();
    }
    _customVoiceCount = 0;
}

const LanguageDescriptor* VoiceProfileManager::getBuiltinLanguages(size_t& outCount) {
    outCount = kBuiltinLanguageCount;
    return kBuiltinLanguages;
}

size_t VoiceProfileManager::getSupportedLanguages(LanguageDescriptor* outBuffer, size_t maxCount) const {
    if (!outBuffer || maxCount == 0) {
        return 0;
    }
    size_t written = 0;
    for (size_t i = 0; i < kBuiltinLanguageCount && written < maxCount; ++i) {
        outBuffer[written++] = kBuiltinLanguages[i];
    }
    for (size_t i = 0; i < _customLanguageCount && written < maxCount; ++i) {
        if (!_customLanguages[i].used) continue;
        LanguageDescriptor ld;
        ld.bcp47 = _customLanguages[i].bcp47;
        ld.iso639_1 = _customLanguages[i].iso639_1;
        ld.displayNameFr = _customLanguages[i].displayNameFr;
        ld.directiveLabelFr = _customLanguages[i].directiveLabelFr;
        ld.samplePhrase = _customLanguages[i].samplePhrase;
        outBuffer[written++] = ld;
    }
    return written;
}

std::vector<LanguageDescriptor> VoiceProfileManager::getSupportedLanguages() const {
    LanguageDescriptor buf[kBuiltinLanguageCount + VOICEKIT_MAX_CUSTOM_LANGUAGES];
    size_t count = getSupportedLanguages(buf, kBuiltinLanguageCount + VOICEKIT_MAX_CUSTOM_LANGUAGES);
    return std::vector<LanguageDescriptor>(buf, buf + count);
}

const LanguageDescriptor* VoiceProfileManager::findLanguage(const String& bcp47) const {
    String trimmed = bcp47;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        return nullptr;
    }
    for (size_t i = 0; i < kBuiltinLanguageCount; ++i) {
        if (trimmed.equalsIgnoreCase(kBuiltinLanguages[i].bcp47)) {
            return &kBuiltinLanguages[i];
        }
    }
    return nullptr;
}

bool VoiceProfileManager::isValidLanguageCode(const String& bcp47) {
    String s = bcp47;
    s.trim();
    if (s.length() < 2 || s.length() > 35) {
        return false;
    }
    if (s != bcp47) {
        // Rejeter les espaces en tête ou en fin non nettoyés s'il y a des espaces internes
    }

    // Validation syntaxique RFC 5646 / BCP-47 :
    // Sous-tag primaire : 2 ou 3 lettres ASCII (ISO 639-1 / 639-2)
    // Sous-tags suivants séparés par '-' : 1 à 8 caractères alphanumériques ASCII (pas de '--' ni '-' final)
    size_t pos = 0;
    size_t primaryLen = 0;
    while (pos < s.length() && s[pos] != '-') {
        char c = s[pos];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            return false;
        }
        primaryLen++;
        pos++;
    }
    if (primaryLen < 2 || primaryLen > 3) {
        return false;
    }

    while (pos < s.length()) {
        if (s[pos] != '-') {
            return false;
        }
        pos++; // sauter '-'
        if (pos >= s.length()) {
            return false; // '-' final interdit
        }
        size_t subLen = 0;
        while (pos < s.length() && s[pos] != '-') {
            char c = s[pos];
            bool isAlphaNum = ((c >= 'a' && c <= 'z') ||
                               (c >= 'A' && c <= 'Z') ||
                               (c >= '0' && c <= '9'));
            if (!isAlphaNum) {
                return false;
            }
            subLen++;
            pos++;
        }
        if (subLen < 1 || subLen > 8) {
            return false;
        }
    }

    return true;
}

String VoiceProfileManager::extractIso639_1(const String& bcp47) {
    String s = bcp47;
    s.trim();
    if (s.length() < 2) {
        return "";
    }
    size_t sep = 0;
    while (sep < s.length() && s[sep] != '-' && s[sep] != '_') {
        char c = s[sep];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            return "";
        }
        sep++;
    }
    if (sep != 2) {
        return "";
    }
    String iso = s.substring(0, 2);
    iso.toLowerCase();
    return iso;
}

String VoiceProfileManager::getSamplePhraseForLanguage(const String& bcp47) const {
    const LanguageDescriptor* ld = findLanguage(bcp47);
    if (ld && ld->samplePhrase && ld->samplePhrase[0] != '\0') {
        return String(ld->samplePhrase);
    }
    for (size_t i = 0; i < _customLanguageCount; ++i) {
        if (_customLanguages[i].used && bcp47.equalsIgnoreCase(_customLanguages[i].bcp47)) {
            if (_customLanguages[i].samplePhrase[0] != '\0') {
                return String(_customLanguages[i].samplePhrase);
            }
        }
    }
    String iso = extractIso639_1(bcp47);
    if (iso == "fr") return "Bonjour, ceci est un essai de mon profil vocal.";
    if (iso == "en") return "Hello, this is a live test of my voice profile.";
    if (iso == "de") return "Guten Tag, dies ist ein Test meines Sprachprofils.";
    if (iso == "it") return "Buongiorno, questo è un test del mio profilo vocale.";
    if (iso == "es") return "Hola, esta es una prueba de mi perfil de voz.";
    return "Hello, voice profile test.";
}

bool VoiceProfileManager::registerCustomLanguage(const char* bcp47,
                                                 const char* iso639_1,
                                                 const char* displayNameFr,
                                                 const char* directiveLabelFr,
                                                 const char* samplePhrase) {
    if (!bcp47 || !isValidLanguageCode(String(bcp47))) {
        _lastError = "Code de langue BCP-47 invalide";
        return false;
    }
    if (findLanguage(String(bcp47)) != nullptr) {
        return true;
    }
    if (_customLanguageCount >= VOICEKIT_MAX_CUSTOM_LANGUAGES) {
        _lastError = "Capacite maximale de langues personnalisees atteinte";
        return false;
    }

    CustomLanguageStorage& slot = _customLanguages[_customLanguageCount++];
    slot.used = true;
    strncpy(slot.bcp47, bcp47, sizeof(slot.bcp47) - 1);
    slot.bcp47[sizeof(slot.bcp47) - 1] = '\0';

    String iso = (iso639_1 && iso639_1[0] != '\0') ? String(iso639_1) : extractIso639_1(String(bcp47));
    strncpy(slot.iso639_1, iso.c_str(), sizeof(slot.iso639_1) - 1);
    slot.iso639_1[sizeof(slot.iso639_1) - 1] = '\0';

    const char* disp = (displayNameFr && displayNameFr[0] != '\0') ? displayNameFr : bcp47;
    strncpy(slot.displayNameFr, disp, sizeof(slot.displayNameFr) - 1);
    slot.displayNameFr[sizeof(slot.displayNameFr) - 1] = '\0';

    const char* dir = (directiveLabelFr && directiveLabelFr[0] != '\0') ? directiveLabelFr : bcp47;
    strncpy(slot.directiveLabelFr, dir, sizeof(slot.directiveLabelFr) - 1);
    slot.directiveLabelFr[sizeof(slot.directiveLabelFr) - 1] = '\0';

    const char* sample = (samplePhrase && samplePhrase[0] != '\0') ? samplePhrase : "Voice profile test.";
    strncpy(slot.samplePhrase, sample, sizeof(slot.samplePhrase) - 1);
    slot.samplePhrase[sizeof(slot.samplePhrase) - 1] = '\0';
    return true;
}

VoiceProfile VoiceProfileManager::getDefaultProfile(VoiceProviderType provider) {
    VoiceProfile p;
    p.provider = provider;
    p.language = "fr-FR";
    p.voiceId = (provider == VoiceProviderType::OPENAI) ? "alloy" : VOICEKIT_DEFAULT_VOICE;
    p.preferredGender = VoiceGender::ANY;
    p.style = VoiceStyle::NATURAL;
    p.tone = VoiceTone::NEUTRAL;
    p.accent = "";
    return p;
}

String VoiceProfileManager::getDefaultVoiceForProvider(VoiceProviderType provider,
                                                       VoiceGender preferredGender) const {
    VoiceDescriptor buf[4];
    size_t n = getVoicesForProvider(provider, preferredGender, buf, 4);
    if (n > 0) {
        return String(buf[0].id);
    }
    return (provider == VoiceProviderType::OPENAI) ? String("alloy") : String(VOICEKIT_DEFAULT_VOICE);
}

bool VoiceProfileManager::validateProfile(const VoiceProfile& profile, String* outError) const {
    if (profile.provider != VoiceProviderType::GEMINI &&
        profile.provider != VoiceProviderType::OPENAI) {
        _lastError = "Fournisseur vocal inconnu ou non supporte";
        if (outError) *outError = _lastError;
        return false;
    }

    if (profile.voiceId.isEmpty()) {
        _lastError = "Identifiant de voix vide";
        if (outError) *outError = _lastError;
        return false;
    }

    if (!isValidVoiceForProvider(profile.provider, profile.voiceId)) {
        // Vérifier si la voix appartient à l'autre fournisseur pour donner un diagnostic explicite
        VoiceProviderType otherProv = (profile.provider == VoiceProviderType::GEMINI)
                                          ? VoiceProviderType::OPENAI
                                          : VoiceProviderType::GEMINI;
        if (isValidVoiceForProvider(otherProv, profile.voiceId)) {
            _lastError = String("La voix '") + profile.voiceId + "' appartient au fournisseur " +
                         voiceProviderTypeToString(otherProv) + " et est incompatible avec " +
                         voiceProviderTypeToString(profile.provider);
        } else {
            _lastError = String("Voix inconnue '") + profile.voiceId + "' pour le fournisseur " +
                         voiceProviderTypeToString(profile.provider);
        }
        if (outError) *outError = _lastError;
        return false;
    }

    if (!isValidLanguageCode(profile.language)) {
        _lastError = String("Code de langue BCP-47 invalide : '") + profile.language + "'";
        if (outError) *outError = _lastError;
        return false;
    }

    uint8_t genderRaw = static_cast<uint8_t>(profile.preferredGender);
    if (genderRaw > static_cast<uint8_t>(VoiceGender::NEUTRAL)) {
        _lastError = "Genre vocal percu invalide";
        if (outError) *outError = _lastError;
        return false;
    }

    uint8_t styleRaw = static_cast<uint8_t>(profile.style);
    if (styleRaw > static_cast<uint8_t>(VoiceStyle::NARRATIVE)) {
        _lastError = "Style d'elocution invalide";
        if (outError) *outError = _lastError;
        return false;
    }

    uint8_t toneRaw = static_cast<uint8_t>(profile.tone);
    if (toneRaw > static_cast<uint8_t>(VoiceTone::SERIOUS)) {
        _lastError = "Tonalite vocale invalide";
        if (outError) *outError = _lastError;
        return false;
    }

    if (profile.accent.length() > 64 || !Utf8Utils::isValidUtf8(profile.accent.c_str())) {
        _lastError = "Accent invalide (UTF-8 incorrect ou longueur > 64 octets)";
        if (outError) *outError = _lastError;
        return false;
    }

    _lastError = "";
    if (outError) *outError = "";
    return true;
}

VoiceProfile VoiceProfileManager::fromConfig(const VoiceKitConfig& config,
                                             VoiceGender preferredGender) {
    VoiceProfile p;
    p.provider = config.provider;
    p.language = config.language;
    p.voiceId = config.voice;
    p.preferredGender = preferredGender;
    p.style = static_cast<VoiceStyle>(config.style);
    p.tone = static_cast<VoiceTone>(config.tone);
    p.accent = config.accent;
    return p;
}

void VoiceProfileManager::applyToConfig(const VoiceProfile& profile, VoiceKitConfig& config) {
    config.provider = profile.provider;
    config.language = profile.language;
    config.voice = profile.voiceId;
    config.style = profile.style;
    config.tone = profile.tone;
    config.accent = profile.accent;
}

uint32_t VoiceProfileManager::computeProfileCrc32(uint16_t schemaVersion, const VoiceProfile& profile) {
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t header[6] = {
        static_cast<uint8_t>(schemaVersion & 0xFFu),
        static_cast<uint8_t>((schemaVersion >> 8) & 0xFFu),
        static_cast<uint8_t>(profile.provider),
        static_cast<uint8_t>(profile.preferredGender),
        static_cast<uint8_t>(profile.style),
        static_cast<uint8_t>(profile.tone)
    };
    crc = crc32Update(crc, header, sizeof(header));
    crc = crc32String(crc, profile.language);
    crc = crc32String(crc, profile.voiceId);
    crc = crc32String(crc, profile.accent);
    return crc ^ 0xFFFFFFFFu;
}

// =============================================================================
// Sauvegarde et restauration NVS (Preferences ESP32-S3 / Mock natif)
// =============================================================================

static bool saveSlotInternal(const VoiceProfileManager& mgr,
                             const char* nvsNamespace,
                             const char* slotPrefix,
                             const VoiceProfile& profile,
                             String& outErr) {
    if (!mgr.validateProfile(profile, &outErr)) {
        return false;
    }

    const char* ns = (nvsNamespace && nvsNamespace[0] != '\0') ? nvsNamespace : "vk_vprof";
    String pfx = makeKeyPrefix(slotPrefix);
    uint32_t crc = VoiceProfileManager::computeProfileCrc32(VOICEKIT_PROFILE_SCHEMA_VERSION, profile);

#if defined(ESP32)
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        outErr = "Echec ouverture NVS Preferences en ecriture";
        return false;
    }
    // RÈGLE DE SÉCURITÉ : Aucune clé API n'est jamais écrite en NVS de profil vocal
    prefs.putUShort((pfx + "_ver").c_str(), VOICEKIT_PROFILE_SCHEMA_VERSION);
    prefs.putUChar((pfx + "_prv").c_str(), static_cast<uint8_t>(profile.provider));
    prefs.putUChar((pfx + "_gnd").c_str(), static_cast<uint8_t>(profile.preferredGender));
    prefs.putUChar((pfx + "_sty").c_str(), static_cast<uint8_t>(profile.style));
    prefs.putUChar((pfx + "_ton").c_str(), static_cast<uint8_t>(profile.tone));
    prefs.putString((pfx + "_lng").c_str(), profile.language);
    prefs.putString((pfx + "_voi").c_str(), profile.voiceId);
    prefs.putString((pfx + "_acc").c_str(), profile.accent);
    prefs.putULong((pfx + "_crc").c_str(), crc);
    prefs.putBool((pfx + "_ok").c_str(), true);
    prefs.end();
    outErr = "";
    return true;
#else
    MockNvsSlotRecord rec;
    rec.present = true;
    rec.schema = VOICEKIT_PROFILE_SCHEMA_VERSION;
    rec.provider = static_cast<uint8_t>(profile.provider);
    rec.gender = static_cast<uint8_t>(profile.preferredGender);
    rec.style = static_cast<uint8_t>(profile.style);
    rec.tone = static_cast<uint8_t>(profile.tone);
    rec.language = profile.language.c_str();
    rec.voiceId = profile.voiceId.c_str();
    rec.accent = profile.accent.c_str();
    rec.crc = crc;
    rec.rawCorruptBlob.clear();
    g_mockNvsStore[ns][pfx.c_str()] = rec;
    outErr = "";
    return true;
#endif
}

static bool loadSlotInternal(const VoiceProfileManager& mgr,
                             const char* nvsNamespace,
                             const char* slotPrefix,
                             VoiceProfile& outProfile,
                             String& outErr) {
    const char* ns = (nvsNamespace && nvsNamespace[0] != '\0') ? nvsNamespace : "vk_vprof";
    String pfx = makeKeyPrefix(slotPrefix);

    uint16_t schema = 0;
    uint8_t provRaw = 0;
    uint8_t genderRaw = 0;
    uint8_t styleRaw = 0;
    uint8_t toneRaw = 0;
    String language;
    String voiceId;
    String accent;
    uint32_t storedCrc = 0;

#if defined(ESP32)
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        outErr = "Espace NVS absent ou non initialise";
        return false;
    }
    bool present = prefs.getBool((pfx + "_ok").c_str(), false);
    if (!present) {
        prefs.end();
        outErr = "Aucun profil vocal sauvegarde dans cet emplacement NVS";
        return false;
    }
    schema = prefs.getUShort((pfx + "_ver").c_str(), 0);
    provRaw = prefs.getUChar((pfx + "_prv").c_str(), 255);
    genderRaw = prefs.getUChar((pfx + "_gnd").c_str(), 255);
    styleRaw = prefs.getUChar((pfx + "_sty").c_str(), 255);
    toneRaw = prefs.getUChar((pfx + "_ton").c_str(), 255);
    language = prefs.getString((pfx + "_lng").c_str(), "");
    voiceId = prefs.getString((pfx + "_voi").c_str(), "");
    accent = prefs.getString((pfx + "_acc").c_str(), "");
    storedCrc = prefs.getULong((pfx + "_crc").c_str(), 0);
    prefs.end();
#else
    auto nsIt = g_mockNvsStore.find(ns);
    if (nsIt == g_mockNvsStore.end()) {
        outErr = "Aucun profil vocal sauvegarde en NVS";
        return false;
    }
    auto slotIt = nsIt->second.find(pfx.c_str());
    if (slotIt == nsIt->second.end() || !slotIt->second.present) {
        outErr = "Aucun profil vocal sauvegarde dans cet emplacement NVS";
        return false;
    }
    const MockNvsSlotRecord& rec = slotIt->second;
    if (!rec.rawCorruptBlob.empty()) {
        outErr = "Donnees NVS corrompues detectees";
        return false;
    }
    schema = rec.schema;
    provRaw = rec.provider;
    genderRaw = rec.gender;
    styleRaw = rec.style;
    toneRaw = rec.tone;
    language = rec.language.c_str();
    voiceId = rec.voiceId.c_str();
    accent = rec.accent.c_str();
    storedCrc = rec.crc;
#endif

    if (schema != VOICEKIT_PROFILE_SCHEMA_VERSION) {
        outErr = String("Version de schema NVS incompatible (lue=") + String(schema) +
                 ", attendue=" + String(VOICEKIT_PROFILE_SCHEMA_VERSION) + ")";
        return false;
    }

    if (provRaw > static_cast<uint8_t>(VoiceProviderType::OPENAI) ||
        genderRaw > static_cast<uint8_t>(VoiceGender::NEUTRAL) ||
        styleRaw > static_cast<uint8_t>(VoiceStyle::NARRATIVE) ||
        toneRaw > static_cast<uint8_t>(VoiceTone::SERIOUS)) {
        outErr = "Valeurs d'enumeration NVS hors limites (corruption)";
        return false;
    }

    VoiceProfile candidate;
    candidate.provider = static_cast<VoiceProviderType>(provRaw);
    candidate.preferredGender = static_cast<VoiceGender>(genderRaw);
    candidate.style = static_cast<VoiceStyle>(styleRaw);
    candidate.tone = static_cast<VoiceTone>(toneRaw);
    candidate.language = language;
    candidate.voiceId = voiceId;
    candidate.accent = accent;

    uint32_t expectedCrc = VoiceProfileManager::computeProfileCrc32(schema, candidate);
    if (storedCrc != expectedCrc) {
        outErr = "Somme de controle CRC32 NVS invalide (donnees corrompues)";
        return false;
    }

    if (!mgr.validateProfile(candidate, &outErr)) {
        return false;
    }

    outProfile = candidate;
    outErr = "";
    return true;
}

static bool hasSlotInternal(const VoiceProfileManager& mgr,
                            const char* nvsNamespace,
                            const char* slotPrefix) {
    VoiceProfile dummy;
    String dummyErr;
    return loadSlotInternal(mgr, nvsNamespace, slotPrefix, dummy, dummyErr);
}

static bool clearSlotInternal(const char* nvsNamespace, const char* slotPrefix) {
    const char* ns = (nvsNamespace && nvsNamespace[0] != '\0') ? nvsNamespace : "vk_vprof";
    String pfx = makeKeyPrefix(slotPrefix);
#if defined(ESP32)
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return false;
    }
    prefs.remove((pfx + "_ok").c_str());
    prefs.remove((pfx + "_ver").c_str());
    prefs.remove((pfx + "_prv").c_str());
    prefs.remove((pfx + "_gnd").c_str());
    prefs.remove((pfx + "_sty").c_str());
    prefs.remove((pfx + "_ton").c_str());
    prefs.remove((pfx + "_lng").c_str());
    prefs.remove((pfx + "_voi").c_str());
    prefs.remove((pfx + "_acc").c_str());
    prefs.remove((pfx + "_crc").c_str());
    prefs.end();
    return true;
#else
    auto nsIt = g_mockNvsStore.find(ns);
    if (nsIt != g_mockNvsStore.end()) {
        nsIt->second.erase(pfx.c_str());
    }
    return true;
#endif
}

bool VoiceProfileManager::saveActiveProfile(const VoiceProfile& profile, const char* nvsNamespace) {
    return saveSlotInternal(*this, nvsNamespace, "act", profile, _lastError);
}

bool VoiceProfileManager::loadActiveProfile(VoiceProfile& outProfile,
                                            VoiceProviderType fallbackProvider,
                                            bool* outRestoredFromNvs,
                                            const char* nvsNamespace) {
    VoiceProfile loaded;
    String err;
    if (loadSlotInternal(*this, nvsNamespace, "act", loaded, err)) {
        outProfile = loaded;
        _lastError = "";
        if (outRestoredFromNvs) *outRestoredFromNvs = true;
        return true;
    }

    // Repli sécurisé non bloquant vers une configuration valide par défaut
    outProfile = getDefaultProfile(fallbackProvider);
    _lastError = err;
    if (outRestoredFromNvs) *outRestoredFromNvs = false;
    Serial.printf("[VoiceProfileManager] Repli sur profil par defaut (%s / %s) : %s\n",
                  voiceProviderTypeToString(outProfile.provider),
                  outProfile.voiceId.c_str(),
                  err.c_str());
    return false;
}

bool VoiceProfileManager::hasSavedActiveProfile(const char* nvsNamespace) const {
    return hasSlotInternal(*this, nvsNamespace, "act");
}

bool VoiceProfileManager::resetActiveProfile(const char* nvsNamespace) {
    _lastError = "";
    return clearSlotInternal(nvsNamespace, "act");
}

bool VoiceProfileManager::saveFavoriteProfile(uint8_t slot, const VoiceProfile& profile, const char* nvsNamespace) {
    if (slot >= VOICEKIT_MAX_FAVORITE_PROFILES) {
        _lastError = "Indice d'emplacement favori hors limites";
        return false;
    }
    char slotName[8];
    snprintf(slotName, sizeof(slotName), "fav%u", static_cast<unsigned>(slot));
    return saveSlotInternal(*this, nvsNamespace, slotName, profile, _lastError);
}

bool VoiceProfileManager::loadFavoriteProfile(uint8_t slot, VoiceProfile& outProfile, const char* nvsNamespace) {
    if (slot >= VOICEKIT_MAX_FAVORITE_PROFILES) {
        _lastError = "Indice d'emplacement favori hors limites";
        outProfile = getDefaultProfile(VoiceProviderType::GEMINI);
        return false;
    }
    char slotName[8];
    snprintf(slotName, sizeof(slotName), "fav%u", static_cast<unsigned>(slot));
    if (loadSlotInternal(*this, nvsNamespace, slotName, outProfile, _lastError)) {
        return true;
    }
    outProfile = getDefaultProfile(VoiceProviderType::GEMINI);
    return false;
}

bool VoiceProfileManager::hasFavoriteProfile(uint8_t slot, const char* nvsNamespace) const {
    if (slot >= VOICEKIT_MAX_FAVORITE_PROFILES) {
        return false;
    }
    char slotName[8];
    snprintf(slotName, sizeof(slotName), "fav%u", static_cast<unsigned>(slot));
    return hasSlotInternal(*this, nvsNamespace, slotName);
}

bool VoiceProfileManager::clearFavoriteProfile(uint8_t slot, const char* nvsNamespace) {
    if (slot >= VOICEKIT_MAX_FAVORITE_PROFILES) {
        _lastError = "Indice d'emplacement favori hors limites";
        return false;
    }
    char slotName[8];
    snprintf(slotName, sizeof(slotName), "fav%u", static_cast<unsigned>(slot));
    _lastError = "";
    return clearSlotInternal(nvsNamespace, slotName);
}

#if !defined(ESP32)
void VoiceProfileManager::resetMockNvsForTesting() {
    g_mockNvsStore.clear();
}

void VoiceProfileManager::injectCorruptedMockNvsForTesting(const char* nvsNamespace,
                                                           uint16_t rawSchema,
                                                           const char* rawBlob,
                                                           uint32_t rawCrc) {
    const char* ns = (nvsNamespace && nvsNamespace[0] != '\0') ? nvsNamespace : "vk_vprof";
    MockNvsSlotRecord rec;
    rec.present = true;
    rec.schema = rawSchema;
    rec.provider = 0;
    rec.gender = 0;
    rec.style = 0;
    rec.tone = 0;
    rec.language = "fr-FR";
    rec.voiceId = "Aoede";
    rec.accent = "";
    rec.crc = rawCrc;
    rec.rawCorruptBlob = rawBlob ? rawBlob : "";
    g_mockNvsStore[ns]["act"] = rec;
}

String VoiceProfileManager::getRawMockNvsDumpForTesting(const char* nvsNamespace) {
    const char* ns = (nvsNamespace && nvsNamespace[0] != '\0') ? nvsNamespace : "vk_vprof";
    auto nsIt = g_mockNvsStore.find(ns);
    if (nsIt == g_mockNvsStore.end()) {
        return "";
    }
    String dump;
    for (const auto& kv : nsIt->second) {
        const MockNvsSlotRecord& r = kv.second;
        dump += String(kv.first.c_str()) + ":ver=" + String(r.schema) +
                "|prv=" + String(r.provider) +
                "|lng=" + String(r.language.c_str()) +
                "|voi=" + String(r.voiceId.c_str()) +
                "|acc=" + String(r.accent.c_str()) + "\n";
    }
    return dump;
}
#endif

