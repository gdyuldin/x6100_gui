// Test-only access to the per-frame diagnostics of cw::CwReceiver. The class
// declares `friend struct CwReceiverDiagAccess;` (see cw_receiver.h); this
// header is never compiled into the production module. Used by the offline WAV
// inspector, which needs the SNR / NCO frequency / detector state of every
// frame.
#pragma once

#include "cw_receiver.h"

namespace cw {

struct CwReceiverDiagAccess {
    static float snr_db(const CwReceiver &rx) { return rx.diag_snr_db_; }
    static float nco_hz(const CwReceiver &rx) { return rx.diag_nco_hz_; }
    static float peak_freq_hz(const CwReceiver &rx) { return rx.diag_peak_freq_hz_; }
    static float snr_lin(const CwReceiver &rx) { return rx.diag_snr_lin_; }
    static float t_on_lin(const CwReceiver &rx) { return rx.diag_t_on_lin_; }
    static float t_off_lin(const CwReceiver &rx) { return rx.diag_t_off_lin_; }
    static int   stable_frames(const CwReceiver &rx) { return rx.diag_stable_frames_; }
    static bool  retuned(const CwReceiver &rx) { return rx.diag_retuned_; }
    static bool  is_active(const CwReceiver &rx) { return rx.diag_is_active_; }
};

} // namespace cw
