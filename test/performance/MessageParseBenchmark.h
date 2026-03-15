#pragma once

#include <openfix/Dictionary.h>
#include <openfix/Utils.h>

#include <string>
#include <vector>

#include "BenchmarkFramework.h"
#include "SessionTestHarness.h"

namespace perf {

inline std::vector<BenchmarkResult> runParseBenchmarks()
{
    auto dict = DictionaryRegistry::instance().load(fix_test::kFixDictionaryPath);

    SessionSettings settings;
    settings.setBool(SessionSettings::LOUD_PARSING, false);

    // Pre-bake raw FIX messages using \x01 separators (as returned by buildRawMessage).
    // Timestamps are fixed at setup time; the parser validates the format, not recency.
    const std::string ts = Utils::getUTCTimestamp();

    const std::string heartbeatRaw = fix_test::buildRawMessage("FIX.4.2", {
        {35, "0"},
        {49, "INITIATOR"},
        {56, "ACCEPTOR"},
        {34, "1"},
        {52, ts},
    });

    const std::string nosRaw = fix_test::buildRawMessage("FIX.4.2", {
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
            auto msg = dict->parse(settings, heartbeatRaw);
            (void)msg;
        }
    ));

    results.push_back(run(
        "Parse/NewOrderSingle",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() {
            auto msg = dict->parse(settings, nosRaw);
            (void)msg;
        }
    ));

    return results;
}

} // namespace perf
