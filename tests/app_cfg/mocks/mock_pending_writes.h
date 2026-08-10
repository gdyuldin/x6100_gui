#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "parameter.h"

// Recording sink implementing WriteSink. Used by test_parameter to verify
// that Parameter::set() enqueues the expected (key, value) pairs and that
// set_quiet() does not.
class MockWriteSink : public WriteSink {
  public:
    struct Record {
        StorageKey  key;
        int32_t     value_int{};
        float       value_float{};
        std::string value_text;
        // Which overload stored the value.
        enum class Kind {
            Int,
            Float,
            Text
        } kind{Kind::Int};
    };

    std::vector<Record> records;

    void write(const StorageKey &key, int32_t value) override {
        records.push_back(Record{key, value, 0.f, {}, Record::Kind::Int});
    }
    void write(const StorageKey &key, float value) override {
        records.push_back(Record{key, 0, value, {}, Record::Kind::Float});
    }
    void write(const StorageKey &key, const std::string &value) override {
        records.push_back(Record{key, 0, 0.f, value, Record::Kind::Text});
    }
};
