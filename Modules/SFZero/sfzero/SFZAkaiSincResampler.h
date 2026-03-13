/*************************************************************************************
 * 8-point windowed-SINC resampler with anti-aliasing
 * Copyright (C) 2024-2026 discoDSP
 * MIT License — see LICENSE file in parent directory
 *
 * 8-tap Hann-windowed sinc with dynamic cutoff for anti-aliasing when pitching up.
 * Coefficients computed on-the-fly in double precision — no quantization artifacts.
 *
 * Tap centring:  taps[-3..+4] relative to integer sample position
 *                (i.e. 3 samples before, the sample itself, and 4 samples after)
 *************************************************************************************/
#ifndef SFZAKAISINCRESAMPLER_H_INCLUDED
#define SFZAKAISINCRESAMPLER_H_INCLUDED

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>

namespace sfzero
{

// Number of filter taps (fixed 8-point kernel: 4 lobes, taps -3..+4)
static constexpr int kAkaiNumTaps   = 8;
// Tap offset: tap 0 reads sample[pos - 3]
static constexpr int kAkaiTapOffset = 3;
// Keep for compatibility with padding code
static constexpr int kAkaiNumPhases = 256;

/*──────────────────────────────────────────────────────────────────────────────
 * akaiSincInterpolate()
 *
 * Performs 8-point windowed-sinc interpolation with anti-aliasing at the
 * given fractional sample position.
 *
 * Parameters:
 *   data       — pointer to sample data (channel buffer from AudioSampleBuffer)
 *   position   — fractional read position (e.g. 1234.567)
 *   bufferSize — total number of valid samples in the buffer (including padding)
 *   pitchRatio — playback speed ratio (>1 = pitching up, <1 = pitching down)
 *
 * The caller must ensure at least kAkaiTapOffset (3) samples exist before the
 * integer position and kAkaiNumTaps - kAkaiTapOffset - 1 (4) samples after.
 * This is handled by the sample loader padding.
 *──────────────────────────────────────────────────────────────────────────────*/
inline float akaiSincInterpolate(const float* data, double position, int bufferSize, double pitchRatio = 1.0)
{
    int intPos = static_cast<int>(position);
    double frac = position - static_cast<double>(intPos);

    // First tap reads from data[intPos - kAkaiTapOffset]
    int startIdx = intPos - kAkaiTapOffset;

    // Boundary protection
    if (startIdx < 0 || startIdx + kAkaiNumTaps > bufferSize)
        return data[intPos < bufferSize ? intPos : bufferSize - 1];

    // Anti-aliasing cutoff: when pitching up, reduce the sinc bandwidth
    // to prevent aliasing (same approach as HighLife/discoDSP)
    double phaseSpeedAbs = std::abs(pitchRatio);
    if (phaseSpeedAbs < 1.0e-25)
        phaseSpeedAbs = 1.0e-25;
    double cutoff = 1.0 / phaseSpeedAbs;
    if (cutoff > 1.0)
        cutoff = 1.0;

    // Avoid division by zero at exact integer positions
    if (frac == 0.0)
        frac = 1.0e-25;

    const float* s = &data[startIdx];

    // FIR convolution: 8-tap windowed sinc
    // taps map to s[0..7] = data[intPos-3 .. intPos+4]
    double mix = 0.0;
    static constexpr int kHalfTaps = kAkaiTapOffset;     // 3
    static constexpr int kHalfTapsP1 = kAkaiNumTaps - kAkaiTapOffset - 1; // 4

    for (int t = -kHalfTaps; t <= kHalfTapsP1; ++t)
    {
        double x = static_cast<double>(t) - frac;

        // Hann window over the 8-tap span: maps [-3.x .. +4.x] to [-pi..+pi]
        double window = 0.5 + 0.5 * std::cos(x / static_cast<double>(kAkaiTapOffset + 1) * M_PI);

        // Sinc with anti-aliasing cutoff
        double sinc = std::sin(cutoff * M_PI * x) / (M_PI * x);

        mix += static_cast<double>(s[t + kHalfTaps]) * sinc * window;
    }

    // Scale output by cutoff to maintain unity gain
    return static_cast<float>(mix * cutoff);
}

} // namespace sfzero

#endif // SFZAKAISINCRESAMPLER_H_INCLUDED
