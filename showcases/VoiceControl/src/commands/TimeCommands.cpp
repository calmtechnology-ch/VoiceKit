#include "TimeCommands.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// =============================================================================
// SwissTime — Gestion déterministe du fuseau horaire suisse (Europe/Zurich)
// =============================================================================

namespace SwissTime {

static bool epochToUtcTm(time_t epoch, struct tm &outUtc) {
#if defined(_WIN32) && !defined(ESP32) && !defined(ARDUINO)
  return gmtime_s(&outUtc, &epoch) == 0;
#else
  return gmtime_r(&epoch, &outUtc) != nullptr;
#endif
}

static bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int daysInMonth(int year, int month1Based) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month1Based < 1 || month1Based > 12)
    return 30;
  if (month1Based == 2 && isLeapYear(year))
    return 29;
  return kDays[month1Based - 1];
}

// Formule de Sakamoto : 0 = Dimanche .. 6 = Samedi
static int dayOfWeek(int year, int month1Based, int day) {
  static const int kOffsets[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month1Based < 3) {
    year -= 1;
  }
  return (year + year / 4 - year / 100 + year / 400 +
          kOffsets[month1Based - 1] + day) %
         7;
}

static int lastSundayOfMonth(int year, int month1Based) {
  int lastDay = daysInMonth(year, month1Based);
  int wday = dayOfWeek(year, month1Based, lastDay);
  return lastDay - wday;
}

void applyTimezone() {
#if defined(ESP32) || defined(ARDUINO)
  const char *currentTz = getenv("TZ");
  if (!currentTz || strcmp(currentTz, VC_TIMEZONE_POSIX) != 0) {
    setenv("TZ", VC_TIMEZONE_POSIX, 1);
    tzset();
  }
#endif
}

bool isSwissDstUtc(time_t utcEpoch) {
  struct tm utcTm{};
  if (!epochToUtcTm(utcEpoch, utcTm)) {
    return false;
  }

  const int year = utcTm.tm_year + 1900;
  const int month = utcTm.tm_mon + 1; // 1..12
  const int day = utcTm.tm_mday;      // 1..31
  const int hour = utcTm.tm_hour;     // 0..23

  if (month < 3 || month > 10) {
    return false; // Jan, Fev, Nov, Dec : CET (UTC+1)
  }
  if (month > 3 && month < 10) {
    return true; // Avr..Sep : CEST (UTC+2)
  }

  if (month == 3) {
    int switchDay = lastSundayOfMonth(year, 3);
    if (day > switchDay)
      return true;
    if (day < switchDay)
      return false;
    return hour >= 1; // Passage CEST à 01:00 UTC (02:00 CET -> 03:00 CEST)
  }

  // month == 10
  int switchDay = lastSundayOfMonth(year, 10);
  if (day < switchDay)
    return true;
  if (day > switchDay)
    return false;
  return hour < 1; // Retour CET à 01:00 UTC (03:00 CEST -> 02:00 CET)
}

bool utcEpochToSwissLocal(time_t utcEpoch, struct tm &outLocal,
                          bool *outIsDst) {
#if defined(ESP32) || defined(ARDUINO)
  applyTimezone();
  if (localtime_r(&utcEpoch, &outLocal) == nullptr) {
    return false;
  }
  if (outIsDst) {
    *outIsDst = (outLocal.tm_isdst > 0);
  }
  return true;
#else
  const bool dst = isSwissDstUtc(utcEpoch);
  const time_t localEpoch = utcEpoch + (dst ? 7200 : 3600);
  if (!epochToUtcTm(localEpoch, outLocal)) {
    return false;
  }
  outLocal.tm_isdst = dst ? 1 : 0;
  if (outIsDst) {
    *outIsDst = dst;
  }
  return true;
#endif
}

void formatTimeHHMMSS(const struct tm &localTm, char *dst, size_t dstSize) {
  if (!dst || dstSize == 0)
    return;
  std::snprintf(dst, dstSize, "%02d:%02d:%02d", localTm.tm_hour, localTm.tm_min,
                localTm.tm_sec);
}

void formatDateDDMMYYYY(const struct tm &localTm, char *dst, size_t dstSize) {
  if (!dst || dstSize == 0)
    return;
  std::snprintf(dst, dstSize, "%02d.%02d.%04d", localTm.tm_mday,
                localTm.tm_mon + 1, localTm.tm_year + 1900);
}

void formatDateTimeLog(const struct tm &localTm, char *dst, size_t dstSize) {
  if (!dst || dstSize == 0)
    return;
  std::snprintf(dst, dstSize, "%02d.%02d.%04d %02d:%02d:%02d", localTm.tm_mday,
                localTm.tm_mon + 1, localTm.tm_year + 1900, localTm.tm_hour,
                localTm.tm_min, localTm.tm_sec);
}

} // namespace SwissTime

// =============================================================================
// TimeCommandLogic — Logique pure C++17 (ESP32 + tests unitaires natifs)
// =============================================================================

const char *TimeCommandLogic::commandToLogName(TimeCommandType cmd) {
  switch (cmd) {
  case TimeCommandType::GET_TIME:
    return "GET_TIME";
  case TimeCommandType::GET_DATE:
    return "GET_DATE";
  case TimeCommandType::GET_DAY:
    return "GET_DAY";
  case TimeCommandType::GET_YEAR:
    return "GET_YEAR";
  case TimeCommandType::GET_TIME_PLUS_1H:
    return "GET_TIME_PLUS_1H";
  case TimeCommandType::GET_TIME_MINUS_1H:
    return "GET_TIME_MINUS_1H";
  case TimeCommandType::UNKNOWN:
  default:
    return "REJECTED";
  }
}

const char *TimeCommandLogic::commandToToolId(TimeCommandType cmd) {
  switch (cmd) {
  case TimeCommandType::GET_TIME:
    return "time.get_time";
  case TimeCommandType::GET_DATE:
    return "time.get_date";
  case TimeCommandType::GET_DAY:
    return "time.get_day";
  case TimeCommandType::GET_YEAR:
    return "time.get_year";
  case TimeCommandType::GET_TIME_PLUS_1H:
    return "time.get_time_plus_1h";
  case TimeCommandType::GET_TIME_MINUS_1H:
    return "time.get_time_minus_1h";
  case TimeCommandType::UNKNOWN:
  default:
    return "";
  }
}

std::string TimeCommandLogic::normalizeText(const std::string &input) {
  if (input.empty())
    return "";

  std::string raw;
  raw.reserve(input.size());

  const unsigned char *bytes =
      reinterpret_cast<const unsigned char *>(input.data());
  const size_t len = input.size();

  size_t i = 0;
  while (i < len) {
    unsigned char b0 = bytes[i];
    if (b0 <= 0x7F) {
      char c = static_cast<char>(b0);
      if (c >= 'A' && c <= 'Z') {
        raw.push_back(static_cast<char>(c + 32));
      } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        raw.push_back(c);
      } else {
        raw.push_back(' ');
      }
      ++i;
      continue;
    }

    // Accents UTF-8 (bloc Latin-1 0xC3)
    if (b0 == 0xC3 && i + 1 < len) {
      unsigned char b1 = bytes[i + 1];
      switch (b1) {
      case 0xA0:
      case 0xA2:
      case 0xA4:
      case 0x80:
      case 0x82:
      case 0x84:
        raw.push_back('a');
        break;
      case 0xA9:
      case 0xA8:
      case 0xAA:
      case 0xAB:
      case 0x89:
      case 0x88:
      case 0x8A:
      case 0x8B:
        raw.push_back('e');
        break;
      case 0xAE:
      case 0xAF:
      case 0x8E:
      case 0x8F:
        raw.push_back('i');
        break;
      case 0xB4:
      case 0xB6:
      case 0x94:
      case 0x96:
        raw.push_back('o');
        break;
      case 0xB9:
      case 0xBB:
      case 0xBC:
      case 0x99:
      case 0x9B:
      case 0x9C:
        raw.push_back('u');
        break;
      case 0xA7:
      case 0x87:
        raw.push_back('c');
        break;
      default:
        raw.push_back(' ');
        break;
      }
      i += 2;
      continue;
    }

    // Ligature œ / Œ
    if (b0 == 0xC5 && i + 1 < len &&
        (bytes[i + 1] == 0x93 || bytes[i + 1] == 0x92)) {
      raw.append("oe");
      i += 2;
      continue;
    }

    raw.push_back(' ');
    ++i;
  }

  // Réduction des espaces multiples
  std::string clean;
  clean.reserve(raw.size());
  bool lastSpace = true;
  for (char ch : raw) {
    if (ch == ' ') {
      if (!lastSpace) {
        clean.push_back(' ');
        lastSpace = true;
      }
    } else {
      clean.push_back(ch);
      lastSpace = false;
    }
  }
  if (!clean.empty() && clean.back() == ' ') {
    clean.pop_back();
  }
  return clean;
}

static bool hasPhrase(const std::string &padded, const char *phrase) {
  std::string needle = std::string(" ") + phrase + " ";
  return padded.find(needle) != std::string::npos;
}

static TimeCommandType classifyFrench(const std::string &padded) {
  // 1. Calcul futur : +1 heure (prioritaire sur GET_TIME)
  if (hasPhrase(padded, "dans une heure") ||
      hasPhrase(padded, "dans 1 heure") ||
      hasPhrase(padded, "une heure plus tard") ||
      hasPhrase(padded, "dans une heure il sera") ||
      hasPhrase(padded, "heure sera t il dans une heure") ||
      hasPhrase(padded, "heure il sera dans une heure") ||
      hasPhrase(padded, "ajoute une heure")) {
    if (hasPhrase(padded, "heure") || hasPhrase(padded, "sera")) {
      return TimeCommandType::GET_TIME_PLUS_1H;
    }
  }

  // 2. Calcul passé : -1 heure (prioritaire sur GET_TIME)
  if (hasPhrase(padded, "il y a une heure") ||
      hasPhrase(padded, "il y a 1 heure") ||
      hasPhrase(padded, "une heure plus tot") ||
      hasPhrase(padded, "une heure auparavant") ||
      hasPhrase(padded, "une heure avant") ||
      hasPhrase(padded, "heure etait il il y a une heure") ||
      hasPhrase(padded, "heure il etait il y a une heure")) {
    if (hasPhrase(padded, "heure") || hasPhrase(padded, "etait")) {
      return TimeCommandType::GET_TIME_MINUS_1H;
    }
  }

  // 3. Année
  if (hasPhrase(padded, "quelle annee") ||
      hasPhrase(padded, "en quelle annee") ||
      hasPhrase(padded, "annee sommes nous") ||
      hasPhrase(padded, "l annee actuelle") ||
      hasPhrase(padded, "annee actuelle") || hasPhrase(padded, "quel an")) {
    return TimeCommandType::GET_YEAR;
  }

  // 4. Jour de la semaine + date
  if (hasPhrase(padded, "quel jour") || hasPhrase(padded, "jour sommes nous") ||
      hasPhrase(padded, "jour de la semaine") ||
      hasPhrase(padded, "on est quel jour") ||
      hasPhrase(padded, "nous sommes quel jour")) {
    return TimeCommandType::GET_DAY;
  }

  // 5. Date du jour
  if (hasPhrase(padded, "quelle est la date") ||
      hasPhrase(padded, "quelle date") || hasPhrase(padded, "la date") ||
      hasPhrase(padded, "donne moi la date") ||
      hasPhrase(padded, "dis moi la date") ||
      hasPhrase(padded, "date du jour") ||
      hasPhrase(padded, "date d aujourd hui") ||
      hasPhrase(padded, "on est le combien") ||
      hasPhrase(padded, "nous sommes le combien")) {
    return TimeCommandType::GET_DATE;
  }

  // 6. Heure actuelle en Suisse
  if (hasPhrase(padded, "quelle heure") ||
      hasPhrase(padded, "donne moi l heure") ||
      hasPhrase(padded, "dis moi l heure") ||
      hasPhrase(padded, "l heure en suisse") ||
      hasPhrase(padded, "heure en suisse") ||
      hasPhrase(padded, "heure actuelle") ||
      hasPhrase(padded, "il est quelle heure") ||
      hasPhrase(padded, "l heure s il te plait") ||
      hasPhrase(padded, "l heure s il vous plait") ||
      hasPhrase(padded, "peux tu me donner l heure") || padded == " l heure " ||
      padded == " heure ") {
    return TimeCommandType::GET_TIME;
  }

  return TimeCommandType::UNKNOWN;
}

static TimeCommandType classifyEnglish(const std::string &padded) {
  // 1. +1 hour
  if (hasPhrase(padded, "in one hour") || hasPhrase(padded, "in an hour") ||
      hasPhrase(padded, "in 1 hour") ||
      hasPhrase(padded, "one hour from now") ||
      hasPhrase(padded, "an hour from now") ||
      hasPhrase(padded, "one hour later")) {
    if (hasPhrase(padded, "time") || hasPhrase(padded, "hour") ||
        hasPhrase(padded, "will it be")) {
      return TimeCommandType::GET_TIME_PLUS_1H;
    }
  }

  // 2. -1 hour
  if (hasPhrase(padded, "one hour ago") || hasPhrase(padded, "an hour ago") ||
      hasPhrase(padded, "1 hour ago") ||
      hasPhrase(padded, "one hour earlier") ||
      hasPhrase(padded, "an hour earlier") ||
      hasPhrase(padded, "one hour before")) {
    if (hasPhrase(padded, "time") || hasPhrase(padded, "hour") ||
        hasPhrase(padded, "was it")) {
      return TimeCommandType::GET_TIME_MINUS_1H;
    }
  }

  // 3. Year
  if (hasPhrase(padded, "what year") || hasPhrase(padded, "which year") ||
      hasPhrase(padded, "current year") || hasPhrase(padded, "the year")) {
    return TimeCommandType::GET_YEAR;
  }

  // 4. Day of week + date
  if (hasPhrase(padded, "what day") || hasPhrase(padded, "which day") ||
      hasPhrase(padded, "day of the week") ||
      hasPhrase(padded, "day is today")) {
    return TimeCommandType::GET_DAY;
  }

  // 5. Date
  if (hasPhrase(padded, "what is the date") ||
      hasPhrase(padded, "what s the date") ||
      hasPhrase(padded, "whats the date") ||
      hasPhrase(padded, "today s date") || hasPhrase(padded, "todays date") ||
      hasPhrase(padded, "tell me the date") ||
      hasPhrase(padded, "give me the date") ||
      hasPhrase(padded, "current date") || hasPhrase(padded, "the date")) {
    return TimeCommandType::GET_DATE;
  }

  // 6. Current time
  if (hasPhrase(padded, "what time") || hasPhrase(padded, "what s the time") ||
      hasPhrase(padded, "whats the time") ||
      hasPhrase(padded, "tell me the time") ||
      hasPhrase(padded, "give me the time") ||
      hasPhrase(padded, "current time") ||
      hasPhrase(padded, "time in switzerland") ||
      hasPhrase(padded, "swiss time") || padded == " time " ||
      padded == " the time ") {
    return TimeCommandType::GET_TIME;
  }

  return TimeCommandType::UNKNOWN;
}

TimeCommandType
TimeCommandLogic::classifyCommand(const std::string &rawText,
                                  VoiceControlConfig::LanguageCode lang) {
  std::string norm = normalizeText(rawText);
  if (norm.empty()) {
    return TimeCommandType::UNKNOWN;
  }
  std::string padded = " " + norm + " ";

  // Priorité à la langue configurée, avec repli sur l'autre langue si
  // nécessaire
  if (lang == VoiceControlConfig::LanguageCode::EN) {
    TimeCommandType enCmd = classifyEnglish(padded);
    if (enCmd != TimeCommandType::UNKNOWN)
      return enCmd;
    return classifyFrench(padded);
  }

  TimeCommandType frCmd = classifyFrench(padded);
  if (frCmd != TimeCommandType::UNKNOWN)
    return frCmd;
  return classifyEnglish(padded);
}

// =============================================================================
// Formatage parlé naturel FR et EN (24h)
// =============================================================================

static std::string numberToFrench(int value, bool feminineOne) {
  static const char *const kUnits[20] = {
      "zéro",   "un",    "deux",     "trois",    "quatre",
      "cinq",   "six",   "sept",     "huit",     "neuf",
      "dix",    "onze",  "douze",    "treize",   "quatorze",
      "quinze", "seize", "dix-sept", "dix-huit", "dix-neuf"};
  static const char *const kTens[6] = {"",       "",         "vingt",
                                       "trente", "quarante", "cinquante"};

  if (value < 0 || value > 59)
    return "";
  if (value < 20) {
    if (value == 1 && feminineOne)
      return "une";
    return kUnits[value];
  }
  int tens = value / 10;
  int units = value % 10;
  std::string res = kTens[tens];
  if (units == 1) {
    res += feminineOne ? " et une" : " et un";
  } else if (units > 1) {
    res += "-";
    res += kUnits[units];
  }
  return res;
}

std::string TimeCommandLogic::formatSpokenTimeFr(int hours, int minutes) {
  hours = ((hours % 24) + 24) % 24;
  if (minutes < 0)
    minutes = 0;
  if (minutes > 59)
    minutes = 59;

  std::string out;
  if (hours == 0) {
    out = "minuit";
  } else if (hours == 12) {
    out = "midi";
  } else if (hours == 1) {
    out = "une heure";
  } else {
    out = numberToFrench(hours, true) + " heures";
  }

  if (minutes > 0) {
    out += " et ";
    out += numberToFrench(minutes, true);
    out += (minutes == 1) ? " minute" : " minutes";
  }
  return out;
}

static std::string numberToEnglish(int value) {
  static const char *const kUnits[20] = {
      "zero",    "one",     "two",       "three",    "four",
      "five",    "six",     "seven",     "eight",    "nine",
      "ten",     "eleven",  "twelve",    "thirteen", "fourteen",
      "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
  static const char *const kTens[6] = {"",       "",      "twenty",
                                       "thirty", "forty", "fifty"};

  if (value < 0 || value > 59)
    return "";
  if (value < 20)
    return kUnits[value];
  int tens = value / 10;
  int units = value % 10;
  if (units == 0)
    return kTens[tens];
  return std::string(kTens[tens]) + "-" + kUnits[units];
}

std::string TimeCommandLogic::formatSpokenTimeEn(int hours, int minutes) {
  hours = ((hours % 24) + 24) % 24;
  if (minutes < 0)
    minutes = 0;
  if (minutes > 59)
    minutes = 59;

  if (hours == 0 && minutes == 0) {
    return "midnight";
  }
  if (minutes == 0) {
    return numberToEnglish(hours) + " o'clock";
  }
  std::string hStr = (hours == 0) ? "zero" : numberToEnglish(hours);
  if (minutes < 10) {
    return hStr + " oh " + numberToEnglish(minutes);
  }
  return hStr + " " + numberToEnglish(minutes);
}

static std::string formatSpokenDateFr(const struct tm &localTm,
                                      bool includeWeekday) {
  static const char *const kDays[7] = {
      "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"};
  static const char *const kMonths[12] = {
      "janvier", "février", "mars",      "avril",   "mai",      "juin",
      "juillet", "août",    "septembre", "octobre", "novembre", "décembre"};

  char dayBuf[16];
  if (localTm.tm_mday == 1) {
    std::snprintf(dayBuf, sizeof(dayBuf), "1er");
  } else {
    std::snprintf(dayBuf, sizeof(dayBuf), "%d", localTm.tm_mday);
  }

  char yearBuf[16];
  std::snprintf(yearBuf, sizeof(yearBuf), "%d", localTm.tm_year + 1900);

  const char *monthStr = (localTm.tm_mon >= 0 && localTm.tm_mon < 12)
                             ? kMonths[localTm.tm_mon]
                             : "";
  if (includeWeekday && localTm.tm_wday >= 0 && localTm.tm_wday < 7) {
    return std::string("Nous sommes ") + kDays[localTm.tm_wday] + " " + dayBuf +
           " " + monthStr + " " + yearBuf + ".";
  }
  return std::string("Nous sommes le ") + dayBuf + " " + monthStr + " " +
         yearBuf + ".";
}

static std::string formatSpokenDateEn(const struct tm &localTm,
                                      bool includeWeekday) {
  static const char *const kDays[7] = {"Sunday",    "Monday",   "Tuesday",
                                       "Wednesday", "Thursday", "Friday",
                                       "Saturday"};
  static const char *const kMonths[12] = {
      "January", "February", "March",     "April",   "May",      "June",
      "July",    "August",   "September", "October", "November", "December"};

  char dayYearBuf[32];
  std::snprintf(dayYearBuf, sizeof(dayYearBuf), "%d, %d", localTm.tm_mday,
                localTm.tm_year + 1900);
  const char *monthStr = (localTm.tm_mon >= 0 && localTm.tm_mon < 12)
                             ? kMonths[localTm.tm_mon]
                             : "";

  if (includeWeekday && localTm.tm_wday >= 0 && localTm.tm_wday < 7) {
    return std::string("Today is ") + kDays[localTm.tm_wday] + ", " + monthStr +
           " " + dayYearBuf + ".";
  }
  return std::string("Today is ") + monthStr + " " + dayYearBuf + ".";
}

TimeCommandEvalResult
TimeCommandLogic::evaluateCommand(TimeCommandType cmd, time_t utcEpoch,
                                  bool clockSynced,
                                  VoiceControlConfig::LanguageCode lang) {
  TimeCommandEvalResult res;
  res.command = cmd;
  res.recognized = (cmd != TimeCommandType::UNKNOWN);
  res.clockSynced = clockSynced && SwissTime::isValidEpoch(utcEpoch);

  const bool isEn = (lang == VoiceControlConfig::LanguageCode::EN);

  // 1. Rejet poli de toute commande hors périmètre
  if (!res.recognized) {
    res.spokenResponse =
        isEn ? "I can only answer questions about the time and date."
             : "Je peux uniquement répondre aux questions concernant l'heure "
               "et la date.";
    return res;
  }

  // 2. Ne jamais inventer l'heure si l'horloge NTP n'est pas synchronisée
  if (!res.clockSynced) {
    res.spokenResponse =
        isEn ? "The clock is not synchronized yet. Please wait."
             : "L'horloge n'est pas encore synchronisée. Veuillez patienter.";
    return res;
  }

  // 3. Calculs horaires exclusivement sur les timestamps UTC avant conversion
  // suisse
  time_t targetEpoch = utcEpoch;
  if (cmd == TimeCommandType::GET_TIME_PLUS_1H) {
    targetEpoch += 3600;
  } else if (cmd == TimeCommandType::GET_TIME_MINUS_1H) {
    targetEpoch -= 3600;
  }

  struct tm localTm{};
  if (!SwissTime::utcEpochToSwissLocal(targetEpoch, localTm)) {
    res.spokenResponse =
        isEn ? "The local clock is temporarily unavailable."
             : "L'horloge locale est temporairement indisponible.";
    return res;
  }

  switch (cmd) {
  case TimeCommandType::GET_TIME:
    res.spokenResponse =
        isEn ? ("It is " + formatSpokenTimeEn(localTm.tm_hour, localTm.tm_min) +
                ".")
             : ("Il est " +
                formatSpokenTimeFr(localTm.tm_hour, localTm.tm_min) + ".");
    break;

  case TimeCommandType::GET_DATE:
    res.spokenResponse = isEn ? formatSpokenDateEn(localTm, false)
                              : formatSpokenDateFr(localTm, false);
    break;

  case TimeCommandType::GET_DAY:
    res.spokenResponse = isEn ? formatSpokenDateEn(localTm, true)
                              : formatSpokenDateFr(localTm, true);
    break;

  case TimeCommandType::GET_YEAR: {
    char buf[64];
    if (isEn) {
      std::snprintf(buf, sizeof(buf), "The year is %d.",
                    localTm.tm_year + 1900);
    } else {
      std::snprintf(buf, sizeof(buf), "Nous sommes en %d.",
                    localTm.tm_year + 1900);
    }
    res.spokenResponse = buf;
    break;
  }

  case TimeCommandType::GET_TIME_PLUS_1H:
    res.spokenResponse =
        isEn ? ("In one hour, it will be " +
                formatSpokenTimeEn(localTm.tm_hour, localTm.tm_min) + ".")
             : ("Dans une heure, il sera " +
                formatSpokenTimeFr(localTm.tm_hour, localTm.tm_min) + ".");
    break;

  case TimeCommandType::GET_TIME_MINUS_1H:
    res.spokenResponse =
        isEn ? ("One hour ago, it was " +
                formatSpokenTimeEn(localTm.tm_hour, localTm.tm_min) + ".")
             : ("Il y a une heure, il était " +
                formatSpokenTimeFr(localTm.tm_hour, localTm.tm_min) + ".");
    break;

  default:
    break;
  }

  return res;
}

TimeCommandEvalResult
TimeCommandLogic::processTranscription(const std::string &rawText,
                                       time_t utcEpoch, bool clockSynced,
                                       VoiceControlConfig::LanguageCode lang) {
  TimeCommandType cmd = classifyCommand(rawText, lang);
  return evaluateCommand(cmd, utcEpoch, clockSynced, lang);
}

// =============================================================================
// TimeCommands — Intégration VoiceKit ToolRegistry (ESP32 / Arduino)
// =============================================================================

#ifdef ARDUINO

bool TimeCommands::begin(VoiceControlConfig::LanguageCode lang) {
  m_lang = lang;
  m_registry.clear();

  auto registerTimeTool = [this](TimeCommandType cmd,
                                 const char *desc) -> bool {
    const char *id = TimeCommandLogic::commandToToolId(cmd);
    return m_registry.registerTool(
        id, desc,
        [this, cmd]() -> ToolResult { return executeRegisteredTool(cmd); },
        ToolSecurityPolicy::READ_ONLY);
  };

  bool ok = true;
  ok &= registerTimeTool(TimeCommandType::GET_TIME,
                         "Retourne l'heure locale suisse");
  ok &= registerTimeTool(TimeCommandType::GET_DATE,
                         "Retourne la date locale suisse");
  ok &= registerTimeTool(TimeCommandType::GET_DAY,
                         "Retourne le jour de la semaine et la date");
  ok &=
      registerTimeTool(TimeCommandType::GET_YEAR, "Retourne l'annee courante");
  ok &= registerTimeTool(TimeCommandType::GET_TIME_PLUS_1H,
                         "Calcule l'heure suisse dans une heure");
  ok &= registerTimeTool(TimeCommandType::GET_TIME_MINUS_1H,
                         "Calcule l'heure suisse il y a une heure");
  return ok;
}

ToolResult TimeCommands::executeRegisteredTool(TimeCommandType cmd) const {
  TimeCommandEvalResult eval = TimeCommandLogic::evaluateCommand(
      cmd, m_evalUtcEpoch, m_evalClockSynced, m_lang);
  return ToolResult::success(String(eval.spokenResponse.c_str()));
}

TimeCommandDispatchResult TimeCommands::process(const String &rawText,
                                                time_t utcEpoch,
                                                bool clockSynced) {
  TimeCommandDispatchResult out;
  m_evalUtcEpoch = utcEpoch;
  m_evalClockSynced = clockSynced;

  TimeCommandType cmd =
      TimeCommandLogic::classifyCommand(std::string(rawText.c_str()), m_lang);
  out.command = cmd;
  out.recognized = (cmd != TimeCommandType::UNKNOWN);
  out.clockSynced = clockSynced && SwissTime::isValidEpoch(utcEpoch);

  if (out.recognized) {
    out.toolId = TimeCommandLogic::commandToToolId(cmd);
    ToolResult toolRes = m_registry.execute(out.toolId);
    if (toolRes.isSuccess()) {
      out.spokenResponse = toolRes.output;
      return out;
    }
  }

  // Rejet poli (UNKNOWN) ou repli déterministe
  TimeCommandEvalResult fallback =
      TimeCommandLogic::evaluateCommand(cmd, utcEpoch, clockSynced, m_lang);
  out.spokenResponse = String(fallback.spokenResponse.c_str());
  return out;
}

#endif // ARDUINO
