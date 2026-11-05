#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>

// The layer effects, in pure C++ (no JUCE), one instance per layer.
namespace Fx
{
    // Stereo delay line with linear interpolation, shared by the delay-based effects
    class DelayLine
    {
    public:
        void prepare (double sampleRate, double maxSeconds)
        {
            size = std::max (16, (int) (sampleRate * maxSeconds) + 4);
            for (auto& b : buf) b.assign ((size_t) size, 0.0f);
            pos = 0;
        }

        void reset()
        {
            for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
            pos = 0;
        }

        int maxDelay() const { return size - 3; }

        float read (int ch, float delaySamples) const
        {
            const float d = std::clamp (delaySamples, 0.0f, (float) (size - 3));
            float r = (float) pos - d;
            if (r < 0.0f) r += (float) size;
            const int i0 = std::min ((int) r, size - 1);
            const int i1 = (i0 + 1) % size;
            const float f = r - (float) i0;
            return buf[ch][(size_t) i0] + (buf[ch][(size_t) i1] - buf[ch][(size_t) i0]) * f;
        }

        void write (float l, float r)
        {
            buf[0][(size_t) pos] = l;
            buf[1][(size_t) pos] = r;
            pos = (pos + 1) % size;
        }

    private:
        std::vector<float> buf[2];
        int size = 16, pos = 0;
    };

    // Pitch shifter: two delay heads sweeping in opposite phase, crossfaded
    class PitchShifter
    {
    public:
        void prepare (double sampleRate) { line.prepare (sampleRate, 0.2); phase = 0.0f; }
        void reset() { line.reset(); phase = 0.0f; }

        // ratio 2 = one octave up. feedback 0..0.7
        void process (float& l, float& r, float ratio, float windowSamples, float feedback)
        {
            phase += (1.0f - ratio) / windowSamples;
            phase -= std::floor (phase);
            const float other = phase + 0.5f - std::floor (phase + 0.5f);
            const float g1 = fade (phase), g2 = fade (other);

            const float outL = line.read (0, 1.0f + phase * windowSamples) * g1 + line.read (0, 1.0f + other * windowSamples) * g2;
            const float outR = line.read (1, 1.0f + phase * windowSamples) * g1 + line.read (1, 1.0f + other * windowSamples) * g2;
            line.write (l + feedback * std::tanh (outL), r + feedback * std::tanh (outR));
            l = outL;
            r = outR;
        }

    private:
        static float fade (float p) { const float s = std::sin (3.14159265f * p); return s * s; }

        DelayLine line;
        float phase = 0.0f;
    };

    // Saturation stages. amountDb is the push into the shaper, tone 0..1 is dark to bright.
    // Drive: smooth tube-like, bias adds even harmonics.   Distortion: mode 0 hard clip, 1 wavefold, 2 rectify
    class Shaper
    {
    public:
        void prepare (double sampleRate) { sr = (float) sampleRate; reset(); }
        void reset() { for (int c = 0; c < 2; ++c) lp[c] = dcIn[c] = dcOut[c] = 0.0f; }

        float drive (int ch, float x, float amountDb, float tone, float bias)
        {
            const float g = std::pow (10.0f, amountDb / 20.0f);
            const float b = bias * 0.5f;
            const float y = (std::tanh (g * x + b) - std::tanh (b)) / std::pow (g, 0.35f);
            return finish (ch, y, tone);
        }

        float distort (int ch, float x, float amountDb, float tone, int mode)
        {
            const float g = std::pow (10.0f, amountDb / 20.0f);
            const float in = g * x;
            float y;
            switch (mode)
            {
                case 1:  y = std::sin (in * 1.5707963f); break;
                case 2:  y = std::abs (std::tanh (in)) * 1.4f; break;
                default: y = std::clamp (in, -1.0f, 1.0f) / std::pow (g, 0.3f); break;
            }
            return finish (ch, y, tone);
        }

    private:
        float finish (int ch, float y, float tone)
        {
            // one-pole lowpass from about 800 Hz to fully open, then a DC blocker
            const float hz = 800.0f * std::pow (25.0f, tone);
            const float coef = 1.0f - std::exp (-6.2831853f * hz / sr);
            lp[ch] += coef * (y - lp[ch]);
            const float out = lp[ch] - dcIn[ch] + 0.995f * dcOut[ch];
            dcIn[ch] = lp[ch];
            dcOut[ch] = out;
            return out;
        }

        float sr = 44100.0f;
        float lp[2] { 0.0f, 0.0f }, dcIn[2] { 0.0f, 0.0f }, dcOut[2] { 0.0f, 0.0f };
    };

    // Noise that does not depend on the input. colour: 0 white, 1 pink, 2 brown. tone 0..1 is dark to bright
    class NoiseGen
    {
    public:
        void seed (std::uint32_t s) { rng = s | 1u; }
        void reset() { for (int c = 0; c < 2; ++c) { p0[c] = p1[c] = p2[c] = brown[c] = lp[c] = 0.0f; } }

        float next (int ch, int colour, float tone)
        {
            float n = white();
            if (colour == 1)
            {
                p0[ch] = 0.99765f * p0[ch] + n * 0.0990460f;
                p1[ch] = 0.96300f * p1[ch] + n * 0.2965164f;
                p2[ch] = 0.57000f * p2[ch] + n * 1.0526913f;
                n = (p0[ch] + p1[ch] + p2[ch] + n * 0.1848f) * 0.25f;
            }
            else if (colour == 2)
            {
                brown[ch] = 0.985f * brown[ch] + n * 0.05f;
                n = brown[ch] * 2.5f;
            }
            const float coef = std::clamp (0.01f + 0.99f * tone * tone, 0.002f, 1.0f);
            lp[ch] += coef * (n - lp[ch]);
            return lp[ch];
        }

    private:
        float white()
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (float) (rng & 0xffffff) / 8388608.0f - 1.0f;
        }

        std::uint32_t rng = 0x2545f491u;
        float p0[2] { 0.0f, 0.0f }, p1[2] { 0.0f, 0.0f }, p2[2] { 0.0f, 0.0f }, brown[2] { 0.0f, 0.0f }, lp[2] { 0.0f, 0.0f };
    };

    // Stereo width with an optional Haas delay on the right. width: 0 mono, 0.5 unchanged, 1 double
    class Widener
    {
    public:
        void prepare (double sampleRate) { sr = (float) sampleRate; line.prepare (sampleRate, 0.03); }
        void reset() { line.reset(); }

        void process (float& l, float& r, float width, float haas)
        {
            line.write (l, r);
            const float delayedR = haas > 0.001f ? line.read (1, 1.0f + haas * 0.025f * sr) : r;
            const float mid = 0.5f * (l + delayedR);
            const float side = 0.5f * (l - delayedR) * width * 2.0f;
            l = mid + side;
            r = mid - side;
        }

    private:
        DelayLine line;
        float sr = 44100.0f;
    };

    // Ping-pong echo. Returns the echo only, the caller blends it in.
    class Echo
    {
    public:
        void prepare (double sampleRate) { line.prepare (sampleRate, 4.0); }
        void reset() { line.reset(); }

        void process (float l, float r, float delaySamples, float feedback, float& wetL, float& wetR)
        {
            wetL = line.read (0, delaySamples);
            wetR = line.read (1, delaySamples);
            line.write (l + feedback * std::tanh (wetR), r + feedback * std::tanh (wetL));
        }

    private:
        DelayLine line;
    };

    // Two swept voices with opposite phase on left and right. Returns the wet signal only.
    class Chorus
    {
    public:
        void prepare (double sampleRate) { sr = (float) sampleRate; line.prepare (sampleRate, 0.05); phase = 0.0f; }
        void reset() { line.reset(); phase = 0.0f; }

        void process (float l, float r, float rateHz, float depth, float& wetL, float& wetR)
        {
            phase += rateHz / sr;
            phase -= std::floor (phase);
            const float s = std::sin (6.2831853f * phase);
            const float base = 0.015f * sr, swing = (0.001f + 0.010f * depth) * sr;
            wetL = line.read (0, base + swing * s);
            wetR = line.read (1, base - swing * s);
            line.write (l, r);
        }

    private:
        DelayLine line;
        float sr = 44100.0f, phase = 0.0f;
    };

    // Stereo state-variable filter. mode: 0 lowpass, 1 highpass, 2 bandpass, 3 notch
    class Svf
    {
    public:
        void reset() { for (int c = 0; c < 2; ++c) ic1[c] = ic2[c] = 0.0f; }

        void set (float cutoffHz, float q, double sampleRate)
        {
            const float fc = std::clamp (cutoffHz, 10.0f, (float) sampleRate * 0.45f);
            const float g = std::tan (3.14159265f * fc / (float) sampleRate);
            k = 1.0f / std::max (0.1f, q);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        float process (int ch, float in, int mode)
        {
            const float v3 = in - ic2[ch];
            const float v1 = a1 * ic1[ch] + a2 * v3;
            const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.0f * v1 - ic1[ch];
            ic2[ch] = 2.0f * v2 - ic2[ch];

            switch (mode)
            {
                case 1:  return in - k * v1 - v2;
                case 2:  return k * v1;
                case 3:  return in - k * v1;
                default: return v2;
            }
        }

    private:
        float ic1[2] { 0.0f, 0.0f }, ic2[2] { 0.0f, 0.0f };
        float k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    };

    // Sample-rate and bit reduction with soft drive
    class Crusher
    {
    public:
        void reset() { phase = 1.0e9f; held[0] = held[1] = 0.0f; }

        // holdSamples >= 1 (1 = no rate reduction), bits 2..16, driveGain >= 1
        void process (float& l, float& r, float holdSamples, float bits, float driveGain)
        {
            phase += 1.0f;
            if (phase >= holdSamples)
            {
                phase = std::fmod (phase, holdSamples);
                const float levels = std::pow (2.0f, bits - 1.0f);
                held[0] = std::round (shape (l, driveGain) * levels) / levels;
                held[1] = std::round (shape (r, driveGain) * levels) / levels;
            }
            l = held[0];
            r = held[1];
        }

    private:
        static float shape (float x, float drive)
        {
            return drive <= 1.001f ? x : std::tanh (x * drive) / std::tanh (drive);
        }

        float phase = 1.0e9f;
        float held[2] { 0.0f, 0.0f };
    };

    // Adds grain that follows the input level, so silence stays silent.
    // flavour: 0 noise, 1 dust (sparse crackle), 2 tape (soft hiss and gentle saturation)
    class Texture
    {
    public:
        void reset()
        {
            for (int c = 0; c < 2; ++c) { env[c] = 0.0f; lp[c] = 0.0f; dust[c] = 0.0f; }
        }

        void prepare (double sampleRate)
        {
            sr = (float) sampleRate;
            attack  = 1.0f - std::exp (-1.0f / (0.002f * sr));
            release = 1.0f - std::exp (-1.0f / (0.120f * sr));
            dustDecay = std::exp (-1.0f / (0.0012f * sr));
            dustChance = 7.0f / sr;
        }

        // amount 0..1, tone 0..1 (dark to bright)
        float process (int ch, float x, int flavour, float amount, float tone)
        {
            const float level = std::abs (x);
            env[ch] += (level > env[ch] ? attack : release) * (level - env[ch]);

            float n = noise();
            if (flavour == 1)
            {
                if (unit() < dustChance) dust[ch] = (unit() < 0.5f ? 1.0f : -1.0f) * (0.4f + 0.6f * unit());
                dust[ch] *= dustDecay;
                n = dust[ch] * 2.5f;
            }

            const float maxTone = flavour == 2 ? 0.25f : 1.0f;
            const float coef = std::clamp ((0.02f + 0.98f * tone * tone) * maxTone, 0.002f, 1.0f);
            lp[ch] += coef * (n - lp[ch]);

            float out = x + lp[ch] * amount * env[ch] * 1.4f;
            if (flavour == 2)
                out = out + amount * (std::tanh (out * 1.8f) / 1.8f * 1.2f - out);
            return out;
        }

    private:
        float unit()
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (float) (rng & 0xffffff) / 16777216.0f;
        }
        float noise() { return 2.0f * unit() - 1.0f; }

        std::uint32_t rng = 0x9e3779b9u;
        float sr = 44100.0f, attack = 0.1f, release = 0.001f, dustDecay = 0.98f, dustChance = 0.0002f;
        float env[2] { 0.0f, 0.0f }, lp[2] { 0.0f, 0.0f }, dust[2] { 0.0f, 0.0f };
    };
}
