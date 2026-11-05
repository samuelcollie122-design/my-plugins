#pragma once
#include <JuceHeader.h>
#include "OptoCore.h"

class OptoProcessor : public juce::AudioProcessor
{
public:
    OptoProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OptoComp"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- presets (message thread) ----
    static const juce::StringArray& factoryPresetNames();
    void loadFactoryPreset (int index);
    juce::Array<juce::File> userPresetFiles() const;
    juce::File userPresetFolder() const;
    bool saveUserPreset (const juce::File&);
    bool loadUserPreset (const juce::File&);
    juce::String getPresetName() const { return setupName[currentSetup]; }

    // ---- two setups to compare (message thread) ----
    int getCurrentSetup() const { return currentSetup; }    // 0 = A, 1 = B
    void toggleSetup();
    void copyCurrentSetupToOther();

    // ---- live values for the meter ----
    std::atomic<float> uiReductionDb { 0.0f }, uiInputLevel { 0.0f }, uiOutputLevel { 0.0f };

    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParam (const juce::String& id, float realValue);
    void applyTree (const juce::ValueTree&);

    Opto::Compressor comp;
    juce::SmoothedValue<float> gainSm, trimSm, mixSm;

    int currentSetup = 0;
    juce::String setupName[2] { "Start Me Up", "Start Me Up" };
    juce::ValueTree otherSetup;     // the setup that is not loaded right now

    std::atomic<float>* pPeak = nullptr;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pLimit = nullptr;
    std::atomic<float>* pHiFreq = nullptr;
    std::atomic<float>* pAnalog = nullptr;
    std::atomic<float>* pAuto = nullptr;
    std::atomic<float>* pTrim = nullptr;
    std::atomic<float>* pMix = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptoProcessor)
};
