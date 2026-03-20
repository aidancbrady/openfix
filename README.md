# openfix

A high-performance FIX engine in modern C++ built for low-latency electronic trading infrastructure.

openfix delivers a complete [FIX protocol](https://www.fixtrading.org/) implementation — session management, message persistence, TLS, and real-time monitoring — on top of an epoll-driven architecture tuned for microsecond-level latency.

## Features

- **Full FIX Protocol** — Header/body/trailer parsing, validation, and serialization with configurable XML-based dictionaries
- **Session Management** — Logon/logout state machine, heartbeat monitoring, sequence number tracking, and automatic message recovery via resend requests
- **Acceptor & Initiator** — Dual-role support for both server and client FIX endpoints
- **High-Performance I/O** — Multi-threaded epoll with non-blocking sockets, TCP_NODELAY/TCP_QUICKACK, vectored writes, and configurable reader threads
- **TLS/SSL** — BoringSSL-backed secure connections with certificate validation, client certs, and custom CA bundles
- **CPU Orchestrator** — Thread-to-core affinity pinning with automatic physical core detection, NUMA awareness, and SMT sibling avoidance
- **Lock-Free Worker Queues** — `moodycamel::ConcurrentQueue` powers the optional dispatcher/timer infrastructure
- **SIMD Acceleration** — AVX2/SSE2-optimized FIX checksum (32 bytes/cycle via `_mm256_sad_epu8`), SIMD-accelerated `memchr` field scanning in the parse loop
- **Message Persistence** — File-based message caching and logging per session for sequence recovery and audit trails
- **Admin Dashboard** — Built-in Crow web interface for real-time session monitoring, sequence management, and diagnostics
- **Zero-Copy Parsing** — `string_view`-based message parsing to minimize allocations on the hot path

## Building

openfix uses [Bazel](https://bazel.build/) for hermetic, reproducible builds. The repo is configured for `gnu++23`, so you will need a C++23-capable compiler.

```bash
# Build the core library
bazel build //src:openfix

# Build and run the example application
bazel build //example:openfix-example

# Run the test suite
bazel test //test:openfix-test

# Run performance benchmarks
bazel run //test/performance:openfix-perf
```

## Quick Start

The example application demonstrates a simple acceptor/initiator pair communicating over FIX 4.2.

**Terminal 1 — Start the acceptor:**
```bash
bazel run //example:openfix-example -- acceptor
```

**Terminal 2 — Start the initiator:**
```bash
bazel run //example:openfix-example -- initiator
```

### Programmatic Usage

```cpp
#include <openfix/Application.h>

Application app;
SessionSettings settings;

settings.setString(SessionSettings::SESSION_TYPE_STR, "acceptor");
settings.setString(SessionSettings::BEGIN_STRING, "FIX.4.2");
settings.setString(SessionSettings::SENDER_COMP_ID, "SERVER");
settings.setString(SessionSettings::TARGET_COMP_ID, "CLIENT");
settings.setString(SessionSettings::FIX_DICTIONARY, "/path/to/FIXDictionary.xml");
settings.setLong(SessionSettings::ACCEPT_PORT, 12121);

app.createSession("MY_SESSION", settings);
app.start();
```

## Configuration

openfix is configured through `PlatformSettings` (global) and `SessionSettings` (per-session).

### Platform Settings

| Setting | Default | Description |
|---|---|---|
| `InputThreads` | `1` | Number of epoll reader threads |
| `SocketSendBufSize` | OS default | TCP send buffer size |
| `SocketRecvBufSize` | OS default | TCP receive buffer size |
| `UpdateDelay` | `1000` | Session update interval (ms) |
| `EpollTimeout` | `1000` | Epoll wait timeout (ms) |
| `LogPath` | `./log` | Directory for log output |
| `DataPath` | `./data` | Directory for persistent data |
| `AdminWebsitePort` | `51234` | Admin dashboard HTTP port (`0` to disable) |
| `CpuCores` | auto | Explicit core list (e.g. `2,3,4,5`) |
| `CpuAvoidHT` | `false` | Skip SMT siblings in auto-detection |
| `CpuNumaNode` | `-1` | NUMA node affinity (`-1` = auto) |

### Session Settings

| Setting | Default | Description |
|---|---|---|
| `BeginString` | — | FIX version (e.g. `FIX.4.2`) |
| `SenderCompID` | — | Sender identifier |
| `TargetCompID` | — | Target identifier |
| `TestSession` | `false` | Mark the session as a FIX test session |
| `FIXDictionary` | — | Path to FIX dictionary XML |
| `SessionType` | — | `acceptor` or `initiator` |
| `AcceptPort` | — | Listening port (acceptor) |
| `ConnectHost` / `ConnectPort` | — | Remote endpoint (initiator) |
| `HeartbeatInterval` | `10` | Heartbeat interval (seconds) |
| `LogonInterval` | `10` | Logon retry interval (seconds) |
| `ReconnectInterval` | `10` | Reconnect interval (seconds) |
| `ConnectTimeout` | `5000` | Connection timeout (ms) |
| `ResetSeqNumOnLogon` | `false` | Reset sequence numbers on each logon |
| `AllowResetSeqNumFlag` | `false` | Accept incoming `ResetSeqNumFlag` on logon |
| `SendNextExpectedMsgSeqNum` | `true` | Include tag 789 in logon |
| `TCPNoDelay` | `true` | Enable `TCP_NODELAY` on session sockets |
| `TCPQuickAck` | `true` | Enable `TCP_QUICKACK` on session sockets |
| `RelaxedParsing` | `false` | Tolerate more malformed input during parse |
| `LoudParsing` | `true` | Log parse/validation errors verbosely |
| `ValidateRequiredFields` | `false` | Enforce required dictionary fields |
| `TestRequestThreshold` | `2.0` | Heartbeat multiplier before sending a test request |
| `SendingTimeThreshold` | `10` | Allowed inbound sending-time skew (seconds) |
| `TLSEnabled` | `false` | Enable TLS |
| `TLSVerifyPeer` | `true` | Verify peer certificate |
| `TLSRequireClientCert` | `false` | Require client certificates on TLS acceptors |
| `TLSCAFile` | — | CA certificate bundle path |
| `TLSCertFile` | — | Client/server certificate path |
| `TLSKeyFile` | — | Private key path |
| `TLSServerName` | — | SNI / TLS hostname override for initiators |

The tables above cover the most important settings; see `src/Config.h` for the full list.

## Architecture

```
Application
├── Session (FIX state machine, one per configured endpoint)
│   ├── Dictionary (FIX protocol definition)
│   ├── NetworkHandler (network I/O delegate)
│   ├── IFIXCache (message caching for resends)
│   ├── IFIXLogger (per-session message logging)
│   └── IFIXStore (persistent session state)
└── Network (I/O orchestrator)
    └── ReaderThread (epoll event loop, one per InputThreads)
        ├── ConnectionHandle (per-socket state)
        ├── ReadBuffer (inbound accumulation)
        └── WriteBuffer (outbound batching)
```

**Message flow:** The ReaderThread receives data via epoll, parses complete FIX messages, and delivers them to the owning Session through the NetworkDelegate interface. The Session validates checksums, sequence numbers, and timestamps before dispatching to application-level callbacks. Outbound messages go back through the NetworkHandler; on non-TLS sockets they are sent inline when possible, otherwise they are queued and flushed by the ReaderThread with vectored writes.

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| [spdlog](https://github.com/gabime/spdlog) | 1.13.0 | Structured logging |
| [pugixml](https://github.com/zeux/pugixml) | 1.14 | FIX dictionary XML parsing |
| [BoringSSL](https://github.com/google/boringssl) | 0.20240930.0 | TLS/SSL |
| [Crow](https://github.com/CrowCpp/Crow) | 1.0 (pinned git override) | Admin web dashboard |
| [concurrentqueue](https://github.com/cameron314/concurrentqueue) | 1.0.4 | Lock-free MPMC queues |
| [unordered_dense](https://github.com/martinus/unordered_dense) | 4.8.1 | High-performance hash maps |
| [Google Test](https://github.com/google/googletest) | 1.17.0 | Testing framework |

All dependencies are fetched automatically by Bazel via `MODULE.bazel` — no manual installation required.

## Performance

Benchmarks are available under `test/performance/` and include CPU-only parse / serialize / checksum tests plus network comparison tests against [QuickFIX](https://github.com/quickfix/quickfix).

```bash
# Run openfix benchmarks
bazel run //test/performance:openfix-perf

# Run QuickFIX comparison benchmarks
bazel run //test/performance/quickfix:quickfix-perf

# Run isolated benchmarks with CPU pinning (best results with sudo)
./test/performance/run_isolated.sh
```

### Results: openfix vs QuickFIX

Recent isolated run on Linux (x86_64), optimized build (`-c opt`), primary cores auto-detected by `run_isolated.sh`, 5 iterations with warmup. Median values reported.

#### Network (full stack, isolated)

| Benchmark | openfix | QuickFIX | Result |
|---|---|---|---|
| Throughput (median, msg/s) | **2,905,193** | 582,655 | **openfix 5.0x higher** |
| RoundTrip TestReq-HB (median avg, µs) | **8.145** | 11.035 | **openfix 26.2% lower** |
| RoundTrip Multileg-AB-8 (median avg, µs) | **10.088** | 15.128 | **openfix 33.3% lower** |

openfix leads across all three network benchmarks — aggregate throughput, simple `TestRequest -> Heartbeat` round trip, and the multileg application-flow round trip. Exact numbers will vary with CPU governor, kernel scheduling, and whether the script is run with `sudo`.

> **Note:** For the most stable results, run the isolated benchmark script with `sudo` to enable CPU frequency pinning: `sudo ./test/performance/run_isolated.sh`. The round-trip latency numbers above reflect a non-pinned run; with frequency pinning and CPU isolation, typical TestReq-HB round-trip averages are in the **8–9 µs** range.

#### CPU (parse and serialize, no network I/O)

Recent CPU-only run on this machine: optimized build (`-c opt`), `taskset -c 0`, 5 iterations per engine, median values reported.

| Benchmark | openfix Msgs/sec | QuickFIX Msgs/sec | openfix Avg (µs) | QuickFIX Avg (µs) | Result |
|---|---|---|---|---|---|
| Parse/Heartbeat | **6,037,725** | 2,692,040 | **0.166** | 0.371 | **openfix 2.24x higher throughput, 55.3% lower avg latency** |
| Parse/NewOrderSingle | **4,282,808** | 1,343,856 | **0.233** | 0.744 | **openfix 3.19x higher throughput, 68.7% lower avg latency** |
| Serialize/Heartbeat | **4,914,196** | 2,390,644 | **0.203** | 0.418 | **openfix 2.06x higher throughput, 51.4% lower avg latency** |
| Serialize/NewOrderSingle | **2,734,727** | 1,311,477 | **0.366** | 0.762 | **openfix 2.09x higher throughput, 52.0% lower avg latency** |
