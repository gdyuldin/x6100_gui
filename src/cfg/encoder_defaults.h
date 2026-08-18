#pragma once

#include <array>
#include <cstddef>
#include "encoder_bind_types.h"

constexpr std::array<cfg_ctrl_t, 11> kEncoderVolDefaults = {{
    CTRL_VOL,
    CTRL_SQL,
    CTRL_RFG,
    CTRL_FILTER_LOW,
    CTRL_FILTER_HIGH,
    CTRL_PWR,
    CTRL_HMIC,
    CTRL_MIC,
    CTRL_IMIC,
    CTRL_MONI,
    CTRL_FILTER_BW,
}};

constexpr std::array<cfg_ctrl_t, 37> kEncoderMfkDefaults = {{
    CTRL_SPECTRUM_FACTOR,
    CTRL_KEY_SPEED,
    CTRL_KEY_MODE,
    CTRL_IAMBIC_MODE,
    CTRL_KEY_TONE,
    CTRL_KEY_VOL,
    CTRL_KEY_TRAIN,
    CTRL_QSK_TIME,
    CTRL_KEY_RATIO,
    CTRL_DNF,
    CTRL_DNF_CENTER,
    CTRL_DNF_WIDTH,
    CTRL_DNF_AUTO,
    CTRL_NB,
    CTRL_NB_LEVEL,
    CTRL_NB_WIDTH,
    CTRL_NR,
    CTRL_NR_LEVEL,
    CTRL_AGC_HANG,
    CTRL_AGC_KNEE,
    CTRL_AGC_SLOPE,
    CTRL_COMP,
    CTRL_CW_DECODER,
    CTRL_CW_TUNE,
    CTRL_CW_DECODER_SNR,
    CTRL_CW_DECODER_PEAK_BETA,
    CTRL_CW_DECODER_NOISE_BETA,
    CTRL_ANT,
    CTRL_RIT,
    CTRL_XIT,
    CTRL_VOX_ON,
    CTRL_VOX_GAIN,
    CTRL_VOX_AG,
    CTRL_VOX_DELAY,
    CTRL_CW_PEAK_ON,
    CTRL_CW_PEAK_Q,
    CTRL_IF_SHIFT,
}};

