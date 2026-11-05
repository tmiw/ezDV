# RADE V2's receiver and transmitter (from rade_c and freedv-backend), with
# int8 weights from tools/rade_models (q8gen): q8_layers.c here, the weights
# themselves (firmware/models/q8_models.bin) in the "models" flash
# partition. (kiss_fft comes from codec2.)
idf_component_register(
    SRCS rade_rx_v2.c rade_v2_ofdm.c rade_dec_v2.c rade_sync.c rade_bpf.c rade_dsp.c
         rade_tx_v2.c rade_enc_v2.c q8_layers.c rade_models.c
    INCLUDE_DIRS .
    REQUIRES opus_dnn esp_partition)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fsingle-precision-constant -Wno-unused-variable -Wno-unused-function)
