#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

class CivPacketView;
class CivTxPacker;

// Notify callback type: called from DSP thread with a formatted CI-V packet.
// The callback pushes the packet into cat/lan's send queue.
using scope_notify_cb_t = void (*)(std::string_view);

// Register the active notify callback. Only one path (LAN or serial) is active.
void scope_streamer_set_notify(scope_notify_cb_t cb);

// Called from dsp.cpp after update_waterfall_psd() succeeds.
// Takes float PSD values in dB, downsamples 1024->475, scales to 0-200,
// formats a CI-V 0x27 0x00 packet, and calls the notify callback.
// center_freq = base_freq from dsp, width_hz = FULL_BW_HZ / zoom
void scope_streamer_push_data(const float *psd_db, size_t len,
                               uint32_t center_freq, uint32_t width_hz);

// Handle 0x27 subcommand get/set. Returns finalized packet if handled,
// empty string_view if caller should use set_unsupported.
std::string_view scope_streamer_handle_27(const CivPacketView &req, CivTxPacker &resp);

// Update the scope center frequency (from cp_fg_freq changes).
void scope_streamer_set_center_freq(int32_t freq_hz);
