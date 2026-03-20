#pragma once

#include <Fields.h>
#include <Message.h>

#include <string>
#include <vector>

#include "BenchmarkFixtures.h"
#include "BenchmarkFramework.h"
#include "QFUtils.h"

namespace perf {

inline std::vector<BenchmarkResult> runQFSerializeBenchmarks()
{
    const std::string ts = qfTimestamp();
    const auto headerFields = bench::sessionHeaderFields("12345", ts);
    const auto nosBodyFields = bench::newOrderSingleBodyFields(ts);

    static volatile size_t sink = 0;

    std::vector<BenchmarkResult> results;

    results.push_back(run(
        "Serialize/Heartbeat",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() {
            FIX::Message msg;
            msg.getHeader().setField(FIX::BeginString(std::string(bench::kBenchmarkBeginString)));
            msg.getHeader().setField(FIX::MsgType("0"));
            bench::applyFields(headerFields, [&](int tag, const std::string& value) {
                msg.getHeader().setField(FIX::StringField(tag, value));
            });
            auto s = msg.toString();
            sink += s.size();
        }
    ));

    results.push_back(run(
        "Serialize/NewOrderSingle",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() {
            FIX::Message msg;
            msg.getHeader().setField(FIX::BeginString(std::string(bench::kBenchmarkBeginString)));
            msg.getHeader().setField(FIX::MsgType("D"));
            bench::applyFields(headerFields, [&](int tag, const std::string& value) {
                msg.getHeader().setField(FIX::StringField(tag, value));
            });
            bench::applyFields(nosBodyFields, [&](int tag, const std::string& value) {
                msg.setField(FIX::StringField(tag, value));
            });
            auto s = msg.toString();
            sink += s.size();
        }
    ));

    return results;
}

} // namespace perf
