/*---------------------------------------------------------------------------*\

  rade_rx_v2.c

  RADE V2 receiver: rate-Fs IQ samples in, speech features out.
  C port of rx2.py / RADEv2Receiver.

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

#include "rade_rx_v2.h"
#include "rade_dec_v2_data.h"
#include "rade_v2_constants.h"
#include "rade_dsp.h"
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*\
                            CONSTANTS
\*---------------------------------------------------------------------------*/

#define ALPHA      0.95f    /* Ry_smooth IIR coefficient                     */
#define BETA       0.999f   /* delta_hat / freq_offset IIR coefficient       */
#define TSIG       0.38f    /* Signal-detection threshold on |Ry_smooth|     */
#define TSIN       4.0f     /* Sine-wave detection ratio threshold            */
#define TEOO       0.75f    /* EOO smoothed sparsity threshold                */
#define ALPHA_EOO  0.70f    /* IIR coefficient for EOO smoother               */

/* SNR estimation: BPF bandwidth relative to 3 kHz reference               */
/* B_bpf = 1.2 * (w[Nc-1] - w[0]) * Fs / (2*pi)                           */
/* For Nc=14 carriers spaced 62.5 Hz: B_bpf ≈ 975 Hz                       */
/* snr_offset_dB = 10*log10(3000 / B_bpf)                                  */
/* These match radae_v2.py defaults.                                         */
#define SNR_CORR_A  1.24392558f
#define SNR_CORR_B  3.33253932f

/* Timing adjustment threshold (sym_len / 4 = 40) */
#define TIMING_SHIFT  (RADE_V2_SYM_LEN / 4)

/*---------------------------------------------------------------------------*\
                           INITIALIZATION
\*---------------------------------------------------------------------------*/

int rade_rx_v2_init(rade_rx_v2_state *rx, int bpf_en) {
    memset(rx, 0, sizeof(*rx));

    /* Load ML model weights */
    if (init_radedecv2(&rx->dec_model, radedecv2_arrays) != 0)
        return -1;
    if (init_radesync(&rx->sync_model, radesync_arrays) != 0)
        return -1;

    /* Initialise sub-components */
    rade_v2_ofdm_init(&rx->ofdm);
    rade_init_decoder_v2(&rx->dec_state);

    rx->timing_adj = 1;

    /* BPF */
    rx->bpf_en = bpf_en;
    if (bpf_en) {
        /* B_bpf = 1.2 * carrier_bandwidth, centre = 1500 Hz */
        float Rs_dash = (float)RADE_FS / RADE_V2_M;  /* 62.5 Hz */
        float w0 = rx->ofdm.w[0];
        float wN = rx->ofdm.w[RADE_V2_NC - 1];
        float bandwidth  = 1.2f * (wN - w0) * (float)RADE_FS / (2.0f * (float)M_PI);
        float centre     = (wN + w0) * (float)RADE_FS / (2.0f * (float)M_PI) / 2.0f;
        rade_bpf_init(&rx->bpf, RADE_BPF_NTAP, (float)RADE_FS, bandwidth, centre,
                      RADE_V2_SYM_LEN + RADE_BPF_NTAP);
        /* SNR offset: 10*log10(3000/B_bpf) */
        rx->snr_offset_dB = 10.0f * log10f(3000.0f / (bandwidth + 1e-12f));
    } else {
        /* Estimate offset based on nominal bandwidth */
        float Rs_dash = (float)RADE_FS / RADE_V2_M;
        float bandwidth = 1.2f * Rs_dash * (RADE_V2_NC - 1);
        rx->snr_offset_dB = 10.0f * log10f(3000.0f / (bandwidth + 1e-12f));
    }
    rx->snr_corr_a = SNR_CORR_A;
    rx->snr_corr_b = SNR_CORR_B;

    /* State machine */
    rx->state    = RADE_RX_V2_IDLE;
    rx->hangover = 75;
    rx->nin      = RADE_V2_SYM_LEN;

    /* AGC on by default. Target RMS is the nominal peak level (1.0) backed
       off by the ~3dB PAPR, matching radae_v2.py's agc_target. */
    rx->agc_en     = 1;
    rx->agc_target = 1.0f * powf(10.0f, -3.0f / 20.0f);
    rx->agc_power  = rx->agc_target * rx->agc_target;

    /* Phase rotator starts at 1+0j */
    rx->rx_phase.real = 1.0f;
    rx->rx_phase.imag = 0.0f;

    return 0;
}

/*---------------------------------------------------------------------------*\
                           QUERY FUNCTIONS
\*---------------------------------------------------------------------------*/

int rade_rx_v2_nin(const rade_rx_v2_state *rx) {
    return rx->nin;
}

int rade_rx_v2_nin_max(void) {
    return RADE_V2_SYM_LEN + TIMING_SHIFT;
}

int rade_rx_v2_n_features_out(void) {
    return RADE_V2_FEATURES_OUT;
}

/*---------------------------------------------------------------------------*\
                           AUTOCORRELATION
\*---------------------------------------------------------------------------*/

/* CP autocorrelation over sym_len offsets.
   For offset gamma: correlate y_cp = buf[sym_len+gamma-Ncp .. sym_len+gamma-1]
   with y_m = buf[sym_len+gamma-Ncp+M .. sym_len+gamma+M-1].
   Normalised result stored in Ry_norm, IIR-smoothed into Ry_smooth. */
static void compute_autocorr(rade_rx_v2_state *rx) {
    int M       = RADE_V2_M;
    int Ncp     = RADE_V2_NCP;
    int sym_len = RADE_V2_SYM_LEN;

    for (int gamma = 0; gamma < sym_len; gamma++) {
        int idx = sym_len + gamma;
        /* y_cp: Ncp samples ending at rx_buf[idx-1] */
        /* y_m:  Ncp samples ending at rx_buf[idx+M-1] */
        RADE_COMP Ry    = {0.0f, 0.0f};
        float     D_cp  = 0.0f;
        float     D_m   = 0.0f;

        assert(idx - Ncp >= 0 && idx - Ncp + M + Ncp - 1 < RADE_V2_RX_BUF_SIZE);
        for (int k = 0; k < Ncp; k++) {
            RADE_COMP a = rx->rx_buf[idx - Ncp + k];
            RADE_COMP b = rx->rx_buf[idx - Ncp + M + k];
            /* Ry += a * conj(b) */
            Ry.real += a.real * b.real + a.imag * b.imag;
            Ry.imag += a.imag * b.real - a.real * b.imag;
            D_cp += a.real * a.real + a.imag * a.imag;
            D_m  += b.real * b.real + b.imag * b.imag;
        }
        float D = D_cp + D_m + 1e-12f;
        rx->Ry_norm[gamma].real = 2.0f * Ry.real / D;
        rx->Ry_norm[gamma].imag = 2.0f * Ry.imag / D;

        /* IIR smooth */
        rx->Ry_smooth[gamma].real = ALPHA * rx->Ry_smooth[gamma].real
                                    + (1.0f - ALPHA) * rx->Ry_norm[gamma].real;
        rx->Ry_smooth[gamma].imag = ALPHA * rx->Ry_smooth[gamma].imag
                                    + (1.0f - ALPHA) * rx->Ry_norm[gamma].imag;
    }
}

/*---------------------------------------------------------------------------*\
                           SIGNAL DETECTION
\*---------------------------------------------------------------------------*/

/* Find argmax|Ry_smooth|, compute Ry_max/Ry_min, signal and sine flags.
   Also updates snr_est_dB. */
static void detect_signal(rade_rx_v2_state *rx, int *sig_det, int *sine_det) {
    int sym_len = RADE_V2_SYM_LEN;

    float max_val = -1.0f;
    float min_val =  1e30f;
    int   max_idx = 0;

    for (int g = 0; g < sym_len; g++) {
        float mag = sqrtf(rx->Ry_smooth[g].real * rx->Ry_smooth[g].real
                        + rx->Ry_smooth[g].imag * rx->Ry_smooth[g].imag);
        if (mag > max_val) { max_val = mag; max_idx = g; }
        if (mag < min_val) { min_val = mag; }
    }

    rx->delta_hat_g = max_idx;
    rx->Ry_max = max_val;
    rx->Ry_min = min_val;

    *sig_det  = (max_val > TSIG);
    *sine_det = (max_val / (min_val + 1e-12f) < TSIN);

    /* SNR estimate */
    float rho = max_val;
    if (rho >= 1.0f) rho = 1.0f - 1e-6f;
    if (rho <= 0.0f) rho = 1e-6f;
    float snr_raw = 10.0f * log10f(rho / (1.0f - rho)) - rx->snr_offset_dB;
    rx->snr_est_dB = rx->snr_corr_a * snr_raw + rx->snr_corr_b;
}

/*---------------------------------------------------------------------------*\
                           SYMBOL EXTRACTION
\*---------------------------------------------------------------------------*/

/* Freq-correct one symbol, shift it into rx_i[2*sym_len], save TD portion.
   Returns nothing; updates rx->rx_i, rx->rx_sym_td, rx->rx_phase. */
static void extract_symbol(rade_rx_v2_state *rx) {
    int sym_len = RADE_V2_SYM_LEN;
    int Ncp     = RADE_V2_NCP;
    int M       = RADE_V2_M;

    int delta_hat_rx = (int)rx->delta_hat - Ncp;
    float omega = 2.0f * (float)M_PI * rx->freq_offset / (float)RADE_FS;

    /* Shift rx_i left by one symbol */
    memmove(rx->rx_i, &rx->rx_i[sym_len], sizeof(RADE_COMP) * sym_len);

    int st = sym_len + delta_hat_rx;
    assert(st >= 0 && st + sym_len <= RADE_V2_RX_BUF_SIZE);

    /* Apply continuous phase rotation, store in rx_i[sym_len..] and rx_sym_td */
    for (int n = 0; n < sym_len; n++) {
        /* rx_phase *= exp(-j*omega) */
        RADE_COMP pstep;
        pstep.real =  cosf(-omega);
        pstep.imag =  sinf(-omega);
        rx->rx_phase = rade_cmul(rx->rx_phase, pstep);

        RADE_COMP samp = rx->rx_buf[st + n];
        rx->rx_i[sym_len + n] = rade_cmul(rx->rx_phase, samp);

        /* TD buffer (M samples after CP removal) */
        if (n >= Ncp)
            rx->rx_sym_td[n - Ncp] = rx->rx_i[sym_len + n];
    }

    /* Normalise phase magnitude to prevent drift */
    float pmag = sqrtf(rx->rx_phase.real * rx->rx_phase.real
                     + rx->rx_phase.imag * rx->rx_phase.imag);
    rx->rx_phase.real /= pmag;
    rx->rx_phase.imag /= pmag;
}

/*---------------------------------------------------------------------------*\
                           FRAME SYNC + DECODE
\*---------------------------------------------------------------------------*/

/* Run FrameSyncNet, update even/odd accumulators.
   If the winning parity symbol, either run the decoder and write features_out
   or (latents_out not NULL) copy the latents to latents_out for
   rade_rx_v2_decode. Returns 1 if either was written. */
static int update_frame_sync_decode(rade_rx_v2_state *rx,
                                    float *features_out, float *latents_out,
                                    int sig_det, int sine_det) {
    int arch = 0;

    float metric = rade_frame_sync(&rx->sync_model, rx->az_hat, arch);

    int winning = 0;
    if (rx->s % 2) {
        rx->frame_sync_odd = BETA * rx->frame_sync_odd + (1.0f - BETA) * metric;
        winning = (rx->frame_sync_odd > rx->frame_sync_even);
    } else {
        rx->frame_sync_even = BETA * rx->frame_sync_even + (1.0f - BETA) * metric;
        winning = (rx->frame_sync_even > rx->frame_sync_odd);
    }

    if (!winning)
        return 0;

    if (latents_out)
        memcpy(latents_out, rx->az_hat, sizeof(rx->az_hat));
    else
        rade_rx_v2_decode(rx, features_out, rx->az_hat);
    return 1;
}

void rade_rx_v2_decode(rade_rx_v2_state *rx, float *features_out,
                       const float *latents) {
    int arch = 0;

    /* Decode latents -> features */
    int frames   = RADE_V2_FRAMES_PER_STEP;
    int num_feat = RADE_V2_NUM_FEATURES;       /* 21 including auxdata */
    int num_used = RADE_V2_NUM_USED_FEATURES;  /* 20 */
    int nb_total = RADE_V2_NB_TOTAL_FEATURES;  /* 36 */

    float dec_features[RADE_V2_FRAMES_PER_STEP * RADE_V2_NUM_FEATURES];
    rade_core_decoder_v2(&rx->dec_state, &rx->dec_model, dec_features, latents, arch);

    memset(features_out, 0, sizeof(float) * RADE_V2_FEATURES_OUT);
    for (int f = 0; f < frames; f++) {
        float *dst = &features_out[f * nb_total];
        float *src = &dec_features[f * num_feat];
        if (src[18] < -1.4f) src[18] = -1.4f;   /* limit_pitch, matches radae_v2.py default */
        for (int j = 0; j < num_used; j++)
            dst[j] = src[j];
    }

    /* Store data symbol from first frame (soft decision ±1) */
    rx->data_symbol = dec_features[0 * num_feat + num_used];
}

float rade_rx_v2_get_data_symbol(const rade_rx_v2_state *rx) {
    return rx->data_symbol;
}

/*---------------------------------------------------------------------------*\
                           EOO DETECTION
\*---------------------------------------------------------------------------*/

/* Channel sparsity EOO metric, IIR-smooth, threshold. */
static int detect_eoo(rade_rx_v2_state *rx) {
    float metric = rade_v2_ofdm_eoo_metric(&rx->ofdm, rx->rx_sym_td);
    rx->eoo_smooth = ALPHA_EOO * rx->eoo_smooth + (1.0f - ALPHA_EOO) * metric;
    return (rx->eoo_smooth > TEOO);
}

/*---------------------------------------------------------------------------*\
                           TIMING ADJUSTMENT
\*---------------------------------------------------------------------------*/

/* Shift delta_hat and rotate Ry_smooth to keep timing centred. */
static int adjust_timing(rade_rx_v2_state *rx) {
    if (!rx->timing_adj)
        return RADE_V2_SYM_LEN;

    int sym_len = RADE_V2_SYM_LEN;
    int shift   = TIMING_SHIFT;
    int nin     = sym_len;

    if (rx->delta_hat > (float)(3 * sym_len / 4)) {
        rx->delta_hat -= (float)shift;
        /* Rotate Ry_smooth left by shift */
        RADE_COMP tmp[TIMING_SHIFT];
        memcpy(tmp, rx->Ry_smooth, sizeof(RADE_COMP) * shift);
        memmove(rx->Ry_smooth, &rx->Ry_smooth[shift], sizeof(RADE_COMP) * (sym_len - shift));
        memcpy(&rx->Ry_smooth[sym_len - shift], tmp, sizeof(RADE_COMP) * shift);
        nin = sym_len + shift;
    } else if (rx->delta_hat < (float)(sym_len / 4)) {
        rx->delta_hat += (float)shift;
        /* Rotate Ry_smooth right by shift */
        RADE_COMP tmp[TIMING_SHIFT];
        memcpy(tmp, &rx->Ry_smooth[sym_len - shift], sizeof(RADE_COMP) * shift);
        memmove(&rx->Ry_smooth[shift], rx->Ry_smooth, sizeof(RADE_COMP) * (sym_len - shift));
        memcpy(rx->Ry_smooth, tmp, sizeof(RADE_COMP) * shift);
        nin = sym_len - shift;
    }

    return nin;
}

/*---------------------------------------------------------------------------*\
                                  AGC
\*---------------------------------------------------------------------------*/

#define AGC_ALPHA 0.99875f  /* IIR filter coeff for AGC power estimate, tau~0.1s @ Fs=8000 */

/* Gain to bring rx_in's RMS to agc_target, clipped to +/-20dB. Power
   estimate is a persistent per-sample IIR average (rx->agc_power) --
   matches radae_v2.py's _compute_gain(). */
static float compute_gain(rade_rx_v2_state *rx, const RADE_COMP *rx_in, int nin) {
    if (!rx->agc_en)
        return 1.0f;

    float p = rx->agc_power;
    for (int i = 0; i < nin; i++) {
        float mag_sq = rx_in[i].real * rx_in[i].real + rx_in[i].imag * rx_in[i].imag;
        p = AGC_ALPHA * p + (1.0f - AGC_ALPHA) * mag_sq;
    }
    rx->agc_power = p;

    float gain = rx->agc_target / (sqrtf(p) + 1e-6f);
    if (gain < 0.1f) gain = 0.1f;
    if (gain > 10.0f) gain = 10.0f;
    return gain;
}

/*---------------------------------------------------------------------------*\
                           MAIN PROCESS FUNCTION
\*---------------------------------------------------------------------------*/

static int process(rade_rx_v2_state *rx, float *features_out,
                   float *latents_out, const RADE_COMP *rx_in) {
    int sym_len  = RADE_V2_SYM_LEN;
    int buf_size = RADE_V2_RX_BUF_SIZE;
    int nin      = rx->nin;

    /* --- BPF --- */
    RADE_COMP rx_filtered[RADE_V2_SYM_LEN + TIMING_SHIFT];
    const RADE_COMP *rx_samples = rx_in;
    if (rx->bpf_en) {
        rade_bpf_process(&rx->bpf, rx_filtered, rx_in, nin);
        rx_samples = rx_filtered;
    }

    /* --- AGC --- */
    RADE_COMP rx_scaled[RADE_V2_SYM_LEN + TIMING_SHIFT];
    float gain = compute_gain(rx, rx_samples, nin);
    rx->gain = gain;
    if (gain != 1.0f) {
        for (int i = 0; i < nin; i++) {
            rx_scaled[i].real = rx_samples[i].real * gain;
            rx_scaled[i].imag = rx_samples[i].imag * gain;
        }
        rx_samples = rx_scaled;
    }

    /* --- Shift rx_buf, append new samples --- */
    memmove(rx->rx_buf, &rx->rx_buf[nin], sizeof(RADE_COMP) * (buf_size - nin));
    memcpy(&rx->rx_buf[buf_size - nin], rx_samples, sizeof(RADE_COMP) * nin);

    /* --- CP autocorrelation --- */
    compute_autocorr(rx);

    /* --- Signal detection --- */
    int sig_det, sine_det;
    detect_signal(rx, &sig_det, &sine_det);

    /* --- State machine --- */
    int valid_output = 0;
    int eoo_flag     = 0;
    int next_state   = rx->state;

    if (rx->state == RADE_RX_V2_IDLE) {
        /* Count consecutive sig_det and not sine_det symbols */
        if (sig_det && !sine_det)
            rx->count++;
        else
            rx->count = 0;

        if (rx->count == 5) {
            /* Acquire: set initial timing/freq from argmax */
            int   dg            = (int)rx->delta_hat_g;
            float delta_phi     = atan2f(rx->Ry_smooth[dg].imag,
                                         rx->Ry_smooth[dg].real);
            rx->delta_hat       = rx->delta_hat_g;
            rx->freq_offset     = -delta_phi * (float)RADE_FS / (2.0f * (float)M_PI * RADE_V2_M);
            rx->count           = 0;
            rx->count1          = 0;
            rx->frame_sync_even = 0.0f;
            rx->frame_sync_odd  = 0.0f;
            rx->eoo_smooth      = 0.0f;
            rx->n_acq++;
            next_state = RADE_RX_V2_SYNC;

            if (rx->verbose)
                fprintf(stderr, "sync: acquired n_acq=%d delta_hat=%.0f freq_offset=%.2f Hz\n",
                        rx->n_acq, rx->delta_hat, rx->freq_offset);
        }

    } else { /* RADE_RX_V2_SYNC */

        /* IIR-track timing and frequency offset */
        int   dg            = (int)rx->delta_hat_g;
        float delta_phi     = atan2f(rx->Ry_smooth[dg].imag,
                                     rx->Ry_smooth[dg].real);
        rx->freq_offset_g   = -delta_phi * (float)RADE_FS / (2.0f * (float)M_PI * RADE_V2_M);
        rx->delta_hat       = BETA * rx->delta_hat       + (1.0f - BETA) * (float)rx->delta_hat_g;
        rx->freq_offset     = BETA * rx->freq_offset     + (1.0f - BETA) * rx->freq_offset_g;

        /* Hangover counter */
        if (!sig_det || sine_det)
            rx->count++;
        else
            rx->count = 0;
        if (rx->count == rx->hangover) {
            next_state = RADE_RX_V2_IDLE;
            rx->count  = 0;
            rx->count1 = 0;
        }

        /* Re-acquire check: new signal with different timing or frequency */
        rx->new_sig_delta_hat = (fabsf((float)rx->delta_hat_g - rx->delta_hat) > (float)RADE_V2_NCP);
        rx->new_sig_f_hat     = (fabsf(rx->freq_offset_g - rx->freq_offset) > 5.0f);
        if (sig_det && (rx->new_sig_delta_hat || rx->new_sig_f_hat))
            rx->count1++;
        else
            rx->count1 = 0;
        if (rx->count1 == 5) {
            next_state = RADE_RX_V2_IDLE;
            rx->count  = 0;
            rx->count1 = 0;
        }

        /* Extract freq-corrected symbol */
        extract_symbol(rx);

        /* OFDM demodulate -> az_hat */
        int time_offset = -16;
        rade_v2_ofdm_demod_frame(&rx->ofdm, rx->az_hat, rx->rx_i, time_offset);

        /* EOO detection */
        if (detect_eoo(rx)) {
            if (rx->verbose)
                fprintf(stderr, "sync: EOO detected\n");
            rx->count      = 0;
            rx->count1     = 0;
            rx->eoo_smooth = 0.0f;
            /* Zero Ry_smooth to prevent instant re-sync */
            memset(rx->Ry_smooth, 0, sizeof(rx->Ry_smooth));
            next_state = RADE_RX_V2_IDLE;
            eoo_flag   = 1;
        } else {
            /* Frame sync + decode */
            valid_output = update_frame_sync_decode(rx, features_out, latents_out, sig_det, sine_det);
            if (valid_output)
                rx->i++;
        }

        /* Timing adjustment */
        rx->nin = adjust_timing(rx);
    }

    /* Symbol counter always advances */
    rx->s++;

    /* Print verbose status */
    if (rx->verbose >= 3) {
        fprintf(stderr, "%4d %4d %s nin: %3d "
                "sig: %d sine: %d c: %2d nsd: %d nsf: %d c1: %2d "
                "fs: %d delta_hat: %3.0f g: %3.0f f_off: %5.2f f_off_g: %5.2f "
                "Ry_max: %5.2f snr: %5.1f dB eoo: %.3f\n",
                rx->s, rx->i,
                (rx->state == RADE_RX_V2_IDLE) ? "idle" : "sync",
                rx->nin, sig_det, sine_det,
                rx->count, rx->new_sig_delta_hat, rx->new_sig_f_hat, rx->count1,
                (rx->frame_sync_odd > rx->frame_sync_even),
                rx->delta_hat, rx->delta_hat_g,
                rx->freq_offset, rx->freq_offset_g,
                rx->Ry_max, rx->snr_est_dB, rx->eoo_smooth);
    } else if (rx->verbose >= 2) {
        fprintf(stderr, "%4d %4d %s sig: %d f_off: %5.2f snr: %5.1f dB eoo: %.3f\n",
                rx->s, rx->i,
                (rx->state == RADE_RX_V2_IDLE) ? "idle" : "sync",
                sig_det, rx->freq_offset, rx->snr_est_dB, rx->eoo_smooth);
    }

    rx->state = next_state;

    /* Reset nin to sym_len when idle */
    if (rx->state == RADE_RX_V2_IDLE)
        rx->nin = sym_len;

    return (valid_output ? 0x1 : 0) | (eoo_flag ? 0x2 : 0);
}

int rade_rx_v2_process(rade_rx_v2_state *rx, float *features_out,
                       const RADE_COMP *rx_in) {
    return process(rx, features_out, NULL, rx_in);
}

int rade_rx_v2_demod(rade_rx_v2_state *rx, float *latents_out,
                     const RADE_COMP *rx_in) {
    return process(rx, NULL, latents_out, rx_in);
}
