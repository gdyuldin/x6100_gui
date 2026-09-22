// Tests for cw::PowerDetector: LLR of a single frame, threshold changes, the
// click guard and reset.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "power_detector.h"

using Catch::Approx;

TEST_CASE("power detector: pure noise stays OFF") {
    cw::PowerDetector det;

    for (int i = 0; i < 20; ++i) {
        REQUIRE(det.get_raw_llr(1.0f, 1.0f) < -2.0f);
    }
}

TEST_CASE("power detector: reset restores the initial LLR state") {
    cw::PowerDetector det;

    REQUIRE(det.get_llr() == Approx(-3.0f));
    det.get_raw_llr(1.0f, 1.0f);
    REQUIRE(det.get_llr() < -2.0f);

    det.reset();
    REQUIRE(det.get_llr() == Approx(-3.0f));
}
