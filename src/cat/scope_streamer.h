#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

class CivPacketView;
class CivTxPacker;

// Number of spectrum bins in a CI-V scope frame. The DSP decimates its
// DSP_MAX_NFFT transform down to this size before calling push_data().
#define SCOPE_NBINS 475

// Notify callback type: called from DSP thread with a formatted CI-V packet.
// The callback pushes the packet into cat/lan's send queue.
using scope_notify_cb_t = void (*)(std::string_view);

// Active-state callback type: called when the scope streamer starts or stops
// needing PSD data (scope data output toggled, or the notify callback set to
// nullptr). The owner of the PSD subscription (cat_lan) forwards this to
// psd->set_active(), so a paused scope does not accumulate frames.
using scope_active_cb_t = void (*)(bool active);

// Cadence callback type: how many BASE chunks must accumulate into one scope
// frame. Registered by the owner of the PSD subscription (cat_lan); it is
// invoked immediately with the current value on registration and on every
// sweep-speed change.
using scope_cadence_cb_t = void (*)(uint16_t chunks_per_frame);

// Register the active notify callback. Only one path (LAN or serial) is active.
void scope_streamer_set_notify(scope_notify_cb_t cb);

// Register the active-state callback. It is invoked immediately with the
// current state so the caller can sync.
void scope_streamer_set_active_cb(scope_active_cb_t cb);

// Register the cadence callback. It is invoked immediately with the current
// cadence so the caller can sync the live PSD subscription.
void scope_streamer_set_cadence_cb(scope_cadence_cb_t cb);

// Called from the DSP PSD subscription handler.
// Takes exactly SCOPE_NBINS float PSD values in dB (already decimated by dsp),
// scales them to 0-200, formats a CI-V 0x27 0x00 packet, and calls the notify
// callback.
// center_freq = base_freq from dsp, width_hz = FULL_BW_HZ / zoom.
// min/max are already resolved by dsp (auto/manual/offset/tx) - this module
// applies no level policy of its own.
void scope_streamer_push_data(const float *psd_db, size_t len,
                               uint32_t center_freq, uint32_t width_hz, float min, float max);

// Handle 0x27 subcommand get/set. Returns finalized packet if handled,
// empty string_view if caller should use set_unsupported.
std::string_view scope_streamer_handle_27(const CivPacketView &req, CivTxPacker &resp);

// Update the scope center frequency (from cp_fg_freq changes).
void scope_streamer_set_center_freq(int32_t freq_hz);
