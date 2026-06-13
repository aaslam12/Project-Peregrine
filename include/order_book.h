#pragma once

#include "bitmap.h"
#include "pool.h"
#include "utility.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>

enum order_side : bool
{
    buy,
    sell
};

struct order
{
    order* next;
    order* prev;

    uint64_t quantity;
    order_side side;
    uint64_t order_uid;

#ifdef PEREGRINE_PROFILING_ENABLED
    uint64_t t0, t1, t2, t3, t4;
#endif // PEREGRINE_PROFILING_ENABLED
};

struct price_level
{
    order* head;
    order* tail;

    uint64_t total;
};

template<uint64_t capacity>
concept is_valid_order_book_config = is_power_of_two(capacity);

template<uint64_t capacity>
    requires is_valid_order_book_config<capacity>
class order_book
{
    int64_t base_price;
    std::span<price_level, capacity> levels;
    AL::bitmap bids;
    AL::bitmap asks;

    AL::pool* order_pool;

    size_t get_index(int64_t price) const { return static_cast<size_t>(price - base_price) & (capacity - 1); }

    price_level& get_price_level(int64_t price) { return levels[get_index(price)]; }

public:
    order_book() = delete;

    // bid_mem and ask_mem must each be at least AL::bitmap::required_size(capacity) bytes,
    // pre-zeroed before being passed in.
    order_book(std::span<price_level, capacity> levels_span, AL::pool* pool, void* bid_memory, void* ask_memory, int64_t base)
        : base_price(base), levels(levels_span), order_pool(pool)
    {
        std::memset(levels_span.data(), 0, capacity * sizeof(price_level));
        bids.init(bid_memory, capacity);
        asks.init(ask_memory, capacity);
    }

    bool insert(order* o, int64_t price)
    {
        const size_t idx = get_index(price);
        price_level& level = levels[idx];

        if (level.head == nullptr)
        {
            o->prev = nullptr;
            level.head = o;
        }
        else
        {
            o->prev = level.tail;
            level.tail->next = o;
        }

        level.tail = o;
        o->next = nullptr;

        level.total += o->quantity;

        if (o->side == buy)
            bids.set_slot(idx);
        else
            asks.set_slot(idx);

        return false;
    }

    void cancel(order* o, int64_t price)
    {
        const size_t idx = get_index(price);
        price_level& level = levels[idx];

        if (o->prev != nullptr)
            o->prev->next = o->next;
        else
            level.head = o->next;

        if (o->next != nullptr)
            o->next->prev = o->prev;
        else
            level.tail = o->prev;

        level.total -= o->quantity;

        if (level.total == 0)
        {
            if (o->side == buy)
                bids.clear_slot(idx);
            else
                asks.clear_slot(idx);
        }

        order_pool->free(o);
    }

    // drains the resting side at exactly price in FIFO order against aggressor.
    // returns total quantity filled.
    uint64_t match(order* aggressor, int64_t price)
    {
        const size_t idx = get_index(price);
        price_level& level = levels[idx];

        uint64_t filled = 0;

        while (level.head != nullptr && aggressor->quantity > 0)
        {
            order* resting = level.head;
            const uint64_t fill_quantity = std::min(resting->quantity, aggressor->quantity);

            resting->quantity -= fill_quantity;
            aggressor->quantity -= fill_quantity;
            level.total -= fill_quantity;
            filled += fill_quantity;

            if (resting->quantity == 0)
            {
                level.head = resting->next;
                if (level.head != nullptr)
                    level.head->prev = nullptr;
                else
                    level.tail = nullptr;

                order_pool->free(resting);
            }
        }

        if (level.total == 0)
        {
            if (aggressor->side == buy)
                asks.clear_slot(idx);
            else
                bids.clear_slot(idx);
        }

        return filled;
    }

    // returns best bid price (highest resting buy), or -1 if no bids.
    int64_t best_bid() const
    {
        const size_t idx = bids.find_highest_set_bit();
        return idx == static_cast<size_t>(-1) ? -1 : base_price + static_cast<int64_t>(idx);
    }

    // returns best ask price (lowest resting sell), or -1 if no asks.
    int64_t best_ask() const
    {
        const size_t idx = asks.find_lowest_set_bit();
        return idx == static_cast<size_t>(-1) ? -1 : base_price + static_cast<int64_t>(idx);
    }
};
