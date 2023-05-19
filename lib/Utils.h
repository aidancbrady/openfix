#pragma once

#include <chrono>
#include <sstream>

struct Utils
{
    inline static std::string UTC_TIMESTAMP_FMT = "%Y%M%d-%H:%M:%S.";

    inline static long getEpochMillis()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    inline static long parseUTCTimestamp(const std::string& timestamp)
    {
        using namespace std::chrono;
        std::istringstream stream(timestamp);

        sys_time<milliseconds> time;
        from_stream(stream, UTC_TIMESTAMP_FMT, time);

        milliseconds millis;
        from_stream(stream, "%S", millis);
        time += millis;

        return time.time_since_epoch().count();
    }
};