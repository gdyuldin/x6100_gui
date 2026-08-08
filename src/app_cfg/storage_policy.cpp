// storage_policy.cpp
// Implementation of the stateless storage policies. Each policy maps the
// Load/Save calls onto the matching table class in db.h:
//   GlobalStorage -> ParamsTable           (flat GLOBAL `params` table)
//   BandStorage   -> BandParamsTable       (`band_params`, keyed by bands_id)
//   ModeStorage   -> ModeParamsTable       (`mode_params`, keyed by mode)
//
// The policies are stateless: the sqlite3 connection and the prepared
// statements live inside the table classes, initialised by
// ParamsTable::Init / BandParamsTable::Init / ModeParamsTable::Init (called
// with the production DB by cfg_db_init, or with an in-memory DB by test
// fixtures). Save errors are logged via LV_LOG_ERROR and the raw sqlite3 rc
// is returned to the caller — PendingWrites can then decide whether to keep the
// pending entry for a later retry. Load returns std::nullopt on NOT_FOUND /
// error so the Parameter keeps its current value.

#include "storage_policy.h"

// Full definition of StorageType (parameter.h) is required for the switch in
// storage_policy_for(); storage_policy.h only forward-declares it.
#include "parameter.h"

#include <lvgl.h>

namespace {

// Shared helper: formats the SQLite rc into the log message described by the
// caller. Returns the rc unchanged so the caller can propagate it.
int log_save_error(const char* what, int rc)
{
    LV_LOG_ERROR("storage_policy: %s failed, sqlite rc=%d", what, rc);
    return rc;
}

} // namespace

// ---------------------------------------------------------------------------
// global policy resolver
// ---------------------------------------------------------------------------

StoragePolicy& storage_policy_for(StorageType type)
{
    // Stateless singletons. Parameters live statically and are never deleted,
    // so a single non-owning static pointer per policy is safe and avoids
    // constructing them on first use inside a hot path.
    static GlobalStorage global_storage;
    static BandStorage band_storage;
    static ModeStorage mode_storage;

    switch (type)
    {
    case StorageType::GLOBAL:
        return global_storage;
    case StorageType::BAND:
        return band_storage;
    case StorageType::MODE:
        return mode_storage;
    }

    // Unreachable; kept as a fallback that cannot throw.
    return global_storage;
}

// ---------------------------------------------------------------------------
// GlobalStorage — flat GLOBAL `params` table, context_id unused
// ---------------------------------------------------------------------------

int GlobalStorage::save_int(int /*context_id*/, const char* name, int32_t value)
{
    int rc = ParamsTable::Save<int32_t>(name, value);
    if (rc != SUCCESS)
    {
        return log_save_error("ParamsTable::Save<int32_t>", rc);
    }
    return SUCCESS;
}

std::optional<int32_t> GlobalStorage::load_int(int /*context_id*/, const char* name)
{
    ParamLoadResult<int32_t> res = ParamsTable::Load<int32_t>(name);
    if (res.rc == SUCCESS)
    {
        return res.value;
    }
    return std::nullopt;
}

int GlobalStorage::save_float(int /*context_id*/, const char* name, float value)
{
    int rc = ParamsTable::Save<float>(name, value);
    if (rc != SUCCESS)
    {
        return log_save_error("ParamsTable::Save<float>", rc);
    }
    return SUCCESS;
}

std::optional<float> GlobalStorage::load_float(int /*context_id*/, const char* name)
{
    ParamLoadResult<float> res = ParamsTable::Load<float>(name);
    if (res.rc == SUCCESS)
    {
        return res.value;
    }
    return std::nullopt;
}

int GlobalStorage::save_text(int /*context_id*/, const char* name, const std::string& value)
{
    int rc = ParamsTable::Save<std::string>(name, value);
    if (rc != SUCCESS)
    {
        return log_save_error("ParamsTable::Save<std::string>", rc);
    }
    return SUCCESS;
}

std::optional<std::string> GlobalStorage::load_text(int /*context_id*/, const char* name)
{
    ParamLoadResult<std::string> res = ParamsTable::Load<std::string>(name);
    if (res.rc == SUCCESS)
    {
        return res.value;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// BandStorage — `band_params` table keyed by bands_id
// ---------------------------------------------------------------------------

int BandStorage::save_int(int context_id, const char* name, int32_t value)
{
    int rc = BandParamsTable::Save<int32_t>(context_id, name, value);
    if (rc != SUCCESS)
    {
        return log_save_error("BandParamsTable::Save<int32_t>", rc);
    }
    return SUCCESS;
}

std::optional<int32_t> BandStorage::load_int(int context_id, const char* name)
{
    ParamLoadResult<int32_t> res = BandParamsTable::Load<int32_t>(context_id, name);
    if (res.rc == SUCCESS)
    {
        return res.value;
    }
    return std::nullopt;
}

int BandStorage::save_float(int context_id, const char* name, float value)
{
    int rc = BandParamsTable::Save<float>(context_id, name, value);
    if (rc != SUCCESS)
    {
        return log_save_error("BandParamsTable::Save<float>", rc);
    }
    return SUCCESS;
}

std::optional<float> BandStorage::load_float(int context_id, const char* name)
{
    ParamLoadResult<float> res = BandParamsTable::Load<float>(context_id, name);
    if (res.rc == SUCCESS)
    {
        return res.value;
    }
    return std::nullopt;
}

int BandStorage::save_text(int context_id, const char* name, const std::string& value)
{
    int rc = BandParamsTable::Save<std::string>(context_id, name, value);
    if (rc != SUCCESS)
    {
        return log_save_error("BandParamsTable::Save<std::string>", rc);
    }
    return SUCCESS;
}

std::optional<std::string> BandStorage::load_text(int context_id, const char* name)
{
    ParamLoadResult<std::string> res = BandParamsTable::Load<std::string>(context_id, name);
    if (res.rc == SUCCESS)
    {
        return res.value;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// ModeStorage — `mode_params` table keyed by mode
// ---------------------------------------------------------------------------

int ModeStorage::save_int(int context_id, const char* name, int32_t value)
{
    int rc = ModeParamsTable::Save<int32_t>(context_id, name, value);
    if (rc != SUCCESS)
    {
        return log_save_error("ModeParamsTable::Save<int32_t>", rc);
    }
    return SUCCESS;
}

std::optional<int32_t> ModeStorage::load_int(int context_id, const char* name)
{
    ParamLoadResult<int32_t> res = ModeParamsTable::Load<int32_t>(context_id, name);
    if (res.rc == SUCCESS)
    {
        return res.value;
    }
    return std::nullopt;
}

int ModeStorage::save_float(int context_id, const char* name, float value)
{
    int rc = ModeParamsTable::Save<float>(context_id, name, value);
    if (rc != SUCCESS)
    {
        return log_save_error("ModeParamsTable::Save<float>", rc);
    }
    return SUCCESS;
}

std::optional<float> ModeStorage::load_float(int context_id, const char* name)
{
    ParamLoadResult<float> res = ModeParamsTable::Load<float>(context_id, name);
    if (res.rc == SUCCESS)
    {
        return res.value;
    }
    return std::nullopt;
}

int ModeStorage::save_text(int context_id, const char* name, const std::string& value)
{
    int rc = ModeParamsTable::Save<std::string>(context_id, name, value);
    if (rc != SUCCESS)
    {
        return log_save_error("ModeParamsTable::Save<std::string>", rc);
    }
    return SUCCESS;
}

std::optional<std::string> ModeStorage::load_text(int context_id, const char* name)
{
    ParamLoadResult<std::string> res = ModeParamsTable::Load<std::string>(context_id, name);
    if (res.rc == SUCCESS)
    {
        return res.value;
    }
    return std::nullopt;
}
