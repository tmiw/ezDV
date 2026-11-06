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

/* CoreDecoderStatefull V2 C inference.
   Architecture (DenseNet, w1=128, w2=32, latent_dim=56):
     dense_1:   56  -> 128  tanh
     gru1:      128 -> 128  (GRUStatefull, GLU gate)
     conv1:     256 -> 32   tanh  dilation=1
     gru2:      288 -> 128  (GRUStatefull, GLU gate)
     conv2:     416 -> 32   tanh  dilation=1
     gru3:      448 -> 128  (GRUStatefull, GLU gate)
     conv3:     576 -> 32   tanh  dilation=1
     gru4:      608 -> 128  (GRUStatefull, GLU gate)
     conv4:     736 -> 32   tanh  dilation=1
     gru5:      768 -> 128  (GRUStatefull, GLU gate)
     conv5:     896 -> 32   tanh  dilation=1
     output:    928 -> 84   linear  (FRAMES_PER_STEP * NUM_FEATURES)
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rade_dec_v2.h"
#include "rade_v2_constants.h"
#include "os_support.h"

void rade_init_decoder_v2(RADEDecV2State *state)
{
    memset(state, 0, sizeof(*state));
}

static void conv1_cond_init(float *mem, int len, int *init)
{
    if (!*init) OPUS_CLEAR(mem, len);
    *init = 1;
}

void rade_core_decoder_v2(
    RADEDecV2State  *state,
    const RADEDecV2 *model,
    float           *features,   /* out: FRAMES_PER_STEP * NUM_FEATURES floats */
    const float     *latents,    /* in:  LATENT_DIM floats */
    int              arch
    )
{
    float *buffer = state->buffer;
    int idx = 0;

    compute_generic_dense(&model->dec_v2_dense1, &buffer[idx], latents, ACTIVATION_TANH, arch);
    idx += DEC_V2_DENSE1_OUT_SIZE;

    compute_generic_gru(&model->dec_v2_gru1_input, &model->dec_v2_gru1_recurrent,
                        state->gru1_state, buffer, arch);
    compute_glu(&model->dec_v2_glu1, &buffer[idx], state->gru1_state, arch);
    idx += DEC_V2_GRU1_OUT_SIZE;
    conv1_cond_init(state->conv1_state, DEC_V2_CONV1_STATE_SIZE, &state->initialized);
    compute_generic_conv1d(&model->dec_v2_conv1, &buffer[idx], state->conv1_state,
                           buffer, idx, ACTIVATION_TANH, arch);
    idx += DEC_V2_CONV1_OUT_SIZE;

    compute_generic_gru(&model->dec_v2_gru2_input, &model->dec_v2_gru2_recurrent,
                        state->gru2_state, buffer, arch);
    compute_glu(&model->dec_v2_glu2, &buffer[idx], state->gru2_state, arch);
    idx += DEC_V2_GRU2_OUT_SIZE;
    conv1_cond_init(state->conv2_state, DEC_V2_CONV2_STATE_SIZE, &state->initialized);
    compute_generic_conv1d(&model->dec_v2_conv2, &buffer[idx], state->conv2_state,
                           buffer, idx, ACTIVATION_TANH, arch);
    idx += DEC_V2_CONV2_OUT_SIZE;

    compute_generic_gru(&model->dec_v2_gru3_input, &model->dec_v2_gru3_recurrent,
                        state->gru3_state, buffer, arch);
    compute_glu(&model->dec_v2_glu3, &buffer[idx], state->gru3_state, arch);
    idx += DEC_V2_GRU3_OUT_SIZE;
    conv1_cond_init(state->conv3_state, DEC_V2_CONV3_STATE_SIZE, &state->initialized);
    compute_generic_conv1d(&model->dec_v2_conv3, &buffer[idx], state->conv3_state,
                           buffer, idx, ACTIVATION_TANH, arch);
    idx += DEC_V2_CONV3_OUT_SIZE;

    compute_generic_gru(&model->dec_v2_gru4_input, &model->dec_v2_gru4_recurrent,
                        state->gru4_state, buffer, arch);
    compute_glu(&model->dec_v2_glu4, &buffer[idx], state->gru4_state, arch);
    idx += DEC_V2_GRU4_OUT_SIZE;
    conv1_cond_init(state->conv4_state, DEC_V2_CONV4_STATE_SIZE, &state->initialized);
    compute_generic_conv1d(&model->dec_v2_conv4, &buffer[idx], state->conv4_state,
                           buffer, idx, ACTIVATION_TANH, arch);
    idx += DEC_V2_CONV4_OUT_SIZE;

    compute_generic_gru(&model->dec_v2_gru5_input, &model->dec_v2_gru5_recurrent,
                        state->gru5_state, buffer, arch);
    compute_glu(&model->dec_v2_glu5, &buffer[idx], state->gru5_state, arch);
    idx += DEC_V2_GRU5_OUT_SIZE;
    conv1_cond_init(state->conv5_state, DEC_V2_CONV5_STATE_SIZE, &state->initialized);
    compute_generic_conv1d(&model->dec_v2_conv5, &buffer[idx], state->conv5_state,
                           buffer, idx, ACTIVATION_TANH, arch);
    idx += DEC_V2_CONV5_OUT_SIZE;

    compute_generic_dense(&model->dec_v2_output, features, buffer, ACTIVATION_LINEAR, arch);
}
