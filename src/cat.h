/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void cat_init();
void cat_destruct();

#ifdef __cplusplus
}

#include <memory>
#include <vector>

struct Impl;

class Frame {
public:
    Frame(const char *data, size_t len);
    std::vector<char> dump() const;
    Frame process() const;
    ~Frame();

    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&&) noexcept = default;
    Frame& operator=(Frame&&) noexcept = default;

private:
    Frame();
    std::unique_ptr<Impl> pimpl_;
};

#endif
