/* Opus's generic layer maths (compute_linear_c and friends) for RADE V2's
   receiver and FARGAN, with int8 layers (see nnet_q8.h) done by the
   ESP32-S3's PIE unit, or by plain C elsewhere. Layers kept in float go
   through Opus's own code unchanged. */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h" /* for CONFIG_IDF_TARGET_ESP32S3 */
#endif

/* Opus's own versions, renamed compute_*_float_c. */
#define RTCD_ARCH float_c
#include "nnet_arch.h"

#include "nnet_q8.h"

void compute_linear_float_c(const LinearLayer *linear, float *out, const float *in);
void compute_activation_float_c(float *output, const float *input, int N, int activation);
void compute_conv2d_float_c(const Conv2dLayer *conv, float *out, float *mem, const float *in, int height, int hstride, int activation);

#define Q8_MAX_INPUTS 2048
#define Q8_MAX_OUTPUTS 512

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(Q8_FORCE_C)
#define Q8_USE_PIE 1
#endif

#if Q8_USE_PIE
extern void pie_gemv_k2(const int8_t *w, const int8_t *x, int32_t *acc, int rows);
extern void pie_gemv_k4(const int8_t *w, const int8_t *x, int32_t *acc, int rows);
extern void pie_gemv_k6(const int8_t *w, const int8_t *x, int32_t *acc, int rows);
#else
/* The same sums as the PIE kernels, in plain C: acc[r] += row r of a block
   of k inputs times x. */
static void gemv_block(const int8_t *w, const int8_t *x, int32_t *acc, int rows, int k)
{
    int r, i;
    for (r = 0; r < rows; r++) {
        int32_t sum = 0;
        for (i = 0; i < k; i++) sum += w[i] * x[i];
        acc[r] += sum;
        w += k;
    }
}
#endif

const int q8_layout_marker[1] = {0};

const char *q8_kernel_name(void)
{
#if Q8_USE_PIE
    return "PIE";
#else
    return "C";
#endif
}

int q8_is_q8(const LinearLayer *linear)
{
    return linear->weights_idx == Q8_LAYOUT;
}

static void compute_linear_q8(const LinearLayer *linear, float *out, const float *in)
{
    const int8_t *w = (const int8_t *)linear->weights;
    int n = linear->nb_inputs, rows = linear->nb_outputs, pad = Q8_PAD(n);
    int i, off;
    float xmax = 0, xscale;
    /* On the stack, sized to the layer, so that the decoder and FARGAN can
       run on different cores at once without large stacks. The PIE kernels
       want both 16-byte aligned. */
    int8_t xbuf[pad + 15];
    int32_t accbuf[rows + 3];
    int8_t *x = (int8_t *)(((uintptr_t)xbuf + 15) & ~(uintptr_t)15);
    int32_t *acc = (int32_t *)(((uintptr_t)accbuf + 15) & ~(uintptr_t)15);

    celt_assert(pad <= Q8_MAX_INPUTS && rows <= Q8_MAX_OUTPUTS);
    for (i = 0; i < n; i++) {
        float a = fabsf(in[i]);
        if (a > xmax) xmax = a;
    }
    if (xmax == 0) {
        for (i = 0; i < rows; i++) out[i] = linear->bias ? linear->bias[i] : 0;
        return;
    }
    xscale = 127.f / xmax;
    /* Round half away from zero by hand: lrintf() is a library call on the
       ESP32-S3, and this loop runs for every input of every layer. */
    for (i = 0; i < n; i++) {
        float v = in[i] * xscale;
        x[i] = (int8_t)(int)(v + (v >= 0 ? 0.5f : -0.5f));
    }
    memset(&x[n], 0, pad - n);
    memset(acc, 0, rows * sizeof(acc[0]));

    for (off = 0; off < pad;) {
        int k = pad - off >= 96 ? 96 : pad - off;
#if Q8_USE_PIE
        if (k == 96) pie_gemv_k6(w, &x[off], acc, rows);
        else if (k == 64) pie_gemv_k4(w, &x[off], acc, rows);
        else pie_gemv_k2(w, &x[off], acc, rows);
#else
        gemv_block(w, &x[off], acc, rows, k);
#endif
        off += k;
        w += (size_t)rows * k;
    }

    xscale = 1.f / xscale;
    for (i = 0; i < rows; i++) {
        out[i] = acc[i] * (linear->scale[i] * xscale) + (linear->bias ? linear->bias[i] : 0);
    }
}

void compute_linear_c(const LinearLayer *linear, float *out, const float *in)
{
    if (q8_is_q8(linear)) {
        compute_linear_q8(linear, out, in);
    } else {
        compute_linear_float_c(linear, out, in);
    }
}

void compute_activation_c(float *output, const float *input, int N, int activation)
{
    compute_activation_float_c(output, input, N, activation);
}

void compute_conv2d_c(const Conv2dLayer *conv, float *out, float *mem, const float *in, int height, int hstride, int activation)
{
    compute_conv2d_float_c(conv, out, mem, in, height, hstride, activation);
}
