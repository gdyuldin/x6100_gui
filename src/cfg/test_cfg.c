#include "cfg.h"

#include "band.private.h"

#include <aether_radio/x6100_control/control.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define CHECK(fn) ({printf("Start " #fn ":\n"); fn; printf(" OK\n");})


void test_load_band_by_freq() {
    struct data_t {
        uint32_t freq;
        int32_t expected_bands;
    } data[] = {
        {400000, -1},
        {13999999, -1},
        {14000000, -1},
        {14000001, 6},
        {14069999, 6},
        {14070000, 6},
        {14070001, 7},
        {14350000, 7},
        {14350001, -1},
        {99000000, -1},
        {14350000, 7},
        {14070001, 7},
        {14070000, 6},
        {14000001, 6},
        {14000000, -1},
    };
    size_t freq_len = sizeof(data) / sizeof(*data);
    bool success = true;
    for (size_t i = 0; i < freq_len; i++) {
        int32_t id = get_band_info_by_freq(data[i].freq)->id;
        if (id != data[i].expected_bands) {
            printf("Wrong band id for freq %lu: expected: %i, actual: %i\n", data[i].freq, data[i].expected_bands, id);
            success = false;
        }
    }
    if (!success)
        exit(1);
}

void test_load_band_by_pk() {
    band_info_t *info = get_band_info_by_pk(7);
    assert(info->start_freq == 14070000 && "Wrong start freq");
    assert(info->stop_freq == 14350000 && "Wrong stop freq");
    assert(strcmp("20m SSB", info->name) == 0 && "Wrong name");
}

void test_set_another_band() {
    int32_t new_band = 7;
    assert(cfg_band.vfo_a.freq.pk != new_band);
    assert(cfg_band.vfo_b.freq.pk != new_band);
    assert(cfg_band.vfo.pk != new_band);
    uint32_t freq_a = subject_get_int(cfg_band.vfo_a.freq.val);
    uint32_t freq_b = subject_get_int(cfg_band.vfo_b.freq.val);
    printf("freq_a: %lu, freq_b: %lu\n", freq_a, freq_b);
    subject_set_int(cfg.band_id.val, new_band);
    printf("freq_a: %lu, freq_b: %lu\n", subject_get_int(cfg_band.vfo_a.freq.val), subject_get_int(cfg_band.vfo_b.freq.val));
    assert(subject_get_int(cfg_band.vfo_a.freq.val) != freq_a && "Freq is not changed");
    assert(subject_get_int(cfg_band.vfo_b.freq.val) != freq_b && "Freq is not changed");
    assert(cfg_band.vfo_a.freq.pk == new_band);
    assert(cfg_band.vfo_b.freq.pk == new_band);
    assert(cfg_band.vfo.pk == new_band);
}

void test_set_another_band_using_freq() {
    subject_set_int(cfg.band_id.val, 6);
    subject_set_int(cfg_band.vfo.val, X6100_VFO_A);
    uint32_t prev_a_freq = subject_get_int(cfg_band.vfo_a.freq.val);
    subject_set_int(cfg_band.vfo_a.freq.val , 14200000);
    assert(subject_get_int(cfg.band_id.val) == 7);
    assert(subject_get_int(cfg_band.vfo_a.freq.val) == 14200000);
    assert(cfg_band.vfo.pk == 7);
    assert(cfg_band.vfo_a.freq.pk == 7);
    assert(cfg_band.vfo_b.freq.pk == 7);

    subject_set_int(cfg.band_id.val, 6);
    assert(subject_get_int(cfg_band.vfo_a.freq.val) == prev_a_freq);
}

void test() {
    subject_set_int(cfg.band_id.val, 6);
    printf("Start testing\n");
    CHECK(test_load_band_by_freq());
    CHECK(test_load_band_by_pk());
    CHECK(test_set_another_band());
    CHECK(test_set_another_band_using_freq());
    printf("Testing is done\n");
    exit(0);
}
