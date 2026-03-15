#pragma once

#include <openfix/Dictionary.h>
#include <openfix/Fields.h>
#include <openfix/Utils.h>

#include <string>
#include <vector>

#include "BenchmarkFramework.h"
#include "SessionTestHarness.h"

namespace perf {

inline std::vector<BenchmarkResult> runSerializeBenchmarks()
{
    auto dict = DictionaryRegistry::instance().load(fix_test::kFixDictionaryPath);

    // Pre-build a timestamp string so timestamp formatting isn't benchmarked.
    const std::string ts = Utils::getUTCTimestamp();

    // volatile sink prevents the compiler from proving the string is unused
    static volatile size_t sink = 0;

    std::vector<BenchmarkResult> results;

    results.push_back(run(
        "Serialize/Heartbeat",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() {
            auto msg = dict->create("0");
            msg.getHeader().setField(FIELD::SenderCompID, "INITIATOR");
            msg.getHeader().setField(FIELD::TargetCompID, "ACCEPTOR");
            msg.getHeader().setField(FIELD::MsgSeqNum,    "12345");
            msg.getHeader().setField(FIELD::SendingTime,  ts);
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
            msg.getHeader().setField(FIELD::SenderCompID, "INITIATOR");
            msg.getHeader().setField(FIELD::TargetCompID, "ACCEPTOR");
            msg.getHeader().setField(FIELD::MsgSeqNum,    "12345");
            msg.getHeader().setField(FIELD::SendingTime,  ts);
            msg.getBody().setField(11, "ORDER123456");  // ClOrdID
            msg.getBody().setField(21, "1");            // HandlInst
            msg.getBody().setField(55, "AAPL");         // Symbol
            msg.getBody().setField(54, "1");            // Side (Buy)
            msg.getBody().setField(60, ts);             // TransactTime
            msg.getBody().setField(40, "2");            // OrdType (Limit)
            msg.getBody().setField(38, "100");          // OrderQty
            msg.getBody().setField(44, "150.00");       // Price
            msg.getBody().setField(59, "0");            // TimeInForce (Day)
            auto s = msg.toString(/*internal=*/true);
            sink += s.size();
        }
    ));

    return results;
}

} // namespace perf
