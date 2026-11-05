// Headless check of OptoComp's sound and window. Not part of the plugin.
// Build with -DOPTOCOMP_TESTS=ON, run: OptoCompTest <folder for window pictures>
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
        if (! ok) ++failures;
    }

    void setParam (OptoProcessor& p, const juce::String& id, float v)
    {
        auto* q = p.apvts.getParameter (id);
        q->setValueNotifyingHost (q->convertTo0to1 (v));
    }

    float param (OptoProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

    float amplitude (float db) { return juce::Decibels::decibelsToGain (db); }

    // Runs a 1 kHz sine for some seconds and returns the output level of the last 100 ms, as peak dB.
    // Reuses the compressor's state unless fresh is true.
    double renderSine (OptoProcessor& p, double sr, float inputDb, double seconds, bool fresh = true, bool* finite = nullptr)
    {
        const int block = 256;
        if (fresh) p.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        double phase = 0.0, sum = 0.0;
        juce::int64 count = 0;
        const float amp = amplitude (inputDb);
        const int blocks = (int) (seconds * sr / block);
        const int measureFrom = blocks - juce::jmax (1, (int) (0.1 * sr / block));

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                phase += 1000.0 / sr;
                const float s = amp * std::sin ((float) (phase * juce::MathConstants<double>::twoPi));
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            p.processBlock (buf, midi);
            if (finite != nullptr)
                for (int i = 0; i < block; ++i)
                    if (! std::isfinite (buf.getSample (0, i)) || ! std::isfinite (buf.getSample (1, i))) *finite = false;
            if (b >= measureFrom)
                for (int i = 0; i < block; ++i)
                {
                    sum += (double) buf.getSample (0, i) * (double) buf.getSample (0, i);
                    ++count;
                }
        }
        const double rms = std::sqrt (sum / (double) juce::jmax ((juce::int64) 1, count));
        return 20.0 * std::log10 (rms * std::sqrt (2.0) + 1.0e-9);
    }

    // Reduction left a given time after a loud burst of a given length
    double reductionAfterBurst (OptoProcessor& p, double sr, double burstSeconds, double afterSeconds)
    {
        renderSine (p, sr, -6.0f, burstSeconds, true);
        renderSine (p, sr, -60.0f, afterSeconds, false);
        return (double) p.uiReductionDb.load();
    }

    // Fundamental and harmonic amplitudes of a -6 dB, 1 kHz sine over a whole number of cycles
    void measureHarmonics (OptoProcessor& p, double sr, double& fundamental, double& harmonics)
    {
        p.prepareToPlay (sr, 256);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        constexpr double f = 1000.0;
        const juce::int64 start = (juce::int64) (0.25 * sr);
        const juce::int64 length = (juce::int64) (250.0 * sr / f);       // exactly 250 cycles
        double re1 = 0, im1 = 0, re2 = 0, im2 = 0, re3 = 0, im3 = 0;

        for (juce::int64 t = 0; t < start + length; t += 256)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float s = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * f * (double) (t + i) / sr);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < 256; ++i)
            {
                const auto when = t + i;
                if (when < start || when >= start + length) continue;
                const double w = juce::MathConstants<double>::twoPi * f * (double) when / sr;
                const double v = buf.getSample (0, i);
                re1 += v * std::cos (w);       im1 += v * std::sin (w);
                re2 += v * std::cos (2.0 * w); im2 += v * std::sin (2.0 * w);
                re3 += v * std::cos (3.0 * w); im3 += v * std::sin (3.0 * w);
            }
        }
        fundamental = std::sqrt (re1 * re1 + im1 * im1);
        harmonics = std::sqrt (re2 * re2 + im2 * im2 + re3 * re3 + im3 * im3);
    }

    // Level with silent input, in dBFS
    double silenceLevel (OptoProcessor& p, double sr)
    {
        p.prepareToPlay (sr, 512);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        double peak = 0.0;
        for (int b = 0; b < (int) (1.0 * sr / 512.0); ++b)
        {
            buf.clear();
            p.processBlock (buf, midi);
            peak = juce::jmax (peak, (double) buf.getMagnitude (0, 512));
        }
        return 20.0 * std::log10 (peak + 1.0e-12);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const juce::String at = " at " + juce::String (sr / 1000.0, 1) + " kHz";
        OptoProcessor p;
        bool finite = true;

        // ---- how the knob acts ----
        setParam (p, "peak", 0.0f);
        const double none = renderSine (p, sr, -12.0f, 3.0, true, &finite);
        check (std::abs (none + 12.0) < 0.1, "peak reduction 0 does nothing (" + juce::String (none + 12.0, 2) + " dB)" + at);

        double last = -1.0;
        bool rising = true;
        juce::String list;
        for (float pr : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            setParam (p, "peak", pr);
            const double gr = -12.0 - renderSine (p, sr, -12.0f, 4.0, true, &finite);
            rising = rising && gr > last;
            last = gr;
            list << juce::String (gr, 1) << " ";
        }
        check (rising, "more peak reduction reduces more (" + list.trim() + " dB)" + at);

        // ---- the curve ----
        setParam (p, "peak", 0.5f);
        const double outLow  = renderSine (p, sr, -24.0f, 5.0, true, &finite);
        const double outMid  = renderSine (p, sr, -12.0f, 5.0, true, &finite);
        const double outHigh = renderSine (p, sr, 0.0f, 5.0, true, &finite);
        const double ratio = 12.0 / (outHigh - outMid);
        check (ratio > 2.3 && ratio < 4.5, "compress ratio " + juce::String (ratio, 1) + ":1 at high level" + at);
        check (-24.0 - outLow < -12.0 - outMid, "soft knee: quieter input is reduced less (" + juce::String (-24.0 - outLow, 1) + " vs " + juce::String (-12.0 - outMid, 1) + " dB)" + at);

        setParam (p, "limit", 1.0f);
        const double limitMid  = renderSine (p, sr, -12.0f, 5.0, true, &finite);
        const double limitHigh = renderSine (p, sr, 0.0f, 5.0, true, &finite);
        const double limitRatio = 12.0 / (limitHigh - limitMid);
        check (limitRatio > ratio, "limit is firmer than compress (" + juce::String (limitRatio, 1) + ":1)" + at);
        setParam (p, "limit", 0.0f);

        // ---- the two-stage release that defines this style ----
        setParam (p, "peak", 0.6f);
        const double justAfterShort = reductionAfterBurst (p, sr, 0.25, 0.01);
        const double shortBurst = reductionAfterBurst (p, sr, 0.25, 0.5);
        const double longBurst  = reductionAfterBurst (p, sr, 6.0, 0.5);
        check (longBurst > shortBurst * 1.5, "long passages recover slower: " + juce::String (longBurst, 2) + " vs " + juce::String (shortBurst, 2) + " dB after 0.5 s" + at);
        check (shortBurst < justAfterShort * 0.5, "short hits recover quickly: " + juce::String (justAfterShort, 2) + " to " + juce::String (shortBurst, 2) + " dB in 0.5 s" + at);
        const double after10 = reductionAfterBurst (p, sr, 6.0, 12.0);
        check (after10 < 0.3, "fully recovers in the end (" + juce::String (after10, 2) + " dB left after 12 s)" + at);

        // ---- attack ----
        {
            p.prepareToPlay (sr, 64);
            juce::AudioBuffer<float> buf (2, 64);
            juce::MidiBuffer midi;
            double phase = 0.0;
            const int attackBlocks = (int) (0.005 * sr / 64.0);
            for (int b = 0; b < attackBlocks; ++b)
            {
                for (int i = 0; i < 64; ++i)
                {
                    phase += 1000.0 / sr;
                    const float s = 0.5f * std::sin ((float) (phase * juce::MathConstants<double>::twoPi));
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                p.processBlock (buf, midi);
            }
            const double at5ms = p.uiReductionDb.load();
            const double settled = -6.0 - renderSine (p, sr, -6.0f, 0.1, false);
            check (at5ms < settled, "lets the first transient through (" + juce::String (at5ms, 1) + " dB at 5 ms, " + juce::String (settled, 1) + " dB at 100 ms)" + at);
        }

        // ---- gain, trim, mix ----
        setParam (p, "peak", 0.5f);
        const double unity = renderSine (p, sr, -12.0f, 3.0, true, &finite);
        setParam (p, "gain", 52.0f);
        const double withGain = renderSine (p, sr, -12.0f, 3.0, true, &finite);
        setParam (p, "gain", 40.0f);
        check (std::abs (withGain - unity - 6.0) < 0.1, "gain 52 adds 6 dB, gain 40 adds nothing" + at);

        setParam (p, "trim", -6.0f);
        const double trimmed = renderSine (p, sr, -12.0f, 3.0, true, &finite);
        setParam (p, "trim", 0.0f);
        check (std::abs (trimmed - unity + 6.0) < 0.1, "trim lowers by its amount" + at);

        setParam (p, "mix", 0.0f);
        const double dry = renderSine (p, sr, -12.0f, 3.0, true, &finite);
        check (std::abs (dry + 12.0) < 0.05, "mix 0 is fully dry" + at);
        setParam (p, "mix", 1.0f);

        // ---- auto makeup ----
        setParam (p, "automakeup", 1.0f);
        const double withMakeup = renderSine (p, sr, -12.0f, 8.0, true, &finite);
        setParam (p, "automakeup", 0.0f);
        const double restored = withMakeup - unity;
        const double taken = -12.0 - unity;
        check (restored > taken * 0.5 && restored <= taken + 0.5, "auto makeup restores " + juce::String (restored, 1) + " of " + juce::String (taken, 1) + " dB" + at);

        // ---- hi freq: the detector should react more to highs ----
        {
            const auto eightK = [&]
            {
                p.prepareToPlay (sr, 256);
                juce::AudioBuffer<float> buf (2, 256);
                juce::MidiBuffer midi;
                double phase = 0.0;
                for (int b = 0; b < (int) (3.0 * sr / 256.0); ++b)
                {
                    for (int i = 0; i < 256; ++i)
                    {
                        phase += 8000.0 / sr;
                        const float s = 0.25f * std::sin ((float) (phase * juce::MathConstants<double>::twoPi));
                        buf.setSample (0, i, s);
                        buf.setSample (1, i, s);
                    }
                    p.processBlock (buf, midi);
                }
                return (double) p.uiReductionDb.load();
            };
            setParam (p, "hifreq", 1.0f);
            const double withHiFreq = eightK();
            setParam (p, "hifreq", 0.0f);
            const double flat = eightK();
            check (withHiFreq > flat + 1.0, "hi freq reacts more to highs (" + juce::String (withHiFreq, 1) + " vs " + juce::String (flat, 1) + " dB)" + at);
        }

        // ---- analog ----
        {
            setParam (p, "peak", 0.0f);
            double f0, h0, f1, h1;
            setParam (p, "analog", 0.0f);
            measureHarmonics (p, sr, f0, h0);
            const double silentOff = silenceLevel (p, sr);
            setParam (p, "analog", 2.0f);
            measureHarmonics (p, sr, f1, h1);
            const double silent60 = silenceLevel (p, sr);
            setParam (p, "analog", 0.0f);

            const double thd = 100.0 * h1 / f1;
            const double levelChange = 20.0 * std::log10 (f1 / f0);
            check (h0 / f0 < 1.0e-4, "analog off adds no harmonics" + at);
            check (thd > 0.5 && thd < 12.0, "analog on adds " + juce::String (thd, 1) + "% harmonics" + at);
            check (std::abs (levelChange) < 2.0, "analog barely moves the level (" + juce::String (levelChange, 2) + " dB)" + at);
            check (silentOff < -200.0, "analog off is silent with no input" + at);
            check (silent60 > -90.0 && silent60 < -65.0, "analog on hums quietly (" + juce::String (silent60, 1) + " dBFS)" + at);
            setParam (p, "peak", 0.5f);
        }

        check (finite, "every output sample is a real number" + at);

        // ---- extreme settings stay stable ----
        {
            setParam (p, "peak", 1.0f);
            setParam (p, "limit", 1.0f);
            setParam (p, "gain", 100.0f);
            setParam (p, "analog", 1.0f);
            setParam (p, "automakeup", 1.0f);
            setParam (p, "hifreq", 1.0f);
            setParam (p, "trim", 18.0f);
            bool extremeFinite = true;
            const double level = renderSine (p, sr, 6.0f, 3.0, true, &extremeFinite);
            check (extremeFinite && level < 80.0, "extreme settings stay stable" + at);
        }
    }

    // ---- presets ----
    {
        const juce::StringArray wanted { "Start Me Up", "Full Reset", "PA Default", "Vox", "Sibilant Vox", "Piano", "Acoustic Guitar",
                                         "Electric Guitar", "Drum Group", "Hard Drum Group", "Bass", "Strings" };
        check (OptoProcessor::factoryPresetNames() == wanted, "all 12 presets are there, in your order");

        OptoProcessor p;
        for (int i = 0; i < wanted.size(); ++i)
        {
            p.loadFactoryPreset (i);
            bool finite = true;
            const double level = renderSine (p, 48000.0, -12.0f, 3.0, true, &finite);
            check (finite && level > -30.0 && level < 12.0 && p.getPresetName() == wanted[i],
                   "preset " + wanted[i] + " sounds sane (" + juce::String (level + 12.0, 1) + " dB change)");
        }

        p.loadFactoryPreset (1);
        const double flat = renderSine (p, 48000.0, -12.0f, 3.0);
        check (std::abs (flat + 12.0) < 0.1, "Full Reset leaves the sound alone");
    }

    // ---- saving your own preset ----
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OptoCompTest My Sound.optopreset");
        OptoProcessor a;
        setParam (a, "peak", 0.33f);
        setParam (a, "limit", 1.0f);
        setParam (a, "gain", 57.0f);
        setParam (a, "analog", 2.0f);
        check (a.saveUserPreset (file) && file.existsAsFile(), "preset file written");

        OptoProcessor b;
        check (b.loadUserPreset (file), "preset file loads");
        check (std::abs (param (b, "peak") - 0.33f) < 0.002f && param (b, "limit") > 0.5f && std::abs (param (b, "gain") - 57.0f) < 0.05f
                   && juce::roundToInt (param (b, "analog")) == 2 && b.getPresetName() == "OptoCompTest My Sound", "preset restores everything");
        file.deleteFile();
    }

    // ---- project save and load ----
    {
        OptoProcessor a;
        a.loadFactoryPreset (3);
        setParam (a, "trim", -3.0f);
        juce::MemoryBlock state;
        a.getStateInformation (state);
        OptoProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (param (b, "trim") + 3.0f) < 0.05f && b.getPresetName() == "Vox", "settings are saved with the project");
    }

    // ---- A/B ----
    {
        OptoProcessor p;
        setParam (p, "peak", 0.2f);
        p.copyCurrentSetupToOther();
        p.toggleSetup();
        check (p.getCurrentSetup() == 1 && std::abs (param (p, "peak") - 0.2f) < 0.002f, "A -> B copies, and B is now loaded");
        setParam (p, "peak", 0.8f);
        p.toggleSetup();
        check (p.getCurrentSetup() == 0 && std::abs (param (p, "peak") - 0.2f) < 0.002f, "back to A gives A's setting");
        p.toggleSetup();
        check (p.getCurrentSetup() == 1 && std::abs (param (p, "peak") - 0.8f) < 0.002f, "back to B gives B's setting");
    }

    // ---- CPU ----
    {
        OptoProcessor p;
        setParam (p, "analog", 1.0f);
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
        check (percent < 2.0, "uses " + juce::String (percent, 2) + "% of one CPU core");
    }

    // ---- window pictures ----
    {
        const auto snap = [&] (OptoProcessor& p, const juce::String& name)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds());
            juce::FileOutputStream out (outDir.getChildFile (name + ".png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (img, out);
            check (img.isValid() && out.getStatus().wasOk(), "window picture " + name);
        };

        OptoProcessor p;
        snap (p, "optocomp");

        p.loadFactoryPreset (9);
        setParam (p, "automakeup", 1.0f);
        setParam (p, "analog", 2.0f);
        setParam (p, "hifreq", 0.4f);
        p.apvts.state.setProperty ("meterMode", 2, nullptr);
        snap (p, "optocomp-hard");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
