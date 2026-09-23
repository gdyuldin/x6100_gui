// Tests for cw::PowerDetector: the frame LLR in dB relative to the configured
// SNR threshold and its response to threshold changes.

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

TEST_CASE("power detector: the user threshold is the zero crossing") {
    cw::PowerDetector det;

    // 10 dB SNR; with a 10 dB threshold the LLR is zero.
    det.set_threshold_db(10.0f);
    REQUIRE(det.get_raw_llr(10.0f, 1.0f) == Approx(0.0f).margin(0.01f));

    // Lowering the threshold raises the LLR by the same amount.
    det.set_threshold_db(3.0f);
    REQUIRE(det.get_raw_llr(10.0f, 1.0f) == Approx(7.0f).margin(0.01f));
}
