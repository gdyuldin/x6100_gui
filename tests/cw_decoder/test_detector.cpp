// Tests for cw::Detector: the relative Schmitt thresholds and the user floor,
// which only shifts the absolute on/off levels. The level decisions used to
// live in the timing classifier (power detector tests); they are checked here.

#include <catch2/catch_test_macros.hpp>

#include "detector.h"

namespace {

// Largest crossing offset: the interpolation may reach one integrator window
// plus one hop behind the reporting hop's start.
constexpr float MAX_DT_MS =
    static_cast<float>(cw::COHERENT_INTEGRATOR_HOP + cw::COHERENT_INTEGRATOR_SIZE) * 1000.0f / cw::SAMPLE_RATE;

// Drives the detector with a fixed per-frame SNR and returns the resulting
// level by tracking the reported edges.
bool drive_state(cw::Detector &det, float snr, int frames) {
    bool on = false;
    for (int i = 0; i < frames; ++i) {
        const cw::DetectorResult res = det.feed(snr, 1.0f); // noise = 1 => power == snr
        if (res.edge > 0)
            on = true;
        else if (res.edge < 0)
            on = false;
    }
    return on;
}

} // namespace

TEST_CASE("detector: pure noise stays OFF") {
    cw::Detector det;
    for (int i = 0; i < 20; ++i) {
        const cw::DetectorResult res = det.feed(1.0f, 1.0f);
        REQUIRE(res.edge == 0);
    }
}

TEST_CASE("detector: a signal above the absolute floor opens") {
    cw::Detector det;
    // 8 dB SNR is above the default 5 dB floor (3.16 linear).
    REQUIRE(drive_state(det, 8.0f, 5));
}

TEST_CASE("detector: the user floor raises the opening level") {
    cw::Detector det;
    det.set_threshold(20.0f); // floor 100 linear
    REQUIRE_FALSE(drive_state(det, 50.0f, 20));
}

TEST_CASE("detector: the user floor raises the release level") {
    cw::Detector det;
    REQUIRE(drive_state(det, 8.0f, 5));

    // Floor 20 dB: the release floor is 16 dB (39.8 linear), above the 8 dB
    // signal, so the detector drops back to OFF.
    det.set_threshold(20.0f);
    REQUIRE_FALSE(drive_state(det, 8.0f, 5));
}

// `edge` names the crossing and `dt` is valid only on it: a held frame reports
// edge 0, and a real crossing reports its direction with a non-negative offset
// behind the hop start (0 is a crossing exactly at the hop boundary).
TEST_CASE("detector: edge and dt are reported only on a crossing") {
    cw::Detector det;

    const cw::DetectorResult held_off = det.feed(1.0f, 1.0f);
    REQUIRE(held_off.edge == 0);
    REQUIRE(held_off.dt == 0.0f);

    cw::DetectorResult open{};
    for (int i = 0; i < 2; ++i)
        open = det.feed(8.0f, 1.0f); // the second above-threshold frame flips
    REQUIRE(open.edge == +1);
    REQUIRE(open.dt >= 0.0f);
    REQUIRE(open.dt <= MAX_DT_MS);

    const cw::DetectorResult held_on = det.feed(8.0f, 1.0f);
    REQUIRE(held_on.edge == 0);
    REQUIRE(held_on.dt == 0.0f);

    cw::DetectorResult close{};
    for (int i = 0; i < 2; ++i)
        close = det.feed(1.0f, 1.0f);
    REQUIRE(close.edge == -1);
    REQUIRE(close.dt >= 0.0f);
    REQUIRE(close.dt <= MAX_DT_MS);
}

// The peak reference decays while OFF, so a weak station after a strong one is
// heard again rather than being muted by the kept strong level.
TEST_CASE("detector: a weak station after a strong one is not muted") {
    cw::Detector det;
    REQUIRE(drive_state(det, 900.0f, 5));
    REQUIRE(drive_state(det, 8.0f, 200));
}
