#pragma once

#include <openfix/Application.h>
#include <openfix/Fields.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "BenchmarkFramework.h"
#include "NetworkThroughputBenchmark.h"  // for makeAcceptorBenchSettings
#include "SessionTestHarness.h"

namespace perf {

inline std::vector<BenchmarkResult> runRoundTripBenchmarks()
{
    constexpr int WARMUP  =  50;
    constexpr int MEASURE = 500;

    std::filesystem::remove_all("./data");

    int port = fix_test::getAvailablePort();

    Application app;
    app.createSession("bench", makeAcceptorBenchSettings(port));
    app.start();

    fix_test::RawFIXClient client;
    if (!client.connectWithRetry(port, std::chrono::seconds(5))) {
        std::cerr << "[RoundTrip] Failed to connect\n";
        app.stop();
        std::filesystem::remove_all("./data");
        return {};
    }

    if (!client.performLogon()) {
        std::cerr << "[RoundTrip] Logon failed\n";
        app.stop();
        std::filesystem::remove_all("./data");
        return {};
    }

    auto session = app.getSession("bench");
    if (!fix_test::waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(5))) {
        std::cerr << "[RoundTrip] Server did not receive logon\n";
        app.stop();
        std::filesystem::remove_all("./data");
        return {};
    }

    int seqNum = 2;  // logon used seq 1

    auto doRoundTrip = [&](BenchmarkTimer& timer, bool timed) {
        const std::string testReqID = std::to_string(seqNum);

        if (timed) timer.start();

        client.sendMessage("1", seqNum++, {{FIELD::TestReqID, testReqID}});

        // Receive messages until we get the Heartbeat matching our TestReqID.
        // Proactive server heartbeats (no TestReqID) are discarded.
        while (true) {
            auto raw = client.receiveMessage(std::chrono::milliseconds(500));
            if (raw.empty()) {
                // timeout — skip this iteration
                if (timed) timer.stop();
                return false;
            }
            auto tags = fix_test::RawFIXClient::parseTags(raw);
            if (tags[FIELD::MsgType] == "0" && tags[FIELD::TestReqID] == testReqID)
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
    app.stop();
    std::filesystem::remove_all("./data");

    return { timer.finalize("RoundTrip/TestReq-HB") };
}

} // namespace perf
