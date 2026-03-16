#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "Log.h"

enum class ThreadRole
{
    READER,
    UPDATE,
    FILE_WRITER,
    ADMIN,
    WORKER,
    TIMER,
};

class CpuOrchestrator
{
public:
    /// Initialize CPU affinity. Call once at startup before any threads are created.
    /// @param cpuCores   explicit core list (e.g. "2,3,4,5"), or empty for auto-detect
    /// @param avoidHT    if true and cpuCores is empty, auto-detect physical cores (skip HT siblings)
    /// @param numaNode   NUMA node to pin to (-1 = auto-detect best node)
    static void initialize(const std::string& cpuCores, bool avoidHT, int numaNode);

    /// Bind the calling thread to the next available core for the given role.
    /// Returns the core ID bound to, or -1 if binding is disabled or cores are exhausted.
    static int bind(ThreadRole role);

private:
    static std::vector<int> parseCpuList(const std::string& list);
    static std::vector<int> detectPhysicalCores();
    static std::vector<int> getCoresForNumaNode(int node);
    static int detectBestNumaNode(const std::vector<int>& physicalCores);

    static inline std::vector<int> s_corePool;
    static inline std::atomic<size_t> s_nextCore{0};
    static inline bool s_enabled = false;

    CREATE_LOGGER("CpuOrchestrator");
};
