#pragma once

#include <cstdint>

struct LocalDateTime
{
    int year = 1970;
    unsigned int month = 1;
    unsigned int day = 1;
    unsigned int hour = 0;
    unsigned int minute = 0;
};

constexpr LocalDateTime UnixToLocalDateTime(
    const uint64_t unixSeconds,
    const int32_t timezoneOffsetMinutes
)
{
    int64_t seconds = static_cast<int64_t>(unixSeconds) +
                      static_cast<int64_t>(timezoneOffsetMinutes) * 60;
    int64_t days = seconds / 86400;
    int64_t secondsOfDay = seconds % 86400;
    if (secondsOfDay < 0)
    {
        secondsOfDay += 86400;
        --days;
    }

    const unsigned int hour = static_cast<unsigned int>(secondsOfDay / 3600);
    const unsigned int minute = static_cast<unsigned int>((secondsOfDay % 3600) / 60);

    // Gregorian civil date conversion for days relative to 1970-01-01.
    const int64_t shiftedDays = days + 719468;
    const int64_t era = (shiftedDays >= 0 ? shiftedDays : shiftedDays - 146096) / 146097;
    const unsigned int dayOfEra =
        static_cast<unsigned int>(shiftedDays - era * 146097);
    const unsigned int yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    int year = static_cast<int>(yearOfEra) + static_cast<int>(era * 400);
    const unsigned int dayOfYear =
        dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const unsigned int monthPrime = (5 * dayOfYear + 2) / 153;
    const unsigned int day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;
    const unsigned int month = monthPrime < 10 ? monthPrime + 3 : monthPrime - 9;
    year += month <= 2;

    return {year, month, day, hour, minute};
}

namespace unix_time_compile_tests
{
    constexpr LocalDateTime epochChina = UnixToLocalDateTime(0, 480);
    static_assert(epochChina.year == 1970 && epochChina.month == 1 &&
                  epochChina.day == 1 && epochChina.hour == 8);

    constexpr LocalDateTime leapDayChina =
        UnixToLocalDateTime(1709164800ULL, 480);
    static_assert(leapDayChina.year == 2024 && leapDayChina.month == 2 &&
                  leapDayChina.day == 29 && leapDayChina.hour == 8);

    constexpr LocalDateTime newYearChina =
        UnixToLocalDateTime(1735689600ULL, 480);
    static_assert(newYearChina.year == 2025 && newYearChina.month == 1 &&
                  newYearChina.day == 1 && newYearChina.hour == 8);
}
