# AGENTS.md — x6100_gui (cfg module rules)

## Scope

All rules below apply exclusively to the `src/cfg/` module and its tests,
mocks and build files:

- `src/cfg/` — module sources (headers + implementations).
- `tests/cfg/` — tests for the module.
- `tests/cfg/mocks/` — test doubles (mocks, test-access friends).
- `src/cfg/CMakeLists.txt` and `tests/cfg/CMakeLists.txt` — build
  configuration.
- Dependencies allowed for `cfg`: C/C++ standard library, `sqlite3`,
  `lvgl` (used for `LV_LOG_*` macros), `pthread`, and `src/common/`
  (header-only, standard-library-only utilities). `settings_manager.h` also
  pulls two sysroot libraries: `ft8lib` (`<ft8lib/constants.h>`) and
  `aether_radio/x6100_control` (`<aether_radio/.../control.h>`), the latter also
  linked by `src/cfg/CMakeLists.txt`.
- Files outside this module must NOT be required to compile or test `cfg`.
- Always use relative path from project root.

---

## Language and standards

- C++17 only (no C++20 features — must compile with the old toolchain used by
  the embedded target).
- No exceptions anywhere in `cfg` (embedded constraints). All errors are
  returned via error codes / result structs, never thrown.
- `#pragma once` for all headers in `src/cfg/`.
- C API surface (if added) must be C-compatible: opaque pointers, `extern "C"`,
  no `void*` in the public interface. Typed functions only
  (e.g. `param_int_get`, `param_float_set`, `param_text_subscribe`).

---

## Code style

- Names and comments: English only.
- Class names: `PascalCase` (`SubjectT`, `PendingWrites`, `KeyValueTable`).
- Method and variable names: `snake_case` (`db_name`, `flush_storage`).
- Template parameters: `PascalCase` (`ValueType`, `DbType`, `Scale`).
- Constants (NTTP, enum values): `UPPER_SNAKE` (`Scale`, `SUCCESS`, `NOT_FOUND`).
- Braces: Attach braces (K&R, opening on same line) — matches the rest of the
  project.
- Use `std::function` for injected callbacks (validators, `on_not_found`).
- Use `const char*` for DB keys / names passed to sqlite (no
  `std::string` allocation in hot paths).

---

## Memory and lifetime

- Strict RAII for owned resources: `Subscription` for observer subscriptions;
  statements/resources guarded by RAII wrappers (`StmtResetGuard`).
- Parameters and subjects live statically (global objects) and are normally
  never deleted. `~Subject()` detaches every observer and releases the Subject's
  reference, so it may also be destroyed safely.
- Ownership of an `Observer` is an intrusive reference count (`Observer::refs_`):
  the `Subject` owns the initial reference while it holds the observer in its
  list; a `Subscription`, `param_unsubscribe` and the delayed queue each hold one
  more. The last `release()` destroys the observer.
- `Subscription` is a move-only RAII handle: on reset/destruction it unsubscribes
  the observer and releases its reference. A `subscribe*` result kept as a raw
  `Observer*` is *borrowed* and lives only as long as the Subject owns it.
- No raw owning pointers. Non-owning references/pointers are explicitly marked
  in the interface.

---

## Concurrency

- Every `SubjectT<T>` has an internal `std::mutex` protecting the value
  (`get()` returns a copy; `set()` compares + notifies under the lock).
- `Observer` list in `Subject` is protected by its own mutex
  (`mutex_subscribe`).
- `PendingWrites` is protected by its own `mutable std::mutex`.
- `StmtResetGuard` locks the statement mutex for the whole statement use and
  resets/clears bindings on scope exit — never call `sqlite3_*` on a prepared
  statement without it.
- Background flush logic (if added) wakes on `condition_variable` or a 3
  second timeout — never busy-wait.

---

## Database access (`src/cfg/db.h`, `db.cpp`)

- One storage table = one `KeyValueTable<StorageType>` instantiation
  (`GLOBAL`/`BAND`/`MODE`/`TRANSVERTER`). Tables with a different access
  pattern (`BandsTable`, `MemoryTable`, `DigitalModesTable`, `AtuTable`) stay
  dedicated classes.
- One table = one namespace with named statement-group structs at namespace
  scope. Rationale: GCC rejects inline-static class members whose type is an
  anonymous struct defined inside the class.
- Inline static structs inside classes are prohibited for statement groups.
- Do NOT use anonymous structs/classes for statement groups.
- Cached parameter indices: cache `sqlite3_bind_parameter_index` results at
  `Init()` time; do not call it on every Load/Save.
- Return codes: `SUCCESS = 0`; negative — `load_save_error_codes_t`
  (`WRONG_TYPE`, `NOT_FOUND`, `WRONG_VALUE`); positive — raw `sqlite3` rc.
- On load: `SQLITE_ROW` → value; `SQLITE_DONE` → `NOT_FOUND` (parameter keeps
  its current value; `on_not_found` callback may fire); other → sqlite3 rc.
- NULL text column → empty `std::string` (never dereference NULL).
- Use `snprintf`/`to_chars` for logging values; float formatting via `%g`.
- SQL statements for a table are prepared only in that table's `Init`;
  `Shutdown` finalizes them.
- `cfg_db_init(sqlite3*)` / `cfg_db_shutdown()` are the global entry points
  for the DB.

---

## Parameter model (`parameter.h`)

- `Parameter<ValueType, DbType = ValueType, int32_t Scale = 1000>` is the
  stored parameter:
  - `ValueType` — runtime value type.
  - `DbType` — type persisted in the DB.
  - `Scale` — compile-time (NTTP) scaling when `ValueType != DbType`
    (e.g. `Parameter<float, int32_t>` with default scale 1000 → MHz↔Hz).
    This is a generic-only mechanism for unit conversion; when
    `ValueType == DbType` it is unused (direct assignment).
  - `quantize` — runtime field (constructor argument, default 0) applied only at
    the storage boundary for float parameters: `round(value*quantize)/quantize`.
    `0` disables rounding. The runtime value (`get()`/`set()`) is never
    rounded.
- All production float parameters must be declared `Parameter<float>` with the
  handle type `ParamFloat`; the SettingsManager member type must match the
  handle type exactly (enforced by `param_ref<...>` + `static_assert` in
  `cfg_api.cpp`, never `reinterpret_cast`). `quantize` is set from the real UI
  step (e.g. `pwr` 0.1 dB → 10), not from a leftover `Scale`.
- `to_db_value`/`from_db_value` apply `quantize` when `ValueType == DbType`;
  the `ValueType != DbType` branch keeps the `Scale` conversion.
- `set()` pipeline: validator → compare → notify → enqueue deferred write.
  The deferred write is enqueued only when the value actually changed after
  validation (same condition as the notification): an unchanged value neither
  notifies observers nor dirties the pending-write buffer. `SubjectT<T>::set`
  returns `bool` (changed) so `Parameter::set` can gate the enqueue atomically
  on the compare done under the value mutex.
- `set_quiet()`: same pipeline but without enqueueing (used during bulk loads).
  It still validates and notifies when the value changes.
- `reset()`: restores the construction-time default via `set_quiet(default_)`
  (no deferred write). Declared pure-virtual on `ParamBase` and implemented by
  `Parameter`; used by `SettingsManager::init_load`'s reset.
- `load()` / `save()` route through the free `store_load<DbType>(storage,
  context_id, name)` / `store_save<DbType>(storage, context_id, name, value)`
  helpers in `db.h`; on `NOT_FOUND` keep the current value, invoke
  `on_not_found_` if provided.
- `StorageType` (`GLOBAL` / `BAND` / `MODE` / `TRANSVERTER`) is defined in
  `db.h` (the storage layer) and marks the logical table.
- `StorageKey` = `(StorageType, int context_id, std::string name)` is the
  unique key for deferred writes; `context_id` is passed to the store helpers
  as an integer, never string-mangled.

---

## Store routing (`db.h`, `db.cpp`)

- `store_load<T>` / `store_save<T>` are free function templates that `switch`
  on `StorageType` and dispatch to the matching `KeyValueTable<...>`
  instantiation. There is no `StoragePolicy` interface and no virtual dispatch:
  `Parameter::load/save` and `PendingWrites::flush_*` both call these helpers.
- `store_save` returns `SUCCESS` or the raw sqlite rc (logging failures via
  `LV_LOG_ERROR`); `store_load` returns `std::optional<T>` (`nullopt` on
  `NOT_FOUND`/error).
- `context_id` semantics: `GLOBAL` ignores it (flat `params` table), `BAND`
  uses it as `bands_id`, `MODE` as `mode`, `TRANSVERTER` as the transverter id.

---

## Deferred writes (`pending_writes.h`, `pending_writes.cpp`)

- `PendingWrites` implements `WriteSink` and stores only the last value per
  `StorageKey` (three `unordered_map`s: int32, float, string).
- `flush_all()` persists all pending changes;
  `flush_storage(StorageType, int context_id)` flushes one logical table.
- A pending entry is erased only when its `store_save` returns `SUCCESS`. On
  failure (positive sqlite rc / negative error code) the entry is kept so a
  later flush can retry it — never drop a value because its first save failed.
- Maps are private with `friend class PendingWritesTestAccess` (defined only
  in tests) — never make them public for testing.

---

## Observers / notifications (`subject.h`, `subject.cpp`)

- `Subject` — thread-safe observer list; `subscribe` / `subscribe_delayed`
  return a borrowed `Observer*` / `ObserverDelayed*`, wrap in a `Subscription`
  in C++ to own a reference.
- `Observer` — plain callback + `user_data` + intrusive `refs_`; `notify()` may
  be overridden (`ObserverDelayed`).
- `Subscription` — move-only RAII handle (default-constructible, `reset()`,
  `operator->`). Must stay move-only and `noexcept`-movable for
  `std::vector<Subscription>`.
- `ObserverDelayed` coalesces multiple pending notifies into one via the
  `scheduled_` atomic guard; a queued entry holds one observer reference until
  `ObserverDelayed::drain()` (main loop) or `ObserverDelayed::shutdown()` (app
  exit, from `main` cleanup) pops it. There is no `cancelled_` flag: an
  unsubscribed observer is skipped because `Observer::notify()` checks `subj`.
- Value notifications fire only when the value actually changed
  (comparison happens inside `SubjectT::set`).
- Notification invariant: `Subject::notify()` copies the observer list under
  `mutex_subscribe` and runs callbacks outside the lock; observers must never
  delete themselves or a peer from inside a `notify()` callback (documented in
  `subject.h`).
- Do not reintroduce `pause_notify` / `changed` flags — the new architecture
  replaces them.

---

## C API consumers (`cfg_api.h`)

- `src/cfg/cfg_api.h` is the umbrella entry point for C (and C++) consumers: it
  aggregates the per-type headers and exposes the `cfg.<group>.<name>()` access
  tree. Application code includes only this header.
- Typed access: `param_i_get` / `param_i_set`, `param_f_get` / `param_f_set`,
  `param_t_get_into` / `param_t_set` (never a shared static buffer: C callers use
  the `PARAM_T_GET(p)` macro, which copies into a fresh stack buffer per
  expansion, so several results can be used in one expression). Writes route
  through `Parameter<T>::set`, so validators, deferred-write enqueue and
  observer notifications all apply.
- Never add application-level `foo_set_*()` wrappers that both write a parameter
  and mutate module state (the removed `clock_set_*` API is the counter-example).
  Write the parameter (C++ `->set()`, C `param_i_set`) and let the owning module
  react through a subscription.
- Subscribing from C: cast the typed handle to `Subject *` and use the generic
  helpers — `subject_subscribe[_and_notify]((Subject *)cfg.<group>.<name>(), cb,
  user_data)` / `subject_subscribe_delayed[...]`. The cast is valid because
  `Parameter<T>` derives from `SubjectT<T>`, which derives from `Subject` (see
  `src/dialog_swrscan.c`, `src/cw_tune_ui.c`).
- UI state callbacks must use the `_delayed` variants: they run on the
  LVGL/main thread via `observer_delayed_drain()` (`src/main.c`) and coalesce
  repeated notifications. Release an owned observer with
  `param_unsubscribe((Observer *)obs)`; in C++ prefer the RAII `Subscription`.
- The observer returned by the C `subject_*_subscribe*` helpers is borrowed: the
  Subject owns one reference while the observer stays subscribed.

---

## Adding / removing a parameter

One parameter lives in four coordinated places; keep them in sync or the `cfg`
library stops compiling:

- `src/cfg/settings_manager.h` — the `Parameter<...> p_<group>_<name>{"db_key",
  ...}` member.
- `src/cfg/cfg_api.h` — the `ParamInt *(*<name>)(void);` field in the matching
  `cfg_<group>_refs_t` struct.
- `src/cfg/cfg_api.cpp` — the matching `static ParamInt *cfg_<group>_<name>(void)`
  accessor next to the other accessors, and the designated initializer
  `.<name> = &cfg_<group>_<name>,` inside the `cfg` struct literal. Designated
  initializers must follow the struct field order.
- Consumers: grep for `cfg.<group>.<name>()` (or the raw accessor), the DB key
  string, and any `src/settings/dialog_settings.cpp` toggle that binds the
  handle.

Removing a parameter needs no DB migration: an orphan row for the removed key in
the `params` table is simply never read or written again. Still check
`src/cfg/settings_manager.cpp` for load / `on_not_found` hooks tied to it.

Verification: `git grep -E '<group>\.<name>|p_<group>_<name>|<db_key>|cfg_<group>_<name>'`
must be empty. Ignore non-built leftovers such as `src/waterfall.c.bak` and
`*.old` — they are not compiled and are expected to still match.

Moving a parameter to another group touches only two of those places:

- `src/cfg/cfg_api.h` — move the `ParamInt *(*<name>)(void);` field into the
  `cfg_<newgroup>_refs_t` struct.
- `src/cfg/cfg_api.cpp` — move `.<name> = &cfg_<group>_<name>,` into the
  `.<newgroup> = { ... }` initializer.
- `p_<group>_<name>` in `settings_manager.h`, the `cfg_<group>_<name>` accessor
  name and the DB key stay unchanged (the accessor keeps its original group
  prefix; that is fine).
- Update every consumer: `cfg.<oldgroup>.<name>()` → `cfg.<newgroup>.<name>()`
  (grep both accessors; e.g. `src/display.cpp`,
  `src/settings/dialog_settings.cpp`).

---

## Errors / edge cases

- No exceptions: constructors must not throw (use in-place init, no
  allocation in constructors that can fail; sqlite setup goes to `Init`).
- Range clamping / validation is done in validators injected into
  `Parameter` — not inside `SubjectT`.
- `NOT_FOUND` is a normal load result, not an error — handle it explicitly.
- All `sqlite3_*` calls must check their return codes; log failures via
  `LV_LOG_ERROR`/`LV_LOG_WARN` and return the rc.

---

## Tests (`tests/cfg/`)

- Catch2 (v3 via `Catch2::Catch2WithMain`), C++ test files named
  `test_*.cpp`.
- One test file per module unit:
  - `test_subject.cpp` — `SubjectT`, `Observer`, subscribe/unsubscribe, RAII,
    subscription refcount/lifetime, delayed-notify coalescing and shutdown.
  - `test_parameter.cpp` — validation, scaling, `set_quiet`, deferred writes.
  - `test_pending_writes.cpp` — last-value-wins, flush_all, flush_storage,
    save-failure retention.
  - `test_storage.cpp` — round-trip save/load of `GlobalStorage`,
    `BandStorage`, `ModeStorage` against in-memory SQLite, `context_id`
    isolation.
  - `test_db.cpp` — table-level load/save helpers.
  - `test_computed.cpp`, `test_manager.cpp` — computed parameters and the
    SettingsManager orchestration.
  - `test_cfg_api.cpp` — the C `cfg_api.h` accessor tree.
  - `test_digital_modes.cpp` — the `DigitalModesTable` preset navigation.
  - `test_memory.cpp` — the `MemoryTable` round-trip/partial load.
  - `test_atu.cpp`, `test_transverter.cpp` — the `AtuTable` and
    `KeyValueTable<TRANSVERTER>` per-key save/load and isolation.
- In-memory DB fixtures that exercise band/mode tables must create all three
  tables (`params`, `band_params`, `mode_params`) and call `Init`/`Shutdown`
  on `KeyValueTable<GLOBAL>`, `KeyValueTable<BAND>` and
  `KeyValueTable<MODE>` — a BAND/MODE round-trip fails with rc=21
  (`SQLITE_MISUSE`) if `KeyValueTable<BAND>`/`KeyValueTable<MODE>` is not
  initialised.
- Private data access only via friend test-access classes in
  `tests/cfg/mocks/` (e.g. `PendingWritesTestAccess`) — never via
  `#define private public`.
- Mock/stub types go to `tests/cfg/mocks/` and are compiled only into the
  test binary, never into `cfg`.
- Mocks are written by hand (no external mocking framework).

---

## Build and run

- Target application is built using buildroot for ARM Allwinner R16.
- Tests is built for x86_64.
- `src/cfg/CMakeLists.txt` builds a static library `cfg`:
  - Sources: `file(GLOB "*.cpp" "*.c")` in `src/cfg/`.
  - PUBLIC includes: `${CMAKE_CURRENT_SOURCE_DIR}` and `${PROJECT_SOURCE_DIR}/lvgl`.
  - Links: `Threads::Threads`, `lvgl`, `sqlite3` (via
    `pkg_check_modules(SQLITE3 ... IMPORTED_TARGET)` fallback
    `find_library(SQLITE3_LIBRARY sqlite3)`).
- `tests/cfg/CMakeLists.txt` builds one test executable `test_cfg`
  from `file(GLOB "*.cpp")` in `tests/cfg/`, links
  `cfg + Catch2::Catch2WithMain`, registered via `add_test`.
- Do not add source files to `src/cfg/` without a matching
  `add_library`/glob picking them up, and do not add a test file without it
  being picked up by the test CMake glob.
- Run tests: `./run_tests.sh` from the project root
  (configures `build_test/` with `-DENABLE_TESTING=YES`, builds, runs `ctest`).
- `run_tests.sh` runs `cmake --build build_test --target all`, so it builds
  every test target and their deps (cfg, qth, ft8, cw_decoder) plus the whole
  `src/cat` subdirectory (`cat_core`, `cat_transport`, `cat_lan`) but NOT the
  application sources that consume `cfg_api.h`. Syntax-check such files manually
  (root `AGENTS.md` → "Quick syntax check (single file, host)"; ARM-NEON sources
  like `src/waterfall.c` cannot be host-checked) and verify with the ARM build.
- All new tests must pass before a change is considered complete.
