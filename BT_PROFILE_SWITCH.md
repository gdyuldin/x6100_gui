# Bluetooth headset profile switch (experimental)

Status: **implemented but disabled by default**. The feature works often enough
to be useful, but switching from HSP/HFP back to A2DP is unreliable on the
tested hardware. This note records what was tried so someone else can pick it
up.

## What the feature does

When a connected Bluetooth **audio** device exposes both the A2DP and the
HSP/HFP profile, the 4th button of the Bluetooth dialog (`Pair`) instead shows
the active profile (`Profile: A2DP` / `Profile: HSP/HFP`) and toggles it on
press. It is only a card-profile switch; no audio is rerouted by this feature.

## How to enable

Change one line in `src/dialog_bt.c`:

```c
#define BT_PROFILE_SWITCH 0   /* -> 1 */
```

With `0` (default) the 4th button keeps its normal Pair behaviour and the
profile code paths in the dialog are compiled out. The PulseAudio side in
`src/audio.c` is left unconditional and harmless when the feature is off.

## Tested hardware / software

- Headset: HUAWEI FreeBuds 5i (`80:CC:12:30:E2:E9`).
- PulseAudio 16.1, module-bluez5, BlueZ.
- Card name: `bluez_card.80_CC_12_30_E2_E9`.
- Profile names: `a2dp_sink`, `handsfree_head_unit`, `off`.

## Observed problem

While HSP/HFP is active, `pactl list cards` reports both `a2dp_sink` and
`handsfree_head_unit` as `available: yes`. After switching to A2DP, the card
falls back to the `off` profile and `a2dp_sink` becomes `available: no`.
A manual switch experiences the same:

```
pactl set-card-profile <card> a2dp_sink
Failure: Input/Output error
```

Relevant PulseAudio log lines:

```
module-bluez5-device.c: Acquiring transport /org/bluez/hci0/dev_.../sep1/fd9
bluez5-util.c: Transport Acquire() failed for transport .../sep1/fd9 (Input/output error)
bluez5-util.c: Clearing transport .../sep1/fd9 profile a2dp_sink
card.c: Setting card ... profile a2dp_sink to availability status no
bluez5-util.c: Remote endpoint .../sep1 was removed
bluez5-util.c: Remote endpoint .../sep2 was removed
bluez5-util.c: Remote endpoint .../sep4 was removed
```

Interpretation: PulseAudio accepts the profile change, but BlueZ fails to
acquire the A2DP transport (`MediaEndpoint.Acquire` returns `EIO`) and the
headset then drops its A2DP endpoints. This is below the GUI and below the
profile-selection logic; the app cannot make `Acquire()` succeed directly.

## Current implementation (behind the switch)

- HFP -> A2DP is done as a two-step change: first the `off` profile, then
  `a2dp_sink`. Going through `off` also makes the BlueZ result explicit
  (`success=0` on failure) instead of silently ending in `off`.
- All `module-loopback` instances are unloaded before a profile switch, because
  a loopback holding the BlueZ sink/source blocks the card reconfiguration.
- If the A2DP change fails, the app reconnects the device
  (`bt_disconnect` -> wait -> `bt_connect`) and retries `a2dp_sink`, up to 2
  attempts. The reconnect runs on the UI thread (BT uses the GLib main loop
  pumped by an LVGL timer). This recovery was confirmed by the reporter to make
  switching work.
- The settle delay between `off` and `a2dp_sink` was removed: with reconnect in
  place a delay only slows the common case.

## Tested variants

| # | Variant | Result |
|---|---------|--------|
| 1 | Direct `a2dp_sink` from HSP (app) | Fails; card ends in `off`, no error surfaced to the app |
| 2 | `pactl` `off` then `a2dp_sink`, app stopped | Works (but not reliably reproducible from the app) |
| 3 | `off` then `a2dp_sink` immediately (app) | Intermittent `EIO`; card ends in `off` |
| 4 | `off` then 2 s settle then `a2dp_sink` | Worked once, failed once; removed |
| 5 | Unload loopbacks before the switch | Fewer failures, but not sufficient on its own |
| 6 | Reconnect device on A2DP failure, then retry | Works (current recovery) |
| 7 | `modprobe btusb force_scofix=1` | HSP stutter got significantly worse, MTU smaller; do not use |
| 8 | Loopback `latency_msec=40 adjust_time=10` | HSP stutter improved, not fully fixed |

## HSP audio stutter (separate issue)

With HSP/HFP active the audio stutters. PulseAudio logs show:

```
bluetooth/bt-codec-msbc.c: Lost 3 input audio packet(s)
module-loopback.c: Could not peek into queue
alsa-sink: Found underrun ... (alsa_output.platform-sound.stereo-fallback)
```

The primary event is the lost SCO/mSBC packet (mSBC frames are 7.5 ms, so a few
lost packets are an audible gap). The `module-loopback` warnings and ALSA
underruns are downstream effects: the loopback cannot feed ALSA while the SCO
source is starving. CPU is not the cause (about 11% pulseaudio, 17% total).

Mitigations tried:
- Larger loopback buffer and drift adjustment (`latency_msec=40`,
  `adjust_time=10`) help somewhat.
- `force_scofix=1` made it worse and reduced the MTU; reverted.

Not yet tried:
- Forcing CVSD instead of mSBC
  (`pactl send-message /card/<card>/bluez switch-codec CVSD`) to check whether
  the loss is mSBC/controller specific.
- Capturing `btmon` / `journalctl -u bluetooth` during the losses to see
  whether packets are lost on HCI/SCO or over the air.
- USB/SCO transport or scheduling tuning for the Bluetooth controller.

## Open questions for the community

- Is there a BlueZ / module-bluez5 sequence that reliably re-acquires A2DP
  after an HFP session without a full reconnect?
- Is avoiding the `off` profile (or issuing it differently) correct for this
  headset, or does it make the endpoint loss worse?
- Is the mSBC packet loss a known issue with this controller / headset, and is
  CVSD the recommended fallback?

## Files

- `src/dialog_bt.c` - the switch macro and the gated 4th-button entry point.
- `src/audio.c` - PulseAudio card/profile handling, loopback unload, reconnect
  recovery.
- `src/audio.h` - the profile API (`audio_bt_profile_*`).
