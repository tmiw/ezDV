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

#ifndef SAMPLE_RATE_CONVERTER_H
#define SAMPLE_RATE_CONVERTER_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/* 8 to 24 kHz sample rate conversion */

#define FDMDV_OS_24             3                               /* oversampling rate                   */
#define FDMDV_OS_TAPS_24K       48                              /* number of OS filter taps at 24kHz   */
#define FDMDV_OS_TAPS_24_8K     (FDMDV_OS_TAPS_24K/FDMDV_OS_24) /* number of OS filter taps at 8kHz    */
#define FDMDV_FLOAT_TO_SHORT    ((float)32767.0)                /* Multiplication factor to convert float between -1.0 and 1.0 to short */
#define FDMDV_SHORT_TO_FLOAT    ((float)1.0 / FDMDV_FLOAT_TO_SHORT) /* Multiplication factor to convert short between -32768 and 32767 to float */

/* Special purpose 8->24K conversion functions for the Flex 6000/8000 series.
   The Flex uses floating point numbers while Codec2 uses shorts, so we
   also have code to convert between those formats. */
void           fdmdv_8_to_24_with_scaling(float out24k[], short in8k[], int n, float scaleFactor);
void           fdmdv_24_to_8(short out8k[], short in24k[], int n);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // SAMPLE_RATE_CONVERTER_H