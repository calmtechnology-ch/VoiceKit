#pragma once
#include <Arduino.h>

/**
 * @brief Convertit une heure et des minutes (format 24h) en phrase française parlée naturelle.
 * Exemples :
 * - (20, 9)  -> "Il est vingt heures neuf."
 * - (12, 0)  -> "Il est midi."
 * - (0, 0)   -> "Il est minuit."
 * - (1, 15)  -> "Il est une heure quinze."
 * - (21, 30) -> "Il est vingt et une heures trente."
 * 
 * @param hours Heure de 0 à 23.
 * @param minutes Minute de 0 à 59.
 * @return String Phrase prête pour la synthèse vocale.
 */
enum class TimeSpeechStyle {
    EXACT,
    NATURAL
};

String formatTimeInFrench(int hours, int minutes,
                          TimeSpeechStyle style = TimeSpeechStyle::NATURAL);

/** Format court destine a l'ecran, par exemple "dimanche 15 novembre". */
String formatDateInFrench(const tm& local_time);
