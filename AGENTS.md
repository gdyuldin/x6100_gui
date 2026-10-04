# X6100 LVGL GUI — Project Rules

This file serves as the active memory bank and operational context for Kilo Code and AI agents working on this repository.

## Project overview

Alternative open-source firmware (GUI) for the **Xiegu X6100** amateur HF/VHF transceiver (Cortex-A7 ARM Allwinner R16). Built on **LVGL** (Light and Versatile Graphics Library) with a mix of C11 and C++17 code.

- **Target:** embedded Linux ARM (buildroot toolchain, `arm-linux-gnueabihf-gcc 7.4`)
- **Host tests:** x86_64 Linux (Clang/GCC)
- **GUI:** LVGL v8.x with custom drivers (`lv_drivers/`)
- **DSP:** liquid-dsp (C), custom Goertzel, spectrum/waterfall pipelines
- **Key libraries:** sqlite3, liquid-dsp, libnm, dbus-glib-1, libpng, libgps, sndfile, libudev, libpulse, alsa, pthread, aether_x6100_control
- **Testing:** Catch2 v3 (C++ tests), `ctest`
- **License:** LGPL-2.1-or-later

## Directory layout

```
.
├── src/                    # Application sources (C + C++)
│   ├── cat/                # CAT/CI-V core + LAN transport (static libs)
│   ├── cfg/                # C++17 config/parameters module (static lib)
│   ├── cw_decoder/         # CW receiver/decoder (FFT, classifier, decoder)
│   ├── ft8/                # FT8/FT4 decoder
│   ├── ports/              # Bare C port interfaces (psd/audio/radio/telemetry)
│   ├── qth/                # QTH locator math
│   ├── settings/           # Settings dialog (pages + shared widget layer)
│   ├── widgets/            # Custom LVGL widgets
│   └── fonts/              # Font assets
├── tests/                  # Test sources (x86_64 host)
│   ├── cfg/                # Catch2 tests for cfg
│   ├── cw_decoder/         # Catch2 tests + offline tools for cw_decoder
│   └── ...                 # qth, ft8 and cat tests
├── buildroot/              # ARM cross-compile scripts (build.sh, conf.sh)
├── lvgl/                   # LVGL library (git submodule)
├── lv_drivers/             # LVGL display/input drivers (tracked in-tree)
├── sql/                    # SQL schema initialization
├── third-party/            # rapidxml, utf8 headers
├── rootfs/                 # Root filesystem overlay for install target
├── images/                 # Static UI images
├── voices/                 # RHVoice voice data
└── sandbox/                # Experimental / scratch code
```

## Settings dialog module (`src/settings/`)

The settings dialog lives entirely under `src/settings/` (not a static library,
just another `target_sources` subdirectory like `src/widgets/`):

- `dialog_settings.{h,cpp}` — public surface (`dialog_t *dialog_settings`) and
  the dialog lifecycle/page dispatcher (`btn_page`, `load_*_page`,
  `construct_cb`/`destruct_cb`/`key_cb`) plus the single `static SettingsPage
  page` instance.
- `settings_context.h` — `SettingsPage`, `SettingBind`, `row_items_t`,
  `SliderIntBind`/`SliderFloatBind`, the shared `SMALL_*`/`COL_DSC_COUNT`/
  `ROW_DSC_COUNT` metrics and the `make_*_page` prototypes.
- `settings_widgets.{h,cpp}` — implementation of the `SettingsPage` grid
  helpers (`reset`/`destroy`/`finish`/`delimiter`/`label`/`cell`/`show_row`),
  the `switch_bool`/`spinbox_int`/`dropdown_int`/`slider_int`/`slider_float`
  factories, the shared `settings_change_bg_opa_cb` and the update callbacks
  (`switch_*`, `spinbox_*`, `dropdown_*`, `slider_int_*`, `slider_float_*`).
- `settings_page_general.cpp`, `settings_page_ui.cpp`,
  `settings_page_voice.cpp`, `settings_page_info.cpp` — one page per TU, each
  `make_*_page(SettingsPage &page)` builds `page.row` and ends with
  `page.finish()` (except `make_info_page`, which does not use the grid).

Rules:

- Page builders take `SettingsPage &` and must not keep file-scope grid/bind
  state; all binds, slider binds and `Subscription` observers are owned by the
  context and dropped together in `SettingsPage::destroy()` on page switch.
- Row builders keep the row in `page.row` and call `page.label(text)` /
  `page.cell(col, span, width)`; do not create raw `lv_obj_create` transparent
  cells or reach for a file-scope `grid`/`dialog` global (use `page.grid` /
  `page.dialog`).
- Do not change row order, delimiter placement, ranges, steps, `fmt` strings or
  the version gates (`base_ver.rev >= 3/8/9/12`): the grid breaks silently.
- Includes of root `src/` headers use `../` (`"../styles.h"`, `"../radio.h"`,
  `"../cfg/cfg_api.h"`); own headers by name. Never add `-Isrc` to the include
  path: `src/gps.h` would shadow the sysroot `<gps.h>`.
- The unguarded C headers (`styles.h`, `radio.h`, `meter.h`, `main.h`,
  `events.h`, `keyboard.h`) must be included inside `extern "C" { ... }` from
  C++ (some, like `audio.h`, also pull in `aether_radio/.../control.h` before
  their own guard, so move the include inside the block).

## Data flow and threading

- **Radio/DSP thread.** `dsp_samples()` (`src/dsp.cpp`) is called from the radio
  receive loop (`src/radio.c`), once per BASE flow chunk (~27 times/second), not
  from LVGL/main. It runs the shared FFT and delivers PSD frames.
- **PSD frames.** `dsp_frame_subscribe()` is the single delivery API for
  spectrum, waterfall and the CI-V scope. Each subscriber passes a
  `dsp_frame_cfg_t` (`src/dsp.h`): `nfft` output bins, `chunks_per_frame`
  cadence, and `allow_vary_freq` (false drops transforms collected while
  retuning). `nfft == 0`, `nfft > DSP_MAX_NFFT` and `chunks_per_frame == 0` are
  rejected; every consumer passes a named constant, e.g.
  `DSP_FRAME_DEFAULT_CHUNKS`. `dsp_frame_t.psd_db` is valid only for the
  duration of the callback, and callbacks run on the radio/DSP thread while an
  internal mutex is held: they must not block, allocate, or call
  `dsp_frame_subscribe()` / `dsp_frame_set_active()` /
  `dsp_frame_set_chunks_per_frame()` / `dsp_frame_unsubscribe()`
  (non-recursive mutex -> deadlock). Subscribers own their bins.
- **Frame pause.** `dsp_frame_set_active(id, false)` stops accumulation and
  delivery for one subscriber; `true` restarts it with fresh accumulated data
  (`accum`/`accum_count` zeroed) but keeps the cadence counter, so the first
  usable chunk after resume is delivered at once. Used by
  `spectrum_set_enabled()` / `waterfall_set_enabled()` (called from
  `src/dialog_ft8.c`) and by the scope active hook.
- **Frame cadence.** Each subscriber has two counters: `accum_count` (chunks
  summed into `accum`, used for the mean) and `chunks_elapsed` (delivery
  interval, saturates at `chunks_per_frame`, reset only on emit). A frame is
  emitted when `chunks_elapsed` reaches `chunks_per_frame` and `accum_count >= 1`,
  so a window can be partial. `chunks_elapsed` is deliberately not reset by
  `frame_sub_zero()` / `reset_accumulators()`, so retuning or a run of dirty
  chunks cannot stall delivery. `dsp_frame_set_chunks_per_frame(id, n)` changes
  the interval of a live subscription without touching the window; lowering it
  below the current `chunks_elapsed` makes the subscriber immediately due. It is
  used by the CI-V scope sweep speed (`scope_streamer` -> `cat_lan` ->
  `psd_port::set_chunks_per_frame`).
- **Timing must be chunk-based.** Frame cadence is counted in BASE flow chunks
  (`chunks_per_frame`), not milliseconds: at ~27 chunks/s a millisecond timer
  silently quantizes to 1-2 chunks and drifts. There is no per-subscriber timer.
- **S-meter / noise / auto-levels** are computed in `dsp.cpp` from a dedicated
  full-resolution (`DSP_MAX_NFFT`, no decimation) accumulator
  (`meter_accum`/`meter_count`, `DSP_METER_CHUNKS = 2`) and must not depend on
  any frame subscriber: the meter tick runs before the subscriber loop and
  `update_s_meter()` always runs, so the S-meter keeps moving even when every
  subscriber is paused (e.g. FT8). Auto min/max refresh only while an external
  subscriber is active. The single `#define DSP_METER_NOISE_ENABLED` gates the
  noise floor + auto-levels (0 = S-meter only) for on-device comparison.
  `src/meter.c` consumes the value for display and CAT reads it via
  `dsp_get_s_meter_db()`.
- **`fft_dec` is bounded.** `flow_info.fft_dec` is a 3-bit field
  (`aether_radio/x6100_control/low/flow.h`), but the BASE firmware guarantees a
  value <= 3, so `spectrum_factor` is one of 1/2/4/8. Do not add clamps for
  larger values.
- **Audio subscriptions** (`dsp_audio_*`) follow the same reentrancy contract:
  callbacks run under `subs_mutex` and must not call back into `dsp_audio_*`.
- **CAT/scope boundary.** The CAT core (CI-V framing, `scope_streamer`) does not
  include `dsp.h`; CI-V scope receives PSD frames through `psd_port_t`
  (`src/ports/psd_port.h`), adapted in `src/app_ports.cpp`. The application glue
  `src/cat/lan/cat_lan.cpp` is the one exception: it includes `dsp.h` only for
  `DSP_FRAME_DEFAULT_CHUNKS`; move that constant into `psd_port.h` if that
  include must go. Scope activity is pushed back via
  `scope_streamer_set_active_cb()`, which `cat_lan` wires to `psd->set_active`,
  so the scope subscription only runs while `scope_data_enabled` and a notify
  callback are both present.
- **UI from the DSP thread** must go through
  `subject_subscribe_delayed[_and_notify]` (drained on the LVGL/main thread in
  `src/main.c`); never touch LVGL directly from the DSP thread.

## External Libraries Source

*All external libraries are readonly.*

- **Buildroot sources and toolchain:**  `../AetherX6100Buildroot/`.
- **FT8_lib:** used for decoding and encoding FT8, sources are here `../ft8_lib`.
- **liquid-dsp:** used for signal processing, sources are here `../liquid-dsp`.
- **X6100Control:** X6100 control library. Contains many x6100 prefixed constants and functions. Includes starts with `aether_radio`. Sources are here `../X6100Control`.
  - Headers for host compilation: `../X6100Control/include` (hand-written) and `../X6100Control/build/include` (generated, contains `aether_radio/x6100_control/api.h`). Both are required on the include path or `src/radio.h` fails to parse.

## Build commands

### Host tests (x86_64)

```bash
./run_tests.sh
```
Configures `build_test/` with `-DENABLE_TESTING=YES`, then runs
`cmake --build build_test --target all` and `ctest --output-on-failure`, so it
compiles every target configured in the test tree: the test executables and
their dependencies (cfg, qth, ft8, cw_decoder) plus the whole `src/cat`
subdirectory (`cat_core`, `cat_transport`, `cat_lan` — i.e. `civ_*`,
`scope_streamer.cpp`, `cat.cpp`, `cat_lan.cpp`). Building `all` also compiles
the `cw_decoder` offline tools (`hist_forget_sweep`, `cw_wav_inspect`) even
though `ctest` does not run them. It does NOT build the application sources
(`clock.c`, `settings/dialog_settings.cpp`, `main_screen.c`, `dialog_ft8.c`,
`waterfall.c`, ...), which are excluded from the test configuration. Changes to
those files need the single-file syntax check below (and ultimately the ARM
build).

Detailed tests are not mandatory for every module. A module may deliberately
have no (or only coarse) tests when the missing coverage would merely pin
tuning constants that are expected to change — for example the `cw_decoder` EMA
coefficients that are trimmed against real off-air signals. Prefer offline
inspection/measurement tools (e.g. `cw_wav_inspect`, `hist_forget_sweep`) for
that kind of adjustment instead of brittle assertions.

### Quick syntax check (single file, host)

For a fast parse/type check of one application source without a full ARM build,
compile it standalone from the project root. The external include paths are
required (`../X6100Control/include` plus the generated
`../X6100Control/build/include`) and, for C, `-D_GNU_SOURCE` must be passed
explicitly — CMake injects it only for the `x6100_gui` target, and without it
`src/common/time.h` fails on `clock_gettime` / `CLOCK_MONOTONIC`.

```bash
# C source
gcc -fsyntax-only -std=c11 -D_GNU_SOURCE \
    -Isrc -I. -Ilvgl \
    -I../X6100Control/include -I../X6100Control/build/include \
    src/clock.c

# C++ source
g++ -fsyntax-only -std=c++17 -D_GNU_SOURCE \
    -Isrc -I. -Ilvgl \
    -I../X6100Control/include -I../X6100Control/build/include -I../ft8_lib \
    src/settings/dialog_settings.cpp
```

Some sources cannot be host-checked because they include ARM-only headers:
`src/waterfall.c` includes `<arm_neon.h>`, so `gcc -fsyntax-only` fails with
`arm_neon.h: No such file or directory`. Validate such files with the ARM build
instead.

Extra include paths for specific files:

- any C++ file that pulls `dsp.h` (`src/dsp.cpp`, `src/app_ports.cpp`,
  `src/cat/lan/cat_lan.cpp`) needs `-I../liquid-dsp/include`;
- CAT files that include `cat/...` need `-Isrc/cat`.

`src/dialog_ft8.c` is also not host-clean: its `audio_cb` is declared with
`unsigned int` (`src/dialog_ft8.c:118`) but defined with `size_t` (`:815`),
which only matches on 32-bit ARM. Validate it with the ARM build until that
signature is fixed.

### Production build (ARM, cross-compiled via buildroot)

```bash
cd buildroot && ./build.sh
```
Uses buildroot's toolchain file. Produces `x6100_gui` binary. Requires `AetherX6100Buildroot` sibling directory with a pre-built buildroot sysroot.

## Coding style

Avoid code duplication, reuse existing code.
Keep comments short and factual: long rationale comments rot when the code
changes. Explain *why* only where the code cannot.

### Where a constant lives

Use a named constant when either holds:

- the value is used in more than one place and the occurrences must change
  together (for example the frame length in ms);
- the raw value is not self-evident from its context (for example a special
  marker byte).

Otherwise an inline literal in the algorithm is fine — do not hoist every
tuning number into a named constant or a distant "constants" header.

A value shared between components stays a named constant in a header both sides
include; component-local tuning stays next to the code that uses it (typically a
local `constexpr`). Corollary for `cw_config.h` (and similar module configs): it
carries the frame pipeline's shared values (sample rate, FFT/hop sizes, the
cross-component decision level), while component-specific tuning lives in
`time_classifier.{h,cpp}`, `tone_level_tracker.h`, and so on.

### Language standards

| Context | Standard |
|---------|----------|
| C files (`*.c`) | C11 (`-std=c11`) |
| C++ files (`*.cpp`, `*.hpp`) | C++17 (no C++20 — toolchain is GCC 7.4) |
| Headers (`*.h`) | C headers use `#pragma once`; optional `extern "C"` wrappers for C++ callers |

### Formatting (all files)

An `.clang-format` file is provided (LLVM-based). Key rules:

- **Indentation:** 4 spaces, no tabs
- **Column limit:** 120
- **Braces:** Attach (opening brace on same line).
- **Pointer alignment:** right (`int32_t *ptr`)
- **Short if/loops:** never on single line
- **Short functions:** empty only
- **Short enums:** never on single line
- **Consecutive assignments/declarations:** aligned
- **Include sort:** case-sensitive via clang-format

**How to apply it.** The tree is not fully format-clean and the tool version
matters, so do NOT run `clang-format -i` over whole files or directories: it
rewrites large blocks of unrelated, pre-existing code and pollutes the diff.
Format only the lines you changed, with `git clang-format` (use
`git clang-format --staged` for staged changes); untouched code stays untouched.
If a file genuinely needs a full reformat, do it as its own
`chore: clang-format` commit, never mixed with a functional change. Use the
pinned version (Ubuntu clang-format 18); other versions reflow differently.
Before formatting, check `git status`; afterwards, inspect `git diff` and revert
any file the task did not intend to touch.

### Naming conventions

| Entity | Style | Example |
|--------|-------|---------|
| C functions (global) | `snake_case` | `split_freq()`, `radio_init()` |
| C functions (static file-local) | `snake_case` | `on_vfo_freq_change()` |
| C structs / enums / typedefs | `snake_case` + `_t` suffix | `band_info_t`, `x6100_flow_fmt_t` |
| C++ class/struct names | `PascalCase` | `SettingsManager`, `SubjectT` |
| C++ class methods | `snake_case` | `flush_all()`, `get_value()` |
| C++ member variables | `snake_case` with trailing `_` | `pending_writes_`, `scheduled_` |
| C++ template params | `PascalCase` | `ValueType`, `Scale` |
| Constants and enums | `UPPER_SNAKE` | `EVENT_ROTARY`, `SUCCESS` |
| Defines (C) | `UPPER_SNAKE` | `RADIO_SAMPLES`, `IDLE_TIMEOUT` |
| File names | `snake_case` | `param_int_get`, `cw_decoder.c` |

### C code style (main application)

- LGPL license header block at top of every file (see existing files for template)
- Section separators: `/***** INCLUDES *****/`, `/***** DEFINES *****/`, `/***** TYPEDEFS *****/`, `/***** STATIC PROTOTYPES *****/`
- `static` forward declarations for file-local functions
- No exceptions, no RTTI (embedded constraints)
- `LV_LOG_ERROR`/`LV_LOG_WARN`/`LV_LOG_INFO` for logging
- `Subject` pub/sub for reactive value change notifications. Exists on cfg
- Subscribing to `cfg` parameters from C: cast the typed handle to `Subject *`
  and use the generic helpers, e.g.
  `subject_subscribe_delayed((Subject *)cfg.clock.view(), cb, NULL)`. A
  `ParamInt *` / `ParamFloat *` / `ParamText *` returned by a
  `cfg.<group>.<name>()` accessor is castable to `Subject *` because
  `Parameter<T>` derives from `SubjectT<T>`, which derives from `Subject`.
- Prefer `subject_subscribe_delayed[_and_notify]` for callbacks that touch UI
  state: they run on the LVGL/main thread via `observer_delayed_drain()` in
  `src/main.c`. Release an owned observer with
  `param_unsubscribe((Observer *)obs)`; in C++ prefer the RAII `Subscription`.
- Do not add ad-hoc `foo_set_*()` wrappers that both write a parameter and
  mutate module state. Write the parameter directly (`->set()` in C++,
  `param_i_set` in C) and let the owning module react through a subscription.
- `settings_types.h` (enums like `clock_view_t`) must be included by the file
  that actually uses those enum values. When removing a transitive include from
  a header, check every file that includes it (`main_screen.c`,
  `settings/dialog_settings.cpp`, ...).
- The settings grid (`src/settings/settings_widgets.cpp` /
  `settings_context.h`) uses a fixed layout of 7 content columns
  (`COL_DSC_COUNT` counts the trailing `LV_GRID_TEMPLATE_LAST` too): column 0 is
  the label, columns 1-3 are the left cell and columns 4-6 are the right cell.
  Place a control by setting its grid cell (column, span); the template does not
  grow per row.

### C++ code style (`src/cfg/`)

Full rules in `src/cfg/AGENTS.md` (auto-loaded when working under `src/cfg/`).
Key highlights:

- `#pragma once` in all headers
- Attach braces (K&R, opening on same line) — matches the rest of the project
- No exceptions — errors via return codes / `std::optional`
- RAII for all owned resources (`std::unique_ptr`, custom deleters)
- Global/static lifetime for parameters and subjects (never deleted)
- `std::function` for injected callbacks; `const char*` for DB keys
- Thread-safe: each `SubjectT` has its own `std::mutex`; `PendingWrites` has its own
- Deferred writes: last-value-wins per `StorageKey`, retry on failure
- Tests: Catch2 v3, hand-written mocks in `tests/cfg/mocks/`, friend test-access classes only

### Commit style

Use conventional commits: `feat:`, `fix:`, `refactor:`, `test:`, `docs:`, `chore:`.

Short imperative subject line. English only.

### External linkage

When a C source file calls a C++ function, both the header declaration and `.cpp` definition must use `extern "C"` to avoid name mangling.

## Analysis guidelines (repeat-offender checks)

- **Find the producer cadence first** before touching anything timing-related.
  The radio flow arrives per chunk (~27/s); wall-clock intervals quantize to
  chunks, so prefer a chunk count over milliseconds.
- **Assign policy to its owner.** Do not encode *which consumer* a value is for
  (e.g. an enum `SPECTRUM/WATERFALL/SCOPE`) inside a shared core; let each
  consumer declare its needs (bins, cadence, `vary_freq`) when it subscribes.
- **Check the thread and mutex of every callback** before calling into it from
  new code; see "Data flow and threading".
- **Trace every writer of a global flag** before reasoning about it. A flag
  touched by only one dialog is not general API. The FT8 CPU-saver globals are
  gone: the FT8 dialog pauses consumers through `spectrum_set_enabled()` /
  `waterfall_set_enabled()` (both call `dsp_frame_set_active()`), and the scope
  pauses through the `scope_streamer` active hook.
- **Reset accumulators together — but not the meter or the cadence.**
  `dsp_reset()`, base-frequency change, tx/rx change and `spectrum_factor`
  change zero only the per-subscriber accumulated data (`reset_accumulators()`
  -> `frame_sub_zero()`); the cadence counter (`chunks_elapsed`) is preserved so
  delivery is not restarted from zero. The meter accumulator
  (`meter_accum`/`meter_count`) must **not** be cleared on those events: it runs
  its own window (`DSP_METER_CHUNKS`) and `update_s_meter()` keeps ticking even
  when every frame subscriber is paused. `dsp_frame_set_active(id, true)` zeroes
  that one subscriber's accumulated data, not its cadence. Missing a reset site
  leaks stale samples into the next frame; adding an extra one (e.g. to the
  meter) silently freezes the S-meter while retuning.
- **Size static arrays** as count x size before adding per-subscriber state;
  `src/dsp.cpp` carries ~8 KB per frame subscriber (`accum` 4 KB +
  `bstart`/`bend` 2+2 KB) plus a shared 4 KB `meter_accum`. The dB frame handed
  to subscribers is a per-call stack buffer in `decimate_emit()`, valid only
  for the duration of the callback.
- **Scope of rules:** `src/cfg/AGENTS.md` applies only to `src/cfg/` and its
  tests; DSP/radio/UI lessons belong in this file.
