# opus_dnn

The parts of Opus's DNN code that RADE V2 needs on ezDV (FARGAN, and the
feature extraction with PitchDNN for the transmitter), from
[Opus](https://github.com/xiph/opus) at commit
`940d4e5af64351ca8ba8390df3f555484c567fbb`, the version rade_c builds (BSD,
see `COPYING`). Changes are kept here rather than upstream.

Changed from Opus:

- `nnet_q8.c/.h`, `pie_gemv.S` (new): `compute_linear_c()` runs layers with
  int8 weights (from `firmware/models/q8_models.bin`) on the ESP32-S3's PIE
  vector unit, and the rest in float.
- `nnet_arch.h`: the 3x3 conv2d is written out for an FPU with no
  auto-vectorizer.
- `fargan.c`, `fargan_data.h`: the quarter-width FARGAN trained for ezDV (its
  weights are in `tools/rade_models/fargan_data.c`), and stack buffers sized
  to the layers (upstream's `skip_cat[10000]` alone is 40 KB of stack).
- `nnet.c`, `pitchdnn.c/.h`, `lpcnet_enc.c`, `freq.c`, `burg.c`: large stack
  buffers moved to the state or sized to the layer, and double precision maths
  replaced with float.
- `config.h`: for the ESP32-S3.

The other files are unchanged from Opus.
