#pragma once

#include "cat.h"

#include <algorithm>
#include <cstdint>
#include <vector>


// Full definition of the Frame pimpl struct, shared between cat_frame.cpp
// (handler logic) and cat.cpp (I/O layer). cat.h only forward-declares Impl
// to keep the public header minimal.

struct Impl {
    uint8_t dst_addr;
    uint8_t src_addr;
    uint8_t command;
    std::vector<uint8_t> data;

    Impl(uint8_t dst, uint8_t src, uint8_t command): dst_addr(dst), src_addr(src), command(command) {};

    Impl(const char *data, const size_t len): data(len - FRAME_ADD_LEN - 1) {
        dst_addr = data[2];
        src_addr = data[3];
        command = data[4];
        std::copy(&data[5], &data[len - 1], this->data.begin());
    };

    void write(std::initializer_list<uint8_t> bytes) {
        data.assign(bytes.begin(), bytes.end());
    }

    void append(const uint8_t *buf, size_t len) {
        data.insert(data.end(), buf, buf + len);
    }

    void set_code(uint8_t code) {
        data.clear();
        command = code;
    }

    size_t get_len() const {
        return data.size() + 1 + FRAME_ADD_LEN;
    }

    std::vector<char> dump() const {
        std::vector<char> buf(data.size() + 1 + FRAME_ADD_LEN);
        buf[0] = FRAME_PRE;
        buf[1] = FRAME_PRE;
        buf[2] = dst_addr;
        buf[3] = src_addr;
        buf[4] = command;
        std::copy(data.begin(), data.end(), buf.begin() + 5);
        buf.back() = FRAME_END;
        return buf;
    }
};
