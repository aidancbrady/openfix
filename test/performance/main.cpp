#include <openfix/Log.h>

#include <string>
#include <vector>

#include "BenchmarkFixtures.h"
#include "BenchmarkFramework.h"
#include "ChecksumBenchmark.h"
#include "MessageParseBenchmark.h"
#include "MessageSerializeBenchmark.h"
#include "NetworkThroughputBenchmark.h"
#include "MultilegRoundTripBenchmark.h"
#include "RoundTripBenchmark.h"
#include "SessionTestHarness.h"

int main(int argc, char** argv)
{
    // Suppress engine logs so only the benchmark table reaches stdout.
    spdlog::set_level(spdlog::level::warn);

    // Initialize platform settings to match the test suite configuration.
    PlatformSettings::load({
        {"AdminWebsitePort", "0"},
        {"DataPath",         perf::bench::openfixStoreDir().string()},
        {"UpdateDelay",      "10"},
        {"InputThreads",     "1"},
        {"EpollTimeout",     "10"},
    });

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
    append(perf::runChecksumBenchmarks());
    append(perf::runParseBenchmarks());
    append(perf::runSerializeBenchmarks());

    // --- Network benchmarks ---
    if (!cpuOnly) {
        append(perf::runNetworkThroughputBenchmarks());
        append(perf::runRoundTripBenchmarks());
        append(perf::runMultilegRoundTripBenchmarks());
    }

    perf::printResults(results);
    return 0;
}
