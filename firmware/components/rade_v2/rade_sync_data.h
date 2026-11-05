/* Auto generated from checkpoint checkpoint_epoch_200.pth */


#ifndef RADE_SYNC_DATA_H
#define RADE_SYNC_DATA_H

#include "nnet.h"


#include "opus_types.h"

#include "rade_v2_core.h"

#include "rade_v2_constants.h"


#define SYNC_DENSE1_OUT_SIZE 64

#define SYNC_DENSE2_OUT_SIZE 64

#define SYNC_DENSE3_OUT_SIZE 1

struct RADESync {
    LinearLayer sync_dense1;
    LinearLayer sync_dense2;
    LinearLayer sync_dense3;
};

int init_radesync(RADESync *model, const WeightArray *arrays);

#endif /* RADE_SYNC_DATA_H */
