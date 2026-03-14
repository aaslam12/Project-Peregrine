#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace AL
{
struct order
{
    uint64_t seq;
    uint8_t type;
};

bool validate_order_packet(const uint8_t* buf, size_t len)
{
    return len >= sizeof(order);
}
} // namespace AL

TEST_CASE("Order packet validation", "[protocol]")
{
    uint8_t buf[16] = {};
    REQUIRE(AL::validate_order_packet(buf, 16));
    REQUIRE_FALSE(AL::validate_order_packet(buf, 7));
}
