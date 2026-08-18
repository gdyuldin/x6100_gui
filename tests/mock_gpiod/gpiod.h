// tests/mock_gpiod/gpiod.h
#pragma once

#include <stddef.h>

// Types
struct gpiod_chip {};
struct gpiod_line {};

//
inline struct gpiod_chip* gpiod_chip_open_by_name(const char*) { return NULL; }
inline struct gpiod_line* gpiod_chip_get_line(struct gpiod_chip*, unsigned int) { return NULL; }
inline int gpiod_line_request_output(struct gpiod_line*, const char*, int) { return 0; }
inline int gpiod_line_set_value(struct gpiod_line*, int) { return 0; }
inline void gpiod_line_release(struct gpiod_line*) {}
inline void gpiod_chip_close(struct gpiod_chip*) {}
