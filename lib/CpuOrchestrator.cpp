#include "CpuOrchestrator.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include <pthread.h>
#include <sched.h>

static const char* roleToString(ThreadRole role)
{
    switch (role) {
        case ThreadRole::READER:      return "reader";
        case ThreadRole::UPDATE:      return "update";
        case ThreadRole::FILE_WRITER: return "file_writer";
        case ThreadRole::ADMIN:       return "admin";
        case ThreadRole::WORKER:      return "worker";
        case ThreadRole::TIMER:       return "timer";
    }
    return "unknown";
}

/// Parse a CPU list string like "0-3,8-11" or "2,3,4,5" into a sorted vector of CPU IDs.
std::vector<int> CpuOrchestrator::parseCpuList(const std::string& list)
{
    std::vector<int> result;
    std::istringstream stream(list);
    std::string token;

    while (std::getline(stream, token, ',')) {
        // trim whitespace
        const auto start = token.find_first_not_of(" \t\n\r");
        if (start == std::string::npos)
            continue;
        const auto end = token.find_last_not_of(" \t\n\r");
        token = token.substr(start, end - start + 1);

        const auto dash = token.find('-');
        if (dash != std::string::npos) {
            const int lo = std::stoi(token.substr(0, dash));
            const int hi = std::stoi(token.substr(dash + 1));
            for (int i = lo; i <= hi; ++i)
                result.push_back(i);
        } else {
            result.push_back(std::stoi(token));
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

std::vector<int> CpuOrchestrator::detectPhysicalCores()
{
    std::vector<int> physical;
    const std::string cpuBase = "/sys/devices/system/cpu";

    for (const auto& entry : std::filesystem::directory_iterator(cpuBase)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("cpu", 0) != 0 || name.size() < 4)
            continue;

        // extract CPU ID from "cpuN"
        int cpuId;
        try {
            cpuId = std::stoi(name.substr(3));
        } catch (...) {
            continue;
        }

        // check if online (cpu0 has no online file, treat as always online)
        const auto onlinePath = entry.path() / "online";
        if (std::filesystem::exists(onlinePath)) {
            std::ifstream f(onlinePath);
            int online = 0;
            f >> online;
            if (!online)
                continue;
        }

        // read thread_siblings_list to determine if this is a physical core
        const auto siblingsPath = entry.path() / "topology" / "thread_siblings_list";
        if (!std::filesystem::exists(siblingsPath))
            continue;

        std::ifstream f(siblingsPath);
        std::string siblingsStr;
        std::getline(f, siblingsStr);

        const auto siblings = parseCpuList(siblingsStr);
        if (!siblings.empty() && siblings[0] == cpuId)
            physical.push_back(cpuId);
    }

    std::sort(physical.begin(), physical.end());
    return physical;
}

std::vector<int> CpuOrchestrator::getCoresForNumaNode(int node)
{
    const std::string path = "/sys/devices/system/node/node" + std::to_string(node) + "/cpulist";
    if (!std::filesystem::exists(path))
        return {};

    std::ifstream f(path);
    std::string cpulist;
    std::getline(f, cpulist);
    return parseCpuList(cpulist);
}

int CpuOrchestrator::detectBestNumaNode(const std::vector<int>& physicalCores)
{
    const std::string nodeBase = "/sys/devices/system/node";
    if (!std::filesystem::exists(nodeBase))
        return -1;

    const std::set<int> physSet(physicalCores.begin(), physicalCores.end());

    int bestNode = -1;
    int bestCount = 0;

    for (const auto& entry : std::filesystem::directory_iterator(nodeBase)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("node", 0) != 0 || name.size() < 5)
            continue;

        int nodeId;
        try {
            nodeId = std::stoi(name.substr(4));
        } catch (...) {
            continue;
        }

        const auto nodeCores = getCoresForNumaNode(nodeId);
        int count = 0;
        for (const int core : nodeCores) {
            if (physSet.count(core))
                ++count;
        }

        if (count > bestCount || (count == bestCount && (bestNode < 0 || nodeId < bestNode))) {
            bestCount = count;
            bestNode = nodeId;
        }
    }

    return bestNode;
}

void CpuOrchestrator::initialize(const std::string& cpuCores, bool avoidHT, int numaNode)
{
    // explicit core list takes precedence
    if (!cpuCores.empty()) {
        s_corePool = parseCpuList(cpuCores);
        if (!s_corePool.empty()) {
            s_enabled = true;
            std::ostringstream oss;
            for (size_t i = 0; i < s_corePool.size(); ++i) {
                if (i > 0) oss << ',';
                oss << s_corePool[i];
            }
            LOG_INFO("CPU affinity enabled (explicit): cores=[" << oss.str() << "]");
        }
        return;
    }

    // auto-detect mode
    if (!avoidHT)
        return;

    auto physicalCores = detectPhysicalCores();
    if (physicalCores.empty()) {
        LOG_WARN("CpuAvoidHT enabled but failed to detect physical cores");
        return;
    }

    LOG_INFO("Detected " << physicalCores.size() << " physical cores");

    // NUMA filtering
    int selectedNode = -1;

    if (numaNode >= 0) {
        selectedNode = numaNode;
    } else {
        // auto-detect: pick the node with the most physical cores
        selectedNode = detectBestNumaNode(physicalCores);
    }

    if (selectedNode >= 0) {
        const auto nodeCores = getCoresForNumaNode(selectedNode);
        const std::set<int> nodeSet(nodeCores.begin(), nodeCores.end());

        std::vector<int> filtered;
        for (const int core : physicalCores) {
            if (nodeSet.count(core))
                filtered.push_back(core);
        }

        if (filtered.empty()) {
            LOG_WARN("NUMA node " << selectedNode << " has no physical cores, using all physical cores");
        } else {
            physicalCores = std::move(filtered);
            LOG_INFO("Selected NUMA node " << selectedNode << " with " << physicalCores.size() << " physical cores");
        }
    }

    s_corePool = std::move(physicalCores);
    s_enabled = true;

    std::ostringstream oss;
    for (size_t i = 0; i < s_corePool.size(); ++i) {
        if (i > 0) oss << ',';
        oss << s_corePool[i];
    }
    LOG_INFO("CPU affinity enabled (auto): cores=[" << oss.str() << "]");
}

int CpuOrchestrator::bind(ThreadRole role)
{
    if (!s_enabled || s_corePool.empty())
        return -1;

    const size_t idx = s_nextCore.fetch_add(1, std::memory_order_relaxed);
    if (idx >= s_corePool.size()) {
        LOG_WARN("No more cores available for " << roleToString(role) << " thread (pool exhausted)");
        return -1;
    }

    const int core = s_corePool[idx];

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);

    const int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        LOG_ERROR("Failed to bind " << roleToString(role) << " thread to core " << core << ": " << strerror(ret));
        return -1;
    }

    LOG_INFO("Bound " << roleToString(role) << " thread to core " << core);
    return core;
}
