#pragma once

#include <array>
#include <complex>

#include "coherent_tone_tracker.h"
#include "cw_config.h"
#include "detector.h"
#include "morse_decoder.h"
#include "peak_filter_response.h"
#include "spgram_real.h"
#include "time_classifier.h"

namespace cw {

// Grants the WAV inspector access to the per-frame diagnostics without widening
// the public interface. Defined only in the test tree (tests/cw_decoder/).
struct CwReceiverDiagAccess;

// Application-facing entry point. Owns the FFT-frame DSP pipeline and the three
// sub-components (coherent tone tracker, detector, timing classifier) plus the
// Morse decoder.
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

    // Tells the receiver whether the external analog peak filter is in the audio
    // path, and its centre/Q. The receiver then whitens the spectrum by the
    // filter's known response before estimating noise, so the detector keeps a
    // filter-independent SNR. `on == false` disables the correction and costs
    // nothing (the default).
    void change_peak_filter(bool on, float key_tone_hz, float q);

  private:
    friend struct CwReceiverDiagAccess;

    // Measures one FFT frame: interpolated peak frequency, per-bin SNR and the
    // run of consecutive strong frames near the same bin. No policy here.
    FrameObservation analyze_frame(const ComplexSpectrum &fft_output);

    // Recomputes the bin search region from hpf_hz_/lpf_hz_.
    void update_search_region();

    float               hpf_hz_                = DEFAULT_HPF_HZ;
    float               lpf_hz_                = DEFAULT_LPF_HZ;
    size_t              region_from_           = 0;
    size_t              region_to_             = 0;
    CoherentToneTracker tone_tracker_;
    Detector            detector_;
    TimeClassifier      classifier_;
    MorseDecoder        decoder_;
    PeakFilterResponse  peak_filter_;
    PowerSpectrum       power_spectrum_{};
    RegionScratch       percentile_scratch_{};
    float               noise_bin_smoothed_    = -1.0f; // per-bin noise, < 0 => not seeded
    float               noise_integr_          = -1.0f; // aligned to the coherent integrator
    int                 prev_peak_bin_         = -1;    // last strong frame's peak bin
    int                 stable_frames_         = 0;     // consecutive strong frames near it
    float               current_freq_hz_       = 0.0f;
    bool                is_signal_detected     = false;
    EmitOnOffFn         emit_on_off;
    FrameFn             on_frame_;

    SpgramReal          spgram_;

    // Per-frame diagnostics of the last processed frame, written just before
    // on_frame_ fires and read only by CwReceiverDiagAccess (the offline WAV
    // inspector). Inert for the DSP path.
    float diag_snr_db_        = 0.0f;
    float diag_nco_hz_        = 0.0f;
    float diag_peak_freq_hz_  = 0.0f;
    float diag_snr_lin_       = 0.0f;
    float diag_t_on_lin_      = 0.0f;
    float diag_t_off_lin_     = 0.0f;
    int   diag_stable_frames_ = 0;
    bool  diag_retuned_       = false;
    bool  diag_is_active_     = false;
};

} // namespace cw
