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

/* FrameSyncNet C inference.
   Architecture: 3-layer dense feedforward (stateless).
     dense1:  LATENT_DIM -> 64  ReLU
     dense2:  64         -> 64  ReLU
     dense3:  64         -> 1   Sigmoid
   Returns a single float in [0, 1].
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rade_sync.h"
#include "rade_v2_constants.h"

float rade_frame_sync(const RADESync *model, const float *latents, int arch)
{
    float h1[SYNC_DENSE1_OUT_SIZE];
    float h2[SYNC_DENSE2_OUT_SIZE];
    float out[SYNC_DENSE3_OUT_SIZE];

    compute_generic_dense(&model->sync_dense1, h1, latents,   ACTIVATION_RELU,    arch);
    compute_generic_dense(&model->sync_dense2, h2, h1,        ACTIVATION_RELU,    arch);
    compute_generic_dense(&model->sync_dense3, out, h2,       ACTIVATION_SIGMOID, arch);

    return out[0];
}
