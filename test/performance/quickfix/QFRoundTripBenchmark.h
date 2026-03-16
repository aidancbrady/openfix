#pragma once

#include <Application.h>
#include <FileStore.h>
#include <Log.h>
#include <SessionSettings.h>
#include <SocketAcceptor.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "BenchmarkFramework.h"
#include "QFNetworkThroughputBenchmark.h"  // for QFBenchApp
#include "QFUtils.h"

namespace perf {

inline std::vector<BenchmarkResult> runQFRoundTripBenchmarks()
{
    constexpr int WARMUP  =  50;
    constexpr int MEASURE = 500;

    int port = qfGetAvailablePort();
    std::string configPath = qfWriteConfig(port);
    std::string storePath  = "/tmp/qf_bench_store_" + std::to_string(port);

    std::filesystem::create_directories(storePath);

    QFBenchApp app;
    FIX::SessionSettings settings(configPath);
    FIX::FileStoreFactory storeFactory(settings);
    FIX::SocketAcceptor acceptor(app, storeFactory, settings);

    acceptor.start();

    QFRawClient client;
    if (!client.connectWithRetry(port, std::chrono::seconds(5))) {
        std::cerr << "[QF RoundTrip] Failed to connect\n";
        acceptor.stop();
        std::filesystem::remove_all(storePath);
        std::filesystem::remove(configPath);
        return {};
    }

    if (!client.performLogon()) {
        std::cerr << "[QF RoundTrip] Logon failed\n";
        acceptor.stop();
        std::filesystem::remove_all(storePath);
        std::filesystem::remove(configPath);
        return {};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    int seqNum = 2;  // logon used seq 1

    auto doRoundTrip = [&](BenchmarkTimer& timer, bool timed) {
        const std::string testReqID = std::to_string(seqNum);

        if (timed) timer.start();

        // Send TestRequest (35=1) with TestReqID.
        client.sendMessage("1", seqNum++, {{112, testReqID}});

        // Receive messages until we get the Heartbeat matching our TestReqID.
        while (true) {
            auto raw = client.receiveMessage(std::chrono::milliseconds(500));
            if (raw.empty()) {
                if (timed) timer.stop();
                return false;
            }
            auto tags = QFRawClient::parseTags(raw);
            // MsgType=0 (Heartbeat) with matching TestReqID (tag 112)
            if (tags[35] == "0" && tags[112] == testReqID)
                break;
        }

        if (timed) timer.stop();
        return true;
    };

    // Warmup
    BenchmarkTimer dummy;
    dummy.reserve(WARMUP);
    for (int i = 0; i < WARMUP; ++i)
        doRoundTrip(dummy, false);

    // Measurement
    BenchmarkTimer timer;
    timer.reserve(MEASURE);
    for (int i = 0; i < MEASURE; ++i)
        doRoundTrip(timer, true);

    client.close();
    acceptor.stop();
    std::filesystem::remove_all(storePath);
    std::filesystem::remove(configPath);

    return { timer.finalize("RoundTrip/TestReq-HB") };
}

} // namespace perf
