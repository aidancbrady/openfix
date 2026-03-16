#pragma once

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

struct Utils
{
    inline static long getEpochMillis()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // parse fixed-format FIX timestamp: YYYYMMDD-HH:MM:SS[.fff[fff]]
    inline static long parseUTCTimestamp(const std::string& timestamp)
    {
        // minimum: "YYYYMMDD-HH:MM:SS" = 17 chars
        if (timestamp.size() < 17)
            return 0;

        auto parseN = [&](size_t pos, size_t len) -> int {
            int val = 0;
            std::from_chars(timestamp.data() + pos, timestamp.data() + pos + len, val);
            return val;
        };

        std::tm tm = {};
        tm.tm_year = parseN(0, 4) - 1900;
        tm.tm_mon = parseN(4, 2) - 1;
        tm.tm_mday = parseN(6, 2);
        tm.tm_hour = parseN(9, 2);
        tm.tm_min = parseN(12, 2);
        tm.tm_sec = parseN(15, 2);

        std::time_t t = timegm(&tm);
        long ms = static_cast<long>(t) * 1000L;

        // parse fractional seconds if present
        if (timestamp.size() > 18 && timestamp[17] == '.') {
            size_t frac_start = 18;
            size_t frac_len = timestamp.size() - frac_start;
            int frac = 0;
            std::from_chars(timestamp.data() + frac_start, timestamp.data() + frac_start + frac_len, frac);

            // normalize to milliseconds: 3 digits = ms, 6 digits = us -> ms
            if (frac_len <= 3) {
                // pad: e.g. "1" -> 100, "12" -> 120
                for (size_t i = frac_len; i < 3; ++i)
                    frac *= 10;
                ms += frac;
            } else {
                // truncate to ms: e.g. 6 digits -> divide by 1000
                for (size_t i = 3; i < frac_len; ++i)
                    frac /= 10;
                ms += frac;
            }
        }

        return ms;
    }

    // Format UTC timestamp for FIX protocol: YYYYMMDD-HH:MM:SS.sss (millisecond precision)
    // Uses thread_local cache: gmtime_r + strftime only called once per second.
    inline static std::string getUTCTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto epoch_s = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        thread_local time_t cached_s = 0;
        thread_local char cached_prefix[18]; // "YYYYMMDD-HH:MM:SS" + null

        if (epoch_s != cached_s) {
            cached_s = static_cast<time_t>(epoch_s);
            std::tm utc{};
            gmtime_r(&cached_s, &utc);
            std::strftime(cached_prefix, sizeof(cached_prefix), "%Y%m%d-%H:%M:%S", &utc);
        }

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

        char buf[32];
        std::memcpy(buf, cached_prefix, 17);
        std::snprintf(buf + 17, sizeof(buf) - 17, ".%03ld", ms);

        return std::string(buf, 21);
    }

    // Format a pre-captured epoch-microsecond timestamp: YYYYMMDD-HH:MM:SS.uuuuuu
    // Uses thread_local cache: gmtime_r + strftime only called once per second.
    inline static std::string formatTimestampMicros(int64_t epoch_us)
    {
        auto epoch_s = static_cast<time_t>(epoch_us / 1000000);
        auto us = static_cast<int>(epoch_us % 1000000);

        thread_local time_t cached_s = 0;
        thread_local char cached_prefix[18];

        if (epoch_s != cached_s) {
            cached_s = epoch_s;
            std::tm utc{};
            gmtime_r(&cached_s, &utc);
            std::strftime(cached_prefix, sizeof(cached_prefix), "%Y%m%d-%H:%M:%S", &utc);
        }

        char buf[32];
        std::memcpy(buf, cached_prefix, 17);
        std::snprintf(buf + 17, sizeof(buf) - 17, ".%06d", us);

        return std::string(buf, 24);
    }

    // Format UTC timestamp for logging: YYYYMMDD-HH:MM:SS.uuuuuu (microsecond precision)
    inline static std::string getUTCTimestampMicros()
    {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return formatTimestampMicros(us);
    }

    // Search for a pre-built tag pattern in a FIX message.
    // The tag parameter should be the numeric tag string (e.g. "9" for BodyLength).
    // Builds the search pattern "\x01{tag}=" and finds it starting from idx.
    inline static std::pair<std::string, size_t> getTagValue(const std::string& msg, const std::string& tag, int idx = 0)
    {
        // build search pattern: SOH + tag + '='
        // for hot paths, callers should use the overload with a pre-built pattern
        thread_local std::string pattern;
        pattern.clear();
        pattern += '\01';
        pattern += tag;
        pattern += '=';

        auto begin_it = msg.find(pattern, idx);
        if (begin_it == std::string::npos)
            return {"", begin_it};
        begin_it += pattern.size();
        auto end_it = msg.find('\01', begin_it);
        if (end_it == std::string::npos)
            return {"", end_it};
        return {msg.substr(begin_it, (end_it - begin_it)), end_it};
    }

    // Overload that takes a pre-built search pattern (SOH + tag + '=') to avoid any allocation.
    // Use buildTagPattern() to construct the pattern at startup.
    inline static std::pair<std::string, size_t> getTagValue(const std::string& msg, const std::string& pattern, size_t patternLen, size_t idx)
    {
        auto begin_it = msg.find(pattern, idx);
        if (begin_it == std::string::npos)
            return {"", begin_it};
        begin_it += patternLen;
        auto end_it = msg.find('\01', begin_it);
        if (end_it == std::string::npos)
            return {"", end_it};
        return {msg.substr(begin_it, (end_it - begin_it)), end_it};
    }

    inline static std::string buildTagPattern(int tag)
    {
        std::string pattern;
        pattern += '\01';
        pattern += std::to_string(tag);
        pattern += '=';
        return pattern;
    }
};