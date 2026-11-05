/* Auto generated from checkpoint checkpoint_epoch_200.pth */


#ifndef RADE_ENC_V2_DATA_H
#define RADE_ENC_V2_DATA_H

#include "nnet.h"


#include "opus_types.h"

#include "rade_v2_core.h"

#include "rade_v2_constants.h"


#define ENC_V2_DENSE1_OUT_SIZE 64

#define ENC_V2_ZDENSE_OUT_SIZE 56

#define ENC_V2_GRU1_OUT_SIZE 64

#define ENC_V2_GRU1_STATE_SIZE 64

#define ENC_V2_GRU2_OUT_SIZE 64

#define ENC_V2_GRU2_STATE_SIZE 64

#define ENC_V2_GRU3_OUT_SIZE 64

#define ENC_V2_GRU3_STATE_SIZE 64

#define ENC_V2_GRU4_OUT_SIZE 64

#define ENC_V2_GRU4_STATE_SIZE 64

#define ENC_V2_GRU5_OUT_SIZE 64

#define ENC_V2_GRU5_STATE_SIZE 64

#define ENC_V2_CONV1_OUT_SIZE 96

#define ENC_V2_CONV1_IN_SIZE 128

#define ENC_V2_CONV1_STATE_SIZE (128 * (1))

#define ENC_V2_CONV1_DELAY 0

#define ENC_V2_CONV2_OUT_SIZE 96

#define ENC_V2_CONV2_IN_SIZE 288

#define ENC_V2_CONV2_STATE_SIZE (288 * (1))

#define ENC_V2_CONV2_DELAY 0

#define ENC_V2_CONV3_OUT_SIZE 96

#define ENC_V2_CONV3_IN_SIZE 448

#define ENC_V2_CONV3_STATE_SIZE (448 * (1))

#define ENC_V2_CONV3_DELAY 0

#define ENC_V2_CONV4_OUT_SIZE 96

#define ENC_V2_CONV4_IN_SIZE 608

#define ENC_V2_CONV4_STATE_SIZE (608 * (1))

#define ENC_V2_CONV4_DELAY 0

#define ENC_V2_CONV5_OUT_SIZE 96

#define ENC_V2_CONV5_IN_SIZE 768

#define ENC_V2_CONV5_STATE_SIZE (768 * (1))

#define ENC_V2_CONV5_DELAY 0

struct RADEEncV2 {
    LinearLayer enc_v2_dense1;
    LinearLayer enc_v2_zdense;
    LinearLayer enc_v2_gru1_input;
    LinearLayer enc_v2_gru1_recurrent;
    LinearLayer enc_v2_gru2_input;
    LinearLayer enc_v2_gru2_recurrent;
    LinearLayer enc_v2_gru3_input;
    LinearLayer enc_v2_gru3_recurrent;
    LinearLayer enc_v2_gru4_input;
    LinearLayer enc_v2_gru4_recurrent;
    LinearLayer enc_v2_gru5_input;
    LinearLayer enc_v2_gru5_recurrent;
    LinearLayer enc_v2_conv1;
    LinearLayer enc_v2_conv2;
    LinearLayer enc_v2_conv3;
    LinearLayer enc_v2_conv4;
    LinearLayer enc_v2_conv5;
};

int init_radeencv2(RADEEncV2 *model, const WeightArray *arrays);

#endif /* RADE_ENC_V2_DATA_H */
