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
        reply.getHeader().setField(FIX::BeginString("FIX.4.4"));
        reply.getHeader().setField(FIX::MsgType("8"));

        const std::string clOrdID = msg.getField(11);

        reply.setField(FIX::OrderID("ORD-" + clOrdID));
        reply.setField(FIX::StringField(17, "EXEC-" + clOrdID));  // ExecID
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

// Locate FIX44.xml in runfiles (needed for AB/multileg support).
inline std::string qfFIX44DictionaryPath()
{
    std::vector<std::string> candidates = {
        "external/quickfix/spec/FIX44.xml",
        "../+_repo_rules2+quickfix/spec/FIX44.xml",
    };
    for (const auto& c : candidates) {
        if (std::ifstream(c).good())
            return c;
    }
    namespace fs = std::filesystem;
    for (auto& entry : fs::recursive_directory_iterator(".")) {
        if (entry.path().filename() == "FIX44.xml" &&
            entry.path().string().find("quickfix") != std::string::npos)
            return entry.path().string();
    }
    throw std::runtime_error("Could not locate FIX44.xml dictionary in runfiles");
}

// Write a QF config with FIX.4.4 for multileg message support.
inline std::string qfWriteMultilegConfig(int port)
{
    std::string path = "/tmp/qf_bench_multileg_" + std::to_string(port) + ".cfg";
    std::ofstream out(path);
    out << "[DEFAULT]\n"
        << "ConnectionType=acceptor\n"
        << "BeginString=FIX.4.4\n"
        << "SenderCompID=ACCEPTOR\n"
        << "TargetCompID=INITIATOR\n"
        << "SocketAcceptPort=" << port << "\n"
        << "StartTime=00:00:00\n"
        << "EndTime=00:00:00\n"
        << "NonStopSession=Y\n"
        << "HeartBtInt=60\n"
        << "CheckLatency=N\n"
        << "UseDataDictionary=Y\n"
        << "DataDictionary=" << qfFIX44DictionaryPath() << "\n"
        << "FileStorePath=/tmp/qf_bench_store_" << port << "\n"
        << "\n[SESSION]\n"
        << "BeginString=FIX.4.4\n"
        << "SenderCompID=ACCEPTOR\n"
        << "TargetCompID=INITIATOR\n";
    out.close();
    return path;
}

inline std::vector<BenchmarkResult> runQFMultilegRoundTripBenchmarks()
{
    constexpr int WARMUP  =  50;
    constexpr int MEASURE = 500;

    const int port = qfGetAvailablePort();
    const std::string configPath = qfWriteMultilegConfig(port);
    const std::string storePath  = "/tmp/qf_bench_store_" + std::to_string(port);

    std::filesystem::create_directories(storePath);

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

    // Perform logon with FIX.4.4 BeginString
    {
        auto logonMsg = qfBuildRawMessage("FIX.4.4", {
            {35, "A"},
            {49, "INITIATOR"},
            {56, "ACCEPTOR"},
            {34, "1"},
            {52, qfTimestamp()},
            {108, "60"},
            {98, "0"},
        });
        client.sendRaw(logonMsg);
        auto response = client.receiveMessage(std::chrono::seconds(3));
        if (response.empty() || response.find("35=A") == std::string::npos) {
            std::cerr << "[QF MultilegRT] Logon failed\n";
            acceptor.stop();
            std::filesystem::remove_all(storePath);
            std::filesystem::remove(configPath);
            return {};
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    int seqNum = 2;

    auto sendMultileg = [&](BenchmarkTimer& timer, bool timed) {
        const std::string clOrdID = std::to_string(seqNum);

        if (timed) timer.start();

        // Field order must match FIX 4.4 dictionary for repeating group parsing
        client.sendRaw(qfBuildRawMessage("FIX.4.4", {
            {35,  "AB"},
            {49,  "INITIATOR"},
            {56,  "ACCEPTOR"},
            {34,  std::to_string(seqNum++)},
            {52,  qfTimestamp()},
            {11,  clOrdID},             // ClOrdID
            {54,  "1"},                 // Side
            {55,  "SPREAD"},            // Symbol
            // Repeating group: 3 legs
            {555, "3"},                 // NoLegs
            {600, "AAPL"},              // LegSymbol
            {624, "1"},                 // LegSide
            {687, "100"},               // LegQty
            {600, "GOOG"},              // LegSymbol
            {624, "2"},                 // LegSide
            {687, "200"},               // LegQty
            {600, "MSFT"},              // LegSymbol
            {624, "1"},                 // LegSide
            {687, "300"},               // LegQty
            {60,  qfTimestamp()},        // TransactTime
            {38,  "1000"},              // OrderQty
            {40,  "2"},                 // OrdType
        }));

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
