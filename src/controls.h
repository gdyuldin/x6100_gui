#pragma once

#include "buttons.h"

#ifdef __cplusplus

void controls_encoder_update(cfg_ctrl_t ctrl, int16_t diff, std::string &msg);


extern "C" {
#endif

cfg_ctrl_t controls_encoder_get_next(encoder_binds_t encoder, cfg_ctrl_t current, int16_t dir);

void control_name_say(cfg_ctrl_t ctrl);

void controls_toggle_agc_hang(button_data_t *data);
void controls_toggle_key_train(button_data_t *data);
void controls_toggle_key_iambic_mode(button_data_t *data);
void controls_toggle_cw_decoder(button_data_t *data);
void controls_toggle_cw_tuner(button_data_t *data);
void controls_toggle_cw_peak(button_data_t *data);

void controls_toggle_dnf(button_data_t *data);
void controls_toggle_dnf_auto(button_data_t *data);
void controls_toggle_nb(button_data_t *data);
void controls_toggle_nr(button_data_t *data);

void controls_toggle_vox(button_data_t *data);

void controls_cw_zap(button_data_t *data);

#ifdef __cplusplus
}
#endif


