#pragma once
#include <JuceHeader.h>
#include "HrtfEngine.h"

class SpaceTrackProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SpaceTrackProcessor();
    ~SpaceTrackProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SpaceTrack"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.5; }    // the shared room rings on in 3D
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    int getSlotIndex() const { return slotIndex; }
    juce::Colour getTrackColour() const;

    // Shared room (3D only). Normally set from SpaceMaster, and remembered with the project.
    void setRoom (float size, float amount);

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void timerCallback() override;
    void claimSlot();
    void beginAllGestures();
    void endAllGestures();

    uint64_t myId = 0;
    int slotIndex = -1;
    uint32_t lastCmdSeq = 0, lastNameSeq = 0, lastModeSeq = 0, lastColorSeq = 0;
    bool gestureOpen = false;

    juce::SmoothedValue<float> xSmooth, ySmooth, zSmooth, modeSmooth;

    juce::dsp::StateVariableTPTFilter<float> lowpass;      // 2D path (stereo)
    juce::dsp::StateVariableTPTFilter<float> elevDownLP;   // 3D path: below the lowest measured direction

    Hrtf::Voice hrtfVoice;
    Hrtf::EarlyRoom earlyRoom;
    juce::Reverb lateRoom;
    std::atomic<float> roomSize { 0.4f }, roomAmount { 0.25f };
    float lastRoomSize = -1.0f, roomGain = 0.0f;
    int controlCounter = 0;
    bool path3DActive = false;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceTrackProcessor)
};
