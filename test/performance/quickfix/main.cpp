#include <string>
#include <vector>

#include "BenchmarkFramework.h"
#include "QFChecksumBenchmark.h"
#include "QFMessageParseBenchmark.h"
#include "QFMessageSerializeBenchmark.h"
#include "QFNetworkThroughputBenchmark.h"
#include "QFMultilegRoundTripBenchmark.h"
#include "QFRoundTripBenchmark.h"

int main(int argc, char** argv)
{
    bool cpuOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--cpu-only")
            cpuOnly = true;
    }

    std::vector<perf::BenchmarkResult> results;

    auto append = [&](std::vector<perf::BenchmarkResult> batch) {
        for (auto& r : batch)
            results.push_back(std::move(r));
    };

    // --- CPU-bound benchmarks (no network) ---
    append(perf::runQFChecksumBenchmarks());
    append(perf::runQFParseBenchmarks());
    append(perf::runQFSerializeBenchmarks());

    // --- Network benchmarks ---
    if (!cpuOnly) {
        append(perf::runQFNetworkThroughputBenchmarks());
        append(perf::runQFRoundTripBenchmarks());
        append(perf::runQFMultilegRoundTripBenchmarks());
    }

    perf::printResults(results, "quickfix");
    return 0;
}
