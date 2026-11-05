#pragma once
#include <JuceHeader.h>

// Audio passes through untouched. This plugin only provides the control surface.
class SpaceMasterProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SpaceMasterProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        publishRoom();
        startTimerHz (5);      // keeps the tracks following the room even with this window closed
    }

    ~SpaceMasterProcessor() override { stopTimer(); }

    void publishRoom();

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
            && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SpaceMaster"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // The view settings are remembered with the project
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::MemoryOutputStream out (dest, false);
        out.writeBool (view3D);
        out.writeFloat (viewYaw);
        out.writeFloat (roomSize);
        out.writeFloat (roomAmount);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (size < 5) return;
        juce::MemoryInputStream in (data, (size_t) size, false);
        view3D = in.readBool();
        viewYaw = in.readFloat();
        if (size >= 13)        // projects saved before the room existed stop here
        {
            roomSize = juce::jlimit (0.0f, 1.0f, in.readFloat());
            roomAmount = juce::jlimit (0.0f, 1.0f, in.readFloat());
        }
        publishRoom();
    }

    bool view3D = false;       // master field view: 2D top-down or 3D room
    float viewYaw = 0.6f;
    float roomSize = 0.4f, roomAmount = 0.25f;     // shared room, heard in 3D only

private:
    void timerCallback() override { publishRoom(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceMasterProcessor)
};
