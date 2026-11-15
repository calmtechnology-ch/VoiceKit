#include "FrenchTimeFormatter.h"

static const char* UNITS_FRENCH[] = {
    "zero", "un", "deux", "trois", "quatre", "cinq", "six", "sept", "huit", "neuf",
    "dix", "onze", "douze", "treize", "quatorze", "quinze", "seize", "dix-sept", "dix-huit", "dix-neuf"
};

static const char* TENS_FRENCH[] = {
    "", "", "vingt", "trente", "quarante", "cinquante"
};

static String formatNumberFrench(int value, bool feminine_one = false) {
    if (value < 0 || value > 59) return "";
    if (value < 20) {
        if (value == 1 && feminine_one) return "une";
        return UNITS_FRENCH[value];
    }
    int tens = value / 10;
    int units = value % 10;
    String result = TENS_FRENCH[tens];
    if (units == 1) result += " et un";
    else if (units > 1) {
        result += "-";
        result += UNITS_FRENCH[units];
    }
    return result;
}

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

static String formatHourFrench(int hours) {
    hours = (hours + 24) % 24;
    if (hours == 0) return "minuit";
    if (hours == 12) return "midi";
    if (hours == 1) return "une heure";
    if (hours == 21) return "vingt et une heures";
    return formatNumberFrench(hours) + " heures";
}

String formatTimeInFrench(int hours, int minutes, TimeSpeechStyle style) {
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
        return "Il est une heure.";
    }

    String sentence = "Il est ";

    if (style == TimeSpeechStyle::NATURAL && minutes > 30) {
        sentence += formatHourFrench((hours + 1) % 24);
        sentence += " moins ";
        if (minutes == 45) sentence += "le quart";
        else sentence += formatMinutesFrench(60 - minutes);
    } else {
        sentence += formatHourFrench(hours);
        if (minutes > 0) {
            if (style == TimeSpeechStyle::NATURAL && minutes == 15) {
                sentence += " et quart";
            } else if (style == TimeSpeechStyle::NATURAL && minutes == 30) {
                sentence += (hours == 0 || hours == 12) ? " et demi" : " et demie";
            } else {
                sentence += " ";
                sentence += formatMinutesFrench(minutes);
            }
        }
    }

    sentence += ".";
    return sentence;
}

String formatDateInFrench(const tm& local_time) {
    static const char* DAYS[] = {
        "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"
    };
    static const char* MONTHS[] = {
        "janvier", "fevrier", "mars", "avril", "mai", "juin",
        "juillet", "aout", "septembre", "octobre", "novembre", "decembre"
    };
    if (local_time.tm_wday < 0 || local_time.tm_wday > 6 ||
        local_time.tm_mon < 0 || local_time.tm_mon > 11) return "";
    String result = DAYS[local_time.tm_wday];
    result += " ";
    result += String(local_time.tm_mday);
    result += " ";
    result += MONTHS[local_time.tm_mon];
    return result;
}
