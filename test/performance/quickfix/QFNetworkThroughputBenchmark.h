#pragma once

#include <Application.h>
#include <FileStore.h>
#include <NullStore.h>
#include <Log.h>
#include <Session.h>
#include <SessionSettings.h>
#include <SocketAcceptor.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "BenchmarkFramework.h"
#include "QFUtils.h"

namespace perf {

// Minimal Application that counts received app messages.
class QFBenchApp : public FIX::NullApplication
{
public:
    std::atomic<int> msgCount{0};

    void fromAdmin(const FIX::Message& msg, const FIX::SessionID&) override
    {
        FIX::MsgType msgType;
        msg.getHeader().getField(msgType);
        // Count all admin messages except logon/logout as "processed"
        if (msgType == "0" || msgType == "1")  // Heartbeat, TestRequest
            msgCount.fetch_add(1, std::memory_order_relaxed);
    }
};

inline std::vector<BenchmarkResult> runQFNetworkThroughputBenchmarks()
{
    constexpr int N = 10'000;

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
        std::cerr << "[QF Network/Throughput] Failed to connect\n";
        acceptor.stop();
        std::filesystem::remove_all(storePath);
        std::filesystem::remove(configPath);
        return {};
    }

    if (!client.performLogon()) {
        std::cerr << "[QF Network/Throughput] Logon failed\n";
        acceptor.stop();
        std::filesystem::remove_all(storePath);
        std::filesystem::remove(configPath);
        return {};
    }

    // Wait for server to process logon.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Pre-build all N heartbeat messages with sequential seq nums.
    const std::string ts = qfTimestamp();
    std::vector<std::string> msgs;
    msgs.reserve(N);
    for (int i = 0; i < N; ++i) {
        msgs.push_back(qfBuildRawMessage("FIX.4.2", {
            {35, "0"},
            {49, "INITIATOR"},
            {56, "ACCEPTOR"},
            {34, std::to_string(2 + i)},
            {52, ts},
        }));
    }

    // Reset counter after logon.
    app.msgCount.store(0, std::memory_order_relaxed);

    // ---- Measurement ----
    auto start = std::chrono::steady_clock::now();

    for (const auto& msg : msgs)
        client.sendRaw(msg);

    bool ok = qfWaitFor(
        [&] { return app.msgCount.load(std::memory_order_relaxed) >= N; },
        std::chrono::seconds(30)
    );

    auto end = std::chrono::steady_clock::now();

    if (!ok) {
        std::cerr << "[QF Network/Throughput] Timed out (got "
                  << app.msgCount.load() << " of " << N << ")\n";
    }

    double elapsed_sec = std::chrono::duration<double>(end - start).count();

    client.close();
    acceptor.stop();
    std::filesystem::remove_all(storePath);
    std::filesystem::remove(configPath);

    return { throughputResult("Network/Throughput", N, elapsed_sec) };
}

} // namespace perf
