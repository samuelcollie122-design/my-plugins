// Headless check of SpaceTrack's stereo and 3D sound. Not part of the plugins.
// Build with -DSPACETRACK_TESTS=ON. It has its own private shared memory, so it cannot touch a running FL Studio.
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
        if (! ok) ++failures;
    }

    void setPosition (SpaceTrackProcessor& p, float x, float y, float z, bool mode3d)
    {
        const auto set = [&p] (const char* id, float v)
        {
            auto* q = p.apvts.getParameter (id);
            q->setValueNotifyingHost (q->convertTo0to1 (v));
        };
        set ("x", x);
        set ("y", y);
        set ("z", z);
        set ("mode3d", mode3d ? 1.0f : 0.0f);
    }

    struct Bands { double all[2] {}, low[2] {}, mid[2] {}, high[2] {}, top[2] {}; };

    // Average power per band of white noise through the plugin, in dB
    Bands measureNoise (SpaceTrackProcessor& p, double sr, bool* finite = nullptr)
    {
        constexpr int order = 12, size = 1 << order;
        p.prepareToPlay (sr, 512);

        juce::Random rng (1234);
        juce::dsp::FFT fft (order);
        std::vector<float> fifo[2] { std::vector<float> (size * 2), std::vector<float> (size * 2) };
        std::vector<double> power[2] { std::vector<double> (size / 2 + 1), std::vector<double> (size / 2 + 1) };
        juce::AudioBuffer<float> buf (2, size);
        juce::MidiBuffer midi;

        for (int block = 0; block < 40; ++block)
        {
            for (int i = 0; i < size; ++i)
            {
                const float s = 0.25f * (rng.nextFloat() * 2.0f - 1.0f);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            p.processBlock (buf, midi);
            if (block < 4) continue;

            for (int c = 0; c < 2; ++c)
            {
                std::fill (fifo[c].begin(), fifo[c].end(), 0.0f);
                for (int i = 0; i < size; ++i)
                {
                    const float v = buf.getSample (c, i);
                    if (finite != nullptr && ! std::isfinite (v)) *finite = false;
                    const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);
                    fifo[c][(size_t) i] = v * win;
                }
                fft.performFrequencyOnlyForwardTransform (fifo[c].data());
                for (int k = 0; k <= size / 2; ++k)
                    power[c][(size_t) k] += (double) fifo[c][(size_t) k] * (double) fifo[c][(size_t) k];
            }
        }

        const auto bandDb = [&] (int c, double lo, double hi)
        {
            double sum = 0.0;
            int n = 0;
            for (int k = 0; k <= size / 2; ++k)
            {
                const double f = (double) k * sr / size;
                if (f >= lo && f < hi) { sum += power[c][(size_t) k]; ++n; }
            }
            return 10.0 * std::log10 (sum / juce::jmax (1, n) + 1.0e-12);
        };

        Bands b;
        for (int c = 0; c < 2; ++c)
        {
            b.all[c]  = bandDb (c, 200.0, 8000.0);
            b.low[c]  = bandDb (c, 200.0, 1000.0);
            b.mid[c]  = bandDb (c, 2000.0, 5000.0);
            b.high[c] = bandDb (c, 6000.0, 10000.0);
            b.top[c]  = bandDb (c, 10000.0, 16000.0);
        }
        return b;
    }

    // Sample at which each ear first responds to a click
    void measureOnset (SpaceTrackProcessor& p, double sr, int& onsetL, int& onsetR)
    {
        p.prepareToPlay (sr, 4096);
        juce::AudioBuffer<float> buf (2, 4096);
        buf.clear();
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        buf.clear();
        buf.setSample (0, 100, 1.0f);
        buf.setSample (1, 100, 1.0f);
        p.processBlock (buf, midi);

        const auto first = [&buf] (int c)
        {
            const float limit = 0.2f * buf.getMagnitude (c, 0, buf.getNumSamples());
            for (int i = 0; i < buf.getNumSamples(); ++i)
                if (std::abs (buf.getSample (c, i)) >= limit) return i;
            return -1;
        };
        onsetL = first (0);
        onsetR = first (1);
    }

    // What remains after the input stops
    double tailLevel (SpaceTrackProcessor& p, double sr)
    {
        p.prepareToPlay (sr, 2048);
        juce::AudioBuffer<float> buf (2, 2048);
        juce::MidiBuffer midi;
        juce::Random rng (7);
        for (int b = 0; b < 20; ++b)
        {
            for (int i = 0; i < 2048; ++i)
            {
                const float s = 0.25f * (rng.nextFloat() * 2.0f - 1.0f);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            p.processBlock (buf, midi);
        }
        buf.clear();
        p.processBlock (buf, midi);     // let the short head response finish
        buf.clear();
        p.processBlock (buf, midi);
        return (double) buf.getRMSLevel (0, 0, 2048);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const juce::String at = " at " + juce::String (sr / 1000.0, 1) + " kHz";
        SpaceTrackProcessor p;
        p.setRoom (0.4f, 0.0f);
        bool finite = true;

        // ---- left and right ----
        setPosition (p, 1.0f, 0.0f, 0.0f, true);
        const auto right = measureNoise (p, sr, &finite);
        setPosition (p, -1.0f, 0.0f, 0.0f, true);
        const auto left = measureNoise (p, sr, &finite);
        check (right.high[1] - right.high[0] > 10.0, "right: right ear louder in the highs by " + juce::String (right.high[1] - right.high[0], 1) + " dB" + at);
        check (std::abs ((right.high[1] - right.high[0]) - (left.high[0] - left.high[1])) < 1.0, "left is the mirror of right" + at);

        int onsetL = 0, onsetR = 0;
        setPosition (p, 1.0f, 0.0f, 0.0f, true);
        measureOnset (p, sr, onsetL, onsetR);
        const double itdUs = 1.0e6 * (double) (onsetL - onsetR) / sr;
        check (itdUs > 500.0 && itdUs < 1100.0, "right: right ear hears it " + juce::String (itdUs, 0) + " us earlier" + at);

        // ---- front, back, up ----
        setPosition (p, 0.0f, 1.0f, 0.0f, true);
        const auto front = measureNoise (p, sr, &finite);
        setPosition (p, 0.0f, -1.0f, 0.0f, true);
        const auto back = measureNoise (p, sr, &finite);
        setPosition (p, 0.0f, 0.0f, 1.0f, true);
        const auto up = measureNoise (p, sr, &finite);
        setPosition (p, 0.0f, 0.0f, -1.0f, true);
        const auto down = measureNoise (p, sr, &finite);
        check (front.top[0] - back.top[0] > 4.0, "front brighter than back by " + juce::String (front.top[0] - back.top[0], 1) + " dB (10-16 kHz)" + at);
        check (up.high[0] - front.high[0] > 3.0, "up differs from front by " + juce::String (up.high[0] - front.high[0], 1) + " dB (6-10 kHz)" + at);
        check (up.top[0] - down.top[0] > 4.0, "up brighter than down by " + juce::String (up.top[0] - down.top[0], 1) + " dB (10-16 kHz)" + at);

        // ---- flat stereo stays what it was ----
        setPosition (p, 0.0f, 0.0f, 0.0f, false);
        const auto centre2D = measureNoise (p, sr, &finite);
        setPosition (p, 0.0f, 0.0f, 1.0f, false);
        const auto high2D = measureNoise (p, sr, &finite);
        check (std::abs (centre2D.top[0] - high2D.top[0]) < 0.01, "stereo mode ignores height" + at);

        const auto stereoState = centre2D.all[0];
        p.setRoom (0.9f, 1.0f);
        setPosition (p, 0.0f, 0.0f, 0.0f, false);
        check (std::abs (measureNoise (p, sr).all[0] - stereoState) < 0.01, "stereo mode ignores the room" + at);
        check (tailLevel (p, sr) < 1.0e-6, "stereo mode leaves no room tail" + at);
        p.setRoom (0.4f, 0.0f);

        // ---- level ----
        setPosition (p, 0.0f, 0.0f, 0.0f, true);
        const auto centre3D = measureNoise (p, sr, &finite);
        check (std::abs (centre3D.all[0] - centre2D.all[0]) < 1.0, "centre: 3D and stereo match within 1 dB (" + juce::String (centre3D.all[0] - centre2D.all[0], 2) + ")" + at);
        const auto frontAt03 = [&]
        {
            setPosition (p, 0.0f, 0.4f, 0.0f, true);
            return measureNoise (p, sr, &finite);
        }();
        check (std::abs (frontAt03.all[0] - centre2D.all[0]) < 4.0, "front: 3D stays near the stereo level (" + juce::String (frontAt03.all[0] - centre2D.all[0], 2) + " dB)" + at);

        // ---- room ----
        setPosition (p, 0.5f, 0.5f, 0.2f, true);
        const double dryTail = tailLevel (p, sr);
        p.setRoom (0.6f, 0.5f);
        const double roomTail = tailLevel (p, sr);
        p.setRoom (0.6f, 1.0f);
        const double moreRoomTail = tailLevel (p, sr);
        check (dryTail < 1.0e-5, "room amount 0: no tail (" + juce::String (dryTail, 7) + ")" + at);
        check (roomTail > 1.0e-3, "room on: audible tail (" + juce::String (roomTail, 4) + ")" + at);
        check (moreRoomTail > roomTail * 1.3, "more room amount gives more tail" + at);
        check (finite, "every output sample is a real number" + at);

        // ---- moving a sound stays clean ----
        {
            p.setRoom (0.5f, 0.5f);
            setPosition (p, -1.0f, 0.0f, 0.0f, true);
            p.prepareToPlay (sr, 256);
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            double phase = 0.0, maxStep = 0.0, maxPeak = 0.0;
            float prev[2] {};
            const int blocks = (int) (4.0 * sr / 256.0);
            for (int b = 0; b < blocks; ++b)
            {
                const float t = (float) b / (float) blocks;
                const float angle = t * juce::MathConstants<float>::twoPi * 2.0f;
                setPosition (p, std::sin (angle), std::cos (angle), std::sin (angle * 0.5f), true);
                for (int i = 0; i < 256; ++i)
                {
                    phase += 220.0 / sr;
                    const float s = 0.5f * std::sin ((float) (phase * juce::MathConstants<double>::twoPi));
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                p.processBlock (buf, midi);
                if (b < 20)
                {
                    prev[0] = buf.getSample (0, 255);
                    prev[1] = buf.getSample (1, 255);
                    continue;
                }
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 256; ++i)
                    {
                        const float v = buf.getSample (c, i);
                        maxStep = juce::jmax (maxStep, (double) std::abs (v - prev[c]));
                        maxPeak = juce::jmax (maxPeak, (double) std::abs (v));
                        prev[c] = v;
                    }
            }
            check (maxStep < 0.08, "moving a sound has no clicks (largest step " + juce::String (maxStep, 3) + ")" + at);
            check (maxPeak < 2.0, "moving a sound stays in range (peak " + juce::String (maxPeak, 2) + ")" + at);
        }

        // ---- switching mode while running ----
        {
            setPosition (p, 0.3f, 0.3f, 0.0f, false);
            p.prepareToPlay (sr, 256);
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            double phase = 0.0, maxStep = 0.0;
            float prev = 0.0f;
            for (int b = 0; b < 200; ++b)
            {
                if (b == 50)  setPosition (p, 0.3f, 0.3f, 0.0f, true);
                if (b == 120) setPosition (p, 0.3f, 0.3f, 0.0f, false);
                for (int i = 0; i < 256; ++i)
                {
                    phase += 220.0 / sr;
                    const float s = 0.5f * std::sin ((float) (phase * juce::MathConstants<double>::twoPi));
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                p.processBlock (buf, midi);
                if (b < 5) { prev = buf.getSample (0, 255); continue; }
                for (int i = 0; i < 256; ++i)
                {
                    const float v = buf.getSample (0, i);
                    maxStep = juce::jmax (maxStep, (double) std::abs (v - prev));
                    prev = v;
                }
            }
            check (maxStep < 0.08, "switching stereo and 3D has no click (largest step " + juce::String (maxStep, 3) + ")" + at);
        }
    }

    // ---- CPU: one track in 3D with the room on, as a share of real time ----
    {
        SpaceTrackProcessor p;
        p.setRoom (0.5f, 0.5f);
        setPosition (p, 0.5f, 0.5f, 0.2f, true);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        juce::Random rng (3);
        const int blocks = 2000;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float s = 0.25f * (rng.nextFloat() * 2.0f - 1.0f);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            p.processBlock (buf, midi);
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - start;
        const double percent = 100.0 * ms / (1000.0 * blocks * 512.0 / 48000.0);
        check (percent < 5.0, "one 3D track uses " + juce::String (percent, 2) + "% of one CPU core");
    }

    // ---- saving ----
    {
        SpaceTrackProcessor a;
        a.setRoom (0.7f, 0.33f);
        juce::MemoryBlock state;
        a.getStateInformation (state);
        SpaceTrackProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        juce::MemoryBlock again;
        b.getStateInformation (again);
        check (again.toString().isNotEmpty() && (float) b.apvts.state.getProperty ("roomSize") > 0.69f
                   && (float) b.apvts.state.getProperty ("roomAmount") < 0.34f, "room settings are saved with each track");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
