#pragma once

#include <chrono>
#include <sstream>
#include <iomanip>

struct Utils
{
    inline static std::string UTC_TIMESTAMP_FMT = "%Y%m%d-%H:%M:%S.";

    inline static long getEpochMillis()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    inline static long parseUTCTimestamp(const std::string& timestamp)
    {
        std::tm tm = {};
        std::stringstream ss(timestamp);
        ss >> std::get_time(&tm, UTC_TIMESTAMP_FMT.c_str());
        auto ms = std::chrono::system_clock::from_time_t(std::mktime(&tm)).time_since_epoch().count();

        auto ms_it = timestamp.find('.');
        if (ms_it != std::string::npos) {
            ms += std::stoi(timestamp.substr(ms_it + 1));
        }

        return ms;
    }
};