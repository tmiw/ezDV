# rade_models

Generates RADE V2's int8 weights for ezDV (see `CMakeLists.txt`):

```
cmake -S tools/rade_models -B build_models
cmake --build build_models
```

This rewrites `firmware/models/q8_models.bin` (flashed to the "models"
partition by `idf.py flash`) and `firmware/components/rade_v2/q8_layers.c`.
Both are checked in, so this only needs rerunning after a model or the int8
layout changes. The two must always be regenerated together.

Float weights used:

- RADE V2's encoder, decoder and frame sync: from rade_c (fetched at a fixed
  commit).
- FARGAN: `fargan_data.c`, a quarter-width FARGAN trained for ezDV (Opus's
  full one is about four times too slow for the ESP32-S3).
- PitchDNN: `pitchdnn_data.c`, Opus's model, as rade_c builds it.
