#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace perf {

struct BenchmarkResult
{
    std::string name;
    uint64_t    iterations    = 0;
    double      msgs_per_sec  = 0.0;
    double      min_us        = 0.0;
    double      avg_us        = 0.0;
    double      max_us        = 0.0;
    double      p50_us        = 0.0;
    double      p95_us        = 0.0;
    double      p99_us        = 0.0;
    bool        has_latency   = false;
    bool        has_throughput = false;
};

class BenchmarkTimer
{
public:
    void reserve(size_t n) { m_samples.reserve(n); }

    void start() { m_start = std::chrono::steady_clock::now(); }

    void stop()
    {
        auto end = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(end - m_start).count();
        m_samples.push_back(us);
    }

    BenchmarkResult finalize(const std::string& name) const
    {
        BenchmarkResult r;
        r.name           = name;
        r.has_latency    = true;
        r.has_throughput = true;
        r.iterations     = m_samples.size();

        if (m_samples.empty())
            return r;

        std::vector<double> sorted = m_samples;
        std::sort(sorted.begin(), sorted.end());

        double total = std::accumulate(sorted.begin(), sorted.end(), 0.0);
        r.avg_us = total / sorted.size();
        r.min_us = sorted.front();
        r.max_us = sorted.back();
        r.p50_us = sorted[sorted.size() * 50 / 100];
        r.p95_us = sorted[sorted.size() * 95 / 100];
        r.p99_us = sorted[sorted.size() * 99 / 100];

        double total_sec  = total / 1e6;
        r.msgs_per_sec    = total_sec > 0.0 ? r.iterations / total_sec : 0.0;

        return r;
    }

private:
    std::chrono::steady_clock::time_point m_start;
    std::vector<double>                   m_samples;
};

// Run fn with per-iteration timing.
// warmup iterations are discarded; measure iterations are recorded.
inline BenchmarkResult run(
    const std::string&    name,
    uint64_t              warmup,
    uint64_t              measure,
    std::function<void()> fn)
{
    for (uint64_t i = 0; i < warmup; ++i)
        fn();

    BenchmarkTimer timer;
    timer.reserve(measure);
    for (uint64_t i = 0; i < measure; ++i) {
        timer.start();
        fn();
        timer.stop();
    }

    return timer.finalize(name);
}

template <typename PrepareFn, typename Fn>
inline BenchmarkResult runPrepared(
    const std::string&    name,
    uint64_t              warmup,
    uint64_t              measure,
    PrepareFn&&           prepare,
    Fn&&                  fn)
{
    for (uint64_t i = 0; i < warmup; ++i) {
        auto state = prepare();
        fn(std::move(state));
    }

    BenchmarkTimer timer;
    timer.reserve(measure);
    for (uint64_t i = 0; i < measure; ++i) {
        auto state = prepare();
        timer.start();
        fn(std::move(state));
        timer.stop();
    }

    return timer.finalize(name);
}

// Create a throughput-only result from a bulk timing measurement.
inline BenchmarkResult throughputResult(
    const std::string& name,
    uint64_t           iterations,
    double             elapsed_sec)
{
    BenchmarkResult r;
    r.name           = name;
    r.iterations     = iterations;
    r.has_throughput = true;
    r.has_latency    = false;
    r.msgs_per_sec   = elapsed_sec > 0.0 ? iterations / elapsed_sec : 0.0;
    return r;
}

// ---- Output formatting ----

static std::string fmtFixed(double v, int decimals = 3)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimals) << v;
    return ss.str();
}

static std::string fmtLargeInt(uint64_t v)
{
    std::string s = std::to_string(v);
    int n = static_cast<int>(s.size());
    for (int i = n - 3; i > 0; i -= 3)
        s.insert(static_cast<size_t>(i), ",");
    return s;
}

inline void printResults(const std::vector<BenchmarkResult>& results,
                        const std::string& engine = "openfix")
{
    constexpr int W_NAME  = 28;
    constexpr int W_ITERS = 12;
    constexpr int W_MPS   = 15;
    constexpr int W_LAT   =  9;

    auto pad = [](const std::string& s, int w, bool right) -> std::string {
        int space = w - static_cast<int>(s.size());
        if (space <= 0) return s;
        std::string p(static_cast<size_t>(space), ' ');
        return right ? (p + s) : (s + p);
    };

    auto printRow = [&](const std::string& name,
                        const std::string& iters,
                        const std::string& mps,
                        const std::string& min_,
                        const std::string& avg_,
                        const std::string& p50,
                        const std::string& p95,
                        const std::string& p99,
                        const std::string& max_) {
        std::cout
            << " " << pad(name,  W_NAME  - 1, false)
            << " | " << pad(iters, W_ITERS - 1, true)
            << " | " << pad(mps,   W_MPS   - 1, true)
            << " | " << pad(min_,  W_LAT   - 1, true)
            << " | " << pad(avg_,  W_LAT   - 1, true)
            << " | " << pad(p50,   W_LAT   - 1, true)
            << " | " << pad(p95,   W_LAT   - 1, true)
            << " | " << pad(p99,   W_LAT   - 1, true)
            << " | " << pad(max_,  W_LAT   - 1, true)
            << "\n";
    };

    auto printSep = [&]() {
        std::cout
            << std::string(W_NAME + 1, '-')  << "+"
            << std::string(W_ITERS + 1, '-') << "+"
            << std::string(W_MPS + 1, '-')   << "+"
            << std::string(W_LAT + 1, '-')   << "+"
            << std::string(W_LAT + 1, '-')   << "+"
            << std::string(W_LAT + 1, '-')   << "+"
            << std::string(W_LAT + 1, '-')   << "+"
            << std::string(W_LAT + 1, '-')   << "+"
            << std::string(W_LAT, '-')
            << "\n";
    };

    std::cout << "\n" << engine << " Performance Benchmark Suite\n";
    std::cout << std::string(engine.size() + 27, '=') << "\n\n";

    printRow("Benchmark", "Iters", "Msgs/sec",
             "Min(us)", "Avg(us)", "P50(us)", "P95(us)", "P99(us)", "Max(us)");
    printSep();

    std::string lastGroup;
    for (const auto& r : results) {
        auto slash = r.name.find('/');
        std::string group = (slash != std::string::npos) ? r.name.substr(0, slash) : r.name;
        if (!lastGroup.empty() && group != lastGroup)
            printSep();
        lastGroup = group;

        std::string iters = fmtLargeInt(r.iterations);
        std::string mps   = r.has_throughput ? fmtLargeInt(static_cast<uint64_t>(r.msgs_per_sec)) : "-";
        std::string min_  = r.has_latency ? fmtFixed(r.min_us) : "-";
        std::string avg_  = r.has_latency ? fmtFixed(r.avg_us) : "-";
        std::string p50   = r.has_latency ? fmtFixed(r.p50_us) : "-";
        std::string p95   = r.has_latency ? fmtFixed(r.p95_us) : "-";
        std::string p99   = r.has_latency ? fmtFixed(r.p99_us) : "-";
        std::string max_  = r.has_latency ? fmtFixed(r.max_us) : "-";

        printRow(r.name, iters, mps, min_, avg_, p50, p95, p99, max_);
    }
    printSep();

    if (engine == "openfix") {
        std::cout
            << "\nNotes:\n"
            << "  - Checksum:   SSE2-accelerated path on x86_64, at various payload sizes\n"
            << "  - Parse:      benchmark FIX dictionary parse from raw bytes, no network I/O\n"
            << "  - Serialize:  generic message build + setField() + toString(), no network I/O\n"
            << "  - Network:    full-stack ingestion: parse + seq validation + store + dispatch\n"
            << "  - RoundTrip:  TestRequest(35=1) -> Heartbeat(35=0) response, full network path\n"
            << "  - Latency in microseconds; '-' = not applicable for this benchmark type\n";
    } else {
        std::cout
            << "\nNotes:\n"
            << "  - Checksum:   scalar byte-by-byte loop (no SIMD), at various payload sizes\n"
            << "  - Parse:      benchmark FIX dictionary parse from raw bytes, no network I/O\n"
            << "  - Serialize:  generic message build + setField() + toString(), no network I/O\n"
            << "  - Network:    full-stack ingestion via SocketAcceptor\n"
            << "  - RoundTrip:  TestRequest(35=1) -> Heartbeat(35=0) response, full network path\n"
            << "  - Latency in microseconds; '-' = not applicable for this benchmark type\n";
    }
}

} // namespace perf
