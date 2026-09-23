#pragma once

#include <array>
#include <complex>

#include "cw_config.h"
#include "morse_decoder.h"
#include "power_detector.h"
#include "spgram_real.h"
#include "time_classifier.h"

namespace cw {

// Application-facing entry point. Owns the FFT-frame DSP pipeline and the three
// sub-components (power detector, timing classifier, Morse decoder).
class CwReceiver {
  public:
    using EmitTextFn  = MorseDecoder::EmitTextFn;
    using EmitOnOffFn = std::function<void(bool)>;
    using FrameFn     = std::function<void()>;

    // Only public constant of the module: the rate the audio stream must be fed
    // with (see cw_config.h note).
    static constexpr float SAMPLE_RATE = cw::SAMPLE_RATE;

    static constexpr float DEFAULT_HPF_HZ        = 400.0f;
    static constexpr float DEFAULT_LPF_HZ        = 1200.0f;
    static constexpr float DEFAULT_THRESHOLD_DB  = 10.0f;
    static constexpr float NOISE_PERCENTILE      = 0.25f;
    static constexpr float NOISE_SMOOTHING_ALPHA = 0.15f;

    CwReceiver(EmitTextFn emit_text, EmitOnOffFn emit_on_off, FrameFn on_frame = {});

    // Feeds a raw 4 kHz real-audio block. Runs the streaming FFT internally and
    // processes every frame it produces (the internal sample counter persists
    // across calls, so a block may contain several frames or a partial one).
    void process_audio_frame(size_t n, float *samples);

    float get_measured_wpm() const;
    float get_tone_freq() const;
    void  change_threshold(float db_val);
    void  change_hpf_hz(float hz);
    void  change_lpf_hz(float hz);

  private:
    // Runs the DSP pipeline on one ready-made spectrum.
    void process_fft_frame(const ComplexSpectrum &fft_output);

    // Returns the raw LLR of the frame and writes the precise peak frequency.
    float process_fft_frame_raw_llr(const ComplexSpectrum &fft_output, float &out_precise_freq);

    // Recomputes the bin search region from hpf_hz_/lpf_hz_ and invalidates the
    // frequency-jump history.
    void update_search_region();

    float          hpf_hz_          = DEFAULT_HPF_HZ;
    float          lpf_hz_          = DEFAULT_LPF_HZ;
    size_t         region_from_     = 0;
    size_t         region_to_       = 0;
    int            last_stable_bin_ = -1;
    PowerDetector  detector_;
    TimeClassifier classifier_;
    MorseDecoder   decoder_;
    PowerSpectrum  power_spectrum_{};
    RegionScratch  percentile_scratch_{};
    float          noise_power_smoothed_ = -1.0f; // < 0 => not seeded yet
    float          current_freq_hz_      = 0.0f;
    bool           is_signal_detected    = false;
    EmitOnOffFn    emit_on_off;
    FrameFn        on_frame_;
    float          th_lin = 10.0f;
    SpgramReal     spgram_;
};

} // namespace cw
