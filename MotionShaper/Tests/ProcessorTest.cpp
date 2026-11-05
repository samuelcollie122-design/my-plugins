// Headless check of the MotionShaper engine and window. Not part of the plugin.
// Build with -DMOTIONSHAPER_TESTS=ON, run: MotionShaperTest <folder for window pictures>
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

    struct Result { double diff = 0.0, peak = 0.0, rms = 0.0; bool finite = true; };

    // A pretend host transport
    struct TestPlayHead : juce::AudioPlayHead
    {
        bool playing = false;
        double ppq = 8.0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (120.0);
            info.setPpqPosition (ppq);
            info.setIsPlaying (playing);
            return info;
        }
    };

    // One block per call; the pretend host advances only while playing
    void hostBlocks (MotionProcessor& p, TestPlayHead& ph, int blocks, juce::AudioBuffer<float>& buf, double& sumAbs)
    {
        juce::MidiBuffer midi;
        sumAbs = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            p.processBlock (buf, midi);
            if (ph.playing) ph.ppq += (double) buf.getNumSamples() * 2.0 / 44100.0;
            sumAbs += buf.getRMSLevel (0, 0, buf.getNumSamples());
        }
    }

    // Renders seconds of a 220 Hz saw-like signal through p and compares it with the input
    Result render (MotionProcessor& p, double seconds, float scLevel = 0.0f, bool withSc = false)
    {
        const int block = 256;
        const int channels = withSc ? 4 : 2;
        p.prepareToPlay (44100.0, block);

        juce::AudioBuffer<float> buf (channels, block);
        juce::MidiBuffer midi;
        Result res;
        double phase = 0.0, sum = 0.0, diff = 0.0;
        juce::int64 count = 0;
        int total = 0;

        const int blocks = (int) (seconds * 44100.0 / block);
        for (int b = 0; b < blocks; ++b)
        {
            std::vector<float> dry[2] { std::vector<float> ((size_t) block), std::vector<float> ((size_t) block) };
            for (int i = 0; i < block; ++i)
            {
                phase += 220.0 / 44100.0;
                phase -= std::floor (phase);
                const float s = 0.25f * (float) (2.0 * phase - 1.0) + 0.1f * std::sin ((float) (phase * 37.0));
                const float s2 = 0.8f * s + 0.1f * std::sin ((float) (phase * 6.2831853));     // left and right differ
                dry[0][(size_t) i] = s;
                dry[1][(size_t) i] = s2;
                buf.setSample (0, i, s);
                buf.setSample (1, i, s2);
                if (withSc)
                {
                    const bool burst = (total / 11025) % 2 == 0;
                    const float sc = burst ? scLevel * std::sin ((float) total * 0.03f) : 0.0f;
                    buf.setSample (2, i, sc);
                    buf.setSample (3, i, sc);
                }
                ++total;
            }

            p.processBlock (buf, midi);

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block; ++i)
                {
                    const float v = buf.getSample (c, i);
                    if (! std::isfinite (v)) res.finite = false;
                    res.peak = juce::jmax (res.peak, (double) std::abs (v));
                    if (b > 8)
                    {
                        diff += std::abs ((double) v - (double) dry[c][(size_t) i]);
                        sum += (double) v * (double) v;
                        ++count;
                    }
                }
        }
        if (count > 0)
        {
            res.diff = diff / (double) count;
            res.rms = std::sqrt (sum / (double) count);
        }
        return res;
    }

    void setParam (MotionProcessor& p, const juce::String& id, float realValue)
    {
        auto* q = p.apvts.getParameter (id);
        q->setValueNotifyingHost (q->convertTo0to1 (realValue));
    }

    void snapshot (MotionProcessor& p, const juce::File& dir, const juce::String& name, const std::function<void (MotionEditor&)>& setup = {})
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* motion = dynamic_cast<MotionEditor*> (ed.get());
        if (setup && motion != nullptr) setup (*motion);
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::FileOutputStream out (dir.getChildFile (name + ".png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
        check (img.isValid() && out.getStatus().wasOk(), "window picture " + name);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    // ---- every factory preset is stable and audible ----
    {
        MotionProcessor p;
        const auto& names = MotionProcessor::factoryPresetNames();
        for (int i = 0; i < names.size(); ++i)
        {
            p.loadFactoryPreset (i);
            const auto r = render (p, 2.0);
            check (r.finite && r.peak < 4.0 && r.rms > 0.001,
                   "preset " + names[i] + " stable (peak " + juce::String (r.peak, 2) + ", rms " + juce::String (r.rms, 3)
                       + ", change " + juce::String (r.diff, 4) + ")");
            if (i > 0) check (r.diff > 0.001, "preset " + names[i] + " changes the sound");
        }
    }

    // ---- each effect works on its own and responds to its main knob ----
    {
        const auto& fx = MotionProcessor::fxNames();
        for (int f = 0; f < MotionProcessor::numFxTypes; ++f)
        {
            MotionProcessor p;
            p.loadFactoryPreset (0);
            p.setLayerFx (0, f);
            Shape::Pattern pat;
            for (auto& q : pat.pts) q = Shape::makePreset (4);
            p.setPattern (0, pat);

            const auto mod = render (p, 1.0);
            setParam (p, MotionProcessor::paramId (0, "mix"), 0.0f);
            const auto dry = render (p, 1.0);
            check (mod.finite && mod.diff > 0.001, fx[f] + " changes the signal (" + juce::String (mod.diff, 4) + ")");
            check (dry.diff < 1.0e-4, fx[f] + " with mix at 0 leaves it alone (" + juce::String (dry.diff, 6) + ")");
        }
    }

    // ---- the sequencer only moves while the host plays ----
    {
        MotionProcessor p;
        TestPlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (44100.0, 256);
        juce::AudioBuffer<float> buf (2, 256);
        double level = 0.0;

        hostBlocks (p, ph, 4, buf, level);
        const int stoppedStep = p.uiStep[0].load();
        const float stoppedT = p.uiT[0].load();
        hostBlocks (p, ph, 200, buf, level);
        check (p.uiStep[0].load() == stoppedStep && juce::approximatelyEqual (p.uiT[0].load(), stoppedT), "stopped: play line stays still");

        ph.playing = true;
        hostBlocks (p, ph, 200, buf, level);
        check (p.uiStep[0].load() != stoppedStep || ! juce::approximatelyEqual (p.uiT[0].load(), stoppedT), "playing: play line moves");

        ph.playing = false;
        hostBlocks (p, ph, 4, buf, level);
        const int heldStep = p.uiStep[0].load();
        const float heldT = p.uiT[0].load();
        hostBlocks (p, ph, 200, buf, level);
        check (p.uiStep[0].load() == heldStep && juce::approximatelyEqual (p.uiT[0].load(), heldT), "stopped again: play line holds its place");

        // noise layers stay quiet while stopped and sound while playing
        p.loadFactoryPreset (16);
        p.prepareToPlay (44100.0, 256);
        hostBlocks (p, ph, 200, buf, level);
        check (level < 1.0e-4, "noise layer is silent while stopped (" + juce::String (level, 6) + ")");
        ph.playing = true;
        hostBlocks (p, ph, 200, buf, level);
        check (level > 0.01, "noise layer sounds while playing (" + juce::String (level, 3) + ")");
        p.setPlayHead (nullptr);
    }

    // ---- pitch: +12 semitones doubles the frequency of a sine ----
    for (const auto& semis : { 12.0f, -12.0f })
    {
        MotionProcessor p;
        p.loadFactoryPreset (0);
        p.setLayerFx (0, MotionProcessor::fxPitch);
        setParam (p, MotionProcessor::paramId (0, "b"), 0.5f + semis / 48.0f);
        setParam (p, MotionProcessor::paramId (0, "depth"), 0.0f);
        p.prepareToPlay (44100.0, 256);

        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        double phase = 0.0;
        float prev = 0.0f;
        int crossings = 0, samples = 0;
        for (int b = 0; b < 700; ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                phase += 440.0 / 44100.0;
                const float s = 0.5f * std::sin ((float) (phase * juce::MathConstants<double>::twoPi));
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            p.processBlock (buf, midi);
            if (b < 20) continue;
            for (int i = 0; i < 256; ++i)
            {
                const float v = buf.getSample (0, i);
                if (prev <= 0.0f && v > 0.0f) ++crossings;
                prev = v;
                ++samples;
            }
        }
        const double hz = (double) crossings * 44100.0 / (double) samples;
        const double want = 440.0 * std::pow (2.0, (double) semis / 12.0);
        check (std::abs (hz - want) / want < 0.06, "pitch " + juce::String (semis, 0) + " st: " + juce::String (hz, 0) + " Hz, wanted " + juce::String (want, 0));
    }

    // ---- random patterns ----
    {
        for (int steps : { 4, 16, 32 })
        {
            Shape::Pattern p;
            Shape::randomisePattern (p, steps);
            int covered = 0;
            for (int head = 0; head < steps; head += Shape::segmentLength (p, head, steps))
                covered += Shape::segmentLength (p, head, steps);
            check (covered == steps && p.start[0], "random pattern fills " + juce::String (steps) + " steps");
        }
        Shape::Pattern a, b;
        Shape::randomisePattern (a, 32);
        Shape::randomisePattern (b, 32);
        bool different = false;
        for (int s = 0; s < 32; ++s)
            different = different || a.start[(size_t) s] != b.start[(size_t) s] || a.pts[(size_t) s].size() != b.pts[(size_t) s].size();
        check (different, "two random patterns differ");
    }

    // ---- saving a preset to a file and loading it back ----
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MotionShaperTest My Sound.mspreset");
        MotionProcessor a;
        a.loadFactoryPreset (23);
        auto pat = a.getPattern (1);
        Shape::randomisePattern (pat, 12);
        a.setPattern (1, pat);
        setParam (a, MotionProcessor::paramId (0, "c"), 0.77f);
        check (a.saveUserPreset (file) && file.existsAsFile(), "preset file written");
        check (a.currentPresetName == "MotionShaperTest My Sound", "saving names the preset after the file");

        MotionProcessor b;
        check (b.loadUserPreset (file), "preset file loads");
        check (b.currentPresetName == "MotionShaperTest My Sound", "loaded preset shows its name");
        check (b.layerFx (1) == MotionProcessor::fxPitch && b.layerUsed (2) && ! b.layerUsed (3), "preset restores its layers");
        check (std::abs (b.apvts.getRawParameterValue (MotionProcessor::paramId (0, "c"))->load() - 0.77f) < 0.002f, "preset restores knobs");
        const auto loaded = b.getPattern (1);
        bool same = true;
        for (int s = 0; s < 12; ++s)
            same = same && loaded.start[(size_t) s] == pat.start[(size_t) s] && loaded.pts[(size_t) s].size() == pat.pts[(size_t) s].size();
        check (same, "preset restores its pattern");
        file.deleteFile();
    }

    // ---- layer bypass, global mix and output ----
    {
        MotionProcessor p;
        p.loadFactoryPreset (1);
        setParam (p, "mix", 0.0f);
        check (render (p, 1.0).diff < 1.0e-4, "global mix at 0 is fully dry");

        setParam (p, "mix", 1.0f);
        for (int l = 0; l < Shape::numLayers; ++l)
            setParam (p, MotionProcessor::paramId (l, "on"), 0.0f);
        check (render (p, 1.0).diff < 1.0e-4, "all layers off is fully dry");

        setParam (p, "out", 0.0f);
        check (render (p, 1.0).rms < 1.0e-4, "output at 0 is silent");
    }

    // ---- segments and timing ----
    {
        Shape::Pattern pat;
        pat.pts[3] = Shape::makePreset (4);
        Shape::setSegmentEnd (pat, 2, 4, 32);
        check (! pat.start[3] && ! pat.start[4] && pat.start[5], "merge makes one segment of three steps");
        check (Shape::segmentHead (pat, 4) == 2 && Shape::segmentLength (pat, 2, 32) == 3, "segment head and length");
        Shape::setSegmentEnd (pat, 2, 2, 32);
        check (pat.start[3] && Shape::segmentLength (pat, 2, 32) == 1, "shrinking splits the segment");

        const auto a = Shape::stepFromPosition (0.5, 32, 0.0);
        check (a.step == 0 && std::abs (a.t - 0.5f) < 1.0e-5f, "no swing: plain steps");
        const auto b = Shape::stepFromPosition (1.2, 32, 0.5);
        check (b.step == 0 && std::abs (b.t - 0.8f) < 1.0e-5f, "swing stretches the first step");
        const auto c = Shape::stepFromPosition (1.75, 32, 0.5);
        check (c.step == 1 && std::abs (c.t - 0.5f) < 1.0e-5f, "swing shortens the second step");
    }

    // ---- sidechain: follow mode reacts to an external signal, and only when one is there ----
    {
        MotionProcessor p;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        check (p.setBusesLayout (layout), "stereo sidechain input accepted");

        p.loadFactoryPreset (0);
        p.setLayerFx (0, MotionProcessor::fxVolume);
        setParam (p, MotionProcessor::paramId (0, "mode"), 1.0f);
        setParam (p, MotionProcessor::paramId (0, "depth"), -1.0f);    // duck when the sidechain is loud
        setParam (p, "sc_src", 0.0f);                                  // external

        const auto quiet = render (p, 2.0, 0.0f, true);
        const auto loud  = render (p, 2.0, 0.8f, true);
        check (quiet.diff < 1.0e-3, "no sidechain signal: no ducking (" + juce::String (quiet.diff, 5) + ")");
        check (loud.rms < quiet.rms * 0.9, "sidechain signal ducks the track (rms " + juce::String (quiet.rms, 3) + " to " + juce::String (loud.rms, 3) + ")");

        setParam (p, "b_on_low", 0.0f);
        setParam (p, "b_on_mid", 0.0f);
        setParam (p, "b_on_high", 0.0f);
        const auto noBands = render (p, 2.0, 0.8f, true);
        check (noBands.diff < 1.0e-3, "all detection bands off: no ducking");
    }

    // ---- saving and loading ----
    {
        MotionProcessor a;
        a.loadFactoryPreset (1);
        auto pat = a.getPattern (0);
        pat.muted[5] = true;
        a.setPattern (0, pat);
        setParam (a, MotionProcessor::paramId (1, "swing"), 0.33f);
        juce::MemoryBlock state;
        a.getStateInformation (state);

        MotionProcessor b;
        b.loadFactoryPreset (0);
        b.setStateInformation (state.getData(), (int) state.getSize());
        const auto loaded = b.getPattern (0);
        check (b.currentPresetName == "Candy", "preset name restored");
        check (loaded.muted[5] && ! loaded.start[4] && loaded.pts[3].size() == pat.pts[3].size(), "pattern restored");
        check (std::abs (b.apvts.getRawParameterValue (MotionProcessor::paramId (1, "swing"))->load() - 0.33f) < 0.002f, "parameter restored");
        check (b.layerUsed (2) && ! b.layerUsed (3), "layers restored");
    }

    // ---- undo ----
    {
        MotionProcessor p;
        const auto before = p.getPattern (0);
        auto after = before;
        after.muted[1] = true;
        p.undo.beginNewTransaction();
        p.setPattern (0, after);
        p.pushPatternUndo (0, before);
        p.undo.undo();
        check (! p.getPattern (0).muted[1], "undo restores a pattern");
        p.undo.redo();
        check (p.getPattern (0).muted[1], "redo applies it again");
    }

    // ---- window pictures ----
    {
        MotionProcessor p;
        render (p, 0.75);
        snapshot (p, outDir, "window-candy");
        snapshot (p, outDir, "window-crush", [] (MotionEditor& e) { e.selectLayer (1); });

        setParam (p, MotionProcessor::paramId (0, "mode"), 1.0f);
        snapshot (p, outDir, "window-follow");
    }
    {
        MotionProcessor p;
        p.loadFactoryPreset (2);
        render (p, 0.5);
        snapshot (p, outDir, "window-pump-volume");
    }
    {
        MotionProcessor p;
        p.loadFactoryPreset (23);
        snapshot (p, outDir, "window-glitch-pitch", [] (MotionEditor& e) { e.selectLayer (1); });
        p.loadFactoryPreset (21);
        snapshot (p, outDir, "window-width");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
