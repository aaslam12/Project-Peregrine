#pragma once

#include "palloc_atomic.h"
#include "utility.h"
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace AL
{

template<typename T, std::size_t capacity>
concept is_valid_spsc_config = std::is_move_assignable_v<T> && is_power_of_two(capacity);

// only one thread should call push and another should call pop.
// this is a single producer, single consumer ring buffer
template<typename T, std::size_t capacity>
    requires is_valid_spsc_config<T, capacity>
class spsc_ring
{
    alignas(64) palloc_atomic<std::size_t> head{0}; // where we write data to
    alignas(64) palloc_atomic<std::size_t> tail{0}; // where we read data from

    T* data;

public:
    spsc_ring() = delete;

    spsc_ring(T* storage) : data{storage} {}

    // returns false if the ring is full
    bool push(T&& val)
    {
        std::size_t local_head = head.load(std::memory_order_relaxed);

        // only one thread writes to head and another thread writes to tail.
        // this is why the head can use relaxed memory ordering whereas tail needs acquire here
        // in pull, this would be flipped.
        if ((local_head - tail.load(std::memory_order_acquire)) == capacity)
            return false;

        std::size_t slot = local_head & (capacity - 1); // normalize index
        data[slot] = std::move(val);

        asm volatile("" ::: "memory"); // to prevent compiler reordering instructions

        head.store(local_head + 1, std::memory_order_release); // increment head
        return true;
    }

    bool pop(T& return_val)
    {
        std::size_t local_tail = tail.load(std::memory_order_relaxed);

        if (local_tail == head.load(std::memory_order_acquire))
            return false;

        std::size_t slot = local_tail & (capacity - 1); // normalize index
        return_val = std::move(data[slot]);

        asm volatile("" ::: "memory"); // to prevent compiler reordering instructions

        tail.store(local_tail + 1, std::memory_order_release); // increment tail
        return true;
    }
};

}; // namespace AL
