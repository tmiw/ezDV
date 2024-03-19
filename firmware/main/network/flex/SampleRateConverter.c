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
#include <esp_dsp.h>
#include "SampleRateConverter.h"

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

    // Zero out output array.
    // Per IEEE754 this is equivalent to out24k[i] = 0.0.
    memset(out24k, 0, FDMDV_OS_24 * n * sizeof(float));
    
    for(i=0; i<n; i++) {
	    for(j=0; j<FDMDV_OS_24; j++) {
	        for(k=0,l=0; k<FDMDV_OS_TAPS_24K; k+=FDMDV_OS_24,l++)
		        out24k[i*FDMDV_OS_24+j] += fdmdv_os_filter24[k+j]*in8k[i-l];
        }
    }	

    // Scale output array appropriately so that values are between +/- 1.
    dsps_mulc_f32(out24k, out24k, n * FDMDV_OS_24, FDMDV_OS_24 * FDMDV_SHORT_TO_FLOAT, 1, 1);

    /* update filter memory */
    memmove(&in8k[-FDMDV_OS_TAPS_24_8K], &in8k[n-FDMDV_OS_TAPS_24_8K], sizeof(short) * FDMDV_OS_TAPS_24_8K);
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

void fdmdv_24_to_8(short out8k[], float in24k[], int n)
{
    int i,j;

    // Zero out output array.
    memset(out8k, 0, n * sizeof(short));

    // Scale input so that values are +/- 2^16
    dsps_mulc_f32(in24k, in24k, FDMDV_OS_24 * n, FDMDV_FLOAT_TO_SHORT, 1, 1);
    
    // Assumption: fdmdv_os_filter24 is symmetrical
    for(i=0; i<n; i++) {
      float temp = 0;
      dsps_dotprod_f32(fdmdv_os_filter24, &in24k[i*FDMDV_OS_24 - FDMDV_OS_TAPS_24K + 1], &temp, FDMDV_OS_TAPS_24K);
      out8k[i] = (short)temp;
    }

    /* update filter memory */
    memmove(&in24k[-FDMDV_OS_TAPS_24K], &in24k[n*FDMDV_OS_24-FDMDV_OS_TAPS_24K], sizeof(float) * FDMDV_OS_TAPS_24K);
}