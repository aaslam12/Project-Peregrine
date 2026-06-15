#pragma once

// ═══════════════════════════════════════════════════════════════════════════════
// Low-Overhead Benchmark Utilities
//
// Optimizations:
//   - rdtscp direct cycle counts: serializes the instruction stream via an
//     implicit LFENCE, preventing the CPU from speculatively hoisting the read
//     before the work it brackets.
//   - Fixed-size preallocated latency buffers (no heap allocation)
//   - Per-thread counters with single atomic aggregation at end
//   - Histogram-based percentile estimation (no sorting)
//   - Deadline via separate thread (no clock reads in hot path)
// ═══════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <thread>

namespace bench
{

// ─── RDTSCP Cycle Counter ─────────────────────────────────────────────────────
// rdtscp issues an implicit LFENCE before reading the counter, preventing
// out-of-order execution from placing the read before preceding work.

inline uint64_t rdtscp()
{
    uint32_t aux;
    uint64_t a, d;
    asm volatile("rdtscp" : "=a"(a), "=d"(d), "=c"(aux));
    return (d << 32) | a;
}

// ─── Compiler Hints ───────────────────────────────────────────────────────────

inline void escape(void* p)
{
    asm volatile("" : : "g"(p) : "memory");
}

inline void clobber()
{
    asm volatile("" : : : "memory");
}

// ─── Fixed-Size Latency Buffer ────────────────────────────────────────────────

template<size_t Capacity>
struct LatencyBuffer
{
    alignas(64) uint64_t samples[Capacity];
    size_t count = 0;

    void record(uint64_t cycles)
    {
        if (count < Capacity)
            samples[count++] = cycles;
    }

    struct Stats
    {
        uint64_t p50 = 0, p90 = 0, p99 = 0, p999 = 0;
        double mean = 0.0;
    };

    // Non-destructive: sorts a copy so the original samples are preserved
    // and compute() can be called multiple times with consistent results.
    Stats compute() const
    {
        if (count == 0)
            return {};

        uint64_t sorted[Capacity];
        std::copy(samples, samples + count, sorted);
        std::sort(sorted, sorted + count);

        Stats s;
        s.p50 = sorted[count * 50 / 100];
        s.p90 = sorted[count * 90 / 100];
        s.p99 = sorted[count * 99 / 100];
        s.p999 = sorted[count * 999 / 1000];
        s.mean = std::accumulate(sorted, sorted + count, 0.0) / static_cast<double>(count);
        return s;
    }
};

// ─── Histogram for Approximate Percentiles (no sorting, no atomics) ───────────
// Single-threaded use only. For multi-threaded aggregation, each thread owns
// its own LatencyHistogram and merges into a shared one at the end.

struct LatencyHistogram
{
    static constexpr size_t NUM_BUCKETS = 64;
    alignas(64) uint64_t buckets[NUM_BUCKETS]{};

    void record(uint64_t cycles)
    {
        const size_t bucket = (cycles == 0) ? 0 : static_cast<size_t>(63 - __builtin_clzll(cycles));
        buckets[bucket < NUM_BUCKETS ? bucket : NUM_BUCKETS - 1]++;
    }

    void merge(const LatencyHistogram& other)
    {
        for (size_t i = 0; i < NUM_BUCKETS; ++i)
            buckets[i] += other.buckets[i];
    }

    // Snapshots counts locally before computing to prevent load-tearing
    // if another thread writes concurrently during a merge window.
    uint64_t percentile(double p) const
    {
        uint64_t local[NUM_BUCKETS];
        uint64_t total = 0;
        for (size_t i = 0; i < NUM_BUCKETS; ++i)
        {
            local[i] = buckets[i];
            total += local[i];
        }

        if (total == 0)
            return 0;

        const uint64_t target = static_cast<uint64_t>(static_cast<double>(total) * p / 100.0);
        uint64_t cumulative = 0;
        for (size_t i = 0; i < NUM_BUCKETS; ++i)
        {
            cumulative += local[i];
            if (cumulative >= target)
                return uint64_t(1) << i;
        }
        return uint64_t(1) << (NUM_BUCKETS - 1);
    }
};

// ─── Deadline Timer (separate thread, no hot-path overhead) ───────────────────

class DeadlineTimer
{
    std::atomic<bool> done_{false};
    std::thread thread_;

public:
    explicit DeadlineTimer(int seconds)
    {
        thread_ = std::thread([this, seconds] {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            done_.store(true, std::memory_order_relaxed);
        });
    }

    ~DeadlineTimer()
    {
        if (thread_.joinable())
            thread_.join();
    }

    bool is_done() const { return done_.load(std::memory_order_relaxed); }

    template<size_t CheckInterval>
    bool check_periodic(size_t iteration) const
    {
        if ((iteration & (CheckInterval - 1)) == 0)
            return is_done();
        return false;
    }
};

// ─── Per-Thread Counter ───────────────────────────────────────────────────────
// local_ is a non-static member to avoid aliasing between distinct instances
// of the same ThreadCounter<T> type on the same thread.

template<typename T = size_t>
class ThreadCounter
{
    T local_{0};
    std::atomic<T> global_{0};

public:
    void increment(T delta = 1) { local_ += delta; }
    T get_local() const { return local_; }

    void publish()
    {
        global_.fetch_add(local_, std::memory_order_relaxed);
        local_ = 0;
    }

    T get_global() const { return global_.load(std::memory_order_relaxed); }
};

// ─── Thread Coordination ──────────────────────────────────────────────────────

inline void wait_for_start(const std::atomic<bool>& start)
{
    while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();
}

inline size_t worker_count()
{
    const unsigned hw = std::thread::hardware_concurrency();
    return (hw == 0) ? 8 : std::min<size_t>(hw, 16);
}

// ─── Timing Helpers ───────────────────────────────────────────────────────────

inline double cycles_per_op(uint64_t cycles, size_t ops)
{
    return static_cast<double>(cycles) / static_cast<double>(ops);
}

inline double mops_per_s(double elapsed_sec, size_t ops)
{
    return static_cast<double>(ops) / elapsed_sec / 1e6;
}

// ─── Print Helpers ────────────────────────────────────────────────────────────

inline void print_table_header(const char* title = nullptr)
{
    if (title)
        std::printf("\n━━━ %s ━━━\n\n", title);
    std::printf("  %-22s %10s %12s\n", "Benchmark", "ns/op", "MOps/s");
    std::printf("  ──────────────────────────────────────────────\n");
}

inline void print_row(const char* name, double ns, double mops)
{
    std::printf("  %-22s %8.1f %12.1f\n", name, ns, mops);
}

} // namespace bench
