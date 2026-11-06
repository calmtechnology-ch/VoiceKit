#include "TextGate.h"
#include <cctype>

TextGate::TextGate() {
}

std::vector<String> TextGate::splitWords(const String& str) {
    std::vector<String> words;
    String current;
    for (size_t i = 0; i < str.length(); ++i) {
        char c = str[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!current.isEmpty()) {
                words.push_back(current);
                current = "";
            }
        } else {
            current += c;
        }
    }
    if (!current.isEmpty()) {
        words.push_back(current);
    }
    return words;
}

String TextGate::normalizeForAnalysis(const String& input) {
    if (input.isEmpty()) return "";

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(input.c_str());
    const size_t len = input.length();

    String result;
    result.reserve(len);

    size_t i = 0;
    while (i < len) {
        uint8_t b0 = bytes[i];

        // 1. ASCII Characters (0x00 .. 0x7F)
        if (b0 <= 0x7F) {
            char c = static_cast<char>(b0);
            if (c >= 'A' && c <= 'Z') {
                result += static_cast<char>(c + 32); // Minuscule
            } else if (c >= 'a' && c <= 'z' || (c >= '0' && c <= '9')) {
                result += c;
            } else {
                // Tout caractère ASCII non alphanumérique (ponctuation, espace, retour chariot) devient espace
                result += ' ';
            }
            i++;
            continue;
        }

        // 2. Traitement UTF-8 multioctet pour les accents français
        if (b0 == 0xC3 && i + 1 < len) {
            uint8_t b1 = bytes[i + 1];
            switch (b1) {
                // a : à (0xA0), â (0xA2), ä (0xA4), À (0x80), Â (0x82), Ä (0x84)
                case 0xA0: case 0xA2: case 0xA4:
                case 0x80: case 0x82: case 0x84:
                    result += 'a';
                    break;

                // e : é (0xA9), è (0xA8), ê (0xAA), ë (0xAB), É (0x89), È (0x88), Ê (0x8A), Ë (0x8B)
                case 0xA9: case 0xA8: case 0xAA: case 0xAB:
                case 0x89: case 0x88: case 0x8A: case 0x8B:
                    result += 'e';
                    break;

                // i : î (0xAE), ï (0xAF), Î (0x8E), Ï (0x8F)
                case 0xAE: case 0xAF:
                case 0x8E: case 0x8F:
                    result += 'i';
                    break;

                // o : ô (0xB4), ö (0xB6), Ô (0x94), Ö (0x96)
                case 0xB4: case 0xB6:
                case 0x94: case 0x96:
                    result += 'o';
                    break;

                // u : ù (0xB9), û (0xBB), ü (0xBC), Ù (0x99), Û (0x9B), Ü (0x9C)
                case 0xB9: case 0xBB: case 0xBC:
                case 0x99: case 0x9B: case 0x9C:
                    result += 'u';
                    break;

                // c : ç (0xA7), Ç (0x87)
                case 0xA7:
                case 0x87:
                    result += 'c';
                    break;

                // æ : æ (0xA6), Æ (0x86) -> ae
                case 0xA6:
                case 0x86:
                    result += "ae";
                    break;

                default:
                    // Autre caractère 2-octets
                    result += ' ';
                    break;
            }
            i += 2;
            continue;
        }

        // Ligature œ (0xC5 0x93 / 0x92)
        if (b0 == 0xC5 && i + 1 < len) {
            uint8_t b1 = bytes[i + 1];
            if (b1 == 0x93 || b1 == 0x92) {
                result += "oe";
            } else {
                result += ' ';
            }
            i += 2;
            continue;
        }

        // Ponctuation typographique 3-octets (ex: apostrophe courbe ’ 0xE2 0x80 0x99, ellipse … 0xE2 0x80 0xA6)
        if (b0 == 0xE2 && i + 2 < len) {
            result += ' ';
            i += 3;
            continue;
        }

        // Caractère UTF-8 non mappé : remplacer par un espace
        result += ' ';
        // Avancer au prochain début de caractère UTF-8
        i++;
        while (i < len && (bytes[i] & 0xC0) == 0x80) {
            i++;
        }
    }

    // Compacter les espaces successifs et rogner les extrémités
    String compacted;
    compacted.reserve(result.length());
    bool lastWasSpace = true;
    for (size_t k = 0; k < result.length(); ++k) {
        char c = result[k];
        if (c == ' ') {
            if (!lastWasSpace) {
                compacted += ' ';
                lastWasSpace = true;
            }
        } else {
            compacted += c;
            lastWasSpace = false;
        }
    }

    // Supprimer l'espace final éventuel
    if (!compacted.isEmpty() && compacted[compacted.length() - 1] == ' ') {
        compacted = compacted.substring(0, compacted.length() - 1);
    }

    return compacted;
}

bool TextGate::isHesitationWord(const String& word) {
    if (word.isEmpty()) return false;
    return (word == "euh" || word == "heu" || word == "hum" ||
            word == "hmm" || word == "hm"  || word == "uh"  ||
            word == "um"  || word == "er");
}

bool TextGate::isOnlyHesitations(const String& normalized) {
    if (normalized.isEmpty()) return false;
    std::vector<String> words = splitWords(normalized);
    if (words.empty()) return false;

    for (const auto& w : words) {
        if (!isHesitationWord(w)) {
            return false;
        }
    }
    return true;
}

bool TextGate::isPotentiallyIncomplete(const String& rawText, const String& normalized) {
    // 1. Points de suspension explicites à la fin du texte brut
    String trimmedRaw = rawText;
    trimmedRaw.trim();
    if (trimmedRaw.endsWith("...") || trimmedRaw.endsWith("..")) {
        return true;
    }
    // Caractère Unicode ellipse '…' (0xE2 0x80 0xA6)
    if (trimmedRaw.endsWith("\xE2\x80\xA6")) {
        return true;
    }

    // 2. Conjonction ou préposition orpheline en fin d'énoncé
    std::vector<String> words = splitWords(normalized);
    if (words.empty()) return false;

    const String& lastWord = words.back();
    if (lastWord == "que"  || lastWord == "qui"  || lastWord == "et"   ||
        lastWord == "ou"   || lastWord == "mais" || lastWord == "car"  ||
        lastWord == "pour" || lastWord == "dans" || lastWord == "avec" ||
        lastWord == "de"   || lastWord == "du"   || lastWord == "des"  ||
        lastWord == "le"   || lastWord == "la"   || lastWord == "les"  ||
        lastWord == "un"   || lastWord == "une"  || lastWord == "si"   ||
        lastWord == "quand"|| lastWord == "a"    || lastWord == "au"   ||
        lastWord == "aux") {
        return true;
    }

    return false;
}

TextGateDecision TextGate::evaluate(const String& rawText,
                                    String* outNormalized,
                                    String* outReason) const {
    String normalized = normalizeForAnalysis(rawText);
    if (outNormalized) {
        *outNormalized = normalized;
    }

    // 1. Chaîne vide ou uniquement composée d'espaces/ponctuation
    if (normalized.isEmpty()) {
        if (outReason) *outReason = "Transcription vide ou ponctuation seule";
        return TextGateDecision::IGNORE;
    }

    // 2. Hésitation isolée ou suite d'hésitations pures
    if (isOnlyHesitations(normalized)) {
        if (outReason) *outReason = "Hesitation isolee";
        return TextGateDecision::IGNORE;
    }

    // 3. Détection de phrase potentiellement incomplète (purement informative)
    if (isPotentiallyIncomplete(rawText, normalized)) {
        if (outReason) *outReason = "Phrase potentiellement incomplete";
        return TextGateDecision::INCOMPLETE;
    }

    // 4. Transcription acceptée (y compris si une hésitation fait partie d'une phrase complète)
    if (outReason) *outReason = "Texte valide";
    return TextGateDecision::ACCEPT;
}

