#pragma once

#include <openfix/Checksum.h>

#include <string>
#include <vector>

#include "BenchmarkFramework.h"

namespace perf {

inline std::vector<BenchmarkResult> runChecksumBenchmarks()
{
    const std::vector<size_t>      sizes  = {64, 256, 1024, 4096};
    const std::vector<std::string> labels = {
        "Checksum/64B", "Checksum/256B", "Checksum/1KB", "Checksum/4KB"
    };

    std::vector<BenchmarkResult> results;
    results.reserve(sizes.size());

    for (size_t i = 0; i < sizes.size(); ++i) {
        std::string payload(sizes[i], 'A');

        // volatile sink prevents the compiler from eliding the call
        volatile uint8_t sink = 0;

        results.push_back(run(
            labels[i],
            /*warmup=*/100'000,
            /*measure=*/1'000'000,
            [&]() { sink = computeChecksum(payload.data(), payload.size()); }
        ));
    }

    return results;
}

} // namespace perf
