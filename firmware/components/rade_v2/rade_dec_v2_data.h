/* Auto generated from checkpoint checkpoint_epoch_200.pth */


#ifndef RADE_DEC_V2_DATA_H
#define RADE_DEC_V2_DATA_H

#include "nnet.h"


#include "opus_types.h"

#include "rade_v2_core.h"

#include "rade_v2_constants.h"


#define DEC_V2_DENSE1_OUT_SIZE 128

#define DEC_V2_GLU1_OUT_SIZE 128

#define DEC_V2_GLU2_OUT_SIZE 128

#define DEC_V2_GLU3_OUT_SIZE 128

#define DEC_V2_GLU4_OUT_SIZE 128

#define DEC_V2_GLU5_OUT_SIZE 128

#define DEC_V2_OUTPUT_OUT_SIZE 84

#define DEC_V2_GRU1_OUT_SIZE 128

#define DEC_V2_GRU1_STATE_SIZE 128

#define DEC_V2_GRU2_OUT_SIZE 128

#define DEC_V2_GRU2_STATE_SIZE 128

#define DEC_V2_GRU3_OUT_SIZE 128

#define DEC_V2_GRU3_STATE_SIZE 128

#define DEC_V2_GRU4_OUT_SIZE 128

#define DEC_V2_GRU4_STATE_SIZE 128

#define DEC_V2_GRU5_OUT_SIZE 128

#define DEC_V2_GRU5_STATE_SIZE 128

#define DEC_V2_CONV1_OUT_SIZE 32

#define DEC_V2_CONV1_IN_SIZE 256

#define DEC_V2_CONV1_STATE_SIZE (256 * (1))

#define DEC_V2_CONV1_DELAY 0

#define DEC_V2_CONV2_OUT_SIZE 32

#define DEC_V2_CONV2_IN_SIZE 416

#define DEC_V2_CONV2_STATE_SIZE (416 * (1))

#define DEC_V2_CONV2_DELAY 0

#define DEC_V2_CONV3_OUT_SIZE 32

#define DEC_V2_CONV3_IN_SIZE 576

#define DEC_V2_CONV3_STATE_SIZE (576 * (1))

#define DEC_V2_CONV3_DELAY 0

#define DEC_V2_CONV4_OUT_SIZE 32

#define DEC_V2_CONV4_IN_SIZE 736

#define DEC_V2_CONV4_STATE_SIZE (736 * (1))

#define DEC_V2_CONV4_DELAY 0

#define DEC_V2_CONV5_OUT_SIZE 32

#define DEC_V2_CONV5_IN_SIZE 896

#define DEC_V2_CONV5_STATE_SIZE (896 * (1))

#define DEC_V2_CONV5_DELAY 0

struct RADEDecV2 {
    LinearLayer dec_v2_dense1;
    LinearLayer dec_v2_glu1;
    LinearLayer dec_v2_glu2;
    LinearLayer dec_v2_glu3;
    LinearLayer dec_v2_glu4;
    LinearLayer dec_v2_glu5;
    LinearLayer dec_v2_output;
    LinearLayer dec_v2_gru1_input;
    LinearLayer dec_v2_gru1_recurrent;
    LinearLayer dec_v2_gru2_input;
    LinearLayer dec_v2_gru2_recurrent;
    LinearLayer dec_v2_gru3_input;
    LinearLayer dec_v2_gru3_recurrent;
    LinearLayer dec_v2_gru4_input;
    LinearLayer dec_v2_gru4_recurrent;
    LinearLayer dec_v2_gru5_input;
    LinearLayer dec_v2_gru5_recurrent;
    LinearLayer dec_v2_conv1;
    LinearLayer dec_v2_conv2;
    LinearLayer dec_v2_conv3;
    LinearLayer dec_v2_conv4;
    LinearLayer dec_v2_conv5;
};

int init_radedecv2(RADEDecV2 *model, const WeightArray *arrays);

#endif /* RADE_DEC_V2_DATA_H */
