/* See rade_models.h. */
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"

#include "nnet_q8.h"
#include "rade_models.h"

#define TAG "rade_models"
#define PARTITION_SUBTYPE 0x40

const void *rade_models_load(void)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, PARTITION_SUBTYPE, "models");
    void *blob;

    if (part == NULL || part->size < q8_models_size) {
        ESP_LOGE(TAG, "no models partition big enough for the %u byte weights", (unsigned)q8_models_size);
        return NULL;
    }
    blob = heap_caps_aligned_alloc(16, q8_models_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (blob == NULL) {
        ESP_LOGE(TAG, "no room in PSRAM for the %u byte weights", (unsigned)q8_models_size);
        return NULL;
    }
    if (esp_partition_read(part, 0, blob, q8_models_size) != ESP_OK || q8_models_bind(blob, q8_models_size) != 0) {
        ESP_LOGE(TAG, "the models partition doesn't hold the weights this firmware was built with");
        heap_caps_free(blob);
        return NULL;
    }
    return blob;
}

void rade_models_unload(const void *models)
{
    heap_caps_free((void *)models);
}
