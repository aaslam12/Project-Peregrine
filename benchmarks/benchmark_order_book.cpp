#include "low_overhead_bench.h"
#include "order_book.h"
#include <array>
#include <benchmark/benchmark.h>
#include <new>

static constexpr uint64_t BOOK_CAPACITY = 4096;
static constexpr int64_t  BASE_PRICE    = 1000;
static constexpr uint64_t PRICE_WINDOW  = 64; // stay cache-hot

// ─── Fixture ─────────────────────────────────────────────────────────────────

struct BenchFixture
{
    std::array<price_level, BOOK_CAPACITY> level_storage{};
    AL::pool<false> order_pool{sizeof(order), BOOK_CAPACITY};
    alignas(64) std::array<std::byte, AL::bitmap<false>::required_size(BOOK_CAPACITY)> bid_mem{};
    alignas(64) std::array<std::byte, AL::bitmap<false>::required_size(BOOK_CAPACITY)> ask_mem{};

    order_book<BOOK_CAPACITY, false> book{
        std::span<price_level, BOOK_CAPACITY>{level_storage},
        &order_pool,
        bid_mem.data(),
        ask_mem.data(),
        BASE_PRICE};

    order* make_order(order_side side, uint64_t quantity)
    {
        auto* o = static_cast<order*>(order_pool.alloc());
        new (o) order{};
        o->side     = side;
        o->quantity = quantity;
        o->next     = nullptr;
        o->prev     = nullptr;
        return o;
    }
};

// ─── Insert ───────────────────────────────────────────────────────────────────
// Insert a sell then immediately cancel to keep the pool and book stable.
// Cycles over PRICE_WINDOW prices to stay in L2.

static void BM_OrderBook_Insert(benchmark::State& state)
{
    BenchFixture f;
    int64_t iteration = 0;

    for (auto _ : state)
    {
        const int64_t price = BASE_PRICE + (iteration % static_cast<int64_t>(PRICE_WINDOW));
        order* o = f.make_order(sell, 100);
        benchmark::DoNotOptimize(f.book.insert(o, price));
        f.book.cancel(o, price);
        ++iteration;
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderBook_Insert);

// ─── Cancel ───────────────────────────────────────────────────────────────────
// Pre-allocates all orders before the loop and re-inserts after each cancel
// so pool::alloc and pool::free are not in the measured path.

static void BM_OrderBook_Cancel(benchmark::State& state)
{
    BenchFixture f;

    std::array<order*, PRICE_WINDOW> orders{};
    for (int64_t i = 0; i < static_cast<int64_t>(PRICE_WINDOW); ++i)
    {
        orders[i] = f.make_order(sell, 100);
        f.book.insert(orders[i], BASE_PRICE + i);
    }

    int64_t iteration = 0;
    for (auto _ : state)
    {
        const int64_t i     = iteration % static_cast<int64_t>(PRICE_WINDOW);
        const int64_t price = BASE_PRICE + i;

        f.book.cancel(orders[i], price);
        f.book.insert(orders[i], price);
        benchmark::DoNotOptimize(price);
        ++iteration;
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderBook_Cancel);

// ─── Match ────────────────────────────────────────────────────────────────────
// Pre-fill one resting sell per iteration slot; match a buy aggressor against
// it; re-insert the resting sell to keep the book populated for the next round.

static void BM_OrderBook_Match(benchmark::State& state)
{
    BenchFixture f;
    int64_t iteration = 0;

    for (int64_t i = 0; i < static_cast<int64_t>(PRICE_WINDOW); ++i)
        f.book.insert(f.make_order(sell, 100), BASE_PRICE + i);

    for (auto _ : state)
    {
        const int64_t price = BASE_PRICE + (iteration % static_cast<int64_t>(PRICE_WINDOW));

        order aggressor{};
        aggressor.side     = buy;
        aggressor.quantity = 100;

        const uint64_t filled = f.book.match(&aggressor, price);
        benchmark::DoNotOptimize(filled);

        f.book.insert(f.make_order(sell, 100), price);
        ++iteration;
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderBook_Match);
