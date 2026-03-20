#pragma once

#include <openfix/Application.h>
#include <openfix/Fields.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "BenchmarkFixtures.h"
#include "BenchmarkFramework.h"
#include "NetworkThroughputBenchmark.h"  // for makeAcceptorBenchSettings
#include "SessionTestHarness.h"

namespace perf {

// FIX tag constants used in this benchmark
namespace {
constexpr int TAG_ClOrdID        = 11;
constexpr int TAG_OrderID        = 37;
constexpr int TAG_ExecID         = 17;
constexpr int TAG_ExecType       = 150;
constexpr int TAG_OrdStatus      = 39;
constexpr int TAG_Symbol         = 55;
constexpr int TAG_Side           = 54;
constexpr int TAG_OrderQty       = 38;
constexpr int TAG_OrdType        = 40;
constexpr int TAG_TransactTime   = 60;
constexpr int TAG_CumQty         = 14;
constexpr int TAG_LeavesQty      = 151;
constexpr int TAG_AvgPx          = 6;
constexpr int TAG_Text           = 58;
constexpr int TAG_NoLegs         = 555;
constexpr int TAG_LegSymbol      = 600;
constexpr int TAG_LegSide        = 624;
constexpr int TAG_LegQty         = 687;
}

// SessionDelegate that receives a NewOrderMultileg, extracts leg data,
// and responds with an ExecutionReport referencing each leg.
class MultilegDelegate : public SessionDelegate
{
public:
    std::atomic<int> responseCount{0};

    void onMessage(Session& session, const Message& msg) override
    {
        const auto& msgType = msg.getHeader().getField(FIELD::MsgType);
        if (msgType != "AB")
            return;

        // Extract leg symbols from repeating group 555
        std::string legInfo;
        const size_t numLegs = msg.getBody().getGroupCount(TAG_NoLegs);
        for (size_t i = 0; i < numLegs; ++i) {
            const auto& leg = msg.getBody().getGroup(TAG_NoLegs, i);
            if (i > 0) legInfo += ",";
            legInfo += std::string(leg.getField(TAG_LegSymbol));
            legInfo += ":";
            legInfo += std::string(leg.getField(TAG_LegQty));
        }

        // Build ExecutionReport response
        auto reply = session.createMessage("8");
        reply.getBody().setField(TAG_OrderID, "ORD-" + std::string(msg.getBody().getField(TAG_ClOrdID)));
        reply.getBody().setField(TAG_ExecID, std::string(msg.getBody().getField(TAG_ClOrdID)));
        reply.getBody().setField(TAG_ExecType, "0");  // New
        reply.getBody().setField(TAG_OrdStatus, "0");  // New
        reply.getBody().setField(TAG_Symbol, std::string(msg.getBody().getField(TAG_Symbol)));
        reply.getBody().setField(TAG_Side, std::string(msg.getBody().getField(TAG_Side)));
        reply.getBody().setField(TAG_OrderQty, "1000");
        reply.getBody().setField(TAG_CumQty, "0");
        reply.getBody().setField(TAG_LeavesQty, "1000");
        reply.getBody().setField(TAG_AvgPx, "0");
        reply.getBody().setField(TAG_Text, legInfo);

        session.send(reply);
        responseCount.fetch_add(1, std::memory_order_relaxed);
    }
};

inline std::vector<BenchmarkResult> runMultilegRoundTripBenchmarks()
{
    constexpr int WARMUP  =  50;
    constexpr int MEASURE = 500;

    bench::resetOpenfixStoreDir();

    const int port = fix_test::getAvailablePort();

    Application app;
    auto session = app.createSession("bench", makeAcceptorBenchSettings(port));

    auto delegate = std::make_shared<MultilegDelegate>();
    session->setDelegate(delegate);

    app.start();

    fix_test::RawFIXClient client;
    if (!client.connectWithRetry(port, std::chrono::seconds(5))) {
        std::cerr << "[MultilegRT] Failed to connect\n";
        app.stop();
        bench::resetOpenfixStoreDir();
        return {};
    }

    if (!client.performLogon("INITIATOR", "ACCEPTOR", 1, 30, std::string(bench::kBenchmarkBeginString))) {
        std::cerr << "[MultilegRT] Logon failed\n";
        app.stop();
        bench::resetOpenfixStoreDir();
        return {};
    }

    if (!fix_test::waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(5))) {
        std::cerr << "[MultilegRT] Server did not receive logon\n";
        app.stop();
        bench::resetOpenfixStoreDir();
        return {};
    }

    int seqNum = 2;

    // Build a raw 35=AB multileg message with 3 legs in the 555 repeating group.
    // The repeating group format: 555=3|600=AAPL|624=1|687=100|600=GOOG|624=2|687=200|600=MSFT|624=1|687=300
    auto sendMultileg = [&](BenchmarkTimer& timer, bool timed) {
        const std::string clOrdID = std::to_string(seqNum);

        if (timed) timer.start();

        client.sendMessage("AB", seqNum++, bench::newOrderMultilegBodyFields(clOrdID, Utils::getUTCTimestamp()),
            "INITIATOR", "ACCEPTOR", std::string(bench::kBenchmarkBeginString));

        // Wait for the ExecutionReport (35=8) response
        while (true) {
            auto raw = client.receiveMessage(std::chrono::milliseconds(1000));
            if (raw.empty()) {
                if (timed) timer.stop();
                return false;
            }
            auto tags = fix_test::RawFIXClient::parseTags(raw);
            if (tags[FIELD::MsgType] == "8") {
                // Verify it references our ClOrdID
                if (tags[TAG_OrderID] == "ORD-" + clOrdID)
                    break;
            }
        }

        if (timed) timer.stop();
        return true;
    };

    // Warmup
    BenchmarkTimer dummy;
    dummy.reserve(WARMUP);
    for (int i = 0; i < WARMUP; ++i)
        sendMultileg(dummy, false);

    // Measurement
    BenchmarkTimer timer;
    timer.reserve(MEASURE);
    for (int i = 0; i < MEASURE; ++i)
        sendMultileg(timer, true);

    client.close();
    app.stop();
    bench::resetOpenfixStoreDir();

    return { timer.finalize("RoundTrip/Multileg-AB-8") };
}

} // namespace perf
