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

#include "esp_dsp.h"
#include "SampleRateConverter.h"

#if 0
/* Generate using fir1(47,1/3) in Octave */

static const float fdmdv_os_filter24[] = {
   -6.08277115e-04,
   -1.18775878e-03,
   -6.30730978e-04,
   1.00197563e-03,
   2.35217510e-03,
   1.38761691e-03,
   -2.18847944e-03,
   -5.15331946e-03,
   -3.00555360e-03,
   4.43137032e-03,
   1.02244611e-02,
   5.87615018e-03,
   -8.16402608e-03,
   -1.87112783e-02,
   -1.07718720e-02,
   1.43777057e-02,
   3.34751421e-02,
   1.98187775e-02,
   -2.62893404e-02,
   -6.51411771e-02,
   -4.25001567e-02,
   6.29864195e-02,
   2.10659949e-01,
   3.17760227e-01,
   3.17760227e-01,
   2.10659949e-01,
   6.29864195e-02,
   -4.25001567e-02,
   -6.51411771e-02,
   -2.62893404e-02,
   1.98187775e-02,
   3.34751421e-02,
   1.43777057e-02,
   -1.07718720e-02,
   -1.87112783e-02,
   -8.16402608e-03,
   5.87615018e-03,
   1.02244611e-02,
   4.43137032e-03,
   -3.00555360e-03,
   -5.15331946e-03,
   -2.18847944e-03,
   1.38761691e-03,
   2.35217510e-03,
   1.00197563e-03,
   -6.30730978e-04,
   -1.18775878e-03,
   -6.08277115e-04,
};
#endif // 0

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

void fdmdv_8_to_24(float out24k[], short in8k[], int n)
{
    int i,j,k,l;

    for(i=0; i<n; i++) {
      short tmp0 = 0;
      short tmp1 = 0;
      short tmp2 = 0;
      dsps_dotprod_s16(fdmdv_os_filter24_short0, &in8k[i - FDMDV_OS_TAPS_24_8K], &tmp0, FDMDV_OS_TAPS_24_8K, 0);
      dsps_dotprod_s16(fdmdv_os_filter24_short1, &in8k[i - FDMDV_OS_TAPS_24_8K], &tmp1, FDMDV_OS_TAPS_24_8K, 0);
      dsps_dotprod_s16(fdmdv_os_filter24_short2, &in8k[i - FDMDV_OS_TAPS_24_8K], &tmp2, FDMDV_OS_TAPS_24_8K, 0);

      out24k[i*FDMDV_OS_24] = tmp0 * FDMDV_SHORT_TO_FLOAT * FDMDV_OS_24;
      out24k[i*FDMDV_OS_24 + 1] = tmp1 * FDMDV_SHORT_TO_FLOAT * FDMDV_OS_24;
      out24k[i*FDMDV_OS_24 + 2] = tmp2 * FDMDV_SHORT_TO_FLOAT * FDMDV_OS_24;
	    /*for(j=0; j<FDMDV_OS_24; j++) {
	        out24k[i*FDMDV_OS_24+j] = 0.0;
	        for(k=0,l=0; k<FDMDV_OS_TAPS_24K; k+=FDMDV_OS_24,l++)
		        out24k[i*FDMDV_OS_24+j] += fdmdv_os_filter24[k+j]*in8k[i-l];
	        out24k[i*FDMDV_OS_24+j] *= FDMDV_OS_24 * FDMDV_SHORT_TO_FLOAT;
        }*/
    }	

    /* update filter memory */

    for(i=-FDMDV_OS_TAPS_24_8K; i<0; i++)
	    in8k[i] = in8k[i + n];
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
    int i,j;

    for(i=0; i<n; i++) {
	    //out8k[i] = 0;
      dsps_dotprod_s16(fdmdv_os_filter24_short, &in24k[-FDMDV_OS_TAPS_24K + i*FDMDV_OS_24], &out8k[i], FDMDV_OS_TAPS_24K, 0);
	    //for(j=0; j<FDMDV_OS_TAPS_24K; j++)
	    //    out8k[i] += fdmdv_os_filter24[j]*in24k[i*FDMDV_OS_24-j];
    }

    /* update filter memory */

    for(i=-FDMDV_OS_TAPS_24K; i<0; i++)
	    in24k[i] = in24k[i + n*FDMDV_OS_24];
}