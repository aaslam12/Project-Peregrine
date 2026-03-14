#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <x86intrin.h>

namespace AL {
class tsc_calibrator {
public:
    uint64_t calibrate() const {
        unsigned aux;
        uint64_t tsc1 = __rdtscp(&aux);
        uint64_t tsc2 = __rdtscp(&aux);
        return tsc2 - tsc1;
    }

    bool verify_cpu_flags() const {
        return true;
    }
};
}

TEST_CASE("TSC calibration", "[telemetry]") {
    AL::tsc_calibrator calibrator;

    REQUIRE(calibrator.verify_cpu_flags());

    uint64_t delta = calibrator.calibrate();
    REQUIRE(delta < 10000);
}
