#pragma once

#include <openfix/Dictionary.h>
#include <openfix/Utils.h>

#include <string>
#include <vector>

#include "BenchmarkFixtures.h"
#include "BenchmarkFramework.h"
#include "SessionTestHarness.h"

namespace perf {

inline std::vector<BenchmarkResult> runParseBenchmarks()
{
    auto dict = DictionaryRegistry::instance().load(std::string(bench::kBenchmarkDictionaryPath));

    SessionSettings settings;
    settings.setBool(SessionSettings::LOUD_PARSING, false);

    // Pre-bake raw FIX messages using \x01 separators (as returned by buildRawMessage).
    // Timestamps are fixed at setup time; the parser validates the format, not recency.
    const std::string ts = Utils::getUTCTimestamp();

    const std::string heartbeatRaw = fix_test::buildRawMessage(
        std::string(bench::kBenchmarkBeginString),
        bench::heartbeatWireFields(1, ts)
    );

    const std::string nosRaw = fix_test::buildRawMessage(
        std::string(bench::kBenchmarkBeginString),
        bench::newOrderSingleWireFields(1, ts)
    );

    std::vector<BenchmarkResult> results;

    results.push_back(runPrepared(
        "Parse/Heartbeat",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() { return heartbeatRaw; },
        [&](std::string text) {
            auto msg = dict->parse(settings, std::move(text));
            (void)msg;
        }
    ));

    results.push_back(runPrepared(
        "Parse/NewOrderSingle",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() { return nosRaw; },
        [&](std::string text) {
            auto msg = dict->parse(settings, std::move(text));
            (void)msg;
        }
    ));

    return results;
}

} // namespace perf
