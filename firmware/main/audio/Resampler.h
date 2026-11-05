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

#ifndef RESAMPLER_H
#define RESAMPLER_H

namespace ezdv
{

namespace audio
{

/**
 * Sample rate conversion by a fixed ratio (e.g. 8 to 16 kHz, or 24 to
 * 16 kHz): a polyphase lowpass filter (Kaiser-windowed sinc).
 *
 * ezDV's user audio (headset, or SmartSDR's) is 16 kHz and the radio's is
 * 8 kHz; this goes between the two, and between them and what the audio
 * hardware or radio runs at.
 */
class Resampler
{
public:
    /**
     * cutoffHz should be a little under half the lower of the two rates;
     * more taps make the transition from passband to stopband sharper.
     */
    Resampler(int inRate, int outRate, float cutoffHz, int numTaps);
    ~Resampler();

    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;

    /** The most samples process() can output for numIn samples in. */
    int maxOutput(int numIn) const { return (numIn * up_ + down_ - 1) / down_; }

    /** Resamples numIn samples to out; returns how many were output. */
    int process(const short* in, int numIn, short* out);

private:
    int up_;            // interpolation (L)
    int down_;          // decimation (M)
    int tapsPerPhase_;
    float* taps_;       // up_ phases of tapsPerPhase_ taps each, with gain up_
    float* history_;    // newest first, written twice so the last tapsPerPhase_ are always contiguous
    int pos_;
    int phase_;         // of the next output, at the interpolated rate, relative to the newest input
};

}

}

#endif // RESAMPLER_H
