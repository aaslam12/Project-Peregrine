#ifdef PEREGRINE_PROFILING_ENABLED

#include "tsc.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// All TSC tests require bare-metal x86 with a stable TSC.
// CI builds pass --no-profiling (-DPEREGRINE_ENABLE_PROFILING=OFF) to exclude
// this entire file from compilation.

TEST_CASE("TSC CPU flags present", "[telemetry]")
{
    REQUIRE(AL::tsc_check_cpu_flags());
}

TEST_CASE("TSC calibration produces plausible ns_per_tick", "[telemetry]")
{
    AL::tsc_init();

    // Modern x86 runs between 1–5 GHz, so 1 tick is between 0.2ns and 1.0ns.
    REQUIRE(AL::tsc_ns_per_tick() > 0.2);
    REQUIRE(AL::tsc_ns_per_tick() < 1.0);
}

TEST_CASE("tsc_now advances monotonically", "[telemetry]")
{
    const uint64_t t0 = AL::tsc_now();
    const uint64_t t1 = AL::tsc_now();
    REQUIRE(t1 > t0);
}

TEST_CASE("tsc_to_ns round-trip is plausible", "[telemetry]")
{
    AL::tsc_init();

    const uint64_t t0 = AL::tsc_now();
    for (int i = 0; i < 1000; ++i)
        asm volatile("" ::: "memory");
    const uint64_t t1 = AL::tsc_now();

    const uint64_t ns = AL::tsc_to_ns(t1 - t0);

    // 1000 barrier iterations: between 100ns and 100µs on any real machine.
    REQUIRE(ns > 100);
    REQUIRE(ns < 100'000);
}

#endif // PEREGRINE_PROFILING_ENABLED
