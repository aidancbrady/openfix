#include <openfix/Dictionary.h>
#include <openfix/Utils.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "BenchmarkFixtures.h"
#include "SessionTestHarness.h"

namespace {

int readEnvInt(const char* name, int fallback)
{
    if (const char* value = std::getenv(name))
        return std::stoi(value);
    return fallback;
}

std::string readEnvString(const char* name, std::string fallback)
{
    if (const char* value = std::getenv(name))
        return value;
    return fallback;
}

std::string buildRawCase(const std::string& which)
{
    const std::string ts = Utils::getUTCTimestamp();

    if (which == "heartbeat") {
        return fix_test::buildRawMessage(
            std::string(perf::bench::kBenchmarkBeginString),
            perf::bench::heartbeatWireFields(1, ts)
        );
    }

    if (which == "nos") {
        return fix_test::buildRawMessage(
            std::string(perf::bench::kBenchmarkBeginString),
            perf::bench::newOrderSingleWireFields(1, ts)
        );
    }

    if (which == "multileg") {
        perf::bench::RawFieldList fields = {{35, "AB"}};
        perf::bench::applyFields(perf::bench::sessionHeaderFields("1", ts),
            [&](int tag, const std::string& value) { fields.emplace_back(tag, value); });
        perf::bench::applyFields(perf::bench::newOrderMultilegBodyFields("CLORD-1", ts),
            [&](int tag, const std::string& value) { fields.emplace_back(tag, value); });
        return fix_test::buildRawMessage(std::string(perf::bench::kBenchmarkBeginString), fields);
    }

    throw std::runtime_error("Unknown OPENFIX_PARSE_CASE: " + which);
}

} // namespace

int main()
{
    auto dict = DictionaryRegistry::instance().load(std::string(perf::bench::kBenchmarkDictionaryPath));

    SessionSettings settings;
    settings.setBool(SessionSettings::LOUD_PARSING, false);

    const std::string parseCase = readEnvString("OPENFIX_PARSE_CASE", "multileg");
    const int warmup = readEnvInt("OPENFIX_PARSE_WARMUP", 100'000);
    const int measure = readEnvInt("OPENFIX_PARSE_MEASURE", 1'000'000);
    const std::string raw = buildRawCase(parseCase);

    for (int i = 0; i < warmup; ++i) {
        auto text = raw;
        auto msg = dict->parse(settings, std::move(text));
        (void)msg;
    }

    for (int i = 0; i < measure; ++i) {
        auto text = raw;
        auto msg = dict->parse(settings, std::move(text));
        (void)msg;
    }

    std::cerr << "parse-profile case=" << parseCase << " warmup=" << warmup << " measure=" << measure << "\n";
    return 0;
}
