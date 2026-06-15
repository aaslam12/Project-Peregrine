#include "low_overhead_bench.h"
#include "spsc_ring.h"
#include <array>
#include <atomic>
#include <benchmark/benchmark.h>
#include <thread>

using namespace AL;

static constexpr std::size_t RING_CAPACITY = 4096;

// Single-thread round-trip: push then immediately pop.
// Measures raw push+pop throughput with no contention.
static void BM_SpscRing_RoundTrip(benchmark::State& state)
{
    std::array<uint64_t, RING_CAPACITY> storage{};
    spsc_ring<uint64_t, RING_CAPACITY> ring(storage.data());

    uint64_t val = 0;
    for (auto _ : state)
    {
        ring.push(uint64_t{1});
        ring.pop(val);
        benchmark::DoNotOptimize(val);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SpscRing_RoundTrip);

// Two-thread throughput: producer spins pushing a counter, benchmark thread
// (consumer) drives the state loop and counts items received.
static void BM_SpscRing_Throughput(benchmark::State& state)
{
    std::array<uint64_t, RING_CAPACITY> storage{};
    spsc_ring<uint64_t, RING_CAPACITY> ring(storage.data());

    std::atomic<bool> running{true};

    std::thread producer([&] {
        uint64_t i = 0;
        while (running.load(std::memory_order_relaxed))
        {
            while (!ring.push(uint64_t{i}) && running.load(std::memory_order_relaxed))
            {}
            ++i;
        }
    });

    int64_t items = 0;
    for (auto _ : state)
    {
        uint64_t val;
        while (!ring.pop(val))
        {}
        benchmark::DoNotOptimize(val);
        ++items;
    }

    running.store(false, std::memory_order_relaxed);
    producer.join();

    state.SetItemsProcessed(items);
}
BENCHMARK(BM_SpscRing_Throughput);
