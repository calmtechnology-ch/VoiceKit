#pragma once

#include <Arduino.h>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include "ToolDefinition.h"

/**
 * @class ToolValidator
 * @brief Validateur des déclarations, paramètres, types et contraintes d'outils.
 */
class ToolValidator {
public:
    /**
     * @brief Valide les arguments fournis par rapport à la définition d'un outil.
     * @param def Définition de l'outil cible.
     * @param args Arguments fournis pour l'exécution.
     * @return ToolResult::success() si valide, ou ToolResult::invalidArgument(...) avec motif explicite.
     */
    static ToolResult validate(const ToolDefinition& def, const ToolArgs& args) {
        // 1. Rejeter les paramètres inconnus, sauf autorisation explicite
        if (!def.allowUnknownParams) {
            for (const auto& entry : args.entries()) {
                if (!def.hasParam(entry.key)) {
                    return ToolResult::invalidArgument("Parametre inconnu : " + entry.key);
                }
            }
        }

        // 2. Vérifier les paramètres obligatoires
        for (const auto& param : def.parameters) {
            if (param.required && !args.has(param.name)) {
                return ToolResult::invalidArgument("Parametre obligatoire manquant : " + param.name);
            }
        }

        // 3. Vérifier les types attendus et les bornes minimales/maximales
        for (const auto& param : def.parameters) {
            if (!args.has(param.name)) continue;

            String val = args.getString(param.name);
            val.trim();

            switch (param.type) {
                case ToolParamType::INT: {
                    int64_t intVal = 0;
                    if (!parseInteger(val, intVal)) {
                        return ToolResult::invalidArgument("Type incorrect pour le parametre '" + param.name + "' : entier attendu, recu '" + val + "'");
                    }
                    if (param.hasMin && (double)intVal < param.minValue) {
                        return ToolResult::invalidArgument("Valeur hors limites pour '" + param.name + "' : " + String((long)intVal) + " < min " + String((long)param.minValue));
                    }
                    if (param.hasMax && (double)intVal > param.maxValue) {
                        return ToolResult::invalidArgument("Valeur hors limites pour '" + param.name + "' : " + String((long)intVal) + " > max " + String((long)param.maxValue));
                    }
                    break;
                }

                case ToolParamType::FLOAT: {
                    double floatVal = 0.0;
                    if (!parseFloat(val, floatVal)) {
                        return ToolResult::invalidArgument("Type incorrect pour le parametre '" + param.name + "' : nombre attendu, recu '" + val + "'");
                    }
                    if (param.hasMin && floatVal < param.minValue) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%.2f < min %.2f", floatVal, param.minValue);
                        return ToolResult::invalidArgument("Valeur hors limites pour '" + param.name + "' : " + String(buf));
                    }
                    if (param.hasMax && floatVal > param.maxValue) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%.2f > max %.2f", floatVal, param.maxValue);
                        return ToolResult::invalidArgument("Valeur hors limites pour '" + param.name + "' : " + String(buf));
                    }
                    break;
                }

                case ToolParamType::BOOLEAN: {
                    bool boolVal = false;
                    if (!parseBoolean(val, boolVal)) {
                        return ToolResult::invalidArgument("Type incorrect pour le parametre '" + param.name + "' : booleen attendu, recu '" + val + "'");
                    }
                    break;
                }

                case ToolParamType::STRING: {
                    if (param.hasMin && (double)val.length() < param.minValue) {
                        return ToolResult::invalidArgument("Longueur hors limites pour '" + param.name + "' : longueur " + String((int)val.length()) + " < min " + String((int)param.minValue));
                    }
                    if (param.hasMax && (double)val.length() > param.maxValue) {
                        return ToolResult::invalidArgument("Longueur hors limites pour '" + param.name + "' : longueur " + String((int)val.length()) + " > max " + String((int)param.maxValue));
                    }
                    break;
                }
            }
        }

        return ToolResult::success();
    }

    /**
     * @brief Analyse syntaxique stricte d'un entier.
     */
    static bool parseInteger(const String& s, int64_t& outVal) {
        if (s.isEmpty()) return false;
        size_t start = 0;
        if (s[0] == '+' || s[0] == '-') {
            start = 1;
            if (s.length() == 1) return false;
        }
        for (size_t i = start; i < s.length(); ++i) {
            if (!isdigit((unsigned char)s[i])) return false;
        }
        char* endptr = nullptr;
        outVal = std::strtoll(s.c_str(), &endptr, 10);
        return (endptr != nullptr && *endptr == '\0');
    }

    /**
     * @brief Analyse syntaxique stricte d'un nombre à virgule flottante.
     */
    static bool parseFloat(const String& s, double& outVal) {
        if (s.isEmpty()) return false;
        char* endptr = nullptr;
        outVal = std::strtod(s.c_str(), &endptr);
        return (endptr != nullptr && endptr != s.c_str() && *endptr == '\0');
    }

    /**
     * @brief Analyse syntaxique stricte d'un booléen.
     */
    static bool parseBoolean(const String& s, bool& outVal) {
        String lower = s;
        lower.trim();
        lower.toLowerCase();
        if (lower == "true" || lower == "1" || lower == "yes") {
            outVal = true;
            return true;
        }
        if (lower == "false" || lower == "0" || lower == "no") {
            outVal = false;
            return true;
        }
        return false;
    }
};
