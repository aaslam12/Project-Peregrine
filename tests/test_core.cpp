#include <catch2/catch_test_macros.hpp>
#include <iostream>

namespace AL
{
extern void initialize_matching_engine();
}

TEST_CASE("Core initialization", "[core]")
{
    REQUIRE_NOTHROW(AL::initialize_matching_engine());
}
