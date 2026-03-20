#pragma once

#include <Application.h>
#include <FileStore.h>
#include <NullStore.h>
#include <Log.h>
#include <Session.h>
#include <SessionSettings.h>
#include <SocketAcceptor.h>

#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <cstdint>
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
    void reset()
    {
        msgCount.store(0, std::memory_order_relaxed);
        firstNs.store(0, std::memory_order_relaxed);
        lastNs.store(0, std::memory_order_relaxed);
    }

    int count() const
    {
        return msgCount.load(std::memory_order_relaxed);
    }

    double elapsedSeconds() const
    {
        const int64_t first = firstNs.load(std::memory_order_relaxed);
        const int64_t last = lastNs.load(std::memory_order_relaxed);
        if (first <= 0 || last <= first)
            return 0.0;
        return static_cast<double>(last - first) / 1e9;
    }

    void fromAdmin(const FIX::Message& msg, const FIX::SessionID&) override
    {
        FIX::MsgType msgType;
        msg.getHeader().getField(msgType);
        // Count all admin messages except logon/logout as "processed"
        if (msgType == "0") {
            const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            int64_t expected = 0;
            (void)firstNs.compare_exchange_strong(expected, now, std::memory_order_relaxed);
            lastNs.store(now, std::memory_order_relaxed);
            msgCount.fetch_add(1, std::memory_order_relaxed);
        }
    }

private:
    std::atomic<int> msgCount{0};
    std::atomic<int64_t> firstNs{0};
    std::atomic<int64_t> lastNs{0};
};

inline std::vector<BenchmarkResult> runQFNetworkThroughputBenchmarks()
{
    constexpr int N = bench::kNetworkThroughputMessageCount;

    int port = qfGetAvailablePort();
    std::string configPath = qfWriteConfig(port);
    std::string storePath  = bench::quickfixStoreDir(port).string();

    bench::resetQuickfixStoreDir(port);

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

    if (!client.performLogon("INITIATOR", "ACCEPTOR", 1, 30, std::string(bench::kBenchmarkBeginString))) {
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
        msgs.push_back(qfBuildRawMessage(
            std::string(bench::kBenchmarkBeginString),
            bench::heartbeatWireFields(2 + i, ts)
        ));
    }

    // Reset counter after logon.
    app.reset();

    const std::string bulk = bench::buildBulkPayload(msgs);

    {
        const char* ptr = bulk.data();
        size_t rem = bulk.size();
        while (rem > 0) {
            const ssize_t n = ::send(client.fd(), ptr, rem, MSG_NOSIGNAL);
            if (n <= 0)
                break;
            ptr += n;
            rem -= static_cast<size_t>(n);
        }
    }

    bool ok = qfWaitFor(
        [&] { return app.count() >= N; },
        std::chrono::seconds(30)
    );

    if (!ok) {
        std::cerr << "[QF Network/Throughput] Timed out (got "
                  << app.count() << " of " << N << ")\n";
    }

    double elapsed_sec = app.elapsedSeconds();

    client.close();
    acceptor.stop();
    std::filesystem::remove_all(storePath);
    std::filesystem::remove(configPath);

    return { throughputResult("Network/Throughput", N, elapsed_sec) };
}

} // namespace perf
