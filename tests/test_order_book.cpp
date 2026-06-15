#include "order_book.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <new>

// ─── Test Fixture ─────────────────────────────────────────────────────────────
// Owns all storage required by order_book<N>: level array, pool, bitmap memory.
// All storage is stack-allocated. pool is pre-warmed for up to N orders.

template<uint64_t N>
struct BookFixture
{
    static constexpr int64_t BASE = 1000;

    std::array<price_level, N> level_storage{};
    AL::pool<false> order_pool{sizeof(order), N};
    alignas(64) std::array<std::byte, AL::bitmap<false>::required_size(N)> bid_mem{};
    alignas(64) std::array<std::byte, AL::bitmap<false>::required_size(N)> ask_mem{};

    order_book<N, false> book{std::span<price_level, N>{level_storage}, &order_pool, bid_mem.data(), ask_mem.data(), BASE};

    order* make_order(order_side side, uint64_t quantity)
    {
        auto* o = static_cast<order*>(order_pool.alloc());
        new (o) order{};
        o->side = side;
        o->quantity = quantity;
        o->next = nullptr;
        o->prev = nullptr;
        return o;
    }
};

// ─── Constructor ──────────────────────────────────────────────────────────────

TEST_CASE("order_book empty book returns -1 for best prices", "[order_book]")
{
    BookFixture<64> f;
    CHECK(f.book.best_bid() == -1);
    CHECK(f.book.best_ask() == -1);
}

// ─── Insert ───────────────────────────────────────────────────────────────────

TEST_CASE("order_book insert buy sets bids bitmap, not asks", "[order_book]")
{
    BookFixture<64> f;
    order* o = f.make_order(buy, 100);
    f.book.insert(o, f.BASE + 5);
    CHECK(f.book.best_bid() == f.BASE + 5);
    CHECK(f.book.best_ask() == -1);
}

TEST_CASE("order_book insert sell sets asks bitmap, not bids", "[order_book]")
{
    BookFixture<64> f;
    order* o = f.make_order(sell, 100);
    f.book.insert(o, f.BASE + 5);
    CHECK(f.book.best_ask() == f.BASE + 5);
    CHECK(f.book.best_bid() == -1);
}

TEST_CASE("order_book insert two orders at same price wires linked list", "[order_book]")
{
    BookFixture<64> f;
    order* first = f.make_order(buy, 100);
    order* second = f.make_order(buy, 200);

    f.book.insert(first, f.BASE);
    f.book.insert(second, f.BASE);

    price_level& level = f.level_storage[0]; // BASE - BASE = index 0
    CHECK(level.total == 300);
    CHECK(level.head == first);
    CHECK(level.tail == second);
    CHECK(first->next == second);
    CHECK(second->prev == first);
    CHECK(first->prev == nullptr);
    CHECK(second->next == nullptr);
}

// ─── Cancel ───────────────────────────────────────────────────────────────────

TEST_CASE("order_book cancel only order clears level and bitmap", "[order_book]")
{
    BookFixture<64> f;
    order* o = f.make_order(buy, 100);
    f.book.insert(o, f.BASE);
    f.book.cancel(o, f.BASE);

    price_level& level = f.level_storage[0];
    CHECK(level.total == 0);
    CHECK(level.head == nullptr);
    CHECK(level.tail == nullptr);
    CHECK(f.book.best_bid() == -1);
}

TEST_CASE("order_book cancel head of two leaves tail intact", "[order_book]")
{
    BookFixture<64> f;
    order* first = f.make_order(buy, 100);
    order* second = f.make_order(buy, 200);
    f.book.insert(first, f.BASE);
    f.book.insert(second, f.BASE);

    f.book.cancel(first, f.BASE);

    price_level& level = f.level_storage[0];
    CHECK(level.head == second);
    CHECK(level.tail == second);
    CHECK(second->prev == nullptr);
    CHECK(second->next == nullptr);
    CHECK(level.total == 200);
    CHECK(f.book.best_bid() == f.BASE);
}

TEST_CASE("order_book cancel tail of two leaves head intact", "[order_book]")
{
    BookFixture<64> f;
    order* first = f.make_order(buy, 100);
    order* second = f.make_order(buy, 200);
    f.book.insert(first, f.BASE);
    f.book.insert(second, f.BASE);

    f.book.cancel(second, f.BASE);

    price_level& level = f.level_storage[0];
    CHECK(level.head == first);
    CHECK(level.tail == first);
    CHECK(first->next == nullptr);
    CHECK(first->prev == nullptr);
    CHECK(level.total == 100);
    CHECK(f.book.best_bid() == f.BASE);
}

// ─── Match ────────────────────────────────────────────────────────────────────

TEST_CASE("order_book match full fill empties level and clears bitmap", "[order_book]")
{
    BookFixture<64> f;
    order* resting = f.make_order(sell, 100);
    f.book.insert(resting, f.BASE + 1);

    order aggressor{};
    aggressor.side = buy;
    aggressor.quantity = 100;

    const uint64_t filled = f.book.match(&aggressor, f.BASE + 1);

    CHECK(filled == 100);
    CHECK(aggressor.quantity == 0);
    CHECK(f.book.best_ask() == -1);
    CHECK(f.level_storage[1].total == 0);
    CHECK(f.level_storage[1].head == nullptr);
}

TEST_CASE("order_book match partial fill: aggressor smaller than resting", "[order_book]")
{
    BookFixture<64> f;
    order* resting = f.make_order(sell, 200);
    f.book.insert(resting, f.BASE + 1);

    order aggressor{};
    aggressor.side = buy;
    aggressor.quantity = 50;

    const uint64_t filled = f.book.match(&aggressor, f.BASE + 1);

    CHECK(filled == 50);
    CHECK(aggressor.quantity == 0);
    CHECK(resting->quantity == 150);
    CHECK(f.level_storage[1].total == 150);
    CHECK(f.book.best_ask() == f.BASE + 1);
}

TEST_CASE("order_book match partial fill: resting smaller than aggressor", "[order_book]")
{
    BookFixture<64> f;
    order* r1 = f.make_order(sell, 30);
    order* r2 = f.make_order(sell, 100);
    f.book.insert(r1, f.BASE + 1);
    f.book.insert(r2, f.BASE + 1);

    order aggressor{};
    aggressor.side = buy;
    aggressor.quantity = 80;

    const uint64_t filled = f.book.match(&aggressor, f.BASE + 1);

    CHECK(filled == 80);
    CHECK(aggressor.quantity == 0);
    // r1 fully consumed, r2 partially consumed
    CHECK(f.level_storage[1].head == r2);
    CHECK(r2->quantity == 50);
    CHECK(f.level_storage[1].total == 50);
    CHECK(f.book.best_ask() == f.BASE + 1);
}

TEST_CASE("order_book match against empty level returns 0", "[order_book]")
{
    BookFixture<64> f;
    order aggressor{};
    aggressor.side = buy;
    aggressor.quantity = 100;

    CHECK(f.book.match(&aggressor, f.BASE + 1) == 0);
    CHECK(aggressor.quantity == 100);
}

// ─── best_bid / best_ask tracking ────────────────────────────────────────────

TEST_CASE("order_book best_bid tracks highest resting buy", "[order_book]")
{
    BookFixture<64> f;
    order* o1 = f.make_order(buy, 1);
    order* o2 = f.make_order(buy, 1);
    order* o3 = f.make_order(buy, 1);
    f.book.insert(o1, f.BASE + 1);
    f.book.insert(o2, f.BASE + 3);
    f.book.insert(o3, f.BASE + 2);

    CHECK(f.book.best_bid() == f.BASE + 3);

    f.book.cancel(o2, f.BASE + 3);
    CHECK(f.book.best_bid() == f.BASE + 2);
}

TEST_CASE("order_book best_ask tracks lowest resting sell", "[order_book]")
{
    BookFixture<64> f;
    order* o1 = f.make_order(sell, 1);
    order* o2 = f.make_order(sell, 1);
    order* o3 = f.make_order(sell, 1);
    f.book.insert(o1, f.BASE + 5);
    f.book.insert(o2, f.BASE + 3);
    f.book.insert(o3, f.BASE + 7);

    CHECK(f.book.best_ask() == f.BASE + 3);

    f.book.cancel(o2, f.BASE + 3);
    CHECK(f.book.best_ask() == f.BASE + 5);
}

TEST_CASE("order_book best_ask clears after full match", "[order_book]")
{
    BookFixture<64> f;
    order* resting = f.make_order(sell, 50);
    f.book.insert(resting, f.BASE + 1);

    CHECK(f.book.best_ask() == f.BASE + 1);

    order aggressor{};
    aggressor.side = buy;
    aggressor.quantity = 50;
    f.book.match(&aggressor, f.BASE + 1);

    CHECK(f.book.best_ask() == -1);
}
