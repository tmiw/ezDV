/* Opus build settings for the parts of Opus's DNN code that RADE V2's
   receiver and FARGAN need, on the ESP32-S3 and on the desktop reference.
   No SIMD paths: compute_linear() is replaced by nnet_q8.c. */
#ifndef OPUS_DNN_CONFIG_H
#define OPUS_DNN_CONFIG_H

#define OPUS_BUILD
#define VAR_ARRAYS 1
#define FLOAT_APPROX 1
#define HAVE_LRINT 1
#define HAVE_LRINTF 1
#define DISABLE_DEBUG_FLOAT 1

#endif
