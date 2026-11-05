#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // steps per beat for each entry of rateNames()
    constexpr double kStepsPerBeat[8] = { 0.5, 1.0, 2.0, 4.0, 8.0, 3.0, 6.0, 0.25 };
    constexpr int    kGridSteps[6]    = { 4, 8, 12, 16, 24, 32 };
    constexpr float  kScFreqs[8]      = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f };

    float cutoffFromNorm (float v) { return 20.0f * std::pow (1000.0f, v); }
    float holdFromNorm (float v)   { return std::pow (100.0f, v); }
    float bitsFromNorm (float v)   { return 16.0f - 14.0f * v; }
    float driveFromNorm (float v)  { return std::pow (10.0f, 24.0f * v / 20.0f); }
    float semisFromNorm (float v)  { return (v - 0.5f) * 48.0f; }
    float grainMsFromNorm (float v){ return 20.0f + 80.0f * v; }
    float chorusHzFromNorm (float v) { return 0.05f * std::pow (100.0f, v); }
    constexpr double kDelayBeats[6] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };

    juce::String serialisePoints (const Shape::Points& pts)
    {
        juce::StringArray parts;
        for (auto& q : pts)
            parts.add (juce::String (q.x, 4) + "," + juce::String (q.y, 4) + "," + juce::String (q.c, 3));
        return parts.joinIntoString (";");
    }

    Shape::Points parsePoints (const juce::String& s)
    {
        Shape::Points out;
        juce::StringArray parts;
        parts.addTokens (s, ";", "");
        for (auto& part : parts)
        {
            juce::StringArray v;
            v.addTokens (part, ",", "");
            if (v.size() == 3)
                out.push_back (Shape::Point { v[0].getFloatValue(), v[1].getFloatValue(), v[2].getFloatValue() });
        }
        Shape::sanitise (out);
        return out;
    }

    class PatternAction : public juce::UndoableAction
    {
    public:
        PatternAction (MotionProcessor& p, int l, Shape::Pattern b, Shape::Pattern a)
            : proc (p), layer (l), before (std::move (b)), after (std::move (a)) {}

        bool perform() override { proc.setPattern (layer, after); return true; }
        bool undo() override    { proc.setPattern (layer, before); return true; }
        int getSizeInUnits() override { return 512; }

    private:
        MotionProcessor& proc;
        int layer;
        Shape::Pattern before, after;
    };
}

juce::String MotionProcessor::paramId (int layer, const juce::String& name)
{
    return "l" + juce::String (layer) + "_" + name;
}

const juce::StringArray& MotionProcessor::fxNames()
{
    static const juce::StringArray n { "Filter", "Crush", "Texture", "Volume", "Pan",
                                       "Pitch", "Drive", "Distortion", "Noise", "Reverb", "Width", "Delay", "Chorus" };
    return n;
}

const juce::StringArray& MotionProcessor::rateNames()
{
    static const juce::StringArray n { "half", "quarter", "8th", "16th", "32nd", "8th T", "16th T", "1 bar" };
    return n;
}

const juce::StringArray& MotionProcessor::gridNames()
{
    static const juce::StringArray n { "1/4", "1/8", "1/12", "1/16", "1/24", "1/32" };
    return n;
}

const juce::StringArray& MotionProcessor::retrigNames()
{
    static const juce::StringArray n { "off", "sidechain", "1 bar", "2 bars" };
    return n;
}

const juce::StringArray& MotionProcessor::scFilterFreqNames()
{
    static const juce::StringArray n { "20 hz", "50 hz", "100 hz", "200 hz", "500 hz", "1 khz", "2 khz", "5 khz" };
    return n;
}

int MotionProcessor::gridSteps (int gridIndex)
{
    return kGridSteps[juce::jlimit (0, 5, gridIndex)];
}

juce::String MotionProcessor::knobCaption (int fx, int knob)
{
    static const char* names[numFxTypes][3] = { { "type",    "cutoff", "resonance" },
                                                { "drive",   "rate",   "bits" },
                                                { "flavour", "amount", "tone" },
                                                { "",        "level",  "" },
                                                { "",        "center", "" },
                                                { "grain",   "pitch",  "feedback" },
                                                { "tone",    "drive",  "bias" },
                                                { "type",    "amount", "tone" },
                                                { "color",   "level",  "tone" },
                                                { "size",    "amount", "damping" },
                                                { "haas",    "width",  "" },
                                                { "time",    "amount", "feedback" },
                                                { "rate",    "amount", "depth" } };
    return names[juce::jlimit (0, numFxTypes - 1, fx)][juce::jlimit (0, 2, knob)];
}

juce::String MotionProcessor::knobText (int fx, int knob, float v)
{
    using juce::String;
    const String percent = String (juce::roundToInt (v * 100.0f)) + "%";

    switch (fx)
    {
        case fxFilter:
            if (knob == 0)
            {
                static const char* t[4] = { "lp", "hp", "bp", "notch" };
                return t[juce::jlimit (0, 3, juce::roundToInt (v * 3.0f))];
            }
            if (knob == 1)
            {
                const float hz = cutoffFromNorm (v);
                return hz >= 1000.0f ? String (hz / 1000.0f, 2) + " khz" : String (juce::roundToInt (hz)) + " hz";
            }
            return percent;

        case fxCrush:
            if (knob == 0) return "+" + String (24.0f * v, 1) + " db";
            if (knob == 1) return "1/" + String (holdFromNorm (v), holdFromNorm (v) < 10.0f ? 1 : 0);
            return String (juce::roundToInt (bitsFromNorm (v))) + " bit";

        case fxTexture:
            if (knob == 0)
            {
                static const char* t[3] = { "noise", "dust", "tape" };
                return t[juce::jlimit (0, 2, juce::roundToInt (v * 2.0f))];
            }
            return percent;

        case fxPan:
        {
            const int amt = juce::roundToInt ((v * 2.0f - 1.0f) * 100.0f);
            return amt == 0 ? String ("center") : (amt < 0 ? "left " + String (-amt) : "right " + String (amt));
        }

        case fxPitch:
            if (knob == 0) return String (juce::roundToInt (grainMsFromNorm (v))) + " ms";
            if (knob == 1)
            {
                const float st = semisFromNorm (v);
                return (st >= 0.0f ? "+" : "") + String (st, 1) + " st";
            }
            return percent;

        case fxDrive:
            if (knob == 1) return "+" + String (36.0f * v, 1) + " db";
            return percent;

        case fxDistortion:
            if (knob == 0)
            {
                static const char* t[3] = { "hard", "fold", "rectify" };
                return t[juce::jlimit (0, 2, juce::roundToInt (v * 2.0f))];
            }
            if (knob == 1) return "+" + String (40.0f * v, 1) + " db";
            return percent;

        case fxNoise:
            if (knob == 0)
            {
                static const char* t[3] = { "white", "pink", "brown" };
                return t[juce::jlimit (0, 2, juce::roundToInt (v * 2.0f))];
            }
            return percent;

        case fxWidth:
            if (knob == 0) return String (juce::roundToInt (v * 25.0f)) + " ms";
            return String (juce::roundToInt (v * 200.0f)) + "%";

        case fxDelay:
            if (knob == 0)
            {
                static const char* t[6] = { "1/16", "1/8", "1/8 dot", "1/4", "1/4 dot", "1/2" };
                return t[juce::jlimit (0, 5, juce::roundToInt (v * 5.0f))];
            }
            return percent;

        case fxChorus:
            if (knob == 0) return String (chorusHzFromNorm (v), 2) + " hz";
            return percent;

        default:
            return percent;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MotionProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 100.0f; });

    const NormalisableRange<float> unit (0.0f, 1.0f, 0.001f);

    for (int l = 0; l < Shape::numLayers; ++l)
    {
        const String n = "Layer " + String (l + 1) + " ";
        const auto id = [l] (const char* name) { return ParameterID { paramId (l, name), 1 }; };

        layout.add (std::make_unique<AudioParameterBool> (id ("used"), n + "In Use", l == 0,
                                                          AudioParameterBoolAttributes().withAutomatable (false)));
        layout.add (std::make_unique<AudioParameterBool> (id ("on"), n + "On", true));
        layout.add (std::make_unique<AudioParameterChoice> (id ("fx"), n + "Effect", fxNames(), 0,
                                                            AudioParameterChoiceAttributes().withAutomatable (false)));
        layout.add (std::make_unique<AudioParameterChoice> (id ("mode"), n + "Mode", StringArray { "shape", "follow" }, 0));
        layout.add (std::make_unique<AudioParameterChoice> (id ("grid"), n + "Grid Size", gridNames(), 5));
        layout.add (std::make_unique<AudioParameterChoice> (id ("rate"), n + "Rate", rateNames(), 3));
        layout.add (std::make_unique<AudioParameterFloat> (id ("swing"), n + "Swing", unit, 0.0f, percent));
        layout.add (std::make_unique<AudioParameterChoice> (id ("retrig"), n + "Retrigger", retrigNames(), 0));
        layout.add (std::make_unique<AudioParameterFloat> (id ("a"), n + "Knob A", unit, 0.0f, percent));
        layout.add (std::make_unique<AudioParameterFloat> (id ("b"), n + "Knob B", unit, 0.25f, percent));
        layout.add (std::make_unique<AudioParameterFloat> (id ("c"), n + "Knob C", unit, 0.15f, percent));
        layout.add (std::make_unique<AudioParameterFloat> (id ("depth"), n + "Depth",
                                                           NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.5f, percent));
        layout.add (std::make_unique<AudioParameterFloat> (id ("out"), n + "Output",
                                                           NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f, percent));
        layout.add (std::make_unique<AudioParameterFloat> (id ("mix"), n + "Mix", unit, 1.0f, percent));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", unit, 1.0f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output", unit, 1.0f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "smooth", 1 }, "Smoothing",
                                                       NormalisableRange<float> (0.0f, 50.0f, 0.1f, 0.5f), 3.0f,
                                                       AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sc_on", 1 }, "Sidechain On", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "sc_src", 1 }, "Sidechain Source",
                                                        StringArray { "External", "Internal" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "sc_type", 1 }, "Sidechain Filter",
                                                        StringArray { "off", "hp", "lp" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "sc_freq", 1 }, "Sidechain Filter Freq",
                                                        scFilterFreqNames(), 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "sc_slope", 1 }, "Sidechain Filter Slope",
                                                        StringArray { "12 db", "24 db" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "sc_trig", 1 }, "Sidechain Trigger",
                                                        StringArray { "peak", "rms" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "sc_sens", 1 }, "Sidechain Sensitivity", unit, 0.7f, percent));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sc_listen", 1 }, "Sidechain Listen", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "x_low", 1 }, "Crossover Low",
                                                       NormalisableRange<float> (20.0f, 1000.0f, 1.0f, 0.4f), 180.0f,
                                                       AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "x_high", 1 }, "Crossover High",
                                                       NormalisableRange<float> (500.0f, 15000.0f, 1.0f, 0.4f), 3200.0f,
                                                       AudioParameterFloatAttributes().withLabel ("Hz")));

    const char* bands[3] = { "low", "mid", "high" };
    for (auto* b : bands)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { String ("b_on_") + b, 1 }, String ("Band On ") + b, true));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { String ("b_solo_") + b, 1 }, String ("Band Solo ") + b, false));
    }
    return layout;
}

MotionProcessor::MotionProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undo, "STATE", createLayout())
{
    for (int l = 0; l < Shape::numLayers; ++l)
    {
        const auto raw = [this, l] (const char* name) { return apvts.getRawParameterValue (paramId (l, name)); };
        pUsed[l] = raw ("used");     pOn[l] = raw ("on");         pFx[l] = raw ("fx");
        pMode[l] = raw ("mode");     pGrid[l] = raw ("grid");     pRate[l] = raw ("rate");
        pSwing[l] = raw ("swing");   pRetrig[l] = raw ("retrig");
        pA[l] = raw ("a");           pB[l] = raw ("b");           pC[l] = raw ("c");
        pDepth[l] = raw ("depth");   pLayerOut[l] = raw ("out");  pLayerMix[l] = raw ("mix");

        uiStep[l].store (-1);
        uiT[l].store (0.0f);
        rt[l] = buildRt (patterns[l]);
    }

    pMix = apvts.getRawParameterValue ("mix");
    pOut = apvts.getRawParameterValue ("out");
    pSmooth = apvts.getRawParameterValue ("smooth");
    pScOn = apvts.getRawParameterValue ("sc_on");
    pScSrc = apvts.getRawParameterValue ("sc_src");
    pScType = apvts.getRawParameterValue ("sc_type");
    pScFreq = apvts.getRawParameterValue ("sc_freq");
    pScSlope = apvts.getRawParameterValue ("sc_slope");
    pScTrig = apvts.getRawParameterValue ("sc_trig");
    pScSens = apvts.getRawParameterValue ("sc_sens");
    pListen = apvts.getRawParameterValue ("sc_listen");
    pXLow = apvts.getRawParameterValue ("x_low");
    pXHigh = apvts.getRawParameterValue ("x_high");

    const char* bands[3] = { "low", "mid", "high" };
    for (int b = 0; b < 3; ++b)
    {
        pBandOn[b]   = apvts.getRawParameterValue (juce::String ("b_on_") + bands[b]);
        pBandSolo[b] = apvts.getRawParameterValue (juce::String ("b_solo_") + bands[b]);
    }

    loadFactoryPreset (1);      // starts on "Candy"
}

// ======================= layers =======================
int MotionProcessor::numUsedLayers() const
{
    int count = 0;
    for (int l = 0; l < Shape::numLayers; ++l)
        if (layerUsed (l)) ++count;
    return count;
}

void MotionProcessor::setParam (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

void MotionProcessor::setLayerFx (int l, int fx)
{
    setParam (paramId (l, "fx"), (float) fx);

    switch (fx)
    {
        case fxCrush:   setParam (paramId (l, "a"), 0.1f); setParam (paramId (l, "b"), 0.1f);  setParam (paramId (l, "c"), 0.4f); setParam (paramId (l, "depth"), 0.5f); break;
        case fxTexture: setParam (paramId (l, "a"), 0.0f); setParam (paramId (l, "b"), 0.1f);  setParam (paramId (l, "c"), 0.6f); setParam (paramId (l, "depth"), 0.5f); break;
        case fxVolume:  setParam (paramId (l, "b"), 1.0f); setParam (paramId (l, "depth"), 1.0f); break;
        case fxPan:     setParam (paramId (l, "b"), 0.5f); setParam (paramId (l, "depth"), 0.5f); break;
        case fxPitch:   setParam (paramId (l, "a"), 0.25f); setParam (paramId (l, "b"), 0.5f);  setParam (paramId (l, "c"), 0.0f);  setParam (paramId (l, "depth"), 0.25f); break;
        case fxDrive:   setParam (paramId (l, "a"), 0.8f);  setParam (paramId (l, "b"), 0.2f);  setParam (paramId (l, "c"), 0.0f);  setParam (paramId (l, "depth"), 0.5f); break;
        case fxDistortion: setParam (paramId (l, "a"), 0.0f); setParam (paramId (l, "b"), 0.2f); setParam (paramId (l, "c"), 0.8f); setParam (paramId (l, "depth"), 0.5f); break;
        case fxNoise:   setParam (paramId (l, "a"), 0.0f);  setParam (paramId (l, "b"), 0.0f);  setParam (paramId (l, "c"), 0.8f);  setParam (paramId (l, "depth"), 0.5f); break;
        case fxReverb:  setParam (paramId (l, "a"), 0.6f);  setParam (paramId (l, "b"), 0.1f);  setParam (paramId (l, "c"), 0.4f);  setParam (paramId (l, "depth"), 0.6f); break;
        case fxWidth:   setParam (paramId (l, "a"), 0.0f);  setParam (paramId (l, "b"), 0.5f);  setParam (paramId (l, "depth"), 0.5f); break;
        case fxDelay:   setParam (paramId (l, "a"), 0.6f);  setParam (paramId (l, "b"), 0.0f);  setParam (paramId (l, "c"), 0.4f);  setParam (paramId (l, "depth"), 0.6f); break;
        case fxChorus:  setParam (paramId (l, "a"), 0.4f);  setParam (paramId (l, "b"), 0.3f);  setParam (paramId (l, "c"), 0.5f);  setParam (paramId (l, "depth"), 0.5f); break;
        default:        setParam (paramId (l, "a"), 0.0f); setParam (paramId (l, "b"), 0.25f); setParam (paramId (l, "c"), 0.15f); setParam (paramId (l, "depth"), 0.5f); break;
    }
}

int MotionProcessor::addLayer (int fx)
{
    for (int l = 0; l < Shape::numLayers; ++l)
        if (! layerUsed (l))
        {
            setParam (paramId (l, "used"), 1.0f);
            setParam (paramId (l, "on"), 1.0f);
            setLayerFx (l, fx);
            const auto before = getPattern (l);
            Shape::Pattern fresh;
            for (auto& p : fresh.pts) p = Shape::makePreset (Shape::presetRandom + 1);
            setPattern (l, fresh);
            pushPatternUndo (l, before);
            return l;
        }
    return -1;
}

void MotionProcessor::removeLayer (int l)
{
    if (numUsedLayers() > 1)
        setParam (paramId (l, "used"), 0.0f);
}

int MotionProcessor::duplicateLayer (int src)
{
    for (int l = 0; l < Shape::numLayers; ++l)
        if (! layerUsed (l))
        {
            for (auto* name : { "on", "fx", "mode", "grid", "rate", "swing", "retrig", "a", "b", "c", "depth", "out", "mix" })
            {
                auto* from = apvts.getParameter (paramId (src, name));
                auto* to   = apvts.getParameter (paramId (l, name));
                to->setValueNotifyingHost (from->getValue());
            }
            setParam (paramId (l, "used"), 1.0f);
            const auto before = getPattern (l);
            setPattern (l, getPattern (src));
            pushPatternUndo (l, before);
            return l;
        }
    return -1;
}

// ======================= patterns =======================
std::unique_ptr<MotionProcessor::RtPattern> MotionProcessor::buildRt (const Shape::Pattern& p) const
{
    auto out = std::make_unique<RtPattern>();
    int head = 0;
    for (int s = 0; s < Shape::maxSteps; ++s)
    {
        if (p.start[(size_t) s] || s == 0) head = s;
        out->head[s] = head;
        out->muted[s] = p.muted[(size_t) s];

        if (head == s)
        {
            Shape::Points pts = p.pts[(size_t) s];
            Shape::sanitise (pts);
            for (int i = 0; i < Shape::lutSize; ++i)
            {
                const float t = (float) i / (float) (Shape::lutSize - 1);
                out->lut[s][i] = juce::jlimit (0.0f, 1.0f, Shape::evaluate (pts, t));
            }
        }
    }

    int next = Shape::maxSteps;
    for (int s = Shape::maxSteps - 1; s >= 0; --s)
    {
        out->nextStart[s] = next;
        if (out->head[s] == s) next = s;
    }
    return out;
}

void MotionProcessor::publishPattern (int layer)
{
    std::unique_ptr<RtPattern> fresh;
    {
        const juce::ScopedLock sl (patternLock);
        fresh = buildRt (patterns[layer]);
    }
    {
        const juce::SpinLock::ScopedLockType sl (rtLock);
        rt[layer].swap (fresh);
    }
    shapeRevision.fetch_add (1);
}

Shape::Pattern MotionProcessor::getPattern (int layer) const
{
    const juce::ScopedLock sl (patternLock);
    return patterns[juce::jlimit (0, Shape::numLayers - 1, layer)];
}

void MotionProcessor::setPattern (int layer, const Shape::Pattern& p)
{
    if (layer < 0 || layer >= Shape::numLayers) return;
    {
        const juce::ScopedLock sl (patternLock);
        patterns[layer] = p;
        patterns[layer].start[0] = true;
        for (auto& pts : patterns[layer].pts) Shape::sanitise (pts);
    }
    publishPattern (layer);
}

void MotionProcessor::pushPatternUndo (int layer, const Shape::Pattern& before)
{
    undo.perform (new PatternAction (*this, layer, before, getPattern (layer)));
}

Shape::Points MotionProcessor::getSegmentPoints (int layer, int step) const
{
    const juce::ScopedLock sl (patternLock);
    const auto& p = patterns[juce::jlimit (0, Shape::numLayers - 1, layer)];
    return p.pts[(size_t) Shape::segmentHead (p, step)];
}

void MotionProcessor::setSegmentPoints (int layer, int step, Shape::Points pts)
{
    if (layer < 0 || layer >= Shape::numLayers) return;
    Shape::sanitise (pts);
    {
        const juce::ScopedLock sl (patternLock);
        auto& p = patterns[layer];
        p.pts[(size_t) Shape::segmentHead (p, step)] = std::move (pts);
    }
    publishPattern (layer);
}

// ======================= presets =======================
const juce::StringArray& MotionProcessor::factoryPresetNames()
{
    static const juce::StringArray n { "Init", "Candy", "Pump", "Stutter", "Wobble", "Bit Bounce", "Dust Swing", "Autopan",
                                       "Trance Gate", "Half Time", "Pitch Drop", "Octave Jumps", "Tape Warble", "Drive Pump",
                                       "Fuzz Gate", "Wave Fold", "Noise Riser", "Noise Hats", "Reverb Swell", "Gated Verb",
                                       "Dub Delay", "Wide Bounce", "Chorus Drift", "Glitch Stack" };
    return n;
}

void MotionProcessor::resetAllParams()
{
    for (auto* p : getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
}

void MotionProcessor::setupLayer (int l, int fx, int rate, int grid, float a, float b, float c, float depth, float mix)
{
    setParam (paramId (l, "used"), 1.0f);
    setParam (paramId (l, "on"), 1.0f);
    setParam (paramId (l, "fx"), (float) fx);
    setParam (paramId (l, "rate"), (float) rate);
    setParam (paramId (l, "grid"), (float) grid);
    setParam (paramId (l, "a"), a);
    setParam (paramId (l, "b"), b);
    setParam (paramId (l, "c"), c);
    setParam (paramId (l, "depth"), depth);
    setParam (paramId (l, "mix"), mix);
}

void MotionProcessor::setSegments (int l, const std::vector<int>& lengths, const std::vector<int>& presets)
{
    Shape::Pattern p;
    p.start.fill (false);
    int step = 0;
    for (size_t i = 0; step < Shape::maxSteps; ++i)
    {
        p.start[(size_t) step] = true;
        p.pts[(size_t) step] = Shape::makePreset (presets[i % presets.size()]);
        step += juce::jmax (1, lengths[i % lengths.size()]);
    }
    setPattern (l, p);
}

void MotionProcessor::loadFactoryPreset (int index)
{
    resetAllParams();
    for (int l = 0; l < Shape::numLayers; ++l)
        setPattern (l, Shape::Pattern());

    constexpr int rate8 = 2, rate16 = 3, rate16T = 6;
    constexpr int grid4 = 0, grid8 = 1, grid12 = 2, grid16 = 3, grid24 = 4, grid32 = 5;

    switch (index)
    {
        case 1:     // Candy: plucked filter with a little crush and grain on top
            setupLayer (0, fxFilter, rate16, grid32, 0.0f, 0.25f, 0.15f, 0.65f, 1.0f);
            setSegments (0, { 1, 1, 1, 4, 1, 1, 1, 1, 1, 4, 1, 1, 1, 1, 3, 3, 3, 3 },
                            { 9, 9, 9, 10, 9, 9, 9, 9, 9, 10, 9, 9, 9, 9, 4, 4, 4, 9 });
            setupLayer (1, fxCrush, rate8, grid16, 0.1f, 0.15f, 0.35f, 0.3f, 0.5f);
            setSegments (1, { 2 }, { 4, 6 });
            setupLayer (2, fxTexture, rate8, grid8, 0.0f, 0.12f, 0.6f, 0.2f, 0.7f);
            setSegments (2, { 1 }, { 6 });
            break;

        case 2:     // Pump
            setupLayer (0, fxVolume, 1, grid4, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f);
            setSegments (0, { 1 }, { 1 });
            break;

        case 3:     // Stutter
            setupLayer (0, fxVolume, rate16, grid16, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f);
            setSegments (0, { 1, 1, 2, 1, 1, 1, 1 }, { 2, 2, 0, 2, 7, 2, 2 });
            break;

        case 4:     // Wobble
            setupLayer (0, fxFilter, rate8, grid8, 0.0f, 0.3f, 0.45f, 0.6f, 1.0f);
            setSegments (0, { 1, 1, 2 }, { 6, 6, 5 });
            break;

        case 5:     // Bit Bounce
            setupLayer (0, fxCrush, rate16, grid16, 0.2f, 0.0f, 0.3f, 0.75f, 1.0f);
            setSegments (0, { 1, 3 }, { 4, 3 });
            break;

        case 6:     // Dust Swing
            setupLayer (0, fxVolume, rate16, grid16, 0.0f, 1.0f, 0.0f, 0.7f, 1.0f);
            setParam (paramId (0, "swing"), 0.45f);
            setSegments (0, { 1 }, { 9 });
            setupLayer (1, fxTexture, rate8, grid8, 0.5f, 0.3f, 0.5f, 0.3f, 1.0f);
            setSegments (1, { 2 }, { 6 });
            break;

        case 7:     // Autopan
            setupLayer (0, fxPan, 7, grid4, 0.0f, 0.5f, 0.0f, 0.8f, 1.0f);
            setSegments (0, { 4 }, { 6 });
            break;

        case 8:     // Trance Gate: classic chopped sixteenths
            setupLayer (0, fxVolume, rate16, grid16, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f);
            setSegments (0, { 1 }, { 2, 2, 0, 2, 2, 0, 2, 2 });
            setParam ("smooth", 2.0f);
            break;

        case 9:     // Half Time: long filter sweep and a slow pump
            setupLayer (0, fxFilter, 1, grid8, 0.0f, 0.35f, 0.3f, 0.6f, 1.0f);
            setSegments (0, { 2, 2, 4 }, { 4, 4, 3 });
            setupLayer (1, fxVolume, 0, grid4, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f);
            setSegments (1, { 1 }, { 1 });
            break;

        case 10:    // Pitch Drop: falls away each bar
            setupLayer (0, fxPitch, 7, grid4, 0.25f, 0.25f, 0.0f, 0.25f, 1.0f);
            setSegments (0, { 1 }, { 4 });
            break;

        case 11:    // Octave Jumps: stepped pitch, blended with the dry sound
            setupLayer (0, fxPitch, rate8, grid8, 0.2f, 0.5f, 0.0f, 0.25f, 0.6f);
            setSegments (0, { 1 }, { 0, 2, 0, 2 });
            break;

        case 12:    // Tape Warble: slow pitch drift, soft drive and hiss
            setupLayer (0, fxPitch, 1, grid4, 0.4f, 0.495f, 0.0f, 0.02f, 1.0f);
            setSegments (0, { 2 }, { 6 });
            setupLayer (1, fxDrive, rate8, grid8, 0.55f, 0.2f, 0.2f, 0.1f, 1.0f);
            setSegments (1, { 2 }, { 5 });
            setupLayer (2, fxTexture, rate8, grid8, 1.0f, 0.12f, 0.5f, 0.1f, 1.0f);
            setSegments (2, { 4 }, { 6 });
            break;

        case 13:    // Drive Pump: harder on every beat
            setupLayer (0, fxDrive, rate16, grid4, 0.75f, 0.1f, 0.1f, 0.7f, 1.0f);
            setSegments (0, { 1 }, { 4 });
            break;

        case 14:    // Fuzz Gate: clipped and chopped
            setupLayer (0, fxDistortion, rate16, grid16, 0.0f, 0.5f, 0.6f, 0.2f, 1.0f);
            setSegments (0, { 1 }, { 9 });
            setupLayer (1, fxVolume, rate16, grid16, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f);
            setSegments (1, { 1, 1, 2 }, { 2, 2, 0 });
            break;

        case 15:    // Wave Fold: rising metallic folds
            setupLayer (0, fxDistortion, 1, grid4, 0.5f, 0.1f, 0.7f, 0.5f, 0.8f);
            setSegments (0, { 2 }, { 3, 4 });
            break;

        case 16:    // Noise Riser: white noise climbing over two bars
            setupLayer (0, fxNoise, rate16, grid32, 0.0f, 0.0f, 0.6f, 0.7f, 1.0f);
            setSegments (0, { 32 }, { 3 });
            setupLayer (1, fxFilter, rate16, grid32, 1.0f, 0.1f, 0.15f, 0.6f, 1.0f);
            setSegments (1, { 32 }, { 4 });
            break;

        case 17:    // Noise Hats: short noise ticks on the off-beats
            setupLayer (0, fxNoise, rate16, grid16, 0.0f, 0.0f, 1.0f, 0.65f, 1.0f);
            setSegments (0, { 1, 1 }, { 0, 9 });
            {
                auto p = getPattern (0);
                for (int s = 0; s < Shape::maxSteps; s += 2) p.muted[(size_t) s] = true;
                setPattern (0, p);
            }
            break;

        case 18:    // Reverb Swell: the room opens up across the bar
            setupLayer (0, fxReverb, 1, grid4, 0.75f, 0.0f, 0.35f, 0.8f, 1.0f);
            setSegments (0, { 4 }, { 3 });
            break;

        case 19:    // Gated Verb: reverb chopped into a rhythm
            setupLayer (0, fxReverb, rate16, grid16, 0.85f, 0.0f, 0.5f, 0.8f, 1.0f);
            setSegments (0, { 1, 1, 2 }, { 9, 2, 0 });
            break;

        case 20:    // Dub Delay: echoes open on the last beat
            setupLayer (0, fxDelay, rate8, grid8, 0.6f, 0.0f, 0.65f, 0.7f, 1.0f);
            setSegments (0, { 6, 2 }, { 0, 3 });
            {
                auto p = getPattern (0);
                for (int s = 0; s < 6; ++s) p.muted[(size_t) s] = true;
                setPattern (0, p);
            }
            setupLayer (1, fxFilter, 1, grid4, 1.0f, 0.2f, 0.2f, 0.5f, 0.35f);
            setSegments (1, { 2 }, { 6 });
            break;

        case 21:    // Wide Bounce: narrow on the hit, wide on the tail
            setupLayer (0, fxWidth, rate8, grid8, 0.3f, 0.25f, 0.0f, 0.75f, 1.0f);
            setSegments (0, { 1 }, { 3 });
            break;

        case 22:    // Chorus Drift
            setupLayer (0, fxChorus, 1, grid4, 0.35f, 0.3f, 0.6f, 0.6f, 1.0f);
            setSegments (0, { 2 }, { 6 });
            setupLayer (1, fxWidth, 1, grid4, 0.0f, 0.65f, 0.0f, 0.0f, 1.0f);
            break;

        case 23:    // Glitch Stack: crush, pitch and gate on different lengths
            setupLayer (0, fxCrush, rate16, grid16, 0.2f, 0.1f, 0.5f, 0.6f, 1.0f);
            setSegments (0, { 1, 2, 1, 3, 1 }, { 2, 4, 9, 3, 2 });
            setupLayer (1, fxPitch, rate16, grid12, 0.1f, 0.5f, 0.0f, 0.25f, 0.5f);
            setSegments (1, { 3, 1 }, { 0, 2 });
            setupLayer (2, fxVolume, rate16T, grid24, 0.0f, 1.0f, 0.0f, 0.8f, 1.0f);
            setSegments (2, { 2, 1, 3 }, { 7, 2, 9 });
            break;

        default:    // Init: one filter layer, wide open, doing nothing yet
            setupLayer (0, fxFilter, rate16, grid16, 0.0f, 1.0f, 0.15f, 0.0f, 1.0f);
            break;
    }

    currentPresetName = factoryPresetNames()[juce::jlimit (0, factoryPresetNames().size() - 1, index)];
    undo.clearUndoHistory();
}

juce::File MotionProcessor::userPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("MotionShaper").getChildFile ("Presets");
}

juce::Array<juce::File> MotionProcessor::userPresetFiles() const
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.mspreset");
    files.sort();
    return files;
}

bool MotionProcessor::saveUserPreset (const juce::File& file)
{
    currentPresetName = file.getFileNameWithoutExtension();
    file.getParentDirectory().createDirectory();
    if (auto xml = buildStateTree().createXml())
        return xml->writeTo (file);
    return false;
}

bool MotionProcessor::loadUserPreset (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
        if (xml->hasTagName (apvts.state.getType()))
        {
            applyStateTree (juce::ValueTree::fromXml (*xml));
            currentPresetName = file.getFileNameWithoutExtension();
            undo.clearUndoHistory();
            return true;
        }
    return false;
}

// ======================= audio =======================
bool MotionProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainInputChannelSet()  != juce::AudioChannelSet::stereo()
     || l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.getChannelSet (true, 1);
        return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
    }
    return true;
}

void MotionProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    std::uint32_t noiseSeed = 0x2545f491u;
    for (auto& r : layers)
    {
        r.svf.reset();
        r.crush.reset();
        r.tex.reset();
        r.tex.prepare (sampleRate);
        r.pitch.prepare (sampleRate);
        r.shaper.prepare (sampleRate);
        r.noise.reset();
        r.noise.seed (noiseSeed);
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        r.widener.prepare (sampleRate);
        r.echo.prepare (sampleRate);
        r.chorus.prepare (sampleRate);
        r.reverb.setSampleRate (sampleRate);
        r.reverb.reset();
        r.noiseGate = 0.0f;
        r.mixSm.reset (sampleRate, 0.02);
        r.outSm.reset (sampleRate, 0.02);
        r.mixSm.setCurrentAndTargetValue (0.0f);
        r.outSm.setCurrentAndTargetValue (1.0f);
        r.sm = 0.0f;
        r.last = 0.0f;
        r.beatOffset = 0.0;
        r.ctrl = 0;
    }

    mixSm.reset (sampleRate, 0.02);
    outSm.reset (sampleRate, 0.02);
    mixSm.setCurrentAndTargetValue (pMix->load());
    outSm.setCurrentAndTargetValue (pOut->load());

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    crossLow.prepare (spec);
    crossHigh.prepare (spec);
    crossLow.reset();
    crossHigh.reset();
    scFilter1.reset();
    scFilter2.reset();
    scEnv = scRms = 0.0f;
    trigArmed = true;
    trigHoldoff = 0;
}

void MotionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainBus = getBusBuffer (buffer, true, 0);
    const int n = mainBus.getNumSamples();
    if (mainBus.getNumChannels() < 2 || n == 0) return;

    // ---- transport: lock to the host when it is playing, otherwise free-run ----
    double bpm = lastBpm;
    bool hostPlaying = false, haveTransport = false;
    double hostBeat = 0.0;
    if (auto* ph = getPlayHead())
    {
        auto posOpt = ph->getPosition();
        if (posOpt.hasValue())
        {
            const auto& pos = *posOpt;
            auto b = pos.getBpm();
            if (b.hasValue() && *b > 1.0) { bpm = *b; lastBpm = bpm; }
            auto p = pos.getPpqPosition();
            if (p.hasValue()) { hostBeat = *p; hostPlaying = pos.getIsPlaying(); haveTransport = true; }
            auto ts = pos.getTimeSignature();
            if (ts.hasValue() && ts->denominator > 0)
            {
                lastBeatsPerBar = (double) ts->numerator * 4.0 / (double) ts->denominator;
                uiNum.store (ts->numerator);
                uiDen.store (ts->denominator);
            }
        }
    }
    uiBpm.store ((float) bpm);
    uiHostPlaying.store (hostPlaying);

    // The sequencer only moves while the host plays. It free-runs only if the host gives no position at all.
    const bool running = hostPlaying || ! haveTransport;
    const double beatsPerSample = running ? bpm / 60.0 / sr : 0.0;
    const double startBeat = haveTransport ? hostBeat : freeBeat;

    // ---- layer settings for this block ----
    struct LayerState
    {
        bool active; int fx, mode, steps, retrig, filterMode, flavour;
        double spb, swing;
        float a, b, c, depth;
    };
    LayerState ls[Shape::numLayers];
    for (int l = 0; l < Shape::numLayers; ++l)
    {
        auto& s = ls[l];
        s.active = pUsed[l]->load() > 0.5f && pOn[l]->load() > 0.5f;
        s.fx     = juce::jlimit (0, numFxTypes - 1, (int) std::round (pFx[l]->load()));
        s.mode   = (int) std::round (pMode[l]->load());
        s.steps  = gridSteps ((int) std::round (pGrid[l]->load()));
        s.retrig = (int) std::round (pRetrig[l]->load());
        s.spb    = kStepsPerBeat[juce::jlimit (0, 7, (int) std::round (pRate[l]->load()))];
        s.swing  = (double) pSwing[l]->load() * 0.75;
        s.a      = pA[l]->load();
        s.b      = pB[l]->load();
        s.c      = pC[l]->load();
        s.depth  = pDepth[l]->load();
        s.filterMode = juce::roundToInt (s.a * 3.0f);
        s.flavour    = juce::roundToInt (s.a * 2.0f);

        auto& r = layers[l];
        r.mixSm.setTargetValue (s.active ? pLayerMix[l]->load() : 0.0f);
        r.outSm.setTargetValue (s.active ? pLayerOut[l]->load() : 1.0f);

        if (s.active && s.fx == fxReverb)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.3f + 0.69f * s.a;
            rp.damping = s.c;
            rp.wetLevel = 0.5f;
            rp.dryLevel = 0.0f;
            rp.width = 1.0f;
            rp.freezeMode = 0.0f;
            r.reverb.setParameters (rp);
        }
    }

    const float grainScale = 0.001f * (float) sr;

    const float smoothMs = pSmooth->load();
    const float coef = smoothMs < 0.05f ? 1.0f : 1.0f - std::exp (-1.0f / (0.001f * smoothMs * (float) sr));
    mixSm.setTargetValue (pMix->load());
    outSm.setTargetValue (pOut->load());

    // ---- sidechain detector set-up ----
    const bool scOn = pScOn->load() > 0.5f;
    const bool external = (int) std::round (pScSrc->load()) == 0;
    const bool listen = pListen->load() > 0.5f;
    const float* scL = nullptr;
    const float* scR = nullptr;
    juce::AudioBuffer<float> scBus;
    if (auto* bus = getBus (true, 1))
        if (bus->isEnabled())
        {
            scBus = getBusBuffer (buffer, true, 1);
            if (scBus.getNumChannels() > 0)
            {
                scL = scBus.getReadPointer (0);
                scR = scBus.getNumChannels() > 1 ? scBus.getReadPointer (1) : scL;
            }
        }
    uiScConnected.store (scL != nullptr);

    const int scType = (int) std::round (pScType->load());
    const bool sc24 = (int) std::round (pScSlope->load()) == 1;
    const bool scRmsMode = (int) std::round (pScTrig->load()) == 1;
    if (scType > 0)
    {
        const float f = kScFreqs[juce::jlimit (0, 7, (int) std::round (pScFreq->load()))];
        scFilter1.set (f, 0.707f, sr);
        scFilter2.set (f, 0.707f, sr);
    }
    const int scMode = scType == 2 ? 0 : 1;
    const float nyq = (float) sr * 0.45f;
    crossLow.setCutoffFrequency (juce::jmin (pXLow->load(), nyq));
    crossHigh.setCutoffFrequency (juce::jmin (pXHigh->load(), nyq));

    bool solo = false;
    bool bandUse[3];
    for (int b = 0; b < 3; ++b) solo = solo || pBandSolo[b]->load() > 0.5f;
    for (int b = 0; b < 3; ++b) bandUse[b] = solo ? pBandSolo[b]->load() > 0.5f : pBandOn[b]->load() > 0.5f;

    const float sensGain = juce::Decibels::decibelsToGain ((pScSens->load() - 0.5f) * 48.0f);
    const float envAttack  = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
    const float envRelease = 1.0f - std::exp (-1.0f / (0.150f * (float) sr));
    const float rmsCoef    = 1.0f - std::exp (-1.0f / (0.030f * (float) sr));
    const int holdoffSamples = (int) (0.05 * sr);

    // If the editor is swapping in a new pattern we simply hold the last shape values for this block.
    juce::SpinLock::ScopedTryLockType tryLock (rtLock);
    const bool haveShapes = tryLock.isLocked();

    auto* chL = mainBus.getWritePointer (0);
    auto* chR = mainBus.getWritePointer (1);
    float peakL = 0.0f, peakR = 0.0f, scPeak = 0.0f, scaled = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const double beat = startBeat + (double) i * beatsPerSample;
        const float inL = chL[i], inR = chR[i];

        // ---- sidechain detector ----
        float scSignal = 0.0f;
        bool triggered = false;
        if (scOn)
        {
            float x = external ? (scL != nullptr ? 0.5f * (scL[i] + scR[i]) : 0.0f) : 0.5f * (inL + inR);
            scPeak = juce::jmax (scPeak, std::abs (x));

            if (scType > 0)
            {
                x = scFilter1.process (0, x, scMode);
                if (sc24) x = scFilter2.process (0, x, scMode);
            }

            float low = 0.0f, rest = 0.0f, mid = 0.0f, high = 0.0f;
            crossLow.processSample (0, x, low, rest);
            crossHigh.processSample (0, rest, mid, high);
            if (bandUse[0]) scSignal += low;
            if (bandUse[1]) scSignal += mid;
            if (bandUse[2]) scSignal += high;

            float level = std::abs (scSignal);
            if (scRmsMode)
            {
                scRms += rmsCoef * (scSignal * scSignal - scRms);
                level = std::sqrt (juce::jmax (0.0f, scRms));
            }
            scEnv += (level > scEnv ? envAttack : envRelease) * (level - scEnv);
            scaled = juce::jmin (1.0f, scEnv * sensGain);

            if (trigHoldoff > 0) --trigHoldoff;
            if (trigArmed && scaled > 0.5f && trigHoldoff == 0)
            {
                triggered = true;
                trigArmed = false;
                trigHoldoff = holdoffSamples;
            }
            else if (! trigArmed && scaled < 0.25f)
            {
                trigArmed = true;
            }
        }
        else
        {
            scEnv = 0.0f;
            scaled = 0.0f;
        }

        // ---- layers, in series ----
        float wl = inL, wr = inR;

        for (int L = 0; L < Shape::numLayers; ++L)
        {
            auto& r = layers[L];
            const auto& s = ls[L];

            const float layerMix = r.mixSm.getNextValue();
            const float layerOut = r.outSm.getNextValue();
            if (! s.active && layerMix <= 0.0f && juce::approximatelyEqual (layerOut, 1.0f))
                continue;

            float value = r.last;
            if (s.mode == 1)
            {
                value = scaled;                                 // follow the sidechain
            }
            else
            {
                if (s.retrig == 1 && triggered)      r.beatOffset = beat;
                else if (s.retrig == 2)              r.beatOffset = std::floor (beat / lastBeatsPerBar) * lastBeatsPerBar;
                else if (s.retrig == 3)              r.beatOffset = std::floor (beat / (2.0 * lastBeatsPerBar)) * 2.0 * lastBeatsPerBar;
                else if (s.retrig == 0)              r.beatOffset = 0.0;

                const auto sp = Shape::stepFromPosition ((beat - r.beatOffset) * s.spb, s.steps, s.swing);

                if (haveShapes)
                {
                    const auto& q = *rt[L];
                    if (q.muted[sp.step])
                    {
                        value = s.fx == fxVolume ? 1.0f : (s.fx == fxPan ? 0.5f : 0.0f);    // a muted step has no effect
                    }
                    else
                    {
                        const int head = q.head[sp.step];
                        const int len = juce::jmax (1, juce::jmin (q.nextStart[head], s.steps) - head);
                        const float u = juce::jlimit (0.0f, 1.0f, ((float) (sp.step - head) + sp.t) / (float) len);
                        const float fi = u * (float) (Shape::lutSize - 1);
                        const int i0 = (int) fi;
                        const int i1 = juce::jmin (i0 + 1, Shape::lutSize - 1);
                        value = q.lut[head][i0] + (q.lut[head][i1] - q.lut[head][i0]) * (fi - (float) i0);
                    }
                    r.last = value;
                }

                if (i == n - 1) { uiStep[L].store (s.active ? sp.step : -1); uiT[L].store (sp.t); }
            }

            r.sm += coef * (value - r.sm);
            const float v = r.sm;
            const float modded = juce::jlimit (0.0f, 1.0f, s.b + s.depth * v);

            float fl = wl, fr = wr;
            switch (s.fx)
            {
                case fxFilter:
                    if (r.ctrl-- <= 0)
                    {
                        r.ctrl = 7;
                        r.svf.set (cutoffFromNorm (modded), 0.5f + 14.0f * s.c * s.c, sr);
                    }
                    fl = r.svf.process (0, wl, s.filterMode);
                    fr = r.svf.process (1, wr, s.filterMode);
                    break;

                case fxCrush:
                    r.crush.process (fl, fr, holdFromNorm (modded), bitsFromNorm (s.c), driveFromNorm (s.a));
                    break;

                case fxTexture:
                    fl = r.tex.process (0, wl, s.flavour, modded, s.c);
                    fr = r.tex.process (1, wr, s.flavour, modded, s.c);
                    break;

                case fxVolume:
                {
                    const float d = std::abs (s.depth);
                    const float g = s.b * (s.depth >= 0.0f ? 1.0f - d * (1.0f - v) : 1.0f - d * v);
                    fl = wl * g;
                    fr = wr * g;
                    break;
                }

                case fxPitch:
                    r.pitch.process (fl, fr, std::pow (2.0f, semisFromNorm (modded) / 12.0f),
                                     grainMsFromNorm (s.a) * grainScale, 0.7f * s.c);
                    break;

                case fxDrive:
                    fl = r.shaper.drive (0, wl, 36.0f * modded, s.a, s.c);
                    fr = r.shaper.drive (1, wr, 36.0f * modded, s.a, s.c);
                    break;

                case fxDistortion:
                {
                    const int kind = juce::roundToInt (s.a * 2.0f);
                    fl = r.shaper.distort (0, wl, 40.0f * modded, s.c, kind);
                    fr = r.shaper.distort (1, wr, 40.0f * modded, s.c, kind);
                    break;
                }

                case fxNoise:
                {
                    // Noise fades out while the transport is stopped, so a stopped project stays quiet
                    r.noiseGate += 0.002f * ((running ? 1.0f : 0.0f) - r.noiseGate);
                    const int colour = juce::roundToInt (s.a * 2.0f);
                    const float level = modded * modded * r.noiseGate * 0.5f;
                    fl = wl + r.noise.next (0, colour, s.c) * level;
                    fr = wr + r.noise.next (1, colour, s.c) * level;
                    break;
                }

                case fxReverb:
                {
                    float rl = wl, rr = wr;
                    r.reverb.processStereo (&rl, &rr, 1);
                    fl = wl + rl * modded;
                    fr = wr + rr * modded;
                    break;
                }

                case fxWidth:
                    r.widener.process (fl, fr, modded, s.a);
                    break;

                case fxDelay:
                {
                    const double beats = kDelayBeats[juce::jlimit (0, 5, juce::roundToInt (s.a * 5.0f))];
                    float el = 0.0f, er = 0.0f;
                    r.echo.process (wl, wr, (float) (beats * 60.0 / bpm * sr), 0.9f * s.c, el, er);
                    fl = wl + el * modded;
                    fr = wr + er * modded;
                    break;
                }

                case fxChorus:
                {
                    float cl = 0.0f, cr = 0.0f;
                    r.chorus.process (wl, wr, chorusHzFromNorm (s.a), s.c, cl, cr);
                    fl = wl + (cl - wl) * modded * 0.5f;
                    fr = wr + (cr - wr) * modded * 0.5f;
                    break;
                }

                default:    // pan
                {
                    const float pos = juce::jlimit (-1.0f, 1.0f, s.b * 2.0f - 1.0f + s.depth * (2.0f * v - 1.0f));
                    const float ang = (pos + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                    fl = wl * juce::jmin (1.0f, std::cos (ang) * juce::MathConstants<float>::sqrt2);
                    fr = wr * juce::jmin (1.0f, std::sin (ang) * juce::MathConstants<float>::sqrt2);
                    break;
                }
            }

            wl = (wl + (fl - wl) * layerMix) * layerOut;
            wr = (wr + (fr - wr) * layerMix) * layerOut;
        }

        const float mix = mixSm.getNextValue();
        const float og  = outSm.getNextValue();
        float outL = (inL + (wl - inL) * mix) * og;
        float outR = (inR + (wr - inR) * mix) * og;

        if (listen) { outL = scSignal; outR = scSignal; }

        chL[i] = outL;
        chR[i] = outR;
        peakL = juce::jmax (peakL, std::abs (outL));
        peakR = juce::jmax (peakR, std::abs (outR));
    }

    for (int L = 0; L < Shape::numLayers; ++L)
        if (! ls[L].active || ls[L].mode == 1) uiStep[L].store (-1);

    uiEnv.store (scaled);
    uiScLevel.store (scPeak);
    uiOutL.store (peakL);
    uiOutR.store (peakR);

    freeBeat = startBeat + (double) n * beatsPerSample;
}

// ======================= editor / state =======================
juce::AudioProcessorEditor* MotionProcessor::createEditor()
{
    return new MotionEditor (*this);
}

juce::ValueTree MotionProcessor::buildStateTree()
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", currentPresetName, nullptr);

    juce::ValueTree curves ("CURVES");
    for (int l = 0; l < Shape::numLayers; ++l)
    {
        const auto p = getPattern (l);
        for (int s = 0; s < Shape::maxSteps; ++s)
        {
            juce::ValueTree node ("S");
            node.setProperty ("l", l, nullptr);
            node.setProperty ("s", s, nullptr);
            node.setProperty ("pts", serialisePoints (p.pts[(size_t) s]), nullptr);
            node.setProperty ("start", p.start[(size_t) s], nullptr);
            node.setProperty ("mute", p.muted[(size_t) s], nullptr);
            curves.appendChild (node, nullptr);
        }
    }
    state.appendChild (curves, nullptr);
    return state;
}

void MotionProcessor::applyStateTree (juce::ValueTree tree)
{
    auto curves = tree.getChildWithName ("CURVES");
    if (curves.isValid())
    {
        Shape::Pattern fresh[Shape::numLayers];
        for (auto node : curves)
        {
            const int l = node.getProperty ("l");
            const int s = node.getProperty ("s");
            if (l < 0 || l >= Shape::numLayers || s < 0 || s >= Shape::maxSteps) continue;
            fresh[l].pts[(size_t) s] = parsePoints (node.getProperty ("pts").toString());
            fresh[l].start[(size_t) s] = node.getProperty ("start", true);
            fresh[l].muted[(size_t) s] = node.getProperty ("mute", false);
        }
        for (int l = 0; l < Shape::numLayers; ++l)
            setPattern (l, fresh[l]);
        tree.removeChild (curves, nullptr);
    }

    currentPresetName = tree.getProperty ("presetName", "Init").toString();
    apvts.replaceState (tree);
}

void MotionProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = buildStateTree().createXml())
        copyXmlToBinary (*xml, dest);
}

void MotionProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            applyStateTree (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MotionProcessor();
}
