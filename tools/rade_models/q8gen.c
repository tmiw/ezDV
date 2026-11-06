/* Writes q8_models.bin and q8_layers.c: the weights of RADE V2's encoder,
   decoder and frame sync networks, FARGAN and PitchDNN (for the
   transmitter's feature extraction), with every big layer as int8 weights
   in nnet_q8.h's layout, and the layer tables that point into the file
   once q8_models_bind() has been given a copy of it. The init_*()
   functions there use those tables instead of the float weights.

   Each layer's weights are found by running it (through Opus's own float
   code) on each unit vector in turn, so sparse layers and GRU diagonals come
   out as plain matrices. Rows are scaled to the int8 range one by one.

   Usage: q8gen <q8_models.bin> <q8_layers.c> */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nnet.h"
#include "nnet_q8.h"
#include "fargan_data.h"
#include "pitchdnn.h"
#include "rade_dec_v2_data.h"
#include "rade_enc_v2_data.h"
#include "rade_sync_data.h"

/* Layers too small to be worth quantizing, FARGAN's layers that Opus also
   leaves in float (cond_net_pembed is also read directly as floats), and the
   encoder's first layer, which takes the raw features, whose range is too
   wide for an int8 input. Small
   float layers aren't free on the ESP32-S3: their weights sit in PSRAM and
   get evicted from the cache by the bigger models' weights. */
#define KEEP_FLOAT_MAX_WEIGHTS 256
static const char *KEEP_FLOAT[] = {"cond_net_pembed", "cond_net_fdense1", "sig_net_cond_gain_dense", "sig_net_gain_dense_out", "enc_v2_dense1"};

typedef struct {
    char kind; /* 'L' for a LinearLayer, 'C' for a Conv2dLayer */
    const char *name;
    void *layer;
} Item;

typedef struct {
    const char *type, *init, *arrays, *header;
    const Item *items;
    int n;
    size_t size; /* of the model struct, to check the items cover it */
} Model;

/* The blob being built: arrays one after the other, 16-byte aligned. */
static unsigned char *blob;
static size_t blob_len, blob_cap;

static size_t add(const void *p, size_t n)
{
    size_t off = (blob_len + 15) & ~(size_t)15;
    if (off + n > blob_cap) {
        blob_cap = 2 * (off + n);
        blob = realloc(blob, blob_cap);
    }
    memset(blob + blob_len, 0, off - blob_len);
    memcpy(blob + off, p, n);
    blob_len = off + n;
    return off;
}

static uint32_t fnv1a(const unsigned char *p, size_t n)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static int keep_float(const char *name, const LinearLayer *l)
{
    const char *extra = getenv("Q8_KEEP_FLOAT"); /* more, comma separated, for experiments */
    size_t i;
    if (extra) {
        const char *p = strstr(extra, name);
        size_t n = strlen(name);
        if (p && (p == extra || p[-1] == ',') && (p[n] == 0 || p[n] == ',')) return 1;
    }
    for (i = 0; i < sizeof(KEEP_FLOAT) / sizeof(KEEP_FLOAT[0]); i++) {
        if (strcmp(name, KEEP_FLOAT[i]) == 0) return 1;
    }
    return l->nb_inputs * l->nb_outputs <= KEEP_FLOAT_MAX_WEIGHTS;
}

/* W[o * nin + i]: output o's weight for input i. */
static float *probe(const LinearLayer *l)
{
    int nin = l->nb_inputs, nout = l->nb_outputs, i, o;
    float *w = malloc(sizeof(float) * nin * nout);
    float *in = calloc(nin, sizeof(float));
    float *base = malloc(sizeof(float) * nout), *out = malloc(sizeof(float) * nout);
    compute_linear_c(l, base, in);
    for (i = 0; i < nin; i++) {
        in[i] = 1;
        compute_linear_c(l, out, in);
        in[i] = 0;
        for (o = 0; o < nout; o++) w[o * nin + i] = out[o] - base[o];
    }
    free(in);
    free(base);
    free(out);
    return w;
}

/* A pointer into the blob, as written in q8_layers.c. */
static void ptr(FILE *f, const char *type, long off)
{
    if (off < 0) fprintf(f, "NULL, ");
    else fprintf(f, "(const %s *)OFF(%ld), ", type, off);
}

static void write_linear(FILE *f, const char *name, const LinearLayer *l, double *worst)
{
    int nin = l->nb_inputs, nout = l->nb_outputs, pad = Q8_PAD(nin), i, o;
    float *w = probe(l);
    long bias = l->bias ? (long)add(l->bias, sizeof(float) * nout) : -1;

    if (keep_float(name, l)) {
        /* Opus's float layout: input i's weights for every output. */
        float *t = malloc(sizeof(float) * nin * nout);
        long fw;
        for (i = 0; i < nin; i++)
            for (o = 0; o < nout; o++) t[i * nout + o] = w[o * nin + i];
        fw = (long)add(t, sizeof(float) * nin * nout);
        free(t);
        fprintf(f, "    {\"%s\", {", name);
        ptr(f, "float", bias);
        fprintf(f, "NULL, NULL, ");
        ptr(f, "float", fw);
        fprintf(f, "NULL, NULL, NULL, %d, %d}, 0},\n", nin, nout);
    } else {
        float *scale = malloc(sizeof(float) * nout);
        signed char *q = calloc(Q8_WEIGHT_BYTES(nin, nout), 1);
        size_t pos = 0;
        long qoff, soff;
        int off;
        for (o = 0; o < nout; o++) {
            float m = 0;
            for (i = 0; i < nin; i++) m = fmaxf(m, fabsf(w[o * nin + i]));
            scale[o] = m > 0 ? m / 127.f : 1.f;
        }
        for (off = 0; off < pad;) {
            int blk = pad - off >= 96 ? 96 : pad - off;
            for (o = 0; o < nout; o++) {
                for (i = off; i < off + blk; i++) {
                    float v = i < nin ? w[o * nin + i] / scale[o] : 0;
                    double e;
                    q[pos] = (signed char)lrintf(v);
                    e = fabs(q[pos] - v) * scale[o];
                    if (e > *worst) *worst = e;
                    pos++;
                }
            }
            off += blk;
        }
        soff = (long)add(scale, sizeof(float) * nout);
        qoff = (long)add(q, Q8_WEIGHT_BYTES(nin, nout));
        fprintf(f, "    {\"%s\", {", name);
        ptr(f, "float", bias);
        fprintf(f, "NULL, ");
        ptr(f, "opus_int8", qoff);
        fprintf(f, "NULL, Q8_LAYOUT, NULL, ");
        ptr(f, "float", soff);
        fprintf(f, "%d, %d}, %zu},\n", nin, nout, Q8_WEIGHT_BYTES(nin, nout));
        free(scale);
        free(q);
    }
    free(w);
}

static int count(const Model *m, char kind)
{
    int k, n = 0;
    for (k = 0; k < m->n; k++) n += m->items[k].kind == kind;
    return n;
}

static void write_model(FILE *f, const Model *m, double *err_out)
{
    int k, nl = count(m, 'L'), nc = count(m, 'C');
    double worst = 0;

    fprintf(f, "Q8Layer %s_q8_layers[%d] = {\n", m->arrays, nl);
    for (k = 0; k < m->n; k++) {
        if (m->items[k].kind == 'L') write_linear(f, m->items[k].name, m->items[k].layer, &worst);
    }
    fprintf(f, "};\nconst int %s_q8_count = %d;\n\n", m->arrays, nl);
    if (nc > 0) {
        fprintf(f, "static Conv2dLayer %s_conv2d[%d] = {\n", m->arrays, nc);
        for (k = 0; k < m->n; k++) {
            const Conv2dLayer *c = m->items[k].layer;
            int nw;
            if (m->items[k].kind != 'C') continue;
            nw = c->in_channels * c->out_channels * c->ktime * c->kheight;
            fprintf(f, "    {");
            ptr(f, "float", c->bias ? (long)add(c->bias, sizeof(float) * c->out_channels) : -1);
            ptr(f, "float", (long)add(c->float_weights, sizeof(float) * nw));
            fprintf(f, "%d, %d, %d, %d}, /* %s */\n", c->in_channels, c->out_channels, c->ktime, c->kheight, m->items[k].name);
        }
        fprintf(f, "};\n\n");
    }

    fprintf(f, "const WeightArray %s[] = {{NULL, 0, 0, NULL}};\n\n", m->arrays);
    fprintf(f, "int %s(%s *model, const WeightArray *arrays)\n{\n    (void)arrays;\n", m->init, m->type);
    for (k = 0, nl = 0, nc = 0; k < m->n; k++) {
        if (m->items[k].kind == 'L') fprintf(f, "    model->%s = %s_q8_layers[%d].layer;\n", m->items[k].name, m->arrays, nl++);
        else fprintf(f, "    model->%s = %s_conv2d[%d];\n", m->items[k].name, m->arrays, nc++);
    }
    fprintf(f, "    return 0;\n}\n\n");
    *err_out = worst;
}

static size_t items_size(const Item *items, int n)
{
    size_t s = 0;
    int k;
    for (k = 0; k < n; k++) s += items[k].kind == 'L' ? sizeof(LinearLayer) : sizeof(Conv2dLayer);
    return s;
}

#define L(m, f) {'L', #f, &(m).f},
#define C(m, f) {'C', #f, &(m).f},

int main(int argc, char **argv)
{
    static RADEDecV2 dec;
    static RADEEncV2 enc;
    static RADESync sync;
    static FARGAN fargan;
    static PitchDNN pitchdnn;
    static const Item dec_items[] = {
#include "layers_radedecv2.inc"
    };
    static const Item enc_items[] = {
#include "layers_radeencv2.inc"
    };
    static const Item sync_items[] = {
#include "layers_radesync.inc"
    };
    static const Item fargan_items[] = {
#include "layers_fargan.inc"
    };
    static const Item pitchdnn_items[] = {
#include "layers_pitchdnn.inc"
    };
#define M(t, i, a, h, items, model) {t, i, a, h, items, sizeof(items) / sizeof(Item), sizeof(model)}
    const Model models[] = {
        M("RADEDecV2", "init_radedecv2", "radedecv2_arrays", "rade_dec_v2_data.h", dec_items, dec),
        M("RADEEncV2", "init_radeencv2", "radeencv2_arrays", "rade_enc_v2_data.h", enc_items, enc),
        M("RADESync", "init_radesync", "radesync_arrays", "rade_sync_data.h", sync_items, sync),
        M("FARGAN", "init_fargan", "fargan_arrays", "fargan_data.h", fargan_items, fargan),
        M("PitchDNN", "init_pitchdnn", "pitchdnn_arrays", "pitchdnn.h", pitchdnn_items, pitchdnn),
    };
    const int nm = sizeof(models) / sizeof(models[0]);
    unsigned char header[Q8_MODELS_HEADER_BYTES] = {0};
    double err[sizeof(models) / sizeof(models[0])];
    uint32_t size, sum;
    FILE *f;
    int k;

    if (argc != 3) {
        fprintf(stderr, "usage: q8gen <q8_models.bin> <q8_layers.c>\n");
        return 1;
    }
    if (init_radedecv2(&dec, radedecv2_arrays) || init_radeencv2(&enc, radeencv2_arrays) || init_radesync(&sync, radesync_arrays) ||
        init_fargan(&fargan, fargan_arrays) || init_pitchdnn(&pitchdnn, pitchdnn_arrays)) {
        fprintf(stderr, "q8gen: could not load the float weights\n");
        return 1;
    }
    /* The tables must cover each model struct exactly. */
    for (k = 0; k < nm; k++) {
        if (items_size(models[k].items, models[k].n) != models[k].size) {
            fprintf(stderr, "q8gen: the layer list for %s doesn't match its struct\n", models[k].type);
            return 1;
        }
    }

    f = fopen(argv[2], "w");
    if (!f) {
        perror(argv[2]);
        return 1;
    }
    fprintf(f, "/* Generated by q8gen; do not edit. The layer tables for q8_models.bin (see nnet_q8.h). */\n\n");
    fprintf(f, "#include <stdint.h>\n#include <string.h>\n\n#include \"nnet.h\"\n#include \"nnet_q8.h\"\n");
    for (k = 0; k < nm; k++) fprintf(f, "#include \"%s\"\n", models[k].header);
    fprintf(f, "\n/* An offset into q8_models.bin, until q8_models_bind() makes it a pointer. */\n");
    fprintf(f, "#define OFF(x) ((const void *)(uintptr_t)(x))\n\n");

    add(header, sizeof(header));
    for (k = 0; k < nm; k++) write_model(f, &models[k], &err[k]);

    /* The header: magic, size and a checksum of the rest. */
    blob_len = (blob_len + 15) & ~(size_t)15;
    size = (uint32_t)blob_len;
    sum = fnv1a(blob + Q8_MODELS_HEADER_BYTES, blob_len - Q8_MODELS_HEADER_BYTES);
    memcpy(blob, Q8_MODELS_MAGIC, 8);
    memcpy(blob + 8, &size, 4);
    memcpy(blob + 12, &sum, 4);

    fprintf(f, "const size_t q8_models_size = %zu;\n\n", blob_len);
    fprintf(f, "static uintptr_t bound_to = 0;\n\n");
    fprintf(f, "static const void *rebase(const void *p, uintptr_t from, uintptr_t to)\n{\n    return p ? (const void *)((uintptr_t)p - from + to) : NULL;\n}\n\n");
    fprintf(f, "static void rebase_layers(Q8Layer *layers, int n, uintptr_t from, uintptr_t to)\n{\n    int i;\n    for (i = 0; i < n; i++) {\n");
    fprintf(f, "        LinearLayer *l = &layers[i].layer;\n");
    fprintf(f, "        l->bias = rebase(l->bias, from, to);\n        l->subias = rebase(l->subias, from, to);\n");
    fprintf(f, "        l->weights = rebase(l->weights, from, to);\n        l->float_weights = rebase(l->float_weights, from, to);\n");
    fprintf(f, "        l->diag = rebase(l->diag, from, to);\n        l->scale = rebase(l->scale, from, to);\n    }\n}\n\n");
    fprintf(f, "int q8_models_bind(const void *blob, size_t len)\n{\n");
    fprintf(f, "    const unsigned char *p = blob;\n    uint32_t size, sum, h = 2166136261u;\n    uintptr_t to = (uintptr_t)blob;\n    size_t i;\n\n");
    fprintf(f, "    if (len < q8_models_size || ((uintptr_t)blob & 15) || memcmp(p, Q8_MODELS_MAGIC, 8) != 0) return -1;\n");
    fprintf(f, "    memcpy(&size, p + 8, 4);\n    memcpy(&sum, p + 12, 4);\n");
    fprintf(f, "    if (size != q8_models_size || sum != %uu) return -1;\n", sum);
    fprintf(f, "    for (i = Q8_MODELS_HEADER_BYTES; i < q8_models_size; i++) h = (h ^ p[i]) * 16777619u;\n");
    fprintf(f, "    if (h != sum) return -1;\n\n");
    for (k = 0; k < nm; k++) {
        int nc = count(&models[k], 'C');
        fprintf(f, "    rebase_layers(%s_q8_layers, %s_q8_count, bound_to, to);\n", models[k].arrays, models[k].arrays);
        if (nc > 0) {
            fprintf(f, "    for (i = 0; i < %d; i++) {\n", nc);
            fprintf(f, "        %s_conv2d[i].bias = rebase(%s_conv2d[i].bias, bound_to, to);\n", models[k].arrays, models[k].arrays);
            fprintf(f, "        %s_conv2d[i].float_weights = rebase(%s_conv2d[i].float_weights, bound_to, to);\n    }\n", models[k].arrays, models[k].arrays);
        }
    }
    fprintf(f, "    bound_to = to;\n    return 0;\n}\n");
    fclose(f);

    f = fopen(argv[1], "wb");
    if (!f || fwrite(blob, 1, blob_len, f) != blob_len || fclose(f) != 0) {
        perror(argv[1]);
        return 1;
    }
    fprintf(stderr, "q8gen: wrote %s (%zu KB) and %s; largest weight rounding error:", argv[1], blob_len / 1024, argv[2]);
    for (k = 0; k < nm; k++) fprintf(stderr, " %s %.3g", models[k].type, err[k]);
    fprintf(stderr, "\n");
    return 0;
}
