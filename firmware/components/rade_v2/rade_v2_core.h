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

#ifndef RADE_V2_CORE_H
#define RADE_V2_CORE_H

#include <stdlib.h>

#include "opus_types.h"
#include "nnet.h"

typedef struct RADEEncV2     RADEEncV2;
typedef struct RADEDecV2     RADEDecV2;
typedef struct RADESync      RADESync;
typedef struct RADEEncV2State RADEEncV2State;
typedef struct RADEDecV2State RADEDecV2State;

void rade_init_encoder_v2(RADEEncV2State *state);
void rade_core_encoder_v2(RADEEncV2State *state, const RADEEncV2 *model,
                          float *latents, const float *input, int arch);

void rade_init_decoder_v2(RADEDecV2State *state);
void rade_core_decoder_v2(RADEDecV2State *state, const RADEDecV2 *model,
                          float *features, const float *latents, int arch);

float rade_frame_sync(const RADESync *model, const float *latents, int arch);

extern const WeightArray radeencv2_arrays[];
extern const WeightArray radedecv2_arrays[];
extern const WeightArray radesync_arrays[];

#endif /* RADE_V2_CORE_H */
