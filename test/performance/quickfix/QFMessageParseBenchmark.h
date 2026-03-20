#pragma once

#include <DataDictionary.h>
#include <Message.h>

#include <string>
#include <vector>

#include "BenchmarkFixtures.h"
#include "BenchmarkFramework.h"
#include "QFUtils.h"

namespace perf {

inline std::vector<BenchmarkResult> runQFParseBenchmarks()
{
    FIX::DataDictionary dict(qfDictionaryPath());

    const std::string ts = qfTimestamp();

    // Build raw FIX messages identical to the openfix benchmark inputs.
    const std::string heartbeatRaw = qfBuildRawMessage(
        std::string(bench::kBenchmarkBeginString),
        bench::heartbeatWireFields(1, ts)
    );

    const std::string nosRaw = qfBuildRawMessage(
        std::string(bench::kBenchmarkBeginString),
        bench::newOrderSingleWireFields(1, ts)
    );

    std::vector<BenchmarkResult> results;

    results.push_back(runPrepared(
        "Parse/Heartbeat",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() { return heartbeatRaw; },
        [&](const std::string& text) {
            FIX::Message msg(text, dict, true);
            (void)msg;
        }
    ));

    results.push_back(runPrepared(
        "Parse/NewOrderSingle",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() { return nosRaw; },
        [&](const std::string& text) {
            FIX::Message msg(text, dict, true);
            (void)msg;
        }
    ));

    return results;
}

} // namespace perf
