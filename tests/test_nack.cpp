#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace AL
{
class sequence_tracker
{
    uint64_t highest_seq_ = 0;

public:
    bool detect_gap(uint64_t seq)
    {
        if (seq > highest_seq_ + 1)
        {
            highest_seq_ = seq;
            return true;
        }
        if (seq > highest_seq_)
        {
            highest_seq_ = seq;
        }
        return false;
    }

    uint64_t highest() const
    {
        return highest_seq_;
    }
};
} // namespace AL

TEST_CASE("NACK gap detection", "[protocol]")
{
    AL::sequence_tracker tracker;

    REQUIRE_FALSE(tracker.detect_gap(0));
    REQUIRE_FALSE(tracker.detect_gap(1));
    REQUIRE(tracker.detect_gap(3));
    REQUIRE_FALSE(tracker.detect_gap(4));
    REQUIRE(tracker.highest() == 4);
}
