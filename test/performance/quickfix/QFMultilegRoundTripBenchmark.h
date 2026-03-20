#pragma once

#include <Application.h>
#include <FileStore.h>
#include <Log.h>
#include <Session.h>
#include <SessionSettings.h>
#include <SocketAcceptor.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "BenchmarkFixtures.h"
#include "BenchmarkFramework.h"
#include "QFNetworkThroughputBenchmark.h"  // for QFBenchApp base
#include "QFUtils.h"

namespace perf {

// QuickFIX application that receives 35=AB multileg orders and responds
// with 35=8 ExecutionReports referencing each leg.
class QFMultilegApp : public FIX::NullApplication
{
public:
    std::atomic<int> responseCount{0};

    void fromApp(const FIX::Message& msg, const FIX::SessionID& sessionID) override
    {
        FIX::MsgType msgType;
        msg.getHeader().getField(msgType);
        if (msgType != "AB")
            return;

        // Extract leg info from repeating group 555
        std::string legInfo;
        const FIX::Group legProbe(555, 600);
        if (msg.hasGroup(legProbe)) {
            const int count = FIX::IntConvertor::convert(msg.getField(555));
            for (int i = 1; i <= count; ++i) {
                FIX::Group leg(555, 600);
                msg.getGroup(i, leg);

                if (i > 1) legInfo += ",";
                legInfo += leg.getField(600);  // LegSymbol
                legInfo += ":";
                legInfo += leg.getField(687);  // LegQty
            }
        }

        // Build ExecutionReport response
        FIX::Message reply;
        reply.getHeader().setField(FIX::BeginString(std::string(bench::kBenchmarkBeginString)));
        reply.getHeader().setField(FIX::MsgType("8"));

        const std::string clOrdID = msg.getField(11);

        reply.setField(FIX::OrderID("ORD-" + clOrdID));
        reply.setField(FIX::StringField(17, clOrdID));  // ExecID
        reply.setField(FIX::StringField(150, "0"));  // ExecType = New
        reply.setField(FIX::StringField(39, "0"));   // OrdStatus = New
        reply.setField(FIX::StringField(55, msg.getField(55)));  // Symbol
        reply.setField(FIX::StringField(54, msg.getField(54)));  // Side
        reply.setField(FIX::StringField(38, "1000"));  // OrderQty
        reply.setField(FIX::StringField(14, "0"));     // CumQty
        reply.setField(FIX::StringField(151, "1000")); // LeavesQty
        reply.setField(FIX::StringField(6, "0"));      // AvgPx
        reply.setField(FIX::StringField(58, legInfo)); // Text

        FIX::Session::sendToTarget(reply, sessionID);
        responseCount.fetch_add(1, std::memory_order_relaxed);
    }
};

inline std::vector<BenchmarkResult> runQFMultilegRoundTripBenchmarks()
{
    constexpr int WARMUP  =  50;
    constexpr int MEASURE = 500;

    const int port = qfGetAvailablePort();
    const std::string configPath = qfWriteConfig(port);
    const std::string storePath  = bench::quickfixStoreDir(port).string();

    bench::resetQuickfixStoreDir(port);

    QFMultilegApp app;
    FIX::SessionSettings settings(configPath);
    FIX::FileStoreFactory storeFactory(settings);
    FIX::SocketAcceptor acceptor(app, storeFactory, settings);

    acceptor.start();

    QFRawClient client;
    if (!client.connectWithRetry(port, std::chrono::seconds(5))) {
        std::cerr << "[QF MultilegRT] Failed to connect\n";
        acceptor.stop();
        std::filesystem::remove_all(storePath);
        std::filesystem::remove(configPath);
        return {};
    }

    if (!client.performLogon("INITIATOR", "ACCEPTOR", 1, 30, std::string(bench::kBenchmarkBeginString))) {
        std::cerr << "[QF MultilegRT] Logon failed\n";
        acceptor.stop();
        std::filesystem::remove_all(storePath);
        std::filesystem::remove(configPath);
        return {};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    int seqNum = 2;

    auto sendMultileg = [&](BenchmarkTimer& timer, bool timed) {
        const std::string clOrdID = std::to_string(seqNum);

        if (timed) timer.start();

        client.sendMessage("AB", seqNum++, bench::newOrderMultilegBodyFields(clOrdID, qfTimestamp()),
            "INITIATOR", "ACCEPTOR", std::string(bench::kBenchmarkBeginString));

        // Wait for the ExecutionReport (35=8) response
        while (true) {
            auto raw = client.receiveMessage(std::chrono::milliseconds(1000));
            if (raw.empty()) {
                if (timed) timer.stop();
                return false;
            }
            auto tags = QFRawClient::parseTags(raw);
            if (tags[35] == "8" && tags[37] == "ORD-" + clOrdID)
                break;
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
    acceptor.stop();
    std::filesystem::remove_all(storePath);
    std::filesystem::remove(configPath);

    return { timer.finalize("RoundTrip/Multileg-AB-8") };
}

} // namespace perf
