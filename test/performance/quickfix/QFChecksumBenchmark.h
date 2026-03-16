#pragma once

#include <string>
#include <vector>

#include "BenchmarkFramework.h"

namespace perf {

// QuickFIX computes checksums with a simple byte-by-byte loop (see Field.h
// calculateMetrics).  We replicate that exact logic here so the benchmark
// measures the same scalar path QuickFIX uses internally.
inline uint8_t qfChecksum(const char* data, size_t len)
{
    int sum = 0;
    for (size_t i = 0; i < len; ++i)
        sum += static_cast<unsigned char>(data[i]);
    return static_cast<uint8_t>(sum % 256);
}

inline std::vector<BenchmarkResult> runQFChecksumBenchmarks()
{
    const std::vector<size_t>      sizes  = {64, 256, 1024, 4096};
    const std::vector<std::string> labels = {
        "Checksum/64B", "Checksum/256B", "Checksum/1KB", "Checksum/4KB"
    };

    std::vector<BenchmarkResult> results;
    results.reserve(sizes.size());

    for (size_t i = 0; i < sizes.size(); ++i) {
        std::string payload(sizes[i], 'A');

        volatile uint8_t sink = 0;

        results.push_back(run(
            labels[i],
            /*warmup=*/100'000,
            /*measure=*/1'000'000,
            [&]() { sink = qfChecksum(payload.data(), payload.size()); }
        ));
    }

    return results;
}

} // namespace perf
