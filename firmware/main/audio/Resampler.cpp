/*
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2026 Mooneer Salem
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

#include <cassert>
#include <cmath>
#include <cstring>
#include <numeric>

#include "esp_dsp.h"
#include "esp_heap_caps.h"

#include "Resampler.h"

#define KAISER_BETA (8.0f) /* about 80 dB stopband */

namespace ezdv
{

namespace audio
{

namespace
{

// The zeroth order modified Bessel function of the first kind.
float BesselI0_(float x)
{
    float sum = 1, term = 1;
    for (int k = 1; k < 32; k++)
    {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
    }
    return sum;
}

}

Resampler::Resampler(int inRate, int outRate, float cutoffHz, int numTaps)
    : pos_(0)
    , phase_(0)
{
    int g = std::gcd(inRate, outRate);
    up_ = outRate / g;
    down_ = inRate / g;
    tapsPerPhase_ = (numTaps + up_ - 1) / up_;
    int n = tapsPerPhase_ * up_;

    // Both small and used every few ms: internal RAM.
    taps_ = (float*)heap_caps_malloc(n * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    history_ = (float*)heap_caps_calloc(2 * tapsPerPhase_, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    assert(taps_ != nullptr && history_ != nullptr);

    // The lowpass at the interpolated rate, split into its phases: phase p
    // has taps p, p + up_, p + 2 * up_...
    float fc = cutoffHz / (inRate * up_);
    float sum = 0;
    float* h = (float*)heap_caps_malloc(n * sizeof(float), MALLOC_CAP_SPIRAM);
    assert(h != nullptr);
    for (int i = 0; i < n; i++)
    {
        float t = i - (n - 1) / 2.0f;
        float sinc = t == 0 ? 2 * fc : sinf(2 * M_PI * fc * t) / (M_PI * t);
        float r = 2.0f * i / (n - 1) - 1;
        h[i] = sinc * BesselI0_(KAISER_BETA * sqrtf(fmaxf(0, 1 - r * r))) / BesselI0_(KAISER_BETA);
        sum += h[i];
    }
    for (int p = 0; p < up_; p++)
    {
        for (int j = 0; j < tapsPerPhase_; j++)
        {
            taps_[p * tapsPerPhase_ + j] = h[p + j * up_] * up_ / sum;
        }
    }
    heap_caps_free(h);
}

Resampler::~Resampler()
{
    heap_caps_free(taps_);
    heap_caps_free(history_);
}

int Resampler::process(const short* in, int numIn, short* out)
{
    int numOut = 0;
    for (int i = 0; i < numIn; i++)
    {
        pos_ = (pos_ == 0 ? tapsPerPhase_ : pos_) - 1;
        history_[pos_] = history_[pos_ + tapsPerPhase_] = in[i];

        // Every output between this input and the next.
        for (; phase_ < up_; phase_ += down_)
        {
            float y;
            dsps_dotprod_f32(&taps_[phase_ * tapsPerPhase_], &history_[pos_], &y, tapsPerPhase_);
            out[numOut++] = (short)lrintf(fminf(32767.0f, fmaxf(-32767.0f, y)));
        }
        phase_ -= up_;
    }
    return numOut;
}

}

}
