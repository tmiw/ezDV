# The parts of Opus's DNN code that RADE V2 needs (FARGAN, and the feature
# extraction with PitchDNN for the transmitter), with compute_linear()
# replaced by nnet_q8.c (int8 layers on the PIE unit).
idf_component_register(
    SRCS dnn/nnet.c dnn/nnet_q8.c dnn/pie_gemv.S dnn/parse_lpcnet_weights.c dnn/fargan.c
         dnn/lpcnet_enc.c dnn/pitchdnn.c dnn/lpcnet_tables.c dnn/freq.c dnn/burg.c
         celt/kiss_fft.c celt/mathops.c celt/celt_lpc.c celt/pitch.c
    INCLUDE_DIRS . include celt dnn)
target_compile_definitions(${COMPONENT_LIB} PUBLIC HAVE_CONFIG_H)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fsingle-precision-constant -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-maybe-uninitialized)
