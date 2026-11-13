#include "FrenchTimeFormatter.h"

static const char* UNITS_FRENCH[] = {
    "zero", "un", "deux", "trois", "quatre", "cinq", "six", "sept", "huit", "neuf",
    "dix", "onze", "douze", "treize", "quatorze", "quinze", "seize", "dix-sept", "dix-huit", "dix-neuf"
};

static const char* TENS_FRENCH[] = {
    "", "", "vingt", "trente", "quarante", "cinquante"
};

static String formatMinutesFrench(int m) {
    if (m < 0 || m > 59) return "";
    if (m < 20) {
        if (m == 1) return "une";
        return UNITS_FRENCH[m];
    }
    int tens = m / 10;
    int units = m % 10;
    String res = TENS_FRENCH[tens];
    if (units == 1) {
        res += " et un";
    } else if (units > 1) {
        res += "-";
        res += UNITS_FRENCH[units];
    }
    return res;
}

String formatTimeInFrench(int hours, int minutes) {
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
        return "Il est une heure.";
    }

    String sentence = "Il est ";

    // Cas particuliers de midi et minuit
    if (hours == 0) {
        sentence += "minuit";
    } else if (hours == 12) {
        sentence += "midi";
    } else if (hours == 1) {
        sentence += "une heure";
    } else if (hours == 21) {
        sentence += "vingt et une heures";
    } else if (hours < 20) {
        sentence += UNITS_FRENCH[hours];
        sentence += " heures";
    } else {
        int tens = hours / 10;
        int units = hours % 10;
        sentence += TENS_FRENCH[tens];
        if (units > 0) {
            sentence += "-";
            sentence += UNITS_FRENCH[units];
        }
        sentence += " heures";
    }

    // Ajout des minutes
    if (minutes > 0) {
        sentence += " ";
        sentence += formatMinutesFrench(minutes);
    }

    sentence += ".";
    return sentence;
}
