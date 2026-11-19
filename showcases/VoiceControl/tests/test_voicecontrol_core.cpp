#include <iostream>
#include <cassert>
#include <cstring>
#include <ctime>
#include <string>

#include "config/VoiceControlConfig.h"
#include "commands/TimeCommands.h"
#include "ui/Localization.h"

static int g_passed = 0;
static int g_failed = 0;

#define VC_CHECK(cond, msg) do { \
    if (cond) { \
        g_passed++; \
    } else { \
        g_failed++; \
        std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    } \
} while (0)

// Helper portable : convertit une date/heure UTC en time_t (algorithme civil Howard Hinnant)
static time_t makeUtcEpoch(int year, int month, int day, int hour, int min, int sec) {
    int y = year - (month <= 2 ? 1 : 0);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(day) - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = static_cast<long long>(era) * 146097LL + static_cast<long long>(doe) - 719468LL;
    return static_cast<time_t>(days * 86400LL + hour * 3600LL + min * 60LL + sec);
}

static void testSwissTimeAndDstTransitions() {
    // 1. Date d'hiver de référence : jeudi 19.11.2026 09:15:00 UTC -> 10:15:00 CET (UTC+1)
    time_t winterUtc = makeUtcEpoch(2026, 11, 19, 9, 15, 0);
    struct tm localTm{};
    bool isDst = true;
    VC_CHECK(SwissTime::utcEpochToSwissLocal(winterUtc, localTm, &isDst), "Convert winter UTC");
    VC_CHECK(!isDst, "Nov 19 2026 is CET (winter, not DST)");
    VC_CHECK(localTm.tm_hour == 10 && localTm.tm_min == 15 && localTm.tm_sec == 0, "Winter local time is 10:15:00 (not double +1h)");
    VC_CHECK(localTm.tm_mday == 19 && localTm.tm_mon == 10 && localTm.tm_year == 126, "Winter local date is 19.11.2026");
    VC_CHECK(localTm.tm_wday == 4, "Nov 19 2026 is Thursday (4)");

    char bufDate[16], bufTime[16];
    SwissTime::formatDateDDMMYYYY(localTm, bufDate, sizeof(bufDate));
    SwissTime::formatTimeHHMMSS(localTm, bufTime, sizeof(bufTime));
    VC_CHECK(std::string(bufDate) == "19.11.2026", "Swiss date format JJ.MM.AAAA");
    VC_CHECK(std::string(bufTime) == "10:15:00", "Swiss 24h time format HH:MM:SS");

    // 2. VC-14 : Passage heure d'hiver -> heure d'été (dimanche 29 mars 2026)
    //    00:59:59 UTC = 01:59:59 CET
    //    01:00:00 UTC = 03:00:00 CEST
    time_t beforeSpring = makeUtcEpoch(2026, 3, 29, 0, 59, 59);
    time_t atSpring     = makeUtcEpoch(2026, 3, 29, 1, 0, 0);

    VC_CHECK(SwissTime::utcEpochToSwissLocal(beforeSpring, localTm, &isDst), "Convert before spring DST");
    VC_CHECK(!isDst && localTm.tm_hour == 1 && localTm.tm_min == 59, "29.03.2026 00:59:59 UTC -> 01:59:59 CET");

    VC_CHECK(SwissTime::utcEpochToSwissLocal(atSpring, localTm, &isDst), "Convert at spring DST");
    VC_CHECK(isDst && localTm.tm_hour == 3 && localTm.tm_min == 0, "29.03.2026 01:00:00 UTC -> 03:00:00 CEST");

    // +1h à travers le passage à l'heure d'été : 01:30 CET (00:30 UTC) + 1h -> 03:30 CEST (01:30 UTC)
    time_t springPre = makeUtcEpoch(2026, 3, 29, 0, 30, 0);
    auto springPlus1h = TimeCommandLogic::processTranscription(
        "Quelle heure sera-t-il dans une heure ?", springPre, true, VoiceControlConfig::LanguageCode::FR);
    VC_CHECK(springPlus1h.command == TimeCommandType::GET_TIME_PLUS_1H, "Spring +1h command recognized");
    VC_CHECK(springPlus1h.spokenResponse == "Dans une heure, il sera trois heures et trente minutes.",
             "Spring DST +1h: 01:30 CET + 1h -> 03:30 CEST");

    // -1h à travers le passage à l'heure d'été : 03:30 CEST (01:30 UTC) - 1h -> 01:30 CET (00:30 UTC)
    time_t springPost = makeUtcEpoch(2026, 3, 29, 1, 30, 0);
    auto springMinus1h = TimeCommandLogic::processTranscription(
        "Quelle heure était-il il y a une heure ?", springPost, true, VoiceControlConfig::LanguageCode::FR);
    VC_CHECK(springMinus1h.spokenResponse == "Il y a une heure, il était une heure et trente minutes.",
             "Spring DST -1h: 03:30 CEST - 1h -> 01:30 CET");

    // 3. VC-14 : Passage heure d'été -> heure d'hiver (dimanche 25 octobre 2026)
    //    00:59:59 UTC = 02:59:59 CEST
    //    01:00:00 UTC = 02:00:00 CET
    time_t beforeFall = makeUtcEpoch(2026, 10, 25, 0, 59, 59);
    time_t atFall     = makeUtcEpoch(2026, 10, 25, 1, 0, 0);

    VC_CHECK(SwissTime::utcEpochToSwissLocal(beforeFall, localTm, &isDst), "Convert before fall DST");
    VC_CHECK(isDst && localTm.tm_hour == 2 && localTm.tm_min == 59, "25.10.2026 00:59:59 UTC -> 02:59:59 CEST");

    VC_CHECK(SwissTime::utcEpochToSwissLocal(atFall, localTm, &isDst), "Convert at fall DST");
    VC_CHECK(!isDst && localTm.tm_hour == 2 && localTm.tm_min == 0, "25.10.2026 01:00:00 UTC -> 02:00:00 CET");

    // +1h à travers le retour à l'heure d'hiver : 02:30 CEST (00:30 UTC) + 1h -> 02:30 CET (01:30 UTC)
    time_t fallPre = makeUtcEpoch(2026, 10, 25, 0, 30, 0);
    auto fallPlus1h = TimeCommandLogic::processTranscription(
        "Quelle heure sera-t-il dans une heure ?", fallPre, true, VoiceControlConfig::LanguageCode::FR);
    VC_CHECK(fallPlus1h.spokenResponse == "Dans une heure, il sera deux heures et trente minutes.",
             "Fall DST +1h: 02:30 CEST + 1h -> 02:30 CET");

    // 4. Passage minuit / nouvel an : 31.12.2026 23:30 CET (22:30 UTC) + 1h -> 01.01.2027 00:30 CET
    time_t nyeUtc = makeUtcEpoch(2026, 12, 31, 22, 30, 0);
    auto nyePlus1h = TimeCommandLogic::processTranscription(
        "Quelle heure sera-t-il dans une heure ?", nyeUtc, true, VoiceControlConfig::LanguageCode::FR);
    VC_CHECK(nyePlus1h.spokenResponse == "Dans une heure, il sera minuit et trente minutes.",
             "Midnight/NYE rollover +1h -> minuit et trente minutes");
}

static void testFrenchCommands() {
    // Jeudi 19 novembre 2026 10:15:00 CET (09:15:00 UTC)
    time_t refUtc = makeUtcEpoch(2026, 11, 19, 9, 15, 0);
    constexpr auto FR = VoiceControlConfig::LanguageCode::FR;

    // VC-03 : heure actuelle (3 formulations obligatoires)
    auto r1 = TimeCommandLogic::processTranscription("Quelle heure est-il ?", refUtc, true, FR);
    VC_CHECK(r1.command == TimeCommandType::GET_TIME, "FR GET_TIME #1");
    VC_CHECK(r1.spokenResponse == "Il est dix heures et quinze minutes.", "FR GET_TIME exact response");

    auto r2 = TimeCommandLogic::processTranscription("Donne-moi l'heure", refUtc, true, FR);
    VC_CHECK(r2.command == TimeCommandType::GET_TIME, "FR GET_TIME #2");

    auto r3 = TimeCommandLogic::processTranscription("Quelle heure est-il en Suisse ?", refUtc, true, FR);
    VC_CHECK(r3.command == TimeCommandType::GET_TIME, "FR GET_TIME #3");

    // VC-04 : date
    auto r4 = TimeCommandLogic::processTranscription("Quelle est la date ?", refUtc, true, FR);
    VC_CHECK(r4.command == TimeCommandType::GET_DATE, "FR GET_DATE");
    VC_CHECK(r4.spokenResponse == "Nous sommes le 19 novembre 2026.", "FR GET_DATE exact response");

    // VC-05 : jour
    auto r5 = TimeCommandLogic::processTranscription("Quel jour sommes-nous ?", refUtc, true, FR);
    VC_CHECK(r5.command == TimeCommandType::GET_DAY, "FR GET_DAY");
    VC_CHECK(r5.spokenResponse == "Nous sommes jeudi 19 novembre 2026.", "FR GET_DAY exact response");

    // Année
    auto r6 = TimeCommandLogic::processTranscription("Nous sommes en quelle année ?", refUtc, true, FR);
    VC_CHECK(r6.command == TimeCommandType::GET_YEAR, "FR GET_YEAR");
    VC_CHECK(r6.spokenResponse == "Nous sommes en 2026.", "FR GET_YEAR exact response");

    // VC-06 : +1 heure
    auto r7 = TimeCommandLogic::processTranscription("Quelle heure sera-t-il dans une heure ?", refUtc, true, FR);
    VC_CHECK(r7.command == TimeCommandType::GET_TIME_PLUS_1H, "FR GET_TIME_PLUS_1H");
    VC_CHECK(r7.spokenResponse == "Dans une heure, il sera onze heures et quinze minutes.", "FR +1h exact response");

    // VC-07 : -1 heure
    auto r8 = TimeCommandLogic::processTranscription("Quelle heure était-il il y a une heure ?", refUtc, true, FR);
    VC_CHECK(r8.command == TimeCommandType::GET_TIME_MINUS_1H, "FR GET_TIME_MINUS_1H");
    VC_CHECK(r8.spokenResponse == "Il y a une heure, il était neuf heures et quinze minutes.", "FR -1h exact response");

    // VC-08 : rejet commandes inconnues / hors périmètre
    auto u1 = TimeCommandLogic::processTranscription("Allume la lumière du salon", refUtc, true, FR);
    VC_CHECK(u1.command == TimeCommandType::UNKNOWN && !u1.recognized, "FR reject light command");
    VC_CHECK(u1.spokenResponse == "Je peux uniquement répondre aux questions concernant l'heure et la date.", "FR polite rejection");

    auto u2 = TimeCommandLogic::processTranscription("Quel temps fait-il dehors ?", refUtc, true, FR);
    VC_CHECK(u2.command == TimeCommandType::UNKNOWN && !u2.recognized, "FR reject weather question");

    auto u3 = TimeCommandLogic::processTranscription("Raconte-moi une blague", refUtc, true, FR);
    VC_CHECK(u3.command == TimeCommandType::UNKNOWN && !u3.recognized, "FR reject joke request");
}

static void testEnglishCommandsAndLocalization() {
    time_t refUtc = makeUtcEpoch(2026, 11, 19, 9, 15, 0); // 10:15 CET
    constexpr auto EN = VoiceControlConfig::LanguageCode::EN;

    // VC-13 : commandes anglaises
    auto e1 = TimeCommandLogic::processTranscription("What time is it?", refUtc, true, EN);
    VC_CHECK(e1.command == TimeCommandType::GET_TIME, "EN GET_TIME");
    VC_CHECK(e1.spokenResponse == "It is ten fifteen.", "EN GET_TIME response");

    auto e2 = TimeCommandLogic::processTranscription("What time is it in Switzerland?", refUtc, true, EN);
    VC_CHECK(e2.command == TimeCommandType::GET_TIME, "EN GET_TIME Switzerland");

    auto e3 = TimeCommandLogic::processTranscription("What is the date?", refUtc, true, EN);
    VC_CHECK(e3.command == TimeCommandType::GET_DATE, "EN GET_DATE");
    VC_CHECK(e3.spokenResponse == "Today is November 19, 2026.", "EN GET_DATE response");

    auto e4 = TimeCommandLogic::processTranscription("What day is it?", refUtc, true, EN);
    VC_CHECK(e4.command == TimeCommandType::GET_DAY, "EN GET_DAY");
    VC_CHECK(e4.spokenResponse == "Today is Thursday, November 19, 2026.", "EN GET_DAY response");

    auto e5 = TimeCommandLogic::processTranscription("What year is it?", refUtc, true, EN);
    VC_CHECK(e5.command == TimeCommandType::GET_YEAR, "EN GET_YEAR");
    VC_CHECK(e5.spokenResponse == "The year is 2026.", "EN GET_YEAR response");

    auto e6 = TimeCommandLogic::processTranscription("What time will it be in one hour?", refUtc, true, EN);
    VC_CHECK(e6.command == TimeCommandType::GET_TIME_PLUS_1H, "EN GET_TIME_PLUS_1H");
    VC_CHECK(e6.spokenResponse == "In one hour, it will be eleven fifteen.", "EN +1h response");

    auto e7 = TimeCommandLogic::processTranscription("What time was it one hour ago?", refUtc, true, EN);
    VC_CHECK(e7.command == TimeCommandType::GET_TIME_MINUS_1H, "EN GET_TIME_MINUS_1H");
    VC_CHECK(e7.spokenResponse == "One hour ago, it was nine fifteen.", "EN -1h response");

    auto e8 = TimeCommandLogic::processTranscription("Turn on the music", refUtc, true, EN);
    VC_CHECK(e8.command == TimeCommandType::UNKNOWN && !e8.recognized, "EN reject unknown");
    VC_CHECK(e8.spokenResponse == "I can only answer questions about the time and date.", "EN polite rejection");

    // Localisation UI
    VC_CHECK(std::string(Localization::get(VoiceControlConfig::LanguageCode::FR).btnTalk) == "PARLER", "UI FR PARLER");
    VC_CHECK(std::string(Localization::get(VoiceControlConfig::LanguageCode::EN).btnTalk) == "TALK", "UI EN TALK");
    VC_CHECK(std::string(Localization::getNtpStatusLabel(NtpUiStatus::UNAVAILABLE, EN)) == "NTP: Unavailable", "UI EN NTP unavailable");
}

static void testNtpUnavailable() {
    // VC-15 : NTP indisponible au démarrage -> ne jamais inventer l'heure
    auto frUnsync = TimeCommandLogic::processTranscription(
        "Quelle heure est-il ?", 0, false, VoiceControlConfig::LanguageCode::FR);
    VC_CHECK(frUnsync.command == TimeCommandType::GET_TIME, "Unsynced FR command classified");
    VC_CHECK(!frUnsync.clockSynced, "Unsynced clock flag false");
    VC_CHECK(frUnsync.spokenResponse == "L'horloge n'est pas encore synchronisée. Veuillez patienter.",
             "FR never invents time when NTP unavailable");

    auto enUnsync = TimeCommandLogic::processTranscription(
        "What time is it?", 1000, false, VoiceControlConfig::LanguageCode::EN);
    VC_CHECK(!enUnsync.clockSynced, "Unsynced EN clock flag false");
    VC_CHECK(enUnsync.spokenResponse == "The clock is not synchronized yet. Please wait.",
             "EN never invents time when NTP unavailable");
}

int main() {
    testSwissTimeAndDstTransitions();
    testFrenchCommands();
    testEnglishCommandsAndLocalization();
    testNtpUnavailable();

    std::cout << "VoiceControl Core Unit Tests: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
