#pragma once

#include <openfix/Application.h>
#include <openfix/Fields.h>
#include <openfix/Utils.h>

#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "BenchmarkFixtures.h"
#include "BenchmarkFramework.h"
#include "SessionTestHarness.h"

namespace perf {

class ThroughputTracker : public SessionDelegate
{
public:
    void reset()
    {
        m_count.store(0, std::memory_order_relaxed);
        m_firstNs.store(0, std::memory_order_relaxed);
        m_lastNs.store(0, std::memory_order_relaxed);
    }

    int count() const
    {
        return m_count.load(std::memory_order_relaxed);
    }

    double elapsedSeconds() const
    {
        const int64_t first = m_firstNs.load(std::memory_order_relaxed);
        const int64_t last = m_lastNs.load(std::memory_order_relaxed);
        if (first <= 0 || last <= first)
            return 0.0;
        return static_cast<double>(last - first) / 1e9;
    }

    void onAdminMessage(Session&, const Message& msg) override
    {
        if (msg.getHeader().getField(FIELD::MsgType) != "0")
            return;

        const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        int64_t expected = 0;
        (void)m_firstNs.compare_exchange_strong(expected, now, std::memory_order_relaxed);
        m_lastNs.store(now, std::memory_order_relaxed);
        m_count.fetch_add(1, std::memory_order_relaxed);
    }

private:
    std::atomic<int> m_count{0};
    std::atomic<int64_t> m_firstNs{0};
    std::atomic<int64_t> m_lastNs{0};
};

// Build acceptor settings tuned for benchmarking:
// - long heartbeat interval so the server doesn't send proactive heartbeats mid-test
// - extended sending-time threshold so pre-built messages are accepted
inline SessionSettings makeAcceptorBenchSettings(int port)
{
    auto s = fix_test::makeAcceptorSettings(port);
    s.setString(SessionSettings::BEGIN_STRING, std::string(bench::kBenchmarkBeginString));
    s.setLong(SessionSettings::HEARTBEAT_INTERVAL, bench::kHeartbeatIntervalSeconds);
    s.setLong(SessionSettings::SENDING_TIME_THRESHOLD, bench::kSendingTimeThresholdSeconds);
    return s;
}

inline std::vector<BenchmarkResult> runNetworkThroughputBenchmarks()
{
    constexpr int N = bench::kNetworkThroughputMessageCount;

    bench::resetOpenfixStoreDir();

    int port = fix_test::getAvailablePort();

    Application app;
    auto session = app.createSession("bench", makeAcceptorBenchSettings(port));
    auto tracker = std::make_shared<ThroughputTracker>();
    session->setDelegate(tracker);
    app.start();

    fix_test::RawFIXClient client;
    if (!client.connectWithRetry(port, std::chrono::seconds(5))) {
        std::cerr << "[Network/Throughput] Failed to connect\n";
        app.stop();
        bench::resetOpenfixStoreDir();
        return {};
    }

    if (!client.performLogon("INITIATOR", "ACCEPTOR", 1, 30, std::string(bench::kBenchmarkBeginString))) {
        std::cerr << "[Network/Throughput] Logon failed\n";
        app.stop();
        bench::resetOpenfixStoreDir();
        return {};
    }

    // Wait until the acceptor has processed the logon (targetSeqNum advances to 2).
    if (!fix_test::waitFor([&] { return session->getTargetSeqNum() >= 2; }, std::chrono::seconds(5))) {
        std::cerr << "[Network/Throughput] Logon not received by server\n";
        app.stop();
        bench::resetOpenfixStoreDir();
        return {};
    }

    // Pre-build all N messages with sequential seq nums starting at 2.
    // Timestamps are set once here; all messages will be within the 300s threshold.
    const std::string ts = Utils::getUTCTimestamp();
    std::vector<std::string> msgs;
    msgs.reserve(N);
    for (int i = 0; i < N; ++i) {
        msgs.push_back(fix_test::buildRawMessage(
            std::string(bench::kBenchmarkBeginString),
            bench::heartbeatWireFields(2 + i, ts)
        ));
    }

    // Concatenate all messages into a single buffer so we can send with one
    // write(2) call.  This eliminates per-message syscall overhead and the
    // non-deterministic scheduler interleaving between the sender and reader
    // threads that caused bimodal throughput when using 10K individual send()s
    // on a TCP_NODELAY socket.
    const std::string bulk = bench::buildBulkPayload(msgs);

    // ---- Measurement ----
    tracker->reset();

    {
        const char* ptr = bulk.data();
        size_t rem = bulk.size();
        while (rem > 0) {
            const ssize_t n = ::send(client.fd(), ptr, rem, MSG_NOSIGNAL);
            if (n <= 0) break;
            ptr += n;
            rem -= static_cast<size_t>(n);
        }
    }

    bool ok = fix_test::waitFor(
        [&] { return tracker->count() >= N; },
        std::chrono::seconds(30)
    );

    if (!ok) {
        std::cerr << "[Network/Throughput] Timed out waiting for messages to be processed "
                  << "(got " << tracker->count() << " of " << N << ")\n";
    }

    double elapsed_sec = tracker->elapsedSeconds();

    client.close();
    app.stop();
    bench::resetOpenfixStoreDir();

    return { throughputResult("Network/Throughput", N, elapsed_sec) };
}

} // namespace perf
