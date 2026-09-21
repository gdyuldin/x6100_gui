#include "time_classifier.h"

#include <algorithm>
#include <cmath>

namespace cw {

float TimeClassifier::find_precise_peak(const std::array<float, HIST_BINS> &hist, size_t start_bin,
                                        size_t end_bin) const {
    float max_smoothed_val = 0.0f;
    int   best_bin         = -1;

    size_t search_start = std::max(start_bin, size_t(1));
    size_t search_end   = std::min(end_bin, HIST_BINS - 2);

    // Rectangular convolution over 3 bins (bin-1, bin, bin+1) to filter noise
    for (size_t i = search_start; i <= search_end; ++i) {
        float smoothed_val = hist[i - 1] + hist[i] + hist[i + 1];
        if (smoothed_val > max_smoothed_val) {
            max_smoothed_val = smoothed_val;
            best_bin         = static_cast<int>(i);
        }
    }

    // Peak weight floor 1.5f: ignore histograms that are still too sparse
    if (best_bin == -1 || max_smoothed_val < 1.5f)
        return -1.0f;

    // Weighted mean (centre of mass) for sub-bin peak precision
    float w_left      = hist[best_bin - 1];
    float w_center    = hist[best_bin];
    float w_right     = hist[best_bin + 1];
    float sum_weights = w_left + w_center + w_right;

    if (sum_weights < 1e-5f)
        return static_cast<float>(best_bin);

    return (static_cast<float>(best_bin - 1) * w_left + static_cast<float>(best_bin) * w_center +
            static_cast<float>(best_bin + 1) * w_right) /
           sum_weights;
}

void TimeClassifier::update_thresholds() {
    // Supports roughly 10-50 WPM (search windows above are tuned for that)
    // Search windows are in bins (1 bin = BIN_SIZE_MS ms):
    constexpr int min_dot = WPM_K / 50 / BIN_SIZE_MS;
    constexpr int max_dot = WPM_K / 10 / BIN_SIZE_MS;
    constexpr int min_dash = WPM_K * 3 / 50 / BIN_SIZE_MS;
    constexpr int max_dash = WPM_K * 3 / 10 / BIN_SIZE_MS;
    constexpr int min_space = WPM_K * 7 / 50 / BIN_SIZE_MS;
    constexpr int max_space = WPM_K * 7 / 10 / BIN_SIZE_MS;

    float dot_bin  = find_precise_peak(hist_on_, min_dot, max_dot);
    float dash_bin = find_precise_peak(hist_on_, min_dash, max_dash);

    if (dot_bin > 0.0f && dash_bin > 0.0f && dash_bin > dot_bin) {
        threshold_dot_dash_ = static_cast<int>(((dot_bin + dash_bin) / 2.0f) * BIN_SIZE_MS);
    } else if (dot_bin > 0.0f) {
        threshold_dot_dash_ = static_cast<int>(dot_bin * BIN_SIZE_MS * 2.0f);
    }

    float elem_space_bin   = find_precise_peak(hist_off_, min_dot, max_dot);
    float letter_space_bin = find_precise_peak(hist_off_, min_dash, max_dash);
    float word_space_bin   = find_precise_peak(hist_off_, min_space, max_space);

    if (elem_space_bin > 0.0f && letter_space_bin > 0.0f && letter_space_bin > elem_space_bin) {
        threshold_elem_letter_ = static_cast<int>(((elem_space_bin + letter_space_bin) / 2.0f) * BIN_SIZE_MS);
    } else if (elem_space_bin > 0.0f) {
        threshold_elem_letter_ = static_cast<int>(elem_space_bin * BIN_SIZE_MS * 2.0f);
    }

    if (letter_space_bin > 0.0f && word_space_bin > 0.0f && word_space_bin > letter_space_bin) {
        threshold_letter_word_ = static_cast<int>(((letter_space_bin + word_space_bin) / 2.0f) * BIN_SIZE_MS);
    } else if (elem_space_bin > 0.0f) {
        // Fallback: letter/word boundary is about 5 element lengths, but the
        // element space is the closest adapted anchor available when the longer
        // spaces have not been seen yet.
        threshold_letter_word_ = static_cast<int>(elem_space_bin * BIN_SIZE_MS * 5.0f);
    }
}

void TimeClassifier::add_to_histogram(std::array<float, HIST_BINS> &hist, int duration_ms, float weight) {
    size_t bin = static_cast<size_t>(duration_ms / BIN_SIZE_MS);
    if (bin < HIST_BINS) {
        hist[bin] += weight;
        sample_count_++;

        // Forgetting mechanism (leak) to track QSO speed changes: after 180
        // samples decay both histograms by 0.75
        if (sample_count_ > 180) {
            for (size_t i = 0; i < HIST_BINS; ++i) {
                hist_on_[i] *= 0.75f;
                hist_off_[i] *= 0.75f;
            }
            sample_count_ = 0;
        }
    }
}

Token TimeClassifier::feed_frame_llr(float current_frame_llr) {
    Token emitted_token = CW_NONE;

    // Two-level Schmitt trigger (+3.0 / -3.0): protects the timing geometry
    // from SNR drops
    bool next_state = is_now_on_;
    if (current_frame_llr > 3.0f)
        next_state = true;
    else if (current_frame_llr < -3.0f)
        next_state = false;

    if (next_state == is_now_on_) {
        current_duration_ms_ += BIN_SIZE_MS;
        accumulated_llr_ += current_frame_llr;
        frame_count_++;
        if (!is_now_on_) {
            // Handle long OFF
            idle_frames_counter++;

            // 200 * 8ms = 1.6s of silence
            if (idle_frames_counter == 200) {
                // reset
                idle_frames_counter = 0;
                return CW_WORD_SPACE;
            }
        } else {
            idle_frames_counter = 0;
        }
    } else {
        // Physical switching edge: the interval is complete
        if (current_duration_ms_ >= 15) {
            float mean_llr         = accumulated_llr_ / static_cast<float>(frame_count_);
            float mean_probability = 1.0f / (1.0f + std::exp(-mean_llr)); // exp() once per symbol

            if (is_now_on_) {
                add_to_histogram(hist_on_, current_duration_ms_, mean_probability);
                emitted_token = (current_duration_ms_ < threshold_dot_dash_) ? CW_DOT : CW_DASH;
            } else {
                float mean_noise_probability = 1.0f - mean_probability;
                add_to_histogram(hist_off_, current_duration_ms_, mean_noise_probability);

                if (current_duration_ms_ < threshold_elem_letter_)
                    emitted_token = CW_ELEMENT_SPACE;
                else if (current_duration_ms_ < threshold_letter_word_)
                    emitted_token = CW_LETTER_SPACE;
                else
                    emitted_token = CW_WORD_SPACE;
            }
            update_thresholds();
        }

        // Initialise the accumulators for the next interval
        is_now_on_           = next_state;
        current_duration_ms_ = BIN_SIZE_MS;
        accumulated_llr_     = current_frame_llr;
        frame_count_         = 1;
    }
    return emitted_token;
}

float TimeClassifier::get_current_wpm() const {
    // Dot length is 1200/WPM ms and the dot/dash boundary sits at about two
    // dots, so WPM = 2400 / boundary_ms.
    return WPM_K * 2.0f / static_cast<float>(threshold_dot_dash_);
}

bool TimeClassifier::is_signal_active() const {
    return is_now_on_;
}

} // namespace cw
