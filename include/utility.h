#pragma once

#include <cstdint>

constexpr bool is_power_of_two(uint64_t n)
{
    return n >= 2 && (n & (n - 1)) == 0;
}
