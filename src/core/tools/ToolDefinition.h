#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>
#include <functional>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include "ToolEvents.h"

/**
 * @enum ToolSecurityPolicy
 * @brief Politique de sécurité et niveau de contrôle d'une commande locale.
 */
enum class ToolSecurityPolicy : uint8_t {
    READ_ONLY = 0,  ///< Commande de lecture seule sans altération d'état (ex: get_status)
    WRITE,          ///< Commande modifiant l'état autorisée sans confirmation (ex: set_volume)
    SENSITIVE       ///< Commande sensible : confirmation explicite préalable obligatoire
};

inline const char* toolSecurityPolicyToString(ToolSecurityPolicy policy) {
    switch (policy) {
        case ToolSecurityPolicy::READ_ONLY: return "READ_ONLY";
        case ToolSecurityPolicy::WRITE:     return "WRITE";
        case ToolSecurityPolicy::SENSITIVE: return "SENSITIVE";
        default:                            return "UNKNOWN";
    }
}

/**
 * @enum ToolExecutionStatus
 * @brief Codes de retour standardisés d'exécution d'un outil (VoiceKit v0.6.4).
 */
enum class ToolExecutionStatus : uint8_t {
    SUCCESS = 0,            ///< Exécution réussie
    NOT_FOUND,              ///< Identifiant d'outil introuvable dans le registre
    INVALID_ARGUMENT,       ///< Paramètre manquant, type incorrect, hors limites ou inconnu
    PERMISSION_DENIED,      ///< Politique d'autorisation violée ou confirmation invalide/expirée
    CONFIRMATION_REQUIRED,  ///< Commande sensible requérant une confirmation explicite
    BUSY,                   ///< Exécuteur déjà occupé (protection contre double demande)
    EXECUTION_FAILED        ///< Échec lors de l'exécution de la fonction outil
};

inline const char* toolExecutionStatusToString(ToolExecutionStatus status) {
    switch (status) {
        case ToolExecutionStatus::SUCCESS:               return "SUCCESS";
        case ToolExecutionStatus::NOT_FOUND:             return "NOT_FOUND";
        case ToolExecutionStatus::INVALID_ARGUMENT:      return "INVALID_ARGUMENT";
        case ToolExecutionStatus::PERMISSION_DENIED:     return "PERMISSION_DENIED";
        case ToolExecutionStatus::CONFIRMATION_REQUIRED: return "CONFIRMATION_REQUIRED";
        case ToolExecutionStatus::BUSY:                  return "BUSY";
        case ToolExecutionStatus::EXECUTION_FAILED:      return "EXECUTION_FAILED";
        default:                                         return "UNKNOWN";
    }
}

/**
 * @struct ToolResult
 * @brief Résultat structuré retourné par l'exécution d'une commande.
 */
struct ToolResult {
    ToolExecutionStatus status = ToolExecutionStatus::SUCCESS;  ///< Statut de retour
    String output;                                              ///< Résultat utile (ex: texte, JSON)
    String error;                                               ///< Motif d'erreur le cas échéant
    String confirmationToken;                                   ///< Jeton unique émis si CONFIRMATION_REQUIRED

    bool isSuccess() const { return status == ToolExecutionStatus::SUCCESS; }
    bool isConfirmationRequired() const { return status == ToolExecutionStatus::CONFIRMATION_REQUIRED; }
    const char* statusString() const { return toolExecutionStatusToString(status); }

    static ToolResult success(const String& out = "") {
        ToolResult r;
        r.status = ToolExecutionStatus::SUCCESS;
        r.output = out;
        return r;
    }

    static ToolResult notFound(const String& err = "Commande introuvable") {
        ToolResult r;
        r.status = ToolExecutionStatus::NOT_FOUND;
        r.error = err;
        return r;
    }

    static ToolResult invalidArgument(const String& err) {
        ToolResult r;
        r.status = ToolExecutionStatus::INVALID_ARGUMENT;
        r.error = err;
        return r;
    }

    static ToolResult permissionDenied(const String& err = "Acces refuse") {
        ToolResult r;
        r.status = ToolExecutionStatus::PERMISSION_DENIED;
        r.error = err;
        return r;
    }

    static ToolResult confirmationRequired(const String& token, const String& prompt = "") {
        ToolResult r;
        r.status = ToolExecutionStatus::CONFIRMATION_REQUIRED;
        r.confirmationToken = token;
        r.error = prompt.isEmpty() ? "Confirmation explicite obligatoire" : prompt;
        return r;
    }

    static ToolResult busy(const String& err = "Executeur occupe") {
        ToolResult r;
        r.status = ToolExecutionStatus::BUSY;
        r.error = err;
        return r;
    }

    static ToolResult executionFailed(const String& err) {
        ToolResult r;
        r.status = ToolExecutionStatus::EXECUTION_FAILED;
        r.error = err;
        return r;
    }
};

/**
 * @enum ToolParamType
 * @brief Types de données supportés pour les paramètres de commande.
 */
enum class ToolParamType : uint8_t {
    STRING = 0, ///< Chaîne de caractères
    INT,        ///< Entier signé (int32 / int64)
    FLOAT,      ///< Nombre à virgule flottante (float / double)
    BOOLEAN     ///< Booléen (true / false / 1 / 0)
};

inline const char* toolParamTypeToString(ToolParamType type) {
    switch (type) {
        case ToolParamType::STRING:  return "STRING";
        case ToolParamType::INT:     return "INT";
        case ToolParamType::FLOAT:   return "FLOAT";
        case ToolParamType::BOOLEAN: return "BOOLEAN";
        default:                     return "UNKNOWN";
    }
}

/**
 * @struct ToolParam
 * @brief Définition descriptive et contraintes d'un paramètre d'outil.
 */
struct ToolParam {
    String name;                            ///< Nom unique du paramètre (ex: "volume")
    String description;                     ///< Description fonctionnelle
    ToolParamType type = ToolParamType::STRING; ///< Type attendu
    bool required = true;                   ///< Paramètre obligatoire
    bool hasMin = false;                    ///< Indique si une borne minimale est active
    double minValue = 0.0;                  ///< Valeur minimale admissible
    bool hasMax = false;                    ///< Indique si une borne maximale est active
    double maxValue = 0.0;                  ///< Valeur maximale admissible

    ToolParam() = default;

    ToolParam(const String& pName, ToolParamType pType, bool pRequired = true, const String& pDesc = "")
        : name(pName), description(pDesc), type(pType), required(pRequired),
          hasMin(false), minValue(0.0), hasMax(false), maxValue(0.0) {}

    ToolParam(const String& pName, ToolParamType pType, double minVal, double maxVal, bool pRequired = true, const String& pDesc = "")
        : name(pName), description(pDesc), type(pType), required(pRequired),
          hasMin(true), minValue(minVal), hasMax(true), maxValue(maxVal) {}
};

/**
 * @class ToolArgs
 * @brief Conteneur de paramètres d'exécution clé-valeur avec accesseurs typés.
 */
class ToolArgs {
public:
    struct Entry {
        String key;
        String value;
    };

    ToolArgs() = default;

    ToolArgs(std::initializer_list<std::pair<String, String>> initList) {
        for (const auto& item : initList) {
            set(item.first, item.second);
        }
    }

    bool has(const String& key) const {
        for (const auto& e : _entries) {
            if (e.key == key) return true;
        }
        return false;
    }

    String getString(const String& key, const String& def = "") const {
        for (const auto& e : _entries) {
            if (e.key == key) return e.value;
        }
        return def;
    }

    int getInt(const String& key, int def = 0) const {
        for (const auto& e : _entries) {
            if (e.key == key) {
                return (int)std::strtol(e.value.c_str(), nullptr, 10);
            }
        }
        return def;
    }

    double getFloat(const String& key, double def = 0.0) const {
        for (const auto& e : _entries) {
            if (e.key == key) {
                return std::strtod(e.value.c_str(), nullptr);
            }
        }
        return def;
    }

    bool getBool(const String& key, bool def = false) const {
        for (const auto& e : _entries) {
            if (e.key == key) {
                String v = e.value;
                v.trim();
                v.toLowerCase();
                if (v == "true" || v == "1" || v == "yes") return true;
                if (v == "false" || v == "0" || v == "no") return false;
            }
        }
        return def;
    }

    void set(const String& key, const String& val) {
        for (auto& e : _entries) {
            if (e.key == key) {
                e.value = val;
                return;
            }
        }
        _entries.push_back({key, val});
    }

    void set(const String& key, const char* val) {
        set(key, String(val ? val : ""));
    }

    void set(const String& key, int val) {
        set(key, String(val));
    }

    void set(const String& key, double val) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.4g", val);
        set(key, String(buf));
    }

    void set(const String& key, bool val) {
        set(key, val ? "true" : "false");
    }

    size_t size() const { return _entries.size(); }
    bool empty() const { return _entries.empty(); }
    void clear() { _entries.clear(); }
    const std::vector<Entry>& entries() const { return _entries; }

    std::vector<String> keys() const {
        std::vector<String> res;
        res.reserve(_entries.size());
        for (const auto& e : _entries) {
            res.push_back(e.key);
        }
        return res;
    }

    String toJson() const {
        String json = "{";
        for (size_t i = 0; i < _entries.size(); ++i) {
            if (i > 0) json += ",";
            json += "\"" + _entries[i].key + "\":\"" + _entries[i].value + "\"";
        }
        json += "}";
        return json;
    }

    /**
     * @brief Parseur de chaîne JSON plat ou paires clé=valeur.
     */
    static ToolArgs fromJson(const String& jsonStr) {
        ToolArgs args;
        String s = jsonStr;
        s.trim();
        if (s.isEmpty()) return args;

        // Parseur JSON plat robuste sans dépendance externe
        if (s.startsWith("{") && s.endsWith("}")) {
            size_t idx = 1;
            while (idx < s.length() && s[idx] != '}') {
                // Trouver la clé entre guillemets
                int keyStart = s.indexOf('"', idx);
                if (keyStart == -1) break;
                int keyEnd = s.indexOf('"', keyStart + 1);
                if (keyEnd == -1) break;
                String key = s.substring(keyStart + 1, keyEnd);

                // Trouver le deux-points
                int colon = s.indexOf(':', keyEnd + 1);
                if (colon == -1) break;

                // Trouver la valeur
                size_t valStart = colon + 1;
                while (valStart < s.length() && (s[valStart] == ' ' || s[valStart] == '\t' || s[valStart] == '\r' || s[valStart] == '\n')) {
                    valStart++;
                }
                if (valStart >= s.length() || s[valStart] == '}') break;

                String val;
                size_t nextIdx = valStart;
                if (s[valStart] == '"') {
                    // Chaîne entre guillemets
                    int strEnd = s.indexOf('"', valStart + 1);
                    if (strEnd == -1) break;
                    val = s.substring(valStart + 1, strEnd);
                    nextIdx = strEnd + 1;
                } else {
                    // Valeur brute (nombre, booléen) jusqu'à ',' ou '}'
                    size_t vEnd = valStart;
                    while (vEnd < s.length() && s[vEnd] != ',' && s[vEnd] != '}' && s[vEnd] != ' ' && s[vEnd] != '\n') {
                        vEnd++;
                    }
                    val = s.substring(valStart, vEnd);
                    val.trim();
                    nextIdx = vEnd;
                }

                args.set(key, val);

                // Avancer jusqu'à la prochaine virgule ou fin
                int comma = s.indexOf(',', nextIdx);
                if (comma == -1) {
                    break;
                }
                idx = comma + 1;
            }
            return args;
        }

        // Repli : paires clé=valeur séparées par virgules ou espaces
        int start = 0;
        while (start < (int)s.length()) {
            int delim = s.indexOf(',', start);
            if (delim == -1) delim = s.indexOf(';', start);
            if (delim == -1) delim = s.length();
            String pair = s.substring(start, delim);
            pair.trim();
            int eq = pair.indexOf('=');
            if (eq != -1) {
                String k = pair.substring(0, eq);
                String v = pair.substring(eq + 1);
                k.trim();
                v.trim();
                if (!k.isEmpty()) args.set(k, v);
            }
            start = delim + 1;
        }
        return args;
    }

private:
    std::vector<Entry> _entries;
};

/**
 * @brief Signature de la fonction d'exécution d'un outil local.
 */
using ToolHandler = std::function<ToolResult(const ToolArgs& args)>;

/**
 * @class ToolDefinition
 * @brief Représentation complète d'une commande locale enregistrable.
 */
class ToolDefinition {
public:
    String id;                                                      ///< Identifiant unique (ex: "system.get_status")
    String description;                                             ///< Description lisible
    ToolSecurityPolicy securityPolicy = ToolSecurityPolicy::READ_ONLY; ///< Politique d'autorisation
    std::vector<ToolParam> parameters;                             ///< Paramètres acceptés
    bool allowUnknownParams = false;                                ///< Autorise les paramètres non déclarés
    ToolHandler handler;                                            ///< Fonction d'exécution

    ToolDefinition() = default;

    ToolDefinition(const String& toolId, const String& desc, ToolSecurityPolicy policy = ToolSecurityPolicy::READ_ONLY)
        : id(toolId), description(desc), securityPolicy(policy) {}

    ToolDefinition& addParam(const ToolParam& param) {
        parameters.push_back(param);
        return *this;
    }

    ToolDefinition& setHandler(ToolHandler h) {
        handler = std::move(h);
        return *this;
    }

    ToolDefinition& setSecurityPolicy(ToolSecurityPolicy policy) {
        securityPolicy = policy;
        return *this;
    }

    ToolDefinition& setAllowUnknownParams(bool allow) {
        allowUnknownParams = allow;
        return *this;
    }

    bool hasParam(const String& name) const {
        for (const auto& p : parameters) {
            if (p.name == name) return true;
        }
        return false;
    }

    const ToolParam* getParam(const String& name) const {
        for (const auto& p : parameters) {
            if (p.name == name) return &p;
        }
        return nullptr;
    }

    /**
     * @brief Valide la conformité structurelle de la définition de commande.
     */
    bool isValid(String& outReason) const {
        if (id.isEmpty()) {
            outReason = "Identifiant de commande vide";
            return false;
        }
        for (char c : id) {
            if (c <= 32 || c > 126) {
                outReason = "Identifiant de commande invalide (espaces ou caracteres speciaux interdits)";
                return false;
            }
        }
        if (!handler) {
            outReason = "Fonction d'execution non definie";
            return false;
        }
        for (size_t i = 0; i < parameters.size(); ++i) {
            const auto& p = parameters[i];
            if (p.name.isEmpty()) {
                outReason = "Nom de parametre vide";
                return false;
            }
            if (p.hasMin && p.hasMax && p.minValue > p.maxValue) {
                outReason = "Contrainte min > max pour le parametre : " + p.name;
                return false;
            }
            for (size_t j = i + 1; j < parameters.size(); ++j) {
                if (parameters[j].name == p.name) {
                    outReason = "Parametre duplique : " + p.name;
                    return false;
                }
            }
        }
        return true;
    }
};
