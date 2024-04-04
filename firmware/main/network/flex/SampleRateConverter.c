/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2024 Mooneer Salem
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include "esp_dsp.h"

#include "SampleRateConverter.h"

// (int16(fir1(47, 1/3) * 32767))' in Octave
static const short fdmdv_os_filter24_short[] = {
    -20,
    -39,
    -21,
     33,
     77,
     45,
    -72,
    -169,
    -98,
     145,
     335,
     193,
    -268,
    -613,
    -353,
     471,
    1097,
     649,
    -861,
   -2134,
   -1393,
    2064,
    6903,
   10412,
   10412,
    6903,
    2064,
   -1393,
   -2134,
    -861,
     649,
    1097,
     471,
    -353,
    -613,
    -268,
     193,
     335,
     145,
     -98,
    -169,
     -72,
      45,
      77,
      33,
     -21,
     -39,
     -20,
};

// The below three arrays split up the one above into thirds
// (i.e. short0 has values from indexes 0, 3, 6, ..., short1 from 
// indexes 1, 4, 7, ... and short2 from 2, 5, 8, ...). This is needed
// in order to use the vectorized ESP-DSP dot product functions to
// calculate the FIR in the upsampling case.
static const short fdmdv_os_filter24_short0[] = {
    -20,
     33,
    -72,
     145,
    -268,
     471,
    -861,
    2064,
   10412,
   -1393,
     649,
    -353,
     193,
     -98,
      45,
     -21,
};

static const short fdmdv_os_filter24_short1[] = {
    -39,
     77,
    -169,
     335,
    -613,
    1097,
   -2134,
    6903,
    6903,
   -2134,
    1097,
    -613,
     335,
    -169,
      77,
     -39,
};

static const short fdmdv_os_filter24_short2[] = {
    -21,
     45,
    -98,
     193,
    -353,
     649,
   -1393,
   10412,
    2064,
    -861,
     471,
    -268,
     145,
     -72,
      33,
     -20,
};

/*---------------------------------------------------------------------------*\
                                                       
  FUNCTION....: fdmdv_8_to_24()	     
  AUTHOR......: Mooneer Salem		      
  DATE CREATED: 16 Feb 2023
  Changes the sample rate of a signal from 8 to 24 kHz (and from short to float).
  n is the number of samples at the 8 kHz rate, there are FDMDV_OS_24*n samples
  at the 24 kHz rate.  A memory of FDMDV_OS_TAPS_24/FDMDV_OS_24 samples is reqd for
  in8k[] (see t24_8.c unit test as example).
\*---------------------------------------------------------------------------*/

void fdmdv_8_to_24_with_scaling(float out24k[], short in8k[], int n, float scaleFactor)
{
    scaleFactor *= FDMDV_OS_24;

    short tmp0 = 0;
    short tmp1 = 0;
    short tmp2 = 0;

    short* data = &in8k[-FDMDV_OS_TAPS_24_8K];
    float* out = &out24k[0];

    // The below is equivalent to the following C code:
    //
    // for(int i=0; i<n; i++) 
    // {
    //     dsps_dotprod_s16(fdmdv_os_filter24_short0, &in8k[i - FDMDV_OS_TAPS_24_8K], &tmp0, FDMDV_OS_TAPS_24_8K, 0);
    //     dsps_dotprod_s16(fdmdv_os_filter24_short1, &in8k[i - FDMDV_OS_TAPS_24_8K], &tmp1, FDMDV_OS_TAPS_24_8K, 0);
    //     dsps_dotprod_s16(fdmdv_os_filter24_short2, &in8k[i - FDMDV_OS_TAPS_24_8K], &tmp2, FDMDV_OS_TAPS_24_8K, 0);
    //     out24k[i*FDMDV_OS_24] = tmp0 * FDMDV_SHORT_TO_FLOAT * FDMDV_OS_24 * scaleFactor;
    //     out24k[i*FDMDV_OS_24 + 1] = tmp1 * FDMDV_SHORT_TO_FLOAT * FDMDV_OS_24 * scaleFactor;
    //     out24k[i*FDMDV_OS_24 + 2] = tmp2 * FDMDV_SHORT_TO_FLOAT * FDMDV_OS_24 * scaleFactor;
    // }
    //
    // As each of the filter arrays are of fixed size (16 entries each) and share one operand (in8k), we don't actually
    // need to eat the overhead of calling into ESP-DSP three separate times. We can simply retrieve each of the blocks
    // of the filter entries and the input data once and perform dot products on each of those registers.
    asm volatile(
      "movi a9, 15\n"                                                          // a9 = 15
      "ld.qr q0, %[filter0], 0\n"                                              // Load filter0 into q0
      "ld.qr q1, %[filter1], 0\n"                                              // Load filter1 into q1
      "ld.qr q2, %[filter2], 0\n"                                              // Load filter1 into q2
      "ld.qr q4, %[filter0], 16\n"                                             // Load filter0 + 16 into q4
      "ld.qr q5, %[filter1], 16\n"                                             // Load filter1 + 16 into q5
      "ld.qr q6, %[filter2], 16\n"                                             // Load filter1 + 16 into q6
      "loopgtz %[n], fdmdv_8_to_24_with_scaling_loop_end\n"                    // while (n > 0) {
      "                                     ld.qr q3, %[data], 0\n"            // Load data into q3
      "                                     ld.qr q7, %[data], 16\n"           // Load data + 16 into q7
      "                                     addi %[data], %[data], 2\n"        // data++

      "                                     ee.zero.accx\n"                    // accx = 0
      "                                     ee.vmulas.s16.accx q0, q3\n"       // accx += q0 * q3
      "                                     ee.vmulas.s16.accx q4, q7\n"       // accx += q4 * q7
      "                                     ee.srs.accx %[tmp0], a9, 0\n"      // tmp0 = accx >> a9

      "                                     ee.zero.accx\n"                    // accx = 0
      "                                     ee.vmulas.s16.accx q1, q3\n"       // accx += q1 * q3
      "                                     ee.vmulas.s16.accx q5, q7\n"       // accx += q5 * q7
      "                                     ee.srs.accx %[tmp1], a9, 0\n"      // tmp1 = accx >> a9

      "                                     ee.zero.accx\n"                    // accx = 0
      "                                     ee.vmulas.s16.accx q2, q3\n"       // accx += q2 * q3
      "                                     ee.vmulas.s16.accx q6, q7\n"       // accx += q6 * q7
      "                                     ee.srs.accx %[tmp2], a9, 0\n"      // tmp2 = accx >> a9

      "                                     float.s f1, %[tmp0], 15\n"         // f1 = tmp0 * FDMDV_SHORT_TO_FLOAT
      "                                     float.s f2, %[tmp1], 15\n"         // f2 = tmp1 * FDMDV_SHORT_TO_FLOAT
      "                                     float.s f3, %[tmp2], 15\n"         // f3 = tmp2 * FDMDV_SHORT_TO_FLOAT
      "                                     mul.s f1, f1, %[scaleFactor]\n"    // f1 *= scaleFactor
      "                                     mul.s f2, f2, %[scaleFactor]\n"    // f2 *= scaleFactor
      "                                     mul.s f3, f3, %[scaleFactor]\n"    // f3 *= scaleFactor
      "                                     ssi f1, %[out], 0\n"               // out[0] = f1        
      "                                     ssi f2, %[out], 4\n"               // out[1] = f2        
      "                                     ssi f3, %[out], 8\n"               // out[2] = f3
      "                                     addi %[out], %[out], 12\n"         // out += FDMDV_OS_24
      "fdmdv_8_to_24_with_scaling_loop_end:\n"                                 // n--
                                                                               // }
      
      : [tmp0] "=r"(tmp0), [tmp1] "=r"(tmp1), [tmp2] "=r"(tmp2), [out] "=r"(out), [data] "=r"(data), [n] "=r"(n)
      : [filter0] "r"(fdmdv_os_filter24_short0), [filter1] "r"(fdmdv_os_filter24_short1), [filter2] "r"(fdmdv_os_filter24_short2), "4"(data), "3"(out), "5"(n), [scaleFactor] "f"(scaleFactor)
      : "a9", "f1", "f2", "f3", "memory"
    );

    /* update filter memory */
    memmove(&in8k[-FDMDV_OS_TAPS_24_8K], &in8k[n - FDMDV_OS_TAPS_24_8K], sizeof(short) * FDMDV_OS_TAPS_24_8K);

    //for(int i=-FDMDV_OS_TAPS_24_8K; i<0; i++)
	  //  in8k[i] = in8k[i + n];

    // quell warning
    (void)out24k[0];
}

/*---------------------------------------------------------------------------*\
                                                       
  FUNCTION....: fdmdv_24_to_8()	     
  AUTHOR......: Mooneer Salem			      
  DATE CREATED: 16 Feb 2023
  Changes the sample rate of a signal from 24 to 8 kHz (and from float to short)
 
  n is the number of samples at the 8 kHz rate, there are FDMDV_OS_24*n
  samples at the 24 kHz rate.  As above however a memory of
  FDMDV_OS_TAPS_24 samples is reqd for in24k[] (see t24_8.c unit test as example).
\*---------------------------------------------------------------------------*/

void fdmdv_24_to_8(short out8k[], short in24k[], int n)
{
    int i;

    for(i=0; i<n; i++) {
      dsps_dotprod_s16(fdmdv_os_filter24_short, &in24k[-FDMDV_OS_TAPS_24K + i*FDMDV_OS_24], &out8k[i], FDMDV_OS_TAPS_24K, 0);
    }

    /* update filter memory */
    for(i=-FDMDV_OS_TAPS_24K; i<0; i++)
	    in24k[i] = in24k[i + n*FDMDV_OS_24];
}