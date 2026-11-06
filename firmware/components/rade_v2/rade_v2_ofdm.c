/*---------------------------------------------------------------------------*\

  rade_v2_ofdm.c

  OFDM modulator/demodulator for RADE V2 (C port).

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

#include "rade_v2_ofdm.h"
#include "rade_v2_constants.h"
#include <string.h>
#include <math.h>

/*---------------------------------------------------------------------------*\
                           INITIALIZATION
\*---------------------------------------------------------------------------*/

void rade_v2_ofdm_init(rade_v2_ofdm *ofdm) {
    int   Nc  = RADE_V2_NC;
    int   M   = RADE_V2_M;
    int   Ncp = RADE_V2_NCP;
    float Fs  = (float)RADE_FS;

    /* Carrier frequencies: centre signal on 1500 Hz
       Rs' = Fs/M = 8000/128 = 62.5 Hz
       carrier_1_freq = 1500 - Rs'*Nc/2 = 1500 - 62.5*7 = 1062.5 Hz
       carrier_1_index = round(1062.5/62.5) = 17 */
    float Rs_dash = Fs / M;
    float carrier_1_freq = 1500.0f - Rs_dash * Nc / 2.0f;
    int   carrier_1_index = (int)roundf(carrier_1_freq / Rs_dash);

    for (int c = 0; c < Nc; c++) {
        ofdm->w[c] = 2.0f * M_PI * (carrier_1_index + c) / M;
    }

    /* IDFT matrix: Winv[n][c] = exp(j*w[c]*n) / M */
    for (int n = 0; n < M; n++) {
        for (int c = 0; c < Nc; c++) {
            ofdm->Winv[n][c] = rade_cscale(rade_cexp(ofdm->w[c] * n), 1.0f / M);
        }
    }

    /* DFT matrix: Wfwd[c][n] = exp(-j*w[c]*n)
       freq_out[c] = sum_n time_in[n] * Wfwd[c][n] */
    for (int c = 0; c < Nc; c++) {
        for (int n = 0; n < M; n++) {
            ofdm->Wfwd[c][n] = rade_cexp(-ofdm->w[c] * n);
        }
    }

    /* Per-carrier phase correction for correct_time_offset = -8 samples:
       phase_corr[c] = exp(j * 8 * w[c])   (= exp(-j * correct_time_offset * w[c])) */
    for (int c = 0; c < Nc; c++) {
        ofdm->phase_corr[c] = rade_cexp(8.0f * ofdm->w[c]);
    }

    /* EOO pilot symbols (freq domain): pend[c] = Pend[c] */
    RADE_COMP P[RADE_V2_NC];
    rade_barker_pilots(P, Nc);
    rade_eoo_pilots(ofdm->pend, P, Nc);

    /* EOO pilot time domain (no CP): pend_td[n] = sum_c pend[c] * Winv[n][c] */
    memset(ofdm->pend_td, 0, sizeof(ofdm->pend_td));
    for (int n = 0; n < M; n++) {
        for (int c = 0; c < Nc; c++) {
            ofdm->pend_td[n] = rade_cadd(ofdm->pend_td[n],
                                          rade_cmul(ofdm->pend[c], ofdm->Winv[n][c]));
        }
    }

    /* Insert cyclic prefix: pend_cp = [pend_td[-Ncp:], pend_td] */
    memcpy(ofdm->pend_cp,        &ofdm->pend_td[M - Ncp], sizeof(RADE_COMP) * Ncp);
    memcpy(&ofdm->pend_cp[Ncp],  ofdm->pend_td,           sizeof(RADE_COMP) * M);

    /* EOO frame: 6 x pend_cp scaled by pilot_gain_eoo_v2
       pilot_backoff_eoo_v2 = 10^(-8/20), pilot_gain = backoff * M / sqrt(Nc) */
    float pilot_backoff = powf(10.0f, -8.0f / 20.0f);
    float pilot_gain    = pilot_backoff * M / sqrtf((float)Nc);

    for (int i = 0; i < RADE_V2_N_EOO_SYMS; i++) {
        for (int n = 0; n < RADE_V2_SYM_LEN; n++) {
            ofdm->eoo[i * RADE_V2_SYM_LEN + n] = rade_cscale(ofdm->pend_cp[n], pilot_gain);
        }
    }
}

/*---------------------------------------------------------------------------*\
                           MODULATION (TX)
\*---------------------------------------------------------------------------*/

/* Modulate one modem frame: z[latent_dim] -> tx_out[RADE_V2_NMF]
   V2 frame: Ns=2 data symbols, no pilots.
   z layout (interleaved real/imag): tx_sym[k] = z[2k] + j*z[2k+1]
   sym[s][c] = tx_sym[s*Nc + c] for s=0..Ns-1, c=0..Nc-1 */
int rade_v2_ofdm_mod_frame(const rade_v2_ofdm *ofdm, RADE_COMP *tx_out, const float *z) {
    int Nc  = RADE_V2_NC;
    int M   = RADE_V2_M;
    int Ncp = RADE_V2_NCP;
    int Ns  = RADE_V2_NS;

    RADE_COMP time_buf[RADE_V2_M];

    for (int s = 0; s < Ns; s++) {
        /* Build frequency-domain symbol from interleaved z */
        RADE_COMP freq_sym[RADE_V2_NC];
        for (int c = 0; c < Nc; c++) {
            int z_idx = (s * Nc + c) * 2;
            freq_sym[c].real = z[z_idx];
            freq_sym[c].imag = z[z_idx + 1];
        }

        /* IDFT: time_buf[n] = sum_c freq_sym[c] * Winv[n][c] */
        for (int n = 0; n < M; n++) {
            time_buf[n] = rade_cdot_comp(freq_sym, ofdm->Winv[n], Nc);
        }

        /* Insert cyclic prefix: [time[-Ncp:], time] */
        RADE_COMP *sym_out = &tx_out[s * RADE_V2_SYM_LEN];
        memcpy(sym_out,        &time_buf[M - Ncp], sizeof(RADE_COMP) * Ncp);
        memcpy(&sym_out[Ncp],  time_buf,           sizeof(RADE_COMP) * M);
    }

    return RADE_V2_NMF;
}

/*---------------------------------------------------------------------------*\
                           DEMODULATION (RX)
\*---------------------------------------------------------------------------*/

/* Demodulate one modem frame.
   rx_i: freq-corrected IQ samples, Ns * SYM_LEN = 320 complex samples
         (last Ns symbols, updated each symbol)
   z_hat: LATENT_DIM output floats
   time_offset: fine timing offset into CP (default -16, gives start at Ncp+time_offset=16)
   phase_corr applied per carrier to compensate correct_time_offset=-8. */
void rade_v2_ofdm_demod_frame(const rade_v2_ofdm *ofdm, float *z_hat,
                               const RADE_COMP *rx_i, int time_offset) {
    int Nc  = RADE_V2_NC;
    int M   = RADE_V2_M;
    int Ncp = RADE_V2_NCP;
    int Ns  = RADE_V2_NS;
    int sym_len = RADE_V2_SYM_LEN;

    for (int s = 0; s < Ns; s++) {
        /* Strip CP with timing offset: start at Ncp + time_offset */
        const RADE_COMP *rx_dash = &rx_i[s * sym_len + Ncp + time_offset];

        /* DFT: rx_sym[c] = sum_n rx_dash[n] * Wfwd[c][n] */
        RADE_COMP rx_sym[RADE_V2_NC];
        for (int c = 0; c < Nc; c++) {
            rx_sym[c] = rade_cdot_comp(rx_dash, ofdm->Wfwd[c], M);
            /* Apply phase correction for correct_time_offset */
            rx_sym[c] = rade_cmul(rx_sym[c], ofdm->phase_corr[c]);
        }

        /* Pack into z_hat: z[2*(s*Nc+c)] = real, z[2*(s*Nc+c)+1] = imag */
        for (int c = 0; c < Nc; c++) {
            int z_idx = (s * Nc + c) * 2;
            z_hat[z_idx]     = rx_sym[c].real;
            z_hat[z_idx + 1] = rx_sym[c].imag;
        }
    }
}

/* EOO channel sparsity metric.
   rx_sym_td: M freq-corrected time-domain samples (no CP).
   Returns instantaneous e_cp/e_total ratio. */
float rade_v2_ofdm_eoo_metric(const rade_v2_ofdm *ofdm, const RADE_COMP *rx_sym_td) {
    int Nc  = RADE_V2_NC;
    int M   = RADE_V2_M;
    int Ncp = RADE_V2_NCP;

    /* DFT of received symbol at carrier frequencies */
    RADE_COMP rx_fd[RADE_V2_NC];
    for (int c = 0; c < Nc; c++) {
        rx_fd[c] = rade_cdot_comp(rx_sym_td, ofdm->Wfwd[c], M);
    }

    /* Channel estimate at each carrier: H_est[c] = rx_fd[c] / pend[c]
       pend[c] = EOO pilot freq-domain symbol (pre-computed in init) */
    RADE_COMP H_est[RADE_V2_NC];
    for (int c = 0; c < Nc; c++) {
        H_est[c] = rade_cdiv(rx_fd[c], ofdm->pend[c]);
    }

    /* IDFT of H_est to get channel impulse response estimate h_est.
       Only active carriers (H_est) contribute; others are zero.
       h_est[n] = sum_c H_est[c] * Winv[n][c]  (uses existing IDFT matrix) */
    float e_total = 1e-12f;
    float e_cp    = 0.0f;
    for (int n = 0; n < M; n++) {
        RADE_COMP h = rade_cdot_comp(H_est, ofdm->Winv[n], Nc);
        float mag2 = h.real * h.real + h.imag * h.imag;
        e_total += mag2;
        if (n < Ncp || n >= M - Ncp)
            e_cp += mag2;
    }

    return e_cp / e_total;
}

/*---------------------------------------------------------------------------*\
                           EOO
\*---------------------------------------------------------------------------*/

const RADE_COMP *rade_v2_ofdm_get_eoo(const rade_v2_ofdm *ofdm, int *n_out) {
    *n_out = RADE_V2_NEOO;
    return ofdm->eoo;
}
