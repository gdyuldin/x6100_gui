#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "../common/math.h"
#include "db.h"
#include "subject.h"

#ifdef __cplusplus

// StorageType is defined in db.h (the storage layer) and included above.

// Fully identifies a persisted parameter. PendingWrites stores pending writes
// by (type, context_id, name); context_id is the band_id/mode_id/etc. and is
// passed to the per-table DB helper as an integer (never string-mangled).
struct StorageKey {
    StorageType type;
    int         context_id;
    std::string name;

    bool operator==(const StorageKey &other) const {
        return type == other.type && context_id == other.context_id && name == other.name;
    }
};

// Hash function for StorageKey (needed for unordered_map)
struct StorageKeyHash {
    std::size_t operator()(const StorageKey &k) const {
        std::size_t h1 = std::hash<int>()(static_cast<int>(k.type));
        std::size_t h2 = std::hash<int>()(k.context_id);
        std::size_t h3 = std::hash<std::string>()(k.name);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

// Type-erased, non-hot-path view of a Parameter<...> used by
// SettingsManager's global/band/mode registries so a single vector can
// hold parameters of any ValueType/DbType/Scale. get()/set() are
// deliberately NOT part of this interface: they stay non-virtual and
// template-typed on the concrete Parameter<T> for hot-path UI access.
class ParamBase {
  public:
    virtual ~ParamBase()                               = default;
    virtual int         load(int context_id)           = 0;
    virtual int         save(int context_id)           = 0;
    virtual void        set_context_id(int context_id) = 0;
    virtual int         context_id() const             = 0;
    virtual StorageType storage_type() const           = 0;
    virtual const char *db_name() const                = 0;
    // Restore the construction-time default value (quietly). Used by
    // SettingsManager::init_load's reset before reloading from the DB.
    virtual void        reset()                        = 0;
};

// Deferred-write sink injected by type. PendingWrites implements it;
// tests pass a mock.
class WriteSink {
  public:
    virtual ~WriteSink()                                                = default;
    virtual void write(const StorageKey &key, int32_t value)            = 0;
    virtual void write(const StorageKey &key, float value)              = 0;
    virtual void write(const StorageKey &key, const std::string &value) = 0;
};

// A stored Parameter: holds a typed value (SubjectT<T>), validates on set,
// enqueues changes for deferred persistence, and loads/saves through the
// store_load/store_save routing helpers in db.h.
//
// NOT_FOUND handling: on load with rc == NOT_FOUND the parameter keeps its
// current value; if an on_not_found callback is provided it is invoked
// (e.g. to assign a default or trigger a cascade/compute).
// DbType/Scale are a generic mechanism (defaults to 1000, e.g. MHz->Hz):
//   Parameter<float, int32_t>            -> value*1000 written, /1000 on load
//   Parameter<float, int32_t, 10>        -> value*10 written, /10 on load
//   Parameter<float, int32_t, 2>         -> value*2 written, /2 on load
// When ValueType == DbType, Scale is unused (direct assignment).
//
// For float parameters use a single Parameter<float> plus the runtime
// `quantize` field (0 = no rounding): the value is stored as REAL in
// engineering units and rounded to the nearest 1/quantize at the storage
// boundary only. The public handle type is ParamFloat and must match the
// SettingsManager member type exactly (see cfg_api.cpp's param_ref).
template <typename ValueType, typename DbType = ValueType, int32_t Scale = 1000>
class Parameter : public SubjectT<ValueType>, public ParamBase {
  public:
    // db_name       - key in the database table
    // default_val   - initial value before loading
    // validator     - clamp/correct on set()
    // storage       - GLOBAL / BAND / MODE / TRANSVERTER marker (for flush sorting)
    // sink          - deferred-write sink receiving pending writes
    // on_not_found  - optional callback on load NOT_FOUND
    // group         - optional registry vector; push_back(this) on construction
    // context_id    - initial context bound to this parameter (band_id/mode_id/
    //                 transverter_id). Defaults to 0; SettingsManager sets it
    //                 when loading a band/mode context.
    // quantize      - rounding step at the storage boundary: round(value*N)/N.
    //                 0 (default) disables rounding. Only meaningful for float
    //                 parameters (ValueType == DbType); ignored otherwise.
    Parameter(const char *db_name, ValueType default_val, std::function<ValueType(ValueType)> validator,
              StorageType storage, WriteSink &sink, std::function<void()> on_not_found = {},
              std::vector<ParamBase *> *group = nullptr, int context_id = 0, int32_t quantize = 0)
        : SubjectT<ValueType>(default_val), db_name_(db_name), storage_(storage), sink_(sink),
          validator_(std::move(validator)), on_not_found_(std::move(on_not_found)), context_id_(context_id),
          default_(default_val), quantize_(quantize) {
        if (group) {
            group->push_back(this);
        }
    }

    // Constructor for min/max clamp validator
    Parameter(const char *db_name, ValueType default_val, ValueType min, ValueType max, StorageType storage,
              WriteSink &sink, std::vector<ParamBase *> *group = nullptr,
              int context_id = 0, int32_t quantize = 0)
        : SubjectT<ValueType>(default_val), db_name_(db_name), storage_(storage), sink_(sink), context_id_(context_id),
          default_(default_val), quantize_(quantize) {
        validator_ = [min, max](ValueType v) { return clip(v, min, max); };
        if (group) {
            group->push_back(this);
        }
    }

    const char *db_name() const override { return db_name_; }
    StorageType storage_type() const override { return storage_; }

    // Context (band_id for BAND, mode_id for MODE) bound to this parameter and
    // used when enqueuing deferred writes. GLOBAL parameters ignore it.
    // Defaults to 0; SettingsManager sets it when loading a band/mode context.
    void set_context_id(int context_id) override { context_id_ = context_id; }
    int  context_id() const override { return context_id_; }

    // Validated set: validator -> compare -> notify -> enqueue deferred write.
    // The deferred write is enqueued only when the value actually changed
    // (same condition as the notification), so an unchanged value neither
    // notifies observers nor dirties the pending-write buffer.
    void set(const ValueType &new_val) {
        ValueType v = new_val;
        if (validator_) {
            v = validator_(v);
        }
        if (SubjectT<ValueType>::set(v)) {
            sink_.write(StorageKey{storage_, context_id_, db_name_}, to_db_value(v));
        }
    }

    // Same as set() but does not enqueue (used during bulk loads).
    void set_quiet(const ValueType &new_val) {
        ValueType v = new_val;
        if (validator_) {
            v = validator_(v);
        }
        SubjectT<ValueType>::set(v);
    }

    // Restore the construction-time default (validated, no deferred write).
    // Used by SettingsManager::init_load's reset before reloading from the DB.
    void reset() override { set_quiet(default_); }

    // Load from the store matching storage(). Returns rc (SUCCESS or
    // NOT_FOUND); on SUCCESS the stored DB value is converted back and applied
    // quietly. NOT_FOUND keeps the current value and may trigger on_not_found_.
    // context_id is the band_id/mode_id for BAND/MODE parameters (0 for the
    // flat GLOBAL table). The default -1 selects the bound context_id_ (set
    // via set_context_id), so callers can load()/save() without threading the
    // context through every call.
    int load(int context_id = -1) override {
        if (context_id == -1) {
            context_id = context_id_;
        }
        std::optional<DbType> res = store_load<DbType>(storage_, context_id, db_name_);
        if (res.has_value()) {
            set_quiet(from_db_value(*res));
            return SUCCESS;
        }
        if (on_not_found_) {
            on_not_found_();
        }
        return NOT_FOUND;
    }

    // Persist immediately through the store matching storage().
    int save(int context_id = -1) override {
        if (context_id == -1) {
            context_id = context_id_;
        }
        const DbType db_value = to_db_value(SubjectT<ValueType>::get());
        return store_save<DbType>(storage_, context_id, db_name_, db_value);
    }

  private:
    DbType to_db_value(const ValueType &v) const {
        if constexpr (std::is_same_v<ValueType, DbType>) {
            return quantize_value(v);
        } else {
            // Scale is a compile-time constant (NTTP), so the multiply is
            // folded to an immediate operand with no runtime overhead.
            return static_cast<DbType>(v * Scale);
        }
    }

    ValueType from_db_value(const DbType &v) const {
        if constexpr (std::is_same_v<ValueType, DbType>) {
            return quantize_value(v);
        } else {
            return static_cast<ValueType>(v) / static_cast<ValueType>(Scale);
        }
    }

    // Round to the nearest 1/quantize_ step. quantize_ == 0 disables rounding.
    // Applied only at the storage boundary, never to the runtime value.
    ValueType quantize_value(ValueType v) const {
        if constexpr (std::is_floating_point_v<ValueType>) {
            if (quantize_ > 0) {
                return static_cast<ValueType>(std::round(static_cast<double>(v) * quantize_) / quantize_);
            }
        }
        return v;
    }

    const char                         *db_name_;
    StorageType                         storage_;
    WriteSink                          &sink_;
    std::function<ValueType(ValueType)> validator_;
    std::function<void()>               on_not_found_;
    int                                 context_id_;
    ValueType                           default_;
    int32_t                             quantize_;
};

#endif
