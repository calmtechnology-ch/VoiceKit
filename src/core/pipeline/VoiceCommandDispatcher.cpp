#include "VoiceCommandDispatcher.h"
#include <cctype>

VoiceCommandDispatcher::VoiceCommandDispatcher()
    : _registry(nullptr) {
}

VoiceCommandDispatcher::VoiceCommandDispatcher(const ToolRegistry& registry)
    : _registry(&registry) {
}

void VoiceCommandDispatcher::setRegistry(const ToolRegistry* registry) {
    _registry = registry;
}

bool VoiceCommandDispatcher::registerMapping(const String& toolId, const std::vector<String>& patterns, const ToolArgs& defaultArgs) {
    if (toolId.isEmpty() || patterns.empty()) {
        return false;
    }

    std::vector<String> normPatterns;
    normPatterns.reserve(patterns.size());
    for (const auto& p : patterns) {
        String n = stripPoliteWrappers(normalize(p));
        if (!n.isEmpty()) {
            normPatterns.push_back(n);
        }
    }

    if (normPatterns.empty()) {
        return false;
    }

    // Si l'outil est déjà enregistré, mettre à jour ses motifs
    for (auto& item : _patterns) {
        if (item.toolId == toolId) {
            item.patterns = std::move(normPatterns);
            item.defaultArgs = defaultArgs;
            return true;
        }
    }

    _patterns.push_back({toolId, std::move(normPatterns), defaultArgs});
    return true;
}

bool VoiceCommandDispatcher::registerMapping(const String& toolId, const String& pattern, const ToolArgs& defaultArgs) {
    return registerMapping(toolId, std::vector<String>{pattern}, defaultArgs);
}

bool VoiceCommandDispatcher::unregisterMapping(const String& toolId) {
    for (auto it = _patterns.begin(); it != _patterns.end(); ++it) {
        if (it->toolId == toolId) {
            _patterns.erase(it);
            return true;
        }
    }
    return false;
}

void VoiceCommandDispatcher::clear() {
    _patterns.clear();
}

size_t VoiceCommandDispatcher::getMappingCount() const {
    return _patterns.size();
}

String VoiceCommandDispatcher::normalize(const String& input) {
    if (input.isEmpty()) return "";

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(input.c_str());
    const size_t len = input.length();

    String result;
    result.reserve(len);

    size_t i = 0;
    while (i < len) {
        uint8_t b0 = bytes[i];

        // 1. Caractères ASCII (0x00 .. 0x7F)
        if (b0 <= 0x7F) {
            char c = static_cast<char>(b0);
            if (c >= 'A' && c <= 'Z') {
                result += static_cast<char>(c + 32); // Minuscule
            } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                result += c;
            } else {
                result += ' '; // Séparateur d'espace pour ponctuation et symboles
            }
            i++;
            continue;
        }

        // 2. Traitement UTF-8 des accents français
        if (b0 == 0xC3 && i + 1 < len) {
            uint8_t b1 = bytes[i + 1];
            switch (b1) {
                case 0xA0: case 0xA2: case 0xA4: // à, â, ä
                case 0x80: case 0x82: case 0x84: // À, Â, Ä
                    result += 'a';
                    break;
                case 0xA9: case 0xA8: case 0xAA: case 0xAB: // é, è, ê, ë
                case 0x89: case 0x88: case 0x8A: case 0x8B: // É, È, Ê, Ë
                    result += 'e';
                    break;
                case 0xAE: case 0xAF: // î, ï
                case 0x8E: case 0x8F: // Î, Ï
                    result += 'i';
                    break;
                case 0xB4: case 0xB6: // ô, ö
                case 0x94: case 0x96: // Ô, Ö
                    result += 'o';
                    break;
                case 0xB9: case 0xBB: case 0xBC: // ù, û, ü
                case 0x99: case 0x9B: case 0x9C: // Ù, Û, Ü
                    result += 'u';
                    break;
                case 0xA7: case 0x87: // ç, Ç
                    result += 'c';
                    break;
                default:
                    result += ' ';
                    break;
            }
            i += 2;
            continue;
        }

        // Autres octets UTF-8 multioctets non gérés
        result += ' ';
        i++;
    }

    // Réduction des espaces multiples et rognage
    String clean;
    clean.reserve(result.length());
    bool lastWasSpace = true;
    for (size_t k = 0; k < result.length(); ++k) {
        char ch = result[k];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            if (!lastWasSpace) {
                clean += ' ';
                lastWasSpace = true;
            }
        } else {
            clean += ch;
            lastWasSpace = false;
        }
    }
    clean.trim();
    return clean;
}

String VoiceCommandDispatcher::stripPoliteWrappers(const String& text) {
    if (text.isEmpty()) return "";

    String s = text;
    s.trim();

    // Préfixes courants
    static const char* prefixes[] = {
        "s il te plait", "s il vous plait", "sil te plait", "sil vous plait",
        "peux tu me dire", "pouvez vous me dire", "pourrais tu me dire", "pourriez vous me dire",
        "peux tu", "pouvez vous", "pourrais tu", "pourriez vous",
        "dis moi", "dites moi", "donne moi", "donnez moi",
        "je voudrais savoir", "je voudrais", "je veux",
        "merci de me dire", "merci de",
        "veuillez", "stp", "svp"
    };

    bool stripped = true;
    while (stripped) {
        stripped = false;
        for (const char* p : prefixes) {
            size_t plen = strlen(p);
            if (s.startsWith(p)) {
                if (s.length() == plen) {
                    s = "";
                    stripped = true;
                    break;
                } else if (s[plen] == ' ') {
                    s = s.substring(plen + 1);
                    s.trim();
                    stripped = true;
                    break;
                }
            }
        }
    }

    // Suffixes courants
    static const char* suffixes[] = {
        "s il te plait", "s il vous plait", "sil te plait", "sil vous plait",
        "merci beaucoup", "merci bien", "merci",
        "stp", "svp"
    };

    stripped = true;
    while (stripped) {
        stripped = false;
        for (const char* su : suffixes) {
            size_t slen = strlen(su);
            if (s.endsWith(su)) {
                if (s.length() == slen) {
                    s = "";
                    stripped = true;
                    break;
                } else if (s[s.length() - slen - 1] == ' ') {
                    s = s.substring(0, s.length() - slen - 1);
                    s.trim();
                    stripped = true;
                    break;
                }
            }
        }
    }

    return s;
}

bool VoiceCommandDispatcher::extractNumber(const String& text, int& outInt, double& outDouble) {
    int foundStart = -1;
    int foundLen = 0;
    bool hasDot = false;

    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c >= '0' && c <= '9') {
            if (foundStart < 0) {
                foundStart = (int)i;
                hasDot = false;
            }
            foundLen++;
        } else if ((c == '.' || c == ',') && foundStart >= 0 && !hasDot) {
            hasDot = true;
            foundLen++;
        } else {
            if (foundStart >= 0) {
                break; // Premier nombre trouvé complet
            }
        }
    }

    if (foundStart >= 0 && foundLen > 0) {
        String numStr = text.substring(foundStart, foundStart + foundLen);
        for (char& ch : numStr) {
            if (ch == ',') ch = '.';
        }
        outInt = std::atoi(numStr.c_str());
        outDouble = std::atof(numStr.c_str());
        return true;
    }

    return false;
}

VoiceCommandMatch VoiceCommandDispatcher::match(const String& rawText) const {
    VoiceCommandMatch res;
    res.originalText = rawText;
    res.matched = false;

    if (rawText.isEmpty() || _patterns.empty()) {
        return res;
    }

    String norm = normalize(rawText);
    String cleaned = stripPoliteWrappers(norm);
    res.normalizedText = cleaned;

    if (cleaned.isEmpty()) {
        return res;
    }

    // Parcourir toutes les correspondances enregistrées
    for (const auto& item : _patterns) {
        // Règle de sécurité stricte : si un ToolRegistry est associé, la commande DOIT exister
        if (_registry != nullptr && !_registry->hasTool(item.toolId)) {
            continue;
        }

        for (const auto& pat : item.patterns) {
            bool matches = false;

            // 1. Correspondance exacte
            if (cleaned == pat || norm == pat) {
                matches = true;
            }
            // 2. Correspondance avec préfixe (ex: "luminosite 80" correspond au motif "luminosite")
            else if (cleaned.startsWith(pat)) {
                size_t patLen = pat.length();
                if (cleaned.length() > patLen && cleaned[patLen] == ' ') {
                    matches = true;
                }
            }
            // 3. Correspondance de sous-chaîne significative
            else if (cleaned.indexOf(pat) >= 0) {
                matches = true;
            }

            if (matches) {
                res.matched = true;
                res.toolId = item.toolId;
                res.args = item.defaultArgs;
                res.matchedPattern = pat;

                // Extraction de paramètre numérique si le ToolRegistry indique un paramètre attendu
                if (_registry != nullptr) {
                    const ToolDefinition* def = _registry->findTool(item.toolId);
                    if (def != nullptr && !def->parameters.empty()) {
                        int valInt = 0;
                        double valDouble = 0.0;
                        if (extractNumber(cleaned, valInt, valDouble)) {
                            const auto& firstParam = def->parameters[0];
                            if (!res.args.has(firstParam.name)) {
                                if (firstParam.type == ToolParamType::INT) {
                                    res.args.set(firstParam.name, valInt);
                                } else if (firstParam.type == ToolParamType::FLOAT) {
                                    res.args.set(firstParam.name, valDouble);
                                } else {
                                    res.args.set(firstParam.name, String(valInt));
                                }
                            }
                        }
                    }
                }
                return res;
            }
        }
    }

    return res;
}
