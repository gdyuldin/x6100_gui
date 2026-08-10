#pragma once

// Storage policies: thin, stateless adapters between the deferred-write
// pending-writes buffer / Parameter load path and the per-table DB helpers in db.h
// (ParamsTable, BandParamsTable, ModeParamsTable).
//
// Each policy knows how to translate a StorageType into the correct table and
// how to route the context_id (band_id / mode_id). The policies are stateless:
// the DB connection and prepared statements live in the table classes in
// db.h/db.cpp, so no sqlite3* is stored here. Class names are PascalCase
// (StoragePolicy, GlobalStorage...), methods snake_case, as required by the
// project rules.

#include <cstdint>
#include <optional>
#include <string>

#include "db.h"

// Forward declaration of StorageType (defined in parameter.h). Needed only for
// the storage_policy_for() free function below; the policies themselves use
// raw int context_id + const char* name and never hold a StorageKey.
enum class StorageType;

#ifdef __cplusplus

// Interface implemented by GlobalStorage / BandStorage / ModeStorage and by
// the test mock (tests/app_cfg/mocks/mock_storage.h). Kept non-virtual for
// the policies themselves (the mock provides the virtual dispatch), matching
// the "one table = one class" rule: each policy is a thin mapper and the
// virtual dispatch is only needed by tests.
class StoragePolicy {
  public:
    virtual ~StoragePolicy() = default;

    // int32_t
    virtual int                    save_int(int context_id, const char *name, int32_t value) = 0;
    virtual std::optional<int32_t> load_int(int context_id, const char *name)                = 0;

    // float
    virtual int                  save_float(int context_id, const char *name, float value) = 0;
    virtual std::optional<float> load_float(int context_id, const char *name)              = 0;

    // std::string
    virtual int                        save_text(int context_id, const char *name, const std::string &value) = 0;
    virtual std::optional<std::string> load_text(int context_id, const char *name)                           = 0;
};

// Stateless adapter for the flat GLOBAL `params` table (context_id unused).
class GlobalStorage : public StoragePolicy {
  public:
    int                    save_int(int context_id, const char *name, int32_t value) override;
    std::optional<int32_t> load_int(int context_id, const char *name) override;

    int                  save_float(int context_id, const char *name, float value) override;
    std::optional<float> load_float(int context_id, const char *name) override;

    int                        save_text(int context_id, const char *name, const std::string &value) override;
    std::optional<std::string> load_text(int context_id, const char *name) override;
};

// Stateless adapter for the `band_params` table keyed by bands_id.
class BandStorage : public StoragePolicy {
  public:
    int                    save_int(int context_id, const char *name, int32_t value) override;
    std::optional<int32_t> load_int(int context_id, const char *name) override;

    int                  save_float(int context_id, const char *name, float value) override;
    std::optional<float> load_float(int context_id, const char *name) override;

    int                        save_text(int context_id, const char *name, const std::string &value) override;
    std::optional<std::string> load_text(int context_id, const char *name) override;
};

// Stateless adapter for the `mode_params` table keyed by mode.
class ModeStorage : public StoragePolicy {
  public:
    int                    save_int(int context_id, const char *name, int32_t value) override;
    std::optional<int32_t> load_int(int context_id, const char *name) override;

    int                  save_float(int context_id, const char *name, float value) override;
    std::optional<float> load_float(int context_id, const char *name) override;

    int                        save_text(int context_id, const char *name, const std::string &value) override;
    std::optional<std::string> load_text(int context_id, const char *name) override;
};

// Stateless adapter for the `transverter` table keyed by transverter id
// (context_id = the fixed transverter number 0 or 1).
class TransverterStorage : public StoragePolicy {
  public:
    int                    save_int(int context_id, const char *name, int32_t value) override;
    std::optional<int32_t> load_int(int context_id, const char *name) override;

    int                  save_float(int context_id, const char *name, float value) override;
    std::optional<float> load_float(int context_id, const char *name) override;

    int                        save_text(int context_id, const char *name, const std::string &value) override;
    std::optional<std::string> load_text(int context_id, const char *name) override;
};

// Returns the policy for a StorageType. The three policies are singleton
// static objects (stateless); the function is used by Parameter::load/save
// and by PendingWrites::flush_* to route a key to the right table.
StoragePolicy &storage_policy_for(StorageType type);

#endif // __cplusplus
