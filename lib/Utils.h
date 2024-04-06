#pragma once

#include <ctime>
#include <chrono>
#include <sstream>
#include <iomanip>

struct Utils
{
    inline static std::string UTC_TIMESTAMP_FMT = "%Y%m%d-%H:%M:%S";

    inline static long getEpochMillis()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    inline static long parseUTCTimestamp(const std::string& timestamp)
    {
        std::tm tm = {};
        std::stringstream ss(timestamp);
        ss >> std::get_time(&tm, UTC_TIMESTAMP_FMT.c_str());

        std::time_t t = std::mktime(&tm);
        long timezoneOffset = 0;
    #ifdef _MSC_VER
        _get_timezone(&timezoneOffset);
    #else
        timezoneOffset = timezone;
    #endif
        t -= timezoneOffset;

        auto time_since_epoch = std::chrono::system_clock::from_time_t(t).time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_since_epoch).count();

        auto ms_it = timestamp.find('.');
        if (ms_it != std::string::npos) {
            auto ms_str = timestamp.substr(ms_it + 1);
            if (ms_str.length() > 3) {
                ms_str = ms_str.substr(0, 3);
            }
            ms += std::stoi(ms_str);
        }

        return ms;
    }

    inline static std::string getUTCTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm utc_time = {};
        gmtime_r(&time, &utc_time);

        std::ostringstream ostr;
        ostr << std::put_time(&utc_time, UTC_TIMESTAMP_FMT.c_str());

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        ostr << '.' << std::setfill('0') << std::setw(3) << ms.count();

        return ostr.str();
    }

    inline static std::pair<std::string, size_t> getTagValue(const std::string& msg, const std::string& tag, int idx = 0)
    {
        auto begin_it = msg.find('\01' + tag + '=', idx);
        if (begin_it == std::string::npos)
            return {"", begin_it};
        begin_it += 2 + tag.size();
        auto end_it = msg.find('\01', begin_it);
        if (end_it == std::string::npos)
            return {"", end_it};
        return {msg.substr(begin_it, (end_it - begin_it)), end_it};
    }
};