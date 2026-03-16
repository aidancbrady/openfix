#pragma once

#include <DataDictionary.h>
#include <Message.h>

#include <string>
#include <vector>

#include "BenchmarkFramework.h"
#include "QFUtils.h"

namespace perf {

inline std::vector<BenchmarkResult> runQFParseBenchmarks()
{
    FIX::DataDictionary dict(qfDictionaryPath());

    const std::string ts = qfTimestamp();

    // Build raw FIX messages identical to the openfix benchmark inputs.
    const std::string heartbeatRaw = qfBuildRawMessage("FIX.4.2", {
        {35, "0"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, ts},
    });

    const std::string nosRaw = qfBuildRawMessage("FIX.4.2", {
        {35, "D"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, ts},
        {11, "ORDER123456"},
        {21, "1"},
        {55, "AAPL"},
        {54, "1"},
        {60, ts},
        {40, "2"},
        {38, "100"},
        {44, "150.00"},
        {59, "0"},
    });

    std::vector<BenchmarkResult> results;

    results.push_back(run(
        "Parse/Heartbeat",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() {
            FIX::Message msg(heartbeatRaw, dict, true);
            (void)msg;
        }
    ));

    results.push_back(run(
        "Parse/NewOrderSingle",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() {
            FIX::Message msg(nosRaw, dict, true);
            (void)msg;
        }
    ));

    return results;
}

} // namespace perf
