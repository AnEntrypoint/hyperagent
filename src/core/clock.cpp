#include "core/clock.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace hyperagent::core::clock {

double now_epoch()
{
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

double round_to(double value, int digits)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", digits, value);
    return std::strtod(buffer, nullptr);
}

std::string iso_stamp(double epoch)
{
    double whole = 0.0;
    const double fraction = std::modf(epoch, &whole);
    long long microseconds = std::llround(std::nearbyint(fraction * 1e6));
    auto seconds = static_cast<long long>(whole);
    if (microseconds >= 1000000) {
        seconds += 1;
        microseconds -= 1000000;
    } else if (microseconds < 0) {
        seconds -= 1;
        microseconds += 1000000;
    }

    const std::time_t moment = static_cast<std::time_t>(seconds);
    std::tm utc{};
    gmtime_r(&moment, &utc);

    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec, microseconds / 1000);
    return buffer;
}

std::string http_date()
{
    const std::time_t moment = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&moment, &utc);
    static constexpr const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static constexpr const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%s, %02d %s %04d %02d:%02d:%02d GMT",
                  days[utc.tm_wday], utc.tm_mday, months[utc.tm_mon], utc.tm_year + 1900,
                  utc.tm_hour, utc.tm_min, utc.tm_sec);
    return buffer;
}

}
