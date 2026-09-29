// Unit tests for cw::PeakFilterResponse, the whitening response of the external
// analog CW peak filter. The closed form is validated against a direct
// simulation of the biquad difference equation, so the sign/structure assumption
// is checked against the recurrence rather than against itself.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "cw_config.h"
#include "peak_biquad_test.h"
#include "peak_filter_response.h"

using Catch::Approx;

TEST_CASE("peak filter response: closed form matches the biquad recurrence") {
    const float key = 700.0f;
    const float q   = 10.0f;

    for (float f : {300.0f, 500.0f, 650.0f, 700.0f, 750.0f, 900.0f, 1200.0f}) {
        const float measured = cwtest::measured_h2(key, q, f, cw::PEAK_FILTER_RATE);
        const float expected = cw::PeakFilterResponse::magnitude_sq_at(key, q, f);
        INFO("f=" << f << " measured=" << measured << " expected=" << expected);
        REQUIRE(measured == Approx(expected).epsilon(0.02));
    }
}

TEST_CASE("peak filter response: disabled leaves the response at unity") {
    cw::PeakFilterResponse resp;
    REQUIRE_FALSE(resp.enabled());
    for (float v : resp.h2())
        REQUIRE(v == 1.0f);

    // A configure(false, ...) must not compute or change anything.
    resp.configure(false, 700.0f, 10.0f);
    REQUIRE_FALSE(resp.enabled());
    for (float v : resp.h2())
        REQUIRE(v == 1.0f);
}

TEST_CASE("peak filter response: enabled peaks at the key tone and rolls off") {
    const float key = 700.0f;
    const float bin = cw::SAMPLE_RATE / static_cast<float>(cw::FFT_SIZE);
    const float q   = 10.0f;

    cw::PeakFilterResponse resp;
    resp.configure(true, key, q);
    REQUIRE(resp.enabled());

    const size_t centre = static_cast<size_t>(std::lround(key / bin));

    size_t argmax = 0;
    for (size_t i = 1; i < cw::SPECTRUM_SIZE; ++i) {
        if (resp.h2()[i] > resp.h2()[argmax])
            argmax = i;
    }
    REQUIRE(std::abs(static_cast<int>(argmax) - static_cast<int>(centre)) <= 1);

    REQUIRE(resp.h2()[centre] > resp.h2()[centre + 10]);
    REQUIRE(resp.h2()[centre] > resp.h2()[centre - 10]);
    REQUIRE(resp.h2()[centre + 10] > resp.h2()[centre + 25]);
}

TEST_CASE("peak filter response: higher Q is narrower") {
    const float  key    = 700.0f;
    const size_t centre = static_cast<size_t>(std::lround(key / (cw::SAMPLE_RATE / cw::FFT_SIZE)));

    cw::PeakFilterResponse narrow;
    cw::PeakFilterResponse wide;
    narrow.configure(true, key, 16.0f);
    wide.configure(true, key, 5.0f);

    const float narrow_ratio = narrow.h2()[centre + 6] / narrow.h2()[centre];
    const float wide_ratio   = wide.h2()[centre + 6] / wide.h2()[centre];
    REQUIRE(narrow_ratio < wide_ratio);
}
