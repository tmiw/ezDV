/* Copyright (c) 2025 David Rowe */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* CoreEncoderStatefull V2 C inference.
   Architecture (DenseNet, enc_stride=4 frames concatenated as input):
     Input:    FRAMES_PER_STEP * NUM_FEATURES (4 * 21 = 84)
     dense_1:  84  -> 64  tanh
     gru1:     64  -> 64  (GRUStatefull, no GLU)
     conv1:    128 -> 96  tanh  dilation=1
     gru2:     224 -> 64  (GRUStatefull, no GLU)  dilation=2
     conv2:    288 -> 96  tanh  dilation=2
     gru3:     384 -> 64  (GRUStatefull, no GLU)
     conv3:    448 -> 96  tanh  dilation=2
     gru4:     544 -> 64  (GRUStatefull, no GLU)
     conv4:    608 -> 96  tanh  dilation=2
     gru5:     704 -> 64  (GRUStatefull, no GLU)
     conv5:    768 -> 96  tanh  dilation=2
     z_dense:  864 -> 56  tanh  (bottleneck=1)
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rade_enc_v2.h"
#include "rade_v2_constants.h"
#include "os_support.h"

void rade_init_encoder_v2(RADEEncV2State *state)
{
    memset(state, 0, sizeof(*state));
}

static void conv1_cond_init_v2(float *mem, int len, int dilation, int *init)
{
    if (!*init) {
        int i;
        for (i = 0; i < dilation; i++) OPUS_CLEAR(&mem[i * len], len);
    }
    *init = 1;
}

void rade_core_encoder_v2(
    RADEEncV2State  *state,
    const RADEEncV2 *model,
    float           *latents,   /* out: LATENT_DIM floats */
    const float     *input,     /* in:  FRAMES_PER_STEP * NUM_FEATURES floats */
    int              arch
    )
{
    float buffer[ENC_V2_DENSE1_OUT_SIZE
               + ENC_V2_GRU1_OUT_SIZE  + ENC_V2_CONV1_OUT_SIZE
               + ENC_V2_GRU2_OUT_SIZE  + ENC_V2_CONV2_OUT_SIZE
               + ENC_V2_GRU3_OUT_SIZE  + ENC_V2_CONV3_OUT_SIZE
               + ENC_V2_GRU4_OUT_SIZE  + ENC_V2_CONV4_OUT_SIZE
               + ENC_V2_GRU5_OUT_SIZE  + ENC_V2_CONV5_OUT_SIZE];
    int idx = 0;

    compute_generic_dense(&model->enc_v2_dense1, &buffer[idx], input, ACTIVATION_TANH, arch);
    idx += ENC_V2_DENSE1_OUT_SIZE;

    compute_generic_gru(&model->enc_v2_gru1_input, &model->enc_v2_gru1_recurrent,
                        state->gru1_state, buffer, arch);
    OPUS_COPY(&buffer[idx], state->gru1_state, ENC_V2_GRU1_OUT_SIZE);
    idx += ENC_V2_GRU1_OUT_SIZE;
    conv1_cond_init_v2(state->conv1_state, idx, 1, &state->initialized);
    compute_generic_conv1d(&model->enc_v2_conv1, &buffer[idx], state->conv1_state,
                           buffer, idx, ACTIVATION_TANH, arch);
    idx += ENC_V2_CONV1_OUT_SIZE;

    compute_generic_gru(&model->enc_v2_gru2_input, &model->enc_v2_gru2_recurrent,
                        state->gru2_state, buffer, arch);
    OPUS_COPY(&buffer[idx], state->gru2_state, ENC_V2_GRU2_OUT_SIZE);
    idx += ENC_V2_GRU2_OUT_SIZE;
    conv1_cond_init_v2(state->conv2_state, idx, 2, &state->initialized);
    compute_generic_conv1d_dilation(&model->enc_v2_conv2, &buffer[idx], state->conv2_state,
                                    buffer, idx, 2, ACTIVATION_TANH, arch);
    idx += ENC_V2_CONV2_OUT_SIZE;

    compute_generic_gru(&model->enc_v2_gru3_input, &model->enc_v2_gru3_recurrent,
                        state->gru3_state, buffer, arch);
    OPUS_COPY(&buffer[idx], state->gru3_state, ENC_V2_GRU3_OUT_SIZE);
    idx += ENC_V2_GRU3_OUT_SIZE;
    conv1_cond_init_v2(state->conv3_state, idx, 2, &state->initialized);
    compute_generic_conv1d_dilation(&model->enc_v2_conv3, &buffer[idx], state->conv3_state,
                                    buffer, idx, 2, ACTIVATION_TANH, arch);
    idx += ENC_V2_CONV3_OUT_SIZE;

    compute_generic_gru(&model->enc_v2_gru4_input, &model->enc_v2_gru4_recurrent,
                        state->gru4_state, buffer, arch);
    OPUS_COPY(&buffer[idx], state->gru4_state, ENC_V2_GRU4_OUT_SIZE);
    idx += ENC_V2_GRU4_OUT_SIZE;
    conv1_cond_init_v2(state->conv4_state, idx, 2, &state->initialized);
    compute_generic_conv1d_dilation(&model->enc_v2_conv4, &buffer[idx], state->conv4_state,
                                    buffer, idx, 2, ACTIVATION_TANH, arch);
    idx += ENC_V2_CONV4_OUT_SIZE;

    compute_generic_gru(&model->enc_v2_gru5_input, &model->enc_v2_gru5_recurrent,
                        state->gru5_state, buffer, arch);
    OPUS_COPY(&buffer[idx], state->gru5_state, ENC_V2_GRU5_OUT_SIZE);
    idx += ENC_V2_GRU5_OUT_SIZE;
    conv1_cond_init_v2(state->conv5_state, idx, 2, &state->initialized);
    compute_generic_conv1d_dilation(&model->enc_v2_conv5, &buffer[idx], state->conv5_state,
                                    buffer, idx, 2, ACTIVATION_TANH, arch);
    idx += ENC_V2_CONV5_OUT_SIZE;

    /* bottleneck=0: linear activation on z_dense output (no tanh) */
    compute_generic_dense(&model->enc_v2_zdense, latents, buffer, ACTIVATION_LINEAR, arch);
}
