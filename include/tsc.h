#pragma once

#include <cstdint>
#include <ctime>
#include <fstream>
#include <string>
#include <x86intrin.h>

namespace AL
{

// Returns the calibrated ns-per-tick constant derived by tsc_init().
// tsc_init() must be called before the first tsc_to_ns() call.
inline double& tsc_ns_per_tick()
{
    static double val = 0.0;
    return val;
}

// Reads /proc/cpuinfo and returns true only if both constant_tsc and
// nonstop_tsc are present. Guards against running on a machine where the
// required OS configuration has not been applied, which would cause
// ns_per_tick to drift silently mid-run.
inline bool tsc_check_cpu_flags()
{
    std::ifstream f("/proc/cpuinfo");
    if (!f.is_open())
        return false;

    std::string line;
    while (std::getline(f, line))
    {
        if (!line.starts_with("flags"))
            continue;
        return line.find("constant_tsc") != std::string::npos && line.find("nonstop_tsc") != std::string::npos;
    }
    return false;
}

// Calibrates TSC against CLOCK_MONOTONIC over a ~100ms busy-spin window.
// Must be called once at startup before any tsc_to_ns() call.
// Aborts if CPU flags are missing.
inline void tsc_init()
{
    if (!tsc_check_cpu_flags())
        std::abort();

    struct timespec ts0{};
    uint32_t aux = 0;

    clock_gettime(CLOCK_MONOTONIC, &ts0);
    const uint64_t tsc0 = __rdtscp(&aux);

    // Busy-spin for ~100ms
    struct timespec now{};
    do
    {
        clock_gettime(CLOCK_MONOTONIC, &now);
    }
    while ((now.tv_sec - ts0.tv_sec) * 1'000'000'000LL + (now.tv_nsec - ts0.tv_nsec) < 100'000'000LL);

    const uint64_t tsc1 = __rdtscp(&aux);

    const uint64_t elapsed_ticks = tsc1 - tsc0;
    const uint64_t elapsed_ns = static_cast<uint64_t>((now.tv_sec - ts0.tv_sec) * 1'000'000'000LL + (now.tv_nsec - ts0.tv_nsec));

    tsc_ns_per_tick() = static_cast<double>(elapsed_ns) / static_cast<double>(elapsed_ticks);
}

// Hot-path timestamp. rdtscp issues an implicit LFENCE before the counter
// read, preventing the CPU from speculatively hoisting it before preceding
// work. Never use __rdtsc() at stage boundaries — it does not serialize.
inline uint64_t tsc_now()
{
    uint32_t aux = 0;
    return __rdtscp(&aux);
}

// Offline use only. Converts raw ticks to nanoseconds using the calibrated
// constant. Do not call this on the pipeline hot path — the floating-point
// multiply adds ~5ns and the result is not needed until the harvester runs.
inline uint64_t tsc_to_ns(uint64_t ticks)
{
    return static_cast<uint64_t>(static_cast<double>(ticks) * tsc_ns_per_tick());
}

} // namespace AL
