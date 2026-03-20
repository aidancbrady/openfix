#pragma once

#include <openfix/Dictionary.h>
#include <openfix/Fields.h>
#include <openfix/Utils.h>

#include <string>
#include <vector>

#include "BenchmarkFixtures.h"
#include "BenchmarkFramework.h"

namespace perf {

inline std::vector<BenchmarkResult> runSerializeBenchmarks()
{
    auto dict = DictionaryRegistry::instance().load(std::string(bench::kBenchmarkDictionaryPath));

    // Pre-build a timestamp string so timestamp formatting isn't benchmarked.
    const std::string ts = Utils::getUTCTimestamp();
    const auto headerFields = bench::sessionHeaderFields("12345", ts);
    const auto nosBodyFields = bench::newOrderSingleBodyFields(ts);

    // volatile sink prevents the compiler from proving the string is unused
    static volatile size_t sink = 0;

    std::vector<BenchmarkResult> results;

    results.push_back(run(
        "Serialize/Heartbeat",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() {
            auto msg = dict->create("0");
            msg.getHeader().setField(FIELD::BeginString, std::string(bench::kBenchmarkBeginString));
            bench::applyFields(headerFields, [&](int tag, const std::string& value) {
                msg.getHeader().setField(tag, value);
            });
            auto s = msg.toString(/*internal=*/true);
            sink += s.size();
        }
    ));

    results.push_back(run(
        "Serialize/NewOrderSingle",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() {
            auto msg = dict->create("D");
            msg.getHeader().setField(FIELD::BeginString, std::string(bench::kBenchmarkBeginString));
            bench::applyFields(headerFields, [&](int tag, const std::string& value) {
                msg.getHeader().setField(tag, value);
            });
            bench::applyFields(nosBodyFields, [&](int tag, const std::string& value) {
                msg.getBody().setField(tag, value);
            });
            auto s = msg.toString(/*internal=*/true);
            sink += s.size();
        }
    ));

    return results;
}

} // namespace perf
