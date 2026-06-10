#include "spsc_ring.h"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

using namespace AL;

TEST_CASE("spsc_ring basic operations", "[spsc_ring]")
{
    constexpr std::size_t capacity = 4;
    std::vector<int> storage(capacity);
    spsc_ring<int, capacity> ring(storage.data());

    SECTION("push and pop single element")
    {
        REQUIRE(ring.push(42));
        int val = 0;
        REQUIRE(ring.pop(val));
        CHECK(val == 42);
    }

    SECTION("pop empty ring returns false")
    {
        int val = 0;
        CHECK_FALSE(ring.pop(val));
    }

    SECTION("push until full")
    {
        CHECK(ring.push(1));
        CHECK(ring.push(2));
        CHECK(ring.push(3));
        CHECK(ring.push(4));
        CHECK_FALSE(ring.push(5));

        int val = 0;
        CHECK(ring.pop(val));
        CHECK(val == 1);
        CHECK(ring.push(5)); // exactly one slot freed; verifies boundary
        CHECK_FALSE(ring.push(6));
    }
}

struct LifetimeCounter
{
    static inline int constructions = 0;
    static inline int destructions = 0;
    static inline int moves = 0;

    LifetimeCounter()
    {
        constructions++;
    }
    ~LifetimeCounter()
    {
        destructions++;
    }
    LifetimeCounter(const LifetimeCounter&) = delete;
    LifetimeCounter& operator=(const LifetimeCounter&) = delete;
    LifetimeCounter(LifetimeCounter&&) noexcept
    {
        moves++;
    }
    LifetimeCounter& operator=(LifetimeCounter&&) noexcept
    {
        moves++;
        return *this;
    }

    static void reset()
    {
        constructions = 0;
        destructions = 0;
        moves = 0;
    }
};

TEST_CASE("spsc_ring lifetime management", "[spsc_ring]")
{
    constexpr std::size_t capacity = 4;
    std::vector<LifetimeCounter> storage(capacity);
    spsc_ring<LifetimeCounter, capacity> ring(storage.data());

    LifetimeCounter::reset();

    SECTION("push and pop maintains count")
    {
        {
            LifetimeCounter lc;
            REQUIRE(ring.push(std::move(lc)));
        }
        // One construction (lc), one move into ring storage (which is a move-assignment to an existing object in vector)
        CHECK(LifetimeCounter::constructions == 1);
        CHECK(LifetimeCounter::moves == 1);

        {
            LifetimeCounter out;
            REQUIRE(ring.pop(out));
            // 'out' was constructed, then move-assigned from ring storage
            CHECK(LifetimeCounter::constructions == 2);
            CHECK(LifetimeCounter::moves == 2);
        }
        // Both 'lc' and 'out' should be destroyed by now.
        // The storage vector also contains 'capacity' elements which will be destroyed at the end of the test case.
    }
}

TEST_CASE("spsc_ring wrap-around behavior", "[spsc_ring]")
{
    constexpr std::size_t capacity = 4;
    std::vector<int> storage(capacity);
    spsc_ring<int, capacity> ring(storage.data());

    // With capacity=4, head and tail each wrap 25+ times over 100 iterations,
    // exercising the mask-and-compare logic under sustained wrap conditions.
    for (int i = 0; i < 100; ++i)
    {
        REQUIRE(ring.push(int{i}));
        int val = -1;
        REQUIRE(ring.pop(val));
        CHECK(val == i);
    }
}

TEST_CASE("spsc_ring drain and refill", "[spsc_ring]")
{
    constexpr std::size_t capacity = 4;
    std::vector<int> storage(capacity);
    spsc_ring<int, capacity> ring(storage.data());

    // A complete drain leaves head == tail, which is the same condition as
    // an empty ring.  A second fill-drain cycle validates that the ring
    // correctly exits this ambiguous state rather than deadlocking.
    for (int i = 0; i < 4; ++i)
        REQUIRE(ring.push(int{i}));
    CHECK_FALSE(ring.push(99));

    int val = -1;
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(ring.pop(val));
        CHECK(val == i);
    }
    CHECK_FALSE(ring.pop(val));

    for (int i = 10; i < 14; ++i)
        REQUIRE(ring.push(int{i}));
    CHECK_FALSE(ring.push(99));

    for (int i = 10; i < 14; ++i)
    {
        REQUIRE(ring.pop(val));
        CHECK(val == i);
    }
    CHECK_FALSE(ring.pop(val));
}

TEST_CASE("spsc_ring boundary at wrap", "[spsc_ring]")
{
    constexpr std::size_t capacity = 4;
    std::vector<int> storage(capacity);
    spsc_ring<int, capacity> ring(storage.data());

    // Push/pop such that head wraps past capacity while unconsumed items
    // remain in slots 2 and 3.  The consumer must drain the old items first
    // (2, 3) before the new ones (10, 11) — verifying FIFO order is
    // determined by the linear index, not by slot position in the array.
    for (int i = 0; i < 4; ++i)
        REQUIRE(ring.push(int{i}));

    int val = -1;
    REQUIRE(ring.pop(val));
    CHECK(val == 0);
    REQUIRE(ring.pop(val));
    CHECK(val == 1);

    REQUIRE(ring.push(10));
    REQUIRE(ring.push(11));
    CHECK_FALSE(ring.push(99));

    REQUIRE(ring.pop(val));
    CHECK(val == 2);
    REQUIRE(ring.pop(val));
    CHECK(val == 3);
    REQUIRE(ring.pop(val));
    CHECK(val == 10);
    REQUIRE(ring.pop(val));
    CHECK(val == 11);
    CHECK_FALSE(ring.pop(val));

    REQUIRE(ring.push(20));
    REQUIRE(ring.pop(val));
    CHECK(val == 20);
    CHECK_FALSE(ring.pop(val));
}

struct NonDefaultConstructible
{
    int value;
    NonDefaultConstructible() = delete;
    explicit NonDefaultConstructible(int v) : value(v)
    {}
    NonDefaultConstructible(NonDefaultConstructible&&) = default;
    NonDefaultConstructible& operator=(NonDefaultConstructible&&) = default;
};

TEST_CASE("spsc_ring non-default-constructible type", "[spsc_ring]")
{
    constexpr std::size_t capacity = 4;
    using T = NonDefaultConstructible;
    alignas(T) unsigned char raw[sizeof(T) * capacity];
    auto* storage = reinterpret_cast<T*>(raw);

    // Raw placement-new (no std::vector) exercises the T* storage interface
    // directly, matching how palloc-backed memory is used in production.
    for (std::size_t i = 0; i < capacity; ++i)
        new (&storage[i]) T(0);

    spsc_ring<T, capacity> ring(storage);

    CHECK(ring.push(T(42)));
    T val(0);
    CHECK(ring.pop(val));
    CHECK(val.value == 42);

    for (std::size_t i = 0; i < capacity; ++i)
        storage[i].~T();
}

TEST_CASE("spsc_ring move-only types", "[spsc_ring]")
{
    constexpr std::size_t capacity = 2;
    std::vector<std::unique_ptr<int>> storage(capacity);
    spsc_ring<std::unique_ptr<int>, capacity> ring(storage.data());

    SECTION("push and pop unique_ptr")
    {
        auto p1 = std::make_unique<int>(10);
        REQUIRE(ring.push(std::move(p1)));
        CHECK(p1 == nullptr); // NOLINT(bugprone-use-after-move)

        std::unique_ptr<int> p2;
        REQUIRE(ring.pop(p2));
        REQUIRE(p2 != nullptr);
        CHECK(*p2 == 10);
    }
}

TEST_CASE("spsc_ring stress test", "[spsc_ring][multi-threaded][slow]")
{
    constexpr std::size_t capacity = 4096;
    std::vector<int> storage(capacity);
    spsc_ring<int, capacity> ring(storage.data());

    // Tight spin with no yield — validates that release/acquire ordering
    // is sufficient under maximum producer-consumer contention.  Any
    // missing barrier would surface as a value mismatch within 1M rounds.
    constexpr int iterations = 1'000'000;
    std::atomic<bool> start{false};

    std::thread producer([&]() {
        while (!start.load())
            ;
        for (int i = 0; i < iterations; ++i)
        {
            while (!ring.push(int{i}))
            {
                // spin
            }
        }
    });

    std::thread consumer([&]() {
        while (!start.load())
            ;
        for (int i = 0; i < iterations; ++i)
        {
            int val = -1;
            while (!ring.pop(val))
            {
                // spin
            }
            if (val != i)
            {
                FAIL("Value mismatch: expected " << i << " but got " << val);
            }
        }
    });

    start.store(true);
    producer.join();
    consumer.join();
    SUCCEED("Stress test passed");
}

TEST_CASE("spsc_ring thread safety (basic SPSC)", "[spsc_ring][multi-threaded]")
{
    constexpr std::size_t capacity = 1024;
    std::vector<uint64_t> storage(capacity);
    spsc_ring<uint64_t, capacity> ring(storage.data());

    // Yield-based backoff with numeric checksum: the consumer independently
    // computes the expected sum from a closed-form sequence, catching silent
    // corruption (wrong values) that a simple count-only test would miss.
    constexpr uint64_t num_elements = 100'000;
    std::atomic<bool> start{false};

    std::thread producer([&]() {
        while (!start.load())
            ;
        for (uint64_t i = 1; i <= num_elements; ++i)
        {
            while (!ring.push(uint64_t{i}))
            {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        while (!start.load())
            ;
        uint64_t sum = 0;
        uint64_t expected_sum = num_elements * (num_elements + 1) / 2;
        uint64_t count = 0;
        while (count < num_elements)
        {
            uint64_t val;
            if (ring.pop(val))
            {
                sum += val;
                count++;
            }
            else
            {
                std::this_thread::yield();
            }
        }
        CHECK(sum == expected_sum);
    });

    start.store(true);
    producer.join();
    consumer.join();
}

// Static tests for template constraints
// 0 and 1 are explicitly tested since they are common off-by-one mistakes
// for ring-buffer sizing that could silently pass without these guards.
static_assert(is_power_of_two(2));
static_assert(is_power_of_two(4));
static_assert(is_power_of_two(1024));
static_assert(!is_power_of_two(0));
static_assert(!is_power_of_two(1));
static_assert(!is_power_of_two(3));
static_assert(!is_power_of_two(1000));

static_assert(is_valid_spsc_config<int, 2>);
static_assert(is_valid_spsc_config<std::unique_ptr<int>, 4>);

struct NonMoveAssignable
{
    NonMoveAssignable& operator=(NonMoveAssignable&&) = delete;
};
static_assert(!is_valid_spsc_config<NonMoveAssignable, 4>);

// Cache-line separation: head and tail must reside on separate 64-byte
// cache lines to prevent false sharing. The struct must be at least
// 128 bytes to accommodate both (64 bytes each) plus the data pointer.
static_assert(sizeof(spsc_ring<int, 4>) >= 128, "head and tail must fit on separate cache lines");
static_assert(alignof(spsc_ring<int, 4>) >= 64, "spsc_ring must be 64-byte aligned for false-sharing protection");
