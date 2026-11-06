/* RADE V2's model weights on the ESP32-S3: read from the "models" flash
   partition (q8_models.bin, written there by the flash script) into PSRAM,
   and bound to the layer tables (q8_models_bind()). */
#ifndef RADE_MODELS_H
#define RADE_MODELS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the copy in PSRAM (keep it until rade_models_unload()), or NULL if
   there's no models partition, it doesn't hold the weights this firmware was
   built with, or there isn't room. Call before initialising any model. */
const void *rade_models_load(void);

void rade_models_unload(const void *models);

#ifdef __cplusplus
}
#endif

#endif /* RADE_MODELS_H */
