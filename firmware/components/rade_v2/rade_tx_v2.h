/*---------------------------------------------------------------------------*\

  rade_tx_v2.h

  RADE V2 transmitter - C port of tx2.py/RADEv2Transmitter.

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

#ifndef RADE_TX_V2_H
#define RADE_TX_V2_H

#include "rade_enc_v2.h"
#include "rade_enc_v2_data.h"
#include "rade_v2_ofdm.h"
#include "rade_bpf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    RADEEncV2      enc_model;
    RADEEncV2State enc_state;
    rade_v2_ofdm   ofdm;
    float          data_symbol;  /* BPSK data symbol (+1.0 or -1.0), default -1.0 */
    int            bpf_en;
    rade_bpf       bpf;          /* SSB BPF state, continuous across rade_tx_v2_process()
                                     and rade_tx_v2_eoo() calls -- matches radae_v2.py's
                                     RADEv2Transmitter, which filters data and EOO through
                                     the same persistent filter (a real SSB radio's front
                                     end doesn't distinguish data samples from EOO ones) */
} rade_tx_v2_state;

/* Initialise V2 transmitter (loads built-in weights).
   bpf_en: 1 to enable Tx SSB bandpass filter (300-2700Hz, matches radae_v2.py's
           ssb_bpf -- the model was trained under this filter).
   Returns 0 on success, -1 on failure. */
int rade_tx_v2_init(rade_tx_v2_state *tx, int bpf_en);

/* Number of input feature values per modem frame (4 frames x 36 floats = 144) */
int rade_tx_v2_n_features_in(void);

/* Number of IQ output samples per modem frame (RADE_V2_NMF = 320) */
int rade_tx_v2_n_samples_out(void);

/* Number of IQ output samples for EOO frame (RADE_V2_NEOO = 960) */
int rade_tx_v2_n_eoo_out(void);

/* Process one modem frame.
   features_in: RADE_V2_FRAMES_PER_STEP x RADE_V2_NB_TOTAL_FEATURES floats
                (lpcnet_demo format: 36 floats/frame, padded)
   tx_out:      RADE_V2_NMF complex samples
   Returns number of output samples written. */
int rade_tx_v2_process(rade_tx_v2_state *tx, RADE_COMP *tx_out, const float *features_in);

/* Generate EOO frame.
   tx_out: RADE_V2_NEOO complex samples
   Returns number of output samples written. */
int rade_tx_v2_eoo(rade_tx_v2_state *tx, RADE_COMP *tx_out);

/* Set the BPSK data symbol to embed in the next modem frame (+1.0 or -1.0). */
void rade_tx_v2_set_data_symbol(rade_tx_v2_state *tx, float symbol);

#ifdef __cplusplus
}
#endif

#endif /* RADE_TX_V2_H */
