#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Gain knob: 0..100, with 40 meaning no change
    float gainDbFromKnob (float v) { return (v - 40.0f) * 0.5f; }

    struct FactoryPreset
    {
        const char* name;
        float peak, gain;
        bool limit;
        float hiFreq, mix, trim;
    };

    // Starting points of my own. The names follow the list the user asked for; the settings are not copied from any product.
    const FactoryPreset kPresets[] = {
        //  name               peak   gain   limit  hifreq mix    trim
        { "Start Me Up",       0.50f, 40.f,  false, 0.00f, 1.00f, 0.f },
        { "Full Reset",        0.00f, 40.f,  false, 0.00f, 1.00f, 0.f },
        { "PA Default",        0.45f, 44.f,  false, 0.00f, 1.00f, 0.f },
        { "Vox",               0.55f, 50.f,  false, 0.15f, 1.00f, 0.f },
        { "Sibilant Vox",      0.55f, 50.f,  false, 0.75f, 1.00f, 0.f },
        { "Piano",             0.42f, 46.f,  false, 0.10f, 0.80f, 0.f },
        { "Acoustic Guitar",   0.48f, 48.f,  false, 0.30f, 0.90f, 0.f },
        { "Electric Guitar",   0.58f, 52.f,  false, 0.00f, 1.00f, 0.f },
        { "Drum Group",        0.50f, 46.f,  false, 0.00f, 0.60f, 0.f },
        { "Hard Drum Group",   0.70f, 54.f,  true,  0.00f, 0.70f, 0.f },
        { "Bass",              0.62f, 54.f,  false, 0.00f, 1.00f, 0.f },
        { "Strings",           0.36f, 44.f,  false, 0.10f, 0.85f, 0.f },
    };
}

const juce::StringArray& OptoProcessor::factoryPresetNames()
{
    static const juce::StringArray names = []
    {
        juce::StringArray n;
        for (const auto& p : kPresets) n.add (p.name);
        return n;
    }();
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout OptoProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() / 100.0f; });

    const NormalisableRange<float> unit (0.0f, 1.0f, 0.001f);

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "peak", 1 }, "Peak Reduction", unit, 0.5f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "limit", 1 }, "Limit", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "hifreq", 1 }, "Hi Freq", unit, 0.0f, percent));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "analog", 1 }, "Analog", StringArray { "Off", "50Hz", "60Hz" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "automakeup", 1 }, "Auto Makeup", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "trim", 1 }, "Trim", NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", unit, 1.0f, percent));
    return layout;
}

OptoProcessor::OptoProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "STATE", createLayout())
{
    pPeak = apvts.getRawParameterValue ("peak");
    pGain = apvts.getRawParameterValue ("gain");
    pLimit = apvts.getRawParameterValue ("limit");
    pHiFreq = apvts.getRawParameterValue ("hifreq");
    pAnalog = apvts.getRawParameterValue ("analog");
    pAuto = apvts.getRawParameterValue ("automakeup");
    pTrim = apvts.getRawParameterValue ("trim");
    pMix = apvts.getRawParameterValue ("mix");

    otherSetup = apvts.copyState();
}

// ======================= presets =======================
void OptoProcessor::setParam (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

void OptoProcessor::loadFactoryPreset (int index)
{
    const auto& p = kPresets[juce::jlimit (0, factoryPresetNames().size() - 1, index)];
    setParam ("peak", p.peak);
    setParam ("gain", p.gain);
    setParam ("limit", p.limit ? 1.0f : 0.0f);
    setParam ("hifreq", p.hiFreq);
    setParam ("analog", 0.0f);
    setParam ("automakeup", 0.0f);
    setParam ("trim", p.trim);
    setParam ("mix", p.mix);
    setupName[currentSetup] = p.name;
    undo.clearUndoHistory();
}

juce::File OptoProcessor::userPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OptoComp").getChildFile ("Presets");
}

juce::Array<juce::File> OptoProcessor::userPresetFiles() const
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.optopreset");
    files.sort();
    return files;
}

bool OptoProcessor::saveUserPreset (const juce::File& file)
{
    setupName[currentSetup] = file.getFileNameWithoutExtension();
    file.getParentDirectory().createDirectory();
    if (auto xml = apvts.copyState().createXml())
        return xml->writeTo (file);
    return false;
}

bool OptoProcessor::loadUserPreset (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
        if (xml->hasTagName (apvts.state.getType()))
        {
            applyTree (juce::ValueTree::fromXml (*xml));
            setupName[currentSetup] = file.getFileNameWithoutExtension();
            undo.clearUndoHistory();
            return true;
        }
    return false;
}

// Only the sound settings change. What the meter is showing stays as it is.
void OptoProcessor::applyTree (const juce::ValueTree& tree)
{
    const auto meter = apvts.state.getProperty ("meterMode", 1);
    apvts.replaceState (tree.createCopy());
    apvts.state.setProperty ("meterMode", meter, nullptr);
}

void OptoProcessor::toggleSetup()
{
    const auto current = apvts.copyState();
    applyTree (otherSetup);
    otherSetup = current;
    currentSetup = 1 - currentSetup;
    undo.clearUndoHistory();
}

void OptoProcessor::copyCurrentSetupToOther()
{
    otherSetup = apvts.copyState();
    setupName[1 - currentSetup] = setupName[currentSetup];
}

// ======================= audio =======================
bool OptoProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void OptoProcessor::prepareToPlay (double sampleRate, int)
{
    comp.prepare (sampleRate);
    gainSm.reset (sampleRate, 0.02);
    trimSm.reset (sampleRate, 0.02);
    mixSm.reset (sampleRate, 0.02);
    gainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDbFromKnob (pGain->load())));
    trimSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pTrim->load()));
    mixSm.setCurrentAndTargetValue (pMix->load());
}

void OptoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0) return;

    gainSm.setTargetValue (juce::Decibels::decibelsToGain (gainDbFromKnob (pGain->load())));
    trimSm.setTargetValue (juce::Decibels::decibelsToGain (pTrim->load()));
    mixSm.setTargetValue (pMix->load());

    const float peak = pPeak->load();
    const bool limit = pLimit->load() > 0.5f;
    const float hiFreq = pHiFreq->load();
    const int analog = (int) std::round (pAnalog->load());
    const bool autoMakeup = pAuto->load() > 0.5f;

    auto* chL = buffer.getWritePointer (0);
    auto* chR = buffer.getWritePointer (1);
    float inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float dryL = chL[i], dryR = chR[i];
        inPeak = juce::jmax (inPeak, std::abs (dryL), std::abs (dryR));

        float l = dryL, r = dryR;
        comp.process (l, r, peak, limit, hiFreq, analog, autoMakeup);

        const float g = gainSm.getNextValue();
        const float mix = mixSm.getNextValue();
        const float trim = trimSm.getNextValue();
        chL[i] = (dryL + (l * g - dryL) * mix) * trim;
        chR[i] = (dryR + (r * g - dryR) * mix) * trim;
        outPeak = juce::jmax (outPeak, std::abs (chL[i]), std::abs (chR[i]));
    }

    uiReductionDb.store (-juce::Decibels::gainToDecibels (comp.getGain(), -60.0f));
    uiInputLevel.store (inPeak);
    uiOutputLevel.store (outPeak);
}

// ======================= editor / state =======================
juce::AudioProcessorEditor* OptoProcessor::createEditor()
{
    return new OptoEditor (*this);
}

void OptoProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", setupName[currentSetup], nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void OptoProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            setupName[currentSetup] = tree.getProperty ("presetName", "Start Me Up").toString();
            apvts.replaceState (tree);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OptoProcessor();
}
