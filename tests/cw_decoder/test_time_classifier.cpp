// Tests for cw::TimeClassifier: adaptive unit tracking, dot/dash and
// element/letter separation, the histogram leak and the retune handling. The
// ON/OFF decision is made by cw::Detector, so the classifier is driven with
// clean edges of a whole-frame duration (crossing exactly at the hop boundary,
// i.e. dt == 0). The sub-hop split has its own test.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "time_classifier.h"
#include "time_classifier_test_access.h"

using Catch::Approx;

namespace {

// Feeds the classifier edge by edge, tracking the level so a run in one state
// carries the edge only on its first frame.
struct Stream {
    cw::TimeClassifier     tc;
    std::vector<cw::Token> tokens;
    bool                   state = false;

    cw::Token frame(bool on, float dt = 0.0f) {
        const int edge = (on != state) ? (on ? +1 : -1) : 0;
        state          = on;
        cw::Token t    = tc.feed(edge, dt);
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
void adapt_25_wpm(Stream &s) {
    for (int rep = 0; rep < 20; ++rep) {
        s.on(6);
        s.off(6);
    }
}

// Feeds `on_frames` mark frames; the following OFF frame closes the mark and
// returns its token.
cw::Token close_mark(Stream &s, int on_frames) {
    s.on(on_frames);
    return s.frame(false);
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
    adapt_25_wpm(s);
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
    Stream s;
    adapt_25_wpm(s);
    REQUIRE(s.tc.get_current_wpm() == Approx(25.0f).margin(2.0f));

    REQUIRE(close_mark(s, 6) == cw::CW_DOT); // 48 ms
    for (int i = 0; i < 5; ++i)
        s.frame(false);                        // finish the element space
    REQUIRE(close_mark(s, 18) == cw::CW_DASH); // 144 ms
}

TEST_CASE("time classifier: on_retune keeps the state but keeps the speed") {
    Stream s;
    adapt_25_wpm(s);
    REQUIRE(s.tc.get_current_wpm() == Approx(25.0f).margin(2.0f));

    // Mid-mark retune: the level stays committed so the classifier keeps step
    // with the detector, which reports the next crossing itself. Only the
    // in-flight interval is dropped, so the mark is re-measured from here.
    s.on(6);
    s.tc.on_retune();
    REQUIRE(s.tc.is_signal_active());
    REQUIRE(s.tc.get_current_wpm() == Approx(25.0f).margin(2.0f));

    REQUIRE(close_mark(s, 6) == cw::CW_DOT);
}

// The end of a message has no following mark to close the OFF interval, so the
// classifier reports one word space after the word gap to flush the decoder.
TEST_CASE("time classifier: a long silence reports one word space") {
    Stream s;
    s.on(6); // dot
    REQUIRE(close_mark(s, 0) == cw::CW_DOT);

    // 960 ms of silence, well over the ~600 ms word gap at the default speed.
    int words = 0;
    for (int i = 0; i < 120; ++i) {
        if (s.frame(false) == cw::CW_WORD_SPACE)
            ++words;
    }
    REQUIRE(words == 1);
}

// Regression: dash-heavy text at 30 WPM used to drag the dot/dash boundary to
// the dash peak and collapse the reported speed to ~9.
TEST_CASE("time classifier: dash-heavy 30 WPM does not collapse the speed") {
    Stream s;
    // 30 WPM: dot = 40 ms = 5 frames, dash = 120 ms = 15 frames (fed 16 = 128 ms
    // to exercise the peak just above the old dot search window).
    for (int rep = 0; rep < 30; ++rep) {
        s.on(5);  // dot
        s.off(5); // element space
        s.on(16); // dash
        s.off(5); // element space
    }

    REQUIRE(s.tc.get_current_wpm() == Approx(30.0f).margin(2.0f));
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
    Stream s;
    adapt_25_wpm(s);
    REQUIRE(s.tc.get_current_wpm() == Approx(25.0f).margin(2.0f));

    // New station at 15 WPM: dot = 80 ms = 10 frames, dash = 240 ms = 30 frames.
    for (int rep = 0; rep < 30; ++rep) {
        s.on(10);  // dot
        s.off(10); // element space
        s.on(30);  // dash
        s.off(10); // element space
    }

    REQUIRE(s.tc.get_current_wpm() == Approx(15.0f).margin(2.0f));
}

TEST_CASE("time classifier: a single cluster still yields the right unit") {
    SECTION("dots only") {
        Stream s;
        for (int rep = 0; rep < 30; ++rep) {
            s.on(6);  // dot
            s.off(6); // element space
        }
        REQUIRE(s.tc.get_current_wpm() == Approx(25.0f).margin(2.0f));
    }

    SECTION("dashes only") {
        Stream s;
        for (int rep = 0; rep < 30; ++rep) {
            s.on(18); // dash = 144 ms
            s.off(6); // element space
        }
        REQUIRE(s.tc.get_current_wpm() == Approx(25.0f).margin(2.0f));
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
    Stream s;
    // 12 WPM: dot = 100 ms ~= 13 frames (104 ms), element space likewise.
    for (int rep = 0; rep < 30; ++rep) {
        s.on(13);  // dot
        s.off(13); // element space
    }
    REQUIRE(s.tc.get_current_wpm() < 20.0f); // catches the u/3 latch
    REQUIRE(s.tc.get_current_wpm() == Approx(12.0f).margin(2.0f));
}

// A mixed mark/space stream whose intervals are quantized a frame apart must
// still resolve to the true unit: the unified histogram averages them back.
TEST_CASE("time classifier: stretched mark and shortened space compensate") {
    Stream s;
    // Nominal unit 60 ms (20 WPM): the mark is fed one frame long (64 ms) and
    // the space one frame short (56 ms), so the merged centroid stays at 60 ms.
    for (int rep = 0; rep < 40; ++rep) {
        s.on(8);
        s.off(7);
    }
    REQUIRE(s.tc.get_current_wpm() == Approx(20.0f).margin(2.0f));
}

// A crossing frame carries the offset behind the start of the reporting hop:
// the closing interval gives the post-crossing part back and the new interval
// opens with it, so each hop is counted exactly once.
TEST_CASE("time classifier: the transition hop is split at the crossing offset") {
    using Access = cw::TimeClassifierTestAccess;
    cw::TimeClassifier tc;
    Access::clear_histograms(tc);
    constexpr float DT = 3.0f; // crossing 3 ms before the hop boundary

    tc.feed(+1, 0.0f);
    for (int i = 0; i < 2; ++i)
        tc.feed(0, 0.0f);
    REQUIRE(Access::current_duration_ms(tc) == Approx(3.0f * cw::BIN_SIZE_MS));

    tc.feed(-1, DT); // falling edge
    // The new OFF interval opens with one hop plus the offset.
    REQUIRE(Access::current_duration_ms(tc) == Approx(cw::BIN_SIZE_MS + DT));

    // The closing mark was shortened by the offset: 3*BIN - DT. Only one sample
    // is in the histogram, so the bins around its centre hold one unit of mass.
    const float closed = 3.0f * cw::BIN_SIZE_MS - DT;
    const int   center = static_cast<int>(std::lround(closed / cw::HIST_BIN_MS));
    float       mass   = 0.0f;
    for (int bin = center - 4; bin <= center + 4; ++bin)
        mass += Access::hist(tc)[bin];
    REQUIRE(mass == Approx(1.0f).margin(0.01f));
}

// dt == 0 is a crossing exactly at the hop boundary: the opening interval takes
// the whole hop and the closing one is not modified.
TEST_CASE("time classifier: a zero crossing offset is a full hop") {
    using Access = cw::TimeClassifierTestAccess;
    cw::TimeClassifier tc;
    Access::clear_histograms(tc);

    tc.feed(+1, 0.0f);
    for (int i = 0; i < 3; ++i)
        tc.feed(0, 0.0f);
    tc.feed(-1, 0.0f);

    REQUIRE(Access::current_duration_ms(tc) == Approx(cw::BIN_SIZE_MS));

    // Closing mark = 4*BIN, no split.
    const int center = static_cast<int>(std::lround(4.0f * cw::BIN_SIZE_MS / cw::HIST_BIN_MS));
    float     mass   = 0.0f;
    for (int bin = center - 4; bin <= center + 4; ++bin)
        mass += Access::hist(tc)[bin];
    REQUIRE(mass == Approx(1.0f).margin(0.01f));
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
