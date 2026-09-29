// Tests for cw::TimeClassifier: adaptive unit tracking, dot/dash and
// element/letter separation, the histogram leak and the retune reset. The
// ON/OFF decision is made by cw::Detector, so the classifier is driven with
// clean on/off intervals of a fixed frame duration.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "time_classifier.h"
#include "time_classifier_test_access.h"

using Catch::Approx;

namespace {

// The classifier is fed once per coherent integrator hop (8 ms), which is also
// the histogram resolution.
constexpr float FRAME_MS = static_cast<float>(cw::COHERENT_INTEGRATOR_HOP) * 1000.0f / cw::SAMPLE_RATE;

// Feeds a stream and records every non-empty token.
struct Stream {
    cw::TimeClassifier     tc;
    std::vector<cw::Token> tokens;

    cw::Token frame(bool on) {
        cw::Token t = tc.feed(on, FRAME_MS);
        if (t != cw::CW_NONE)
            tokens.push_back(t);
        return t;
    }

    void on(int n) {
        for (int i = 0; i < n; ++i)
            frame(true);
    }
    void off(int n) {
        for (int i = 0; i < n; ++i)
            frame(false);
    }
};

// 25 WPM is 48 ms = 6 frames.
void adapt_25_wpm(cw::TimeClassifier &tc) {
    for (int rep = 0; rep < 20; ++rep) {
        for (int i = 0; i < 6; ++i)
            tc.feed(true, FRAME_MS);
        for (int i = 0; i < 6; ++i)
            tc.feed(false, FRAME_MS);
    }
}

// Feeds `on_frames` mark frames; the following OFF frame closes the mark and
// returns its token.
cw::Token close_mark(cw::TimeClassifier &tc, int on_frames) {
    for (int i = 0; i < on_frames; ++i)
        tc.feed(true, FRAME_MS);
    return tc.feed(false, FRAME_MS);
}

int count(const std::vector<cw::Token> &tokens, cw::Token want) {
    int n = 0;
    for (cw::Token t : tokens)
        if (t == want)
            ++n;
    return n;
}

// From the first DOT, the stream must alternate DOT and ELEMENT_SPACE.
void require_dot_element_pairs(const std::vector<cw::Token> &tokens) {
    size_t i = 0;
    while (i < tokens.size() && tokens[i] != cw::CW_DOT)
        ++i;
    REQUIRE(i < tokens.size());
    for (size_t k = 0; i + k < tokens.size(); ++k)
        REQUIRE(tokens[i + k] == ((k % 2 == 0) ? cw::CW_DOT : cw::CW_ELEMENT_SPACE));
}

} // namespace

TEST_CASE("time classifier: 21 WPM dot and element space") {
    Stream s;
    adapt_25_wpm(s.tc);
    // 7 frames = 56 ms -> ~21.4 WPM, a touch slower than the 25 WPM warm-up.
    for (int rep = 0; rep < 20; ++rep) {
        s.on(7);
        s.off(7);
    }
    s.off(8); // drain

    require_dot_element_pairs(s.tokens);
    REQUIRE(s.tc.get_current_wpm() == Approx(21.0f).margin(2.0f));
}

TEST_CASE("time classifier: dot vs dash after adaptation") {
    cw::TimeClassifier tc;
    adapt_25_wpm(tc);
    REQUIRE(tc.get_current_wpm() == Approx(25.0f).margin(2.0f));

    REQUIRE(close_mark(tc, 6) == cw::CW_DOT);   // 48 ms
    for (int i = 0; i < 5; ++i)
        tc.feed(false, FRAME_MS); // finish the element space
    REQUIRE(close_mark(tc, 18) == cw::CW_DASH); // 144 ms
}

TEST_CASE("time classifier: on_retune drops the edge but keeps the speed") {
    cw::TimeClassifier tc;
    adapt_25_wpm(tc);
    REQUIRE(tc.get_current_wpm() == Approx(25.0f).margin(2.0f));

    // Mid-mark retune: the in-flight mark is dropped, not measured across two
    // stations, while the learned speed survives.
    for (int i = 0; i < 6; ++i)
        tc.feed(true, FRAME_MS);
    tc.on_retune();
    REQUIRE_FALSE(tc.is_signal_active());
    REQUIRE(tc.get_current_wpm() == Approx(25.0f).margin(2.0f));

    REQUIRE(close_mark(tc, 6) == cw::CW_DOT);
}

// The end of a message has no following mark to close the OFF interval, so the
// classifier reports one word space after the word gap to flush the decoder.
TEST_CASE("time classifier: a long silence reports one word space") {
    cw::TimeClassifier tc;
    for (int i = 0; i < 6; ++i)
        tc.feed(true, FRAME_MS); // dot
    REQUIRE(close_mark(tc, 0) == cw::CW_DOT);

    // 960 ms of silence, well over the ~600 ms word gap at the default speed.
    int words = 0;
    for (int i = 0; i < 120; ++i) {
        if (tc.feed(false, FRAME_MS) == cw::CW_WORD_SPACE)
            ++words;
    }
    REQUIRE(words == 1);
}

// Regression: dash-heavy text at 30 WPM used to drag the dot/dash boundary to
// the dash peak and collapse the reported speed to ~9.
TEST_CASE("time classifier: dash-heavy 30 WPM does not collapse the speed") {
    cw::TimeClassifier tc;
    // 30 WPM: dot = 40 ms = 5 frames, dash = 120 ms = 15 frames (fed 16 = 128 ms
    // to exercise the peak just above the old dot search window).
    for (int rep = 0; rep < 30; ++rep) {
        for (int i = 0; i < 5; ++i)
            tc.feed(true, FRAME_MS); // dot
        for (int i = 0; i < 5; ++i)
            tc.feed(false, FRAME_MS); // element space
        for (int i = 0; i < 16; ++i)
            tc.feed(true, FRAME_MS); // dash
        for (int i = 0; i < 5; ++i)
            tc.feed(false, FRAME_MS); // element space
    }

    REQUIRE(tc.get_current_wpm() == Approx(30.0f).margin(2.0f));
}

// Regression: a station that jumps from a slower mixed text to a dash-only
// 30 WPM text (letter O) used to hunt between the dash cluster (10 WPM) and the
// unit (30 WPM). Each relock empties the histogram, the first samples report the
// dash length as a far candidate, relock again, then the element spaces pull it
// back. The speed must settle on the dash-only station instead.
TEST_CASE("time classifier: dash-only 30 WPM after a slower station settles") {
    Stream s;
    // Slower station, mixed content: 12 WPM dot = 13 frames, dash = 38 frames.
    for (int rep = 0; rep < 25; ++rep) {
        s.on(13);
        s.off(13);
        s.on(38);
        s.off(13);
    }
    REQUIRE(s.tc.get_current_wpm() == Approx(12.0f).margin(2.0f));

    // O = --- at 30 WPM: dash = 15 frames, element space = 5, letter space = 15
    // plus a short pause before the next O. Feed a few groups to let the relock
    // settle, then require dashes-only decoding and a stable speed.
    for (int rep = 0; rep < 4; ++rep) {
        for (int d = 0; d < 3; ++d) {
            s.on(15);
            s.off(d < 2 ? 5 : 15);
        }
        s.off(15);
    }
    s.tokens.clear();

    for (int rep = 0; rep < 6; ++rep) {
        for (int d = 0; d < 3; ++d) {
            s.on(15);
            s.off(d < 2 ? 5 : 15);
        }
        s.off(15);
        REQUIRE(s.tc.get_current_wpm() > 24.0f); // catches the 10 WPM dash latch
        REQUIRE(s.tc.get_current_wpm() == Approx(30.0f).margin(3.0f));
    }

    // No dot may be emitted: the dashes must not be read as dots (O -> S/K).
    REQUIRE(count(s.tokens, cw::CW_DOT) == 0);
    REQUIRE(count(s.tokens, cw::CW_DASH) >= 12);
}

TEST_CASE("time classifier: tracks a station changing speed") {
    cw::TimeClassifier tc;
    adapt_25_wpm(tc);
    REQUIRE(tc.get_current_wpm() == Approx(25.0f).margin(2.0f));

    // New station at 15 WPM: dot = 80 ms = 10 frames, dash = 240 ms = 30 frames.
    for (int rep = 0; rep < 30; ++rep) {
        for (int i = 0; i < 10; ++i)
            tc.feed(true, FRAME_MS); // dot
        for (int i = 0; i < 10; ++i)
            tc.feed(false, FRAME_MS); // element space
        for (int i = 0; i < 30; ++i)
            tc.feed(true, FRAME_MS); // dash
        for (int i = 0; i < 10; ++i)
            tc.feed(false, FRAME_MS); // element space
    }

    REQUIRE(tc.get_current_wpm() == Approx(15.0f).margin(2.0f));
}

TEST_CASE("time classifier: a single cluster still yields the right unit") {
    SECTION("dots only") {
        cw::TimeClassifier tc;
        for (int rep = 0; rep < 30; ++rep) {
            for (int i = 0; i < 6; ++i)
                tc.feed(true, FRAME_MS); // dot
            for (int i = 0; i < 6; ++i)
                tc.feed(false, FRAME_MS); // element space
        }
        REQUIRE(tc.get_current_wpm() == Approx(25.0f).margin(2.0f));
    }

    SECTION("dashes only") {
        cw::TimeClassifier tc;
        for (int rep = 0; rep < 30; ++rep) {
            for (int i = 0; i < 18; ++i)
                tc.feed(true, FRAME_MS); // dash = 144 ms
            for (int i = 0; i < 6; ++i)
                tc.feed(false, FRAME_MS); // element space
        }
        REQUIRE(tc.get_current_wpm() == Approx(25.0f).margin(2.0f));
    }
}

TEST_CASE("time classifier: the leak decays the histogram once per sample") {
    cw::TimeClassifier tc;
    cw::TimeClassifierTestAccess::clear_histograms(tc);
    cw::TimeClassifierTestAccess::set_hist(tc, 3, 1.0f);
    cw::TimeClassifierTestAccess::set_hist(tc, 40, 2.0f);

    // 96 ms -> bin 24 (HIST_BIN_MS = 4): the sample lands away from the seeded bins.
    cw::TimeClassifierTestAccess::add_sample(tc, 96.0f);

    REQUIRE(cw::TimeClassifierTestAccess::hist(tc)[3] == Approx(cw::HIST_FORGET));
    REQUIRE(cw::TimeClassifierTestAccess::hist(tc)[40] == Approx(2.0f * cw::HIST_FORGET));

    float added = 0.0f;
    for (int bin = 20; bin <= 28; ++bin)
        added += cw::TimeClassifierTestAccess::hist(tc)[bin];
    REQUIRE(added == Approx(1.0f).margin(0.01f));
}

TEST_CASE("time classifier: an out-of-range interval still ages the history") {
    cw::TimeClassifier tc;
    cw::TimeClassifierTestAccess::clear_histograms(tc);
    cw::TimeClassifierTestAccess::set_hist(tc, 0, 1.0f);

    // 5000 ms -> bin 1250, outside HIST_BINS: no sample written, but the decay
    // must still apply (no gate bypasses it).
    cw::TimeClassifierTestAccess::add_sample(tc, 5000.0f);

    REQUIRE(cw::TimeClassifierTestAccess::hist(tc)[0] == Approx(cw::HIST_FORGET));
}

// Regression: a single cluster (dots and element spaces only) used to alias as
// a phantom dash at u/3, dividing the unit by three and latching the speed.
TEST_CASE("time classifier: a single dot cluster does not alias") {
    cw::TimeClassifier tc;
    // 12 WPM: dot = 100 ms ~= 13 frames (104 ms), element space likewise.
    for (int rep = 0; rep < 30; ++rep) {
        for (int i = 0; i < 13; ++i)
            tc.feed(true, FRAME_MS); // dot
        for (int i = 0; i < 13; ++i)
            tc.feed(false, FRAME_MS); // element space
    }
    REQUIRE(tc.get_current_wpm() < 20.0f); // catches the u/3 latch
    REQUIRE(tc.get_current_wpm() == Approx(12.0f).margin(2.0f));
}

// The crossing stretches a mark and shortens the following space by the same
// amount; the unified histogram must average them back to the true unit.
TEST_CASE("time classifier: stretched mark and shortened space compensate") {
    cw::TimeClassifier tc;
    // Nominal unit 60 ms (20 WPM): the mark is fed one frame long (64 ms) and
    // the space one frame short (56 ms), so the merged centroid stays at 60 ms.
    for (int rep = 0; rep < 40; ++rep) {
        for (int i = 0; i < 8; ++i)
            tc.feed(true, FRAME_MS);
        for (int i = 0; i < 7; ++i)
            tc.feed(false, FRAME_MS);
    }
    REQUIRE(tc.get_current_wpm() == Approx(20.0f).margin(2.0f));
}

TEST_CASE("time classifier: 50 WPM dots are not suppressed") {
    Stream s;
    // 50 WPM: dot and element space are 24 ms = 3 frames.
    for (int rep = 0; rep < 40; ++rep) {
        s.on(3);
        s.off(3);
    }
    s.off(8); // drain

    require_dot_element_pairs(s.tokens);
    REQUIRE(s.tc.get_current_wpm() == Approx(50.0f).margin(2.0f));
}
