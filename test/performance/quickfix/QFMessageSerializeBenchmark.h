#pragma once

#include <FieldNumbers.h>
#include <Fields.h>
#include <fix42/Heartbeat.h>
#include <fix42/NewOrderSingle.h>

#include <string>
#include <vector>

#include "BenchmarkFramework.h"
#include "QFUtils.h"

namespace perf {

inline std::vector<BenchmarkResult> runQFSerializeBenchmarks()
{
    const std::string ts = qfTimestamp();
    FIX::UtcTimeStamp utcTs = FIX::UtcTimeStamp::now();

    static volatile size_t sink = 0;

    std::vector<BenchmarkResult> results;

    results.push_back(run(
        "Serialize/Heartbeat",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() {
            FIX42::Heartbeat msg;
            msg.getHeader().setField(FIX::SenderCompID("INITIATOR"));
            msg.getHeader().setField(FIX::TargetCompID("ACCEPTOR"));
            msg.getHeader().setField(FIX::MsgSeqNum(12345));
            msg.getHeader().setField(FIX::SendingTime(utcTs));
            auto s = msg.toString();
            sink += s.size();
        }
    ));

    results.push_back(run(
        "Serialize/NewOrderSingle",
        /*warmup=*/50'000,
        /*measure=*/500'000,
        [&]() {
            FIX42::NewOrderSingle msg(
                FIX::ClOrdID("ORDER123456"),
                FIX::HandlInst(FIX::HandlInst_AUTOMATED_EXECUTION_NO_INTERVENTION),
                FIX::Symbol("AAPL"),
                FIX::Side(FIX::Side_BUY),
                FIX::TransactTime(utcTs),
                FIX::OrdType(FIX::OrdType_LIMIT)
            );
            msg.getHeader().setField(FIX::SenderCompID("INITIATOR"));
            msg.getHeader().setField(FIX::TargetCompID("ACCEPTOR"));
            msg.getHeader().setField(FIX::MsgSeqNum(12345));
            msg.getHeader().setField(FIX::SendingTime(utcTs));
            msg.set(FIX::OrderQty(100));
            msg.set(FIX::Price(150.00));
            msg.set(FIX::TimeInForce(FIX::TimeInForce_DAY));
            auto s = msg.toString();
            sink += s.size();
        }
    ));

    return results;
}

} // namespace perf
