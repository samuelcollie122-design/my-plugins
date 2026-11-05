#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// A feedback optical compressor, in pure C++ (no JUCE).
//
// The light cell is what gives this style of compressor its sound. It reacts in about 10 ms, then lets go in two
// stages: part of the reduction recovers within about 70 ms, the rest over seconds. How long that second stage is
// depends on how long the cell has been lit, so a short hit recovers quickly and a long loud passage recovers slowly.
//
// The detector listens to the compressor's own output (feedback), which gives the gentle, soft-kneed curve.
namespace Opto
{
    class LightCell
    {
    public:
        void prepare (double sampleRate)
        {
            const auto coef = [sampleRate] (double seconds) { return (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate))); };
            attack      = coef (0.010);
            fastRelease = coef (0.070);
            charge      = coef (1.2);
            slowRelease = coef (2.5);
            reset();
        }

        void reset() { fast = slow = 0.0f; }

        float process (float light)
        {
            fast += (light > fast ? attack : fastRelease) * (light - fast);
            slow += (fast > slow ? charge : slowRelease) * (fast - slow);
            return fastShare * fast + (1.0f - fastShare) * slow;
        }

    private:
        static constexpr float fastShare = 0.55f;
        float attack = 0.1f, fastRelease = 0.01f, charge = 0.0001f, slowRelease = 0.0001f;
        float fast = 0.0f, slow = 0.0f;
    };

    class Compressor
    {
    public:
        enum Analog { analogOff = 0, analog50Hz, analog60Hz };

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            cell.prepare (sampleRate);
            emphasisCoef = (float) (1.0 - std::exp (-6.283185307 * 1500.0 / sampleRate));
            dcCoef = (float) std::exp (-6.283185307 * 10.0 / sampleRate);
            makeupCoef = (float) (1.0 - std::exp (-1.0 / sampleRate));     // about one second
            reset();
        }

        void reset()
        {
            cell.reset();
            for (int c = 0; c < 2; ++c) { lastOut[c] = 0.0f; emphasisLp[c] = 0.0f; dcIn[c] = 0.0f; dcOut[c] = 0.0f; }
            gain = 1.0f;
            makeupDb = 0.0f;
            humPhase = 0.0;
        }

        // peakReduction 0..1, limit false = compress, hiFreq 0..1 (detector hears more highs).
        // analog: tube-style harmonics plus a very quiet mains hum and hiss. autoMakeup follows the reduction slowly.
        void process (float& l, float& r, float peakReduction, bool limit, float hiFreq, int analog, bool autoMakeup)
        {
            float detector = 0.0f;
            for (int c = 0; c < 2; ++c)
            {
                emphasisLp[c] += emphasisCoef * (lastOut[c] - emphasisLp[c]);
                const float highs = lastOut[c] - emphasisLp[c];
                detector = std::max (detector, std::abs (lastOut[c] + 3.0f * hiFreq * highs));     // stereo linked
            }

            const float sensitivity = 30.0f * std::pow (peakReduction, 2.5f) * (limit ? 1.5f : 1.0f);
            const float e = cell.process (detector * sensitivity);

            // With feedback, power 3 settles towards 4:1 and power 5 towards 6:1, both with a very soft knee.
            // Across normal levels that works out at roughly 3:1 for compress and firmer for limit.
            const float e3 = e * e * e;
            gain = 1.0f / (1.0f + (limit ? e3 * e * e : e3));

            l *= gain;
            r *= gain;
            lastOut[0] = l;
            lastOut[1] = r;

            if (autoMakeup)
            {
                makeupDb += makeupCoef * (-20.0f * std::log10 (gain) * 0.85f - makeupDb);
                const float g = std::pow (10.0f, makeupDb / 20.0f);
                l *= g;
                r *= g;
            }
            else
            {
                makeupDb = 0.0f;
            }

            if (analog != analogOff)
            {
                l = tube (0, l);
                r = tube (1, r);

                humPhase += (analog == analog50Hz ? 50.0 : 60.0) / sr;
                if (humPhase >= 1.0) humPhase -= 1.0;
                const float w = (float) (humPhase * 6.283185307);
                const float hum = (std::sin (w) + 0.5f * std::sin (3.0f * w) + 0.25f * std::sin (5.0f * w)) * 1.26e-4f;   // about -78 dB
                l += hum + hiss() * 4.0e-5f;                                                                              // about -88 dB
                r += hum + hiss() * 4.0e-5f;
            }
        }

        float getGain() const { return gain; }

    private:
        float tube (int c, float x)
        {
            // Slightly lopsided saturation adds even harmonics. The DC blocker removes the offset this creates.
            constexpr float drive = 1.35f, bias = 0.05f;
            const float y = (std::tanh (x * drive + bias) - std::tanh (bias)) / drive;
            const float out = y - dcIn[c] + dcCoef * dcOut[c];
            dcIn[c] = y;
            dcOut[c] = out;
            return out;
        }

        float hiss()
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (float) (rng & 0xffffff) / 8388608.0f - 1.0f;
        }

        LightCell cell;
        double sr = 44100.0, humPhase = 0.0;
        std::uint32_t rng = 0x2545f491u;
        float lastOut[2] { 0.0f, 0.0f }, emphasisLp[2] { 0.0f, 0.0f }, dcIn[2] { 0.0f, 0.0f }, dcOut[2] { 0.0f, 0.0f };
        float emphasisCoef = 0.1f, dcCoef = 0.999f, makeupCoef = 0.0001f, makeupDb = 0.0f, gain = 1.0f;
    };
}
