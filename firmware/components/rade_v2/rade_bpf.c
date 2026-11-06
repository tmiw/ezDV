/*---------------------------------------------------------------------------*\

  rade_bpf.c

  Complex bandpass filter for RADAE.

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2024 David Rowe

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

#include "rade_bpf.h"
#include <string.h>
#include <assert.h>

/*---------------------------------------------------------------------------*\
                           INITIALIZATION
\*---------------------------------------------------------------------------*/

void rade_bpf_init(rade_bpf *bpf, int ntap, float Fs_Hz, float bandwidth_Hz,
                   float centre_freq_Hz, int max_len) {
    assert(ntap <= RADE_BPF_NTAP);
    assert(ntap % 2 == 1);  /* ntap should be odd for symmetric filter */

    bpf->ntap = ntap;
    bpf->alpha = 2.0f * M_PI * centre_freq_Hz / Fs_Hz;
    bpf->max_len = max_len;

    /* Generate lowpass filter coefficients using sinc function
       Bandwidth B = bandwidth_Hz / Fs_Hz (normalized)
       h[n] = B * sinc(n * B) where n is centered at (ntap-1)/2 */
    float B = bandwidth_Hz / Fs_Hz;
    for (int i = 0; i < ntap; i++) {
        int n = i - (ntap - 1)/2;
        bpf->h[i] = B * rade_sinc(n * B);
    }

    for (int k = 0; k < ntap; k++) {
        bpf->g_real[k] = bpf->h[k] * cosf(bpf->alpha * k);
        bpf->g_imag[k] = bpf->h[k] * sinf(bpf->alpha * k);
    }

    /* Initialize state */
    rade_bpf_reset(bpf);
}

void rade_bpf_reset(rade_bpf *bpf) {
    /* Clear filter memory */
    memset(bpf->x_real, 0, sizeof(bpf->x_real));
    memset(bpf->x_imag, 0, sizeof(bpf->x_imag));
    bpf->pos = 0;
    bpf->real_run = bpf->ntap;
}

/*---------------------------------------------------------------------------*\
                              PROCESSING
\*---------------------------------------------------------------------------*/

void rade_bpf_process(rade_bpf *bpf, RADE_COMP *y, const RADE_COMP *x, int n) {
    assert(n <= bpf->max_len);

    int ntap = bpf->ntap;
    const float *gr = bpf->g_real, *gi = bpf->g_imag;

    for (int i = 0; i < n; i++) {
        int pos = bpf->pos = (bpf->pos == 0 ? ntap : bpf->pos) - 1;
        bpf->x_real[pos] = bpf->x_real[pos + ntap] = x[i].real;
        bpf->x_imag[pos] = bpf->x_imag[pos + ntap] = x[i].imag;
        const float *xr = &bpf->x_real[pos], *xi = &bpf->x_imag[pos];
        bpf->real_run = x[i].imag != 0.0f ? 0 : bpf->real_run < ntap ? bpf->real_run + 1 : ntap;

        /* y = sum over k of g[k] x[n-k] */
        float re = 0.0f, im = 0.0f;
        if (bpf->real_run == ntap) {
            /* No imaginary parts in the history */
            for (int k = 0; k < ntap; k++) {
                re += gr[k] * xr[k];
                im += gi[k] * xr[k];
            }
        } else {
            for (int k = 0; k < ntap; k++) {
                re += gr[k] * xr[k] - gi[k] * xi[k];
                im += gi[k] * xr[k] + gr[k] * xi[k];
            }
        }
        y[i].real = re;
        y[i].imag = im;
    }
}
