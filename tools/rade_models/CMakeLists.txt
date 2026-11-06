# Generates RADE V2's int8 model weights for ezDV, on the desktop (not with
# ESP-IDF):
#   firmware/models/q8_models.bin         the int8 weights, flashed to the
#                                         "models" partition
#   firmware/components/rade_v2/q8_layers.c  the layer tables pointing into it
#
#   cmake -S tools/rade_models -B build_models && cmake --build build_models
#
# Both outputs are checked in; rerun this only after changing a model or the
# int8 layout (nnet_q8.h). The float weights: RADE V2's encoder, decoder and
# frame sync networks from rade_c (fetched at a fixed commit); the
# quarter-width FARGAN trained for ezDV and Opus's PitchDNN, both here.
cmake_minimum_required(VERSION 3.16)
project(ezdv_rade_models C)

set(CMAKE_C_STANDARD 99)
set(FIRMWARE ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware)
set(OPUS ${FIRMWARE}/components/opus_dnn)
set(RADE ${FIRMWARE}/components/rade_v2)

include(FetchContent)
FetchContent_Declare(rade_c
    GIT_REPOSITORY https://github.com/freedv/rade_c.git
    GIT_TAG c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9)
FetchContent_GetProperties(rade_c)
if(NOT rade_c_POPULATED)
    FetchContent_Populate(rade_c)
endif()

# No NEON, so the activations are Opus's generic ones, as on the ESP32-S3.
add_compile_definitions(HAVE_CONFIG_H DISABLE_NEON)
add_compile_options(-O2 -Wall -Wno-unused-function)

add_library(opus_dnn STATIC
    ${OPUS}/dnn/nnet.c ${OPUS}/dnn/nnet_q8.c ${OPUS}/dnn/parse_lpcnet_weights.c
    ${OPUS}/dnn/fargan.c ${OPUS}/dnn/freq.c ${OPUS}/dnn/burg.c
    ${OPUS}/dnn/lpcnet_enc.c ${OPUS}/dnn/pitchdnn.c ${OPUS}/dnn/lpcnet_tables.c
    ${OPUS}/celt/kiss_fft.c ${OPUS}/celt/mathops.c ${OPUS}/celt/celt_lpc.c ${OPUS}/celt/pitch.c)
target_include_directories(opus_dnn PUBLIC ${OPUS} ${OPUS}/include ${OPUS}/celt ${OPUS}/dnn)
target_link_libraries(opus_dnn PUBLIC m)

foreach(m "rade_v2/rade_dec_v2_data.h;RADEDecV2;dec;radedecv2"
          "rade_v2/rade_enc_v2_data.h;RADEEncV2;enc;radeencv2"
          "rade_v2/rade_sync_data.h;RADESync;sync;radesync"
          "opus_dnn/dnn/fargan_data.h;FARGAN;fargan;fargan"
          "opus_dnn/dnn/pitchdnn_data.h;PitchDNN;pitchdnn;pitchdnn")
    list(GET m 0 header)
    list(GET m 1 struct)
    list(GET m 2 var)
    list(GET m 3 name)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/layers_${name}.inc
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/layers_inc.sh ${FIRMWARE}/components/${header} ${struct} ${var} > ${CMAKE_CURRENT_BINARY_DIR}/layers_${name}.inc
        DEPENDS ${FIRMWARE}/components/${header} ${CMAKE_CURRENT_SOURCE_DIR}/layers_inc.sh)
    list(APPEND LAYER_INCS ${CMAKE_CURRENT_BINARY_DIR}/layers_${name}.inc)
endforeach()

add_executable(q8gen q8gen.c
    ${rade_c_SOURCE_DIR}/src/rade_dec_v2_data.c
    ${rade_c_SOURCE_DIR}/src/rade_enc_v2_data.c
    ${rade_c_SOURCE_DIR}/src/rade_sync_data.c
    fargan_data.c pitchdnn_data.c
    ${LAYER_INCS})
target_include_directories(q8gen PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${RADE})
target_link_libraries(q8gen opus_dnn)

add_custom_command(OUTPUT ${FIRMWARE}/models/q8_models.bin ${RADE}/q8_layers.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FIRMWARE}/models
    COMMAND q8gen ${FIRMWARE}/models/q8_models.bin ${RADE}/q8_layers.c
    DEPENDS q8gen)
add_custom_target(q8_models ALL DEPENDS ${FIRMWARE}/models/q8_models.bin)
