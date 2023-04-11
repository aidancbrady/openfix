#pragma once

#include <chrono>

struct Utils
{
    inline static long getEpochMillis()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }
};