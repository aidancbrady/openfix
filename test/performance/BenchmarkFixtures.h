#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace perf::bench {

using RawFieldList = std::vector<std::pair<int, std::string>>;

inline constexpr std::string_view kBenchmarkDictionaryPath = "test/FIXDictionary.xml";
inline constexpr std::string_view kBenchmarkBeginString    = "FIX.4.4";
inline constexpr std::string_view kInitiatorCompID         = "INITIATOR";
inline constexpr std::string_view kAcceptorCompID          = "ACCEPTOR";

inline constexpr long kHeartbeatIntervalSeconds      = 60L;
inline constexpr long kSendingTimeThresholdSeconds   = 300L;
inline constexpr int  kNetworkThroughputMessageCount = 10'000;

inline std::filesystem::path benchmarkRuntimeRoot()
{
    return "/tmp/openfix-perf";
}

inline std::filesystem::path openfixStoreDir()
{
    return benchmarkRuntimeRoot() / "openfix-data";
}

inline std::filesystem::path quickfixRuntimeRoot()
{
    return benchmarkRuntimeRoot() / "quickfix";
}

inline std::filesystem::path quickfixConfigPath(std::string_view name, int port)
{
    return quickfixRuntimeRoot() / (std::string(name) + "-" + std::to_string(port) + ".cfg");
}

inline std::filesystem::path quickfixStoreDir(int port)
{
    return quickfixRuntimeRoot() / ("store-" + std::to_string(port));
}

inline void resetOpenfixStoreDir()
{
    std::filesystem::remove_all(openfixStoreDir());
    std::filesystem::create_directories(openfixStoreDir());
}

inline void resetQuickfixStoreDir(int port)
{
    std::filesystem::create_directories(quickfixRuntimeRoot());
    std::filesystem::remove_all(quickfixStoreDir(port));
    std::filesystem::create_directories(quickfixStoreDir(port));
}

inline std::string buildBulkPayload(const std::vector<std::string>& msgs)
{
    size_t total = 0;
    for (const auto& msg : msgs)
        total += msg.size();

    std::string bulk;
    bulk.reserve(total);
    for (const auto& msg : msgs)
        bulk += msg;
    return bulk;
}

template <typename Setter>
inline void applyFields(const RawFieldList& fields, Setter&& setter)
{
    for (const auto& [tag, value] : fields)
        setter(tag, value);
}

inline RawFieldList sessionHeaderFields(const std::string& seqNum, const std::string& sendingTime)
{
    return {
        {49, std::string(kInitiatorCompID)},
        {56, std::string(kAcceptorCompID)},
        {34, seqNum},
        {52, sendingTime},
    };
}

inline RawFieldList heartbeatWireFields(int seqNum, const std::string& sendingTime)
{
    RawFieldList fields = {{35, "0"}};
    applyFields(sessionHeaderFields(std::to_string(seqNum), sendingTime),
        [&](int tag, const std::string& value) { fields.emplace_back(tag, value); });
    return fields;
}

inline RawFieldList newOrderSingleBodyFields(const std::string& transactTime)
{
    return {
        {11, "ORDER123456"},
        {21, "1"},
        {55, "AAPL"},
        {54, "1"},
        {60, transactTime},
        {40, "2"},
        {38, "100"},
        {44, "150.00"},
        {59, "0"},
    };
}

inline RawFieldList newOrderSingleWireFields(int seqNum, const std::string& sendingTime)
{
    RawFieldList fields = {{35, "D"}};
    applyFields(sessionHeaderFields(std::to_string(seqNum), sendingTime),
        [&](int tag, const std::string& value) { fields.emplace_back(tag, value); });
    applyFields(newOrderSingleBodyFields(sendingTime),
        [&](int tag, const std::string& value) { fields.emplace_back(tag, value); });
    return fields;
}

inline RawFieldList newOrderMultilegBodyFields(const std::string& clOrdID, const std::string& transactTime)
{
    return {
        {11,  clOrdID},
        {54,  "1"},
        {55,  "SPREAD"},
        {60,  transactTime},
        {40,  "2"},
        {38,  "1000"},
        {555, "3"},
        {600, "AAPL"},
        {624, "1"},
        {687, "100"},
        {600, "GOOG"},
        {624, "2"},
        {687, "200"},
        {600, "MSFT"},
        {624, "1"},
        {687, "300"},
    };
}

} // namespace perf::bench
