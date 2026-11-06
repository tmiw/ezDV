/*---------------------------------------------------------------------------*\

  rade_tx_v2.c

  RADE V2 transmitter: speech features in, rate-Fs IQ samples out.

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2025 David Rowe

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
  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "rade_tx_v2.h"
#include "rade_enc_v2_data.h"
#include <string.h>

int rade_tx_v2_init(rade_tx_v2_state *tx, int bpf_en) {
    memset(tx, 0, sizeof(*tx));

    if (init_radeencv2(&tx->enc_model, radeencv2_arrays) != 0)
        return -1;

    memset(&tx->enc_state, 0, sizeof(tx->enc_state));
    rade_v2_ofdm_init(&tx->ofdm);
    tx->data_symbol = -1.0f;

    tx->bpf_en = bpf_en;
    if (bpf_en) {
        float bandwidth = 2700.0f - 300.0f;
        float centre    = (2700.0f + 300.0f) / 2.0f;
        int   max_len   = RADE_V2_NEOO > RADE_V2_NMF ? RADE_V2_NEOO : RADE_V2_NMF;
        rade_bpf_init(&tx->bpf, RADE_BPF_NTAP, (float)RADE_FS, bandwidth, centre, max_len);
    }

    return 0;
}

int rade_tx_v2_n_features_in(void) {
    return RADE_V2_FRAMES_PER_STEP * RADE_V2_NB_TOTAL_FEATURES;
}

int rade_tx_v2_n_samples_out(void) {
    return RADE_V2_NMF;
}

int rade_tx_v2_n_eoo_out(void) {
    return RADE_V2_NEOO;
}

int rade_tx_v2_process(rade_tx_v2_state *tx, RADE_COMP *tx_out, const float *features_in) {
    int enc_stride   = RADE_V2_FRAMES_PER_STEP;
    int nb_total     = RADE_V2_NB_TOTAL_FEATURES;
    int num_used     = RADE_V2_NUM_USED_FEATURES;
    int num_features = RADE_V2_NUM_FEATURES;   /* 21: 20 + auxdata */
    int arch         = 0;

    /* Reformat: strip padding, append auxdata=-1 */
    float enc_features[RADE_V2_FRAMES_PER_STEP * RADE_V2_NUM_FEATURES];
    for (int i = 0; i < enc_stride; i++) {
        const float *src = &features_in[i * nb_total];
        float       *dst = &enc_features[i * num_features];
        for (int j = 0; j < num_used; j++)
            dst[j] = src[j];
        dst[num_used] = tx->data_symbol;   /* BPSK data symbol */
    }

    /* Encode: enc_stride feature frames -> latent_dim floats */
    float z[RADE_V2_LATENT_DIM];
    rade_core_encoder_v2(&tx->enc_state, &tx->enc_model, z, enc_features, arch);

    /* Modulate: z -> IQ samples */
    int n_out = rade_v2_ofdm_mod_frame(&tx->ofdm, tx_out, z);

    if (tx->bpf_en) {
        rade_bpf_process(&tx->bpf, tx_out, tx_out, n_out);
    }

    return n_out;
}

void rade_tx_v2_set_data_symbol(rade_tx_v2_state *tx, float symbol) {
    tx->data_symbol = symbol;
}

int rade_tx_v2_eoo(rade_tx_v2_state *tx, RADE_COMP *tx_out) {
    int n_eoo;
    const RADE_COMP *eoo = rade_v2_ofdm_get_eoo(&tx->ofdm, &n_eoo);
    memcpy(tx_out, eoo, sizeof(RADE_COMP) * n_eoo);

    if (tx->bpf_en) {
        rade_bpf_process(&tx->bpf, tx_out, tx_out, n_eoo);
    }

    return n_eoo;
}
