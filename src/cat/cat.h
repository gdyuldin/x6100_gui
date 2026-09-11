#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_PRE 0xFE
#define FRAME_END 0xFD
#define LOCAL_ADDRESS 0xA4

#define FRAME_ADD_LEN 5 /* Header and end len */

#define C_SND_FREQ 0x00 /* Send frequency data */

void cat_init();
void cat_destruct();

#ifdef __cplusplus
}

#include <memory>
#include <vector>

class SettingsManager;

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

// Redirect the SettingsManager used by the CI-V command handlers to `sm`.
// Passing nullptr restores the production global cfg_sm. Used by host tests
// to drive handlers against a local SettingsManager instance.
void cat_frame_set_sm(SettingsManager *sm);

#endif
