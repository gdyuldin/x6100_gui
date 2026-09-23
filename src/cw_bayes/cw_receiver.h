#pragma once

#include <array>
#include <complex>

#include "coherent_tone_tracker.h"
#include "cw_config.h"
#include "morse_decoder.h"
#include "power_detector.h"
#include "spgram_real.h"
#include "time_classifier.h"
#include "tone_level_tracker.h"

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
    // Runs the DSP pipeline on one ready-made spectrum. `raw_hop` is the newest
    // unwindowed audio of the frame, fed to the coherent tone tracker.
    void process_fft_frame(const ComplexSpectrum &fft_output, const RawHop &raw_hop);

    // Frame estimate: finds the precise peak frequency (stored in
    // current_freq_hz_) and the noise floor (stored in noise_power_smoothed_).
    void analyze_frame(const ComplexSpectrum &fft_output);

    // Recomputes the bin search region from hpf_hz_/lpf_hz_.
    void update_search_region();

    float               hpf_hz_                = DEFAULT_HPF_HZ;
    float               lpf_hz_                = DEFAULT_LPF_HZ;
    size_t              region_from_           = 0;
    size_t              region_to_             = 0;
    int                 last_stable_bin_       = -1;
    int                 bin_candidate_         = -1;
    int                 bin_candidate_counter_ = 0;
    CoherentToneTracker tone_tracker_;
    PowerDetector       detector_;
    TimeClassifier      classifier_;
    ToneLevelTracker    level_tracker_;
    MorseDecoder        decoder_;
    PowerSpectrum       power_spectrum_{};
    RegionScratch       percentile_scratch_{};
    float               noise_power_smoothed_ = -1.0f; // < 0 => not seeded yet
    float               current_freq_hz_      = 0.0f;
    bool                is_signal_detected    = false;
    EmitOnOffFn         emit_on_off;
    FrameFn             on_frame_;
    float               th_lin = 10.0f;
    SpgramReal          spgram_;
};

} // namespace cw
