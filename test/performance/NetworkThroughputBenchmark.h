#pragma once

#include <openfix/Application.h>
#include <openfix/Utils.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "BenchmarkFramework.h"
#include "SessionTestHarness.h"

namespace perf {

// Build acceptor settings tuned for benchmarking:
// - long heartbeat interval so the server doesn't send proactive heartbeats mid-test
// - extended sending-time threshold so pre-built messages are accepted
inline SessionSettings makeAcceptorBenchSettings(int port)
{
    auto s = fix_test::makeAcceptorSettings(port);
    s.setLong(SessionSettings::HEARTBEAT_INTERVAL,      60L);
    s.setLong(SessionSettings::SENDING_TIME_THRESHOLD, 300L);
    return s;
}

inline std::vector<BenchmarkResult> runNetworkThroughputBenchmarks()
{
    constexpr int N = 10'000;

    std::filesystem::remove_all("./data");

    int port = fix_test::getAvailablePort();

    Application app;
    app.createSession("bench", makeAcceptorBenchSettings(port));
    app.start();

    auto session = app.getSession("bench");

    fix_test::RawFIXClient client;
    if (!client.connectWithRetry(port, std::chrono::seconds(5))) {
        std::cerr << "[Network/Throughput] Failed to connect\n";
        app.stop();
        std::filesystem::remove_all("./data");
        return {};
    }

    if (!client.performLogon()) {
        std::cerr << "[Network/Throughput] Logon failed\n";
        app.stop();
        std::filesystem::remove_all("./data");
        return {};
    }

    // Wait until the acceptor has processed the logon (targetSeqNum advances to 2).
    if (!fix_test::waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(5))) {
        std::cerr << "[Network/Throughput] Logon not received by server\n";
        app.stop();
        std::filesystem::remove_all("./data");
        return {};
    }

    // Pre-build all N messages with sequential seq nums starting at 2.
    // Timestamps are set once here; all messages will be within the 300s threshold.
    const std::string ts = Utils::getUTCTimestamp();
    std::vector<std::string> msgs;
    msgs.reserve(N);
    for (int i = 0; i < N; ++i) {
        msgs.push_back(fix_test::buildRawMessage("FIX.4.2", {
            {35, "0"},
            {49, "INITIATOR"},
            {56, "ACCEPTOR"},
            {34, std::to_string(2 + i)},
            {52, ts},
        }));
    }

    // ---- Measurement ----
    const int expectedSeqNum = 2 + N;

    auto start = std::chrono::steady_clock::now();

    for (const auto& msg : msgs)
        client.sendRaw(msg);

    bool ok = fix_test::waitFor(
        [&] { return session->getTargetSeqNum() >= expectedSeqNum; },
        std::chrono::seconds(30)
    );

    auto end = std::chrono::steady_clock::now();

    if (!ok) {
        std::cerr << "[Network/Throughput] Timed out waiting for messages to be processed "
                  << "(got " << session->getTargetSeqNum() << " of " << expectedSeqNum << ")\n";
    }

    double elapsed_sec = std::chrono::duration<double>(end - start).count();

    client.close();
    app.stop();
    std::filesystem::remove_all("./data");

    return { throughputResult("Network/Throughput", N, elapsed_sec) };
}

} // namespace perf
