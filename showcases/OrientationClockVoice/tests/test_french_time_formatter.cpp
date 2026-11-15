#include <cstdlib>
#include <iostream>

#include "FrenchTimeFormatter.h"

static int failures = 0;

static void expectEqual(const char* name, const String& actual, const char* expected) {
    if (actual != expected) {
        std::cerr << "ECHEC " << name << "\n  obtenu:  " << actual
                  << "\n  attendu: " << expected << "\n";
        ++failures;
    }
}

int main() {
    expectEqual("minuit", formatTimeInFrench(0, 0), "Il est minuit.");
    expectEqual("midi", formatTimeInFrench(12, 0), "Il est midi.");
    expectEqual("quart", formatTimeInFrench(7, 15), "Il est sept heures et quart.");
    expectEqual("demie", formatTimeInFrench(1, 30), "Il est une heure et demie.");
    expectEqual("demi", formatTimeInFrench(12, 30), "Il est midi et demi.");
    expectEqual("moins le quart", formatTimeInFrench(7, 45),
                "Il est huit heures moins le quart.");
    expectEqual("avant minuit", formatTimeInFrench(23, 50),
                "Il est minuit moins dix.");
    expectEqual("exact", formatTimeInFrench(20, 45, TimeSpeechStyle::EXACT),
                "Il est vingt heures quarante-cinq.");
    expectEqual("invalide", formatTimeInFrench(25, 0), "Il est une heure.");

    tm date{};
    date.tm_wday = 0;
    date.tm_mday = 15;
    date.tm_mon = 10;
    expectEqual("date", formatDateInFrench(date), "dimanche 15 novembre");

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "10 tests de formatage francais: OK\n";
    return EXIT_SUCCESS;
}
