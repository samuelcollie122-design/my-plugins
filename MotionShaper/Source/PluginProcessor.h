#pragma once
#include <JuceHeader.h>
#include "ShapeCore.h"
#include "FxCore.h"

class MotionProcessor : public juce::AudioProcessor
{
public:
    // New effects go at the END of this list and of fxNames(), so saved projects keep their effect.
    enum FxType { fxFilter = 0, fxCrush, fxTexture, fxVolume, fxPan,
                  fxPitch, fxDrive, fxDistortion, fxNoise, fxReverb, fxWidth, fxDelay, fxChorus, numFxTypes };

    MotionProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MotionShaper"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }    // reverb and delay layers ring on
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- parameter ids, choice lists and knob text shared with the editor ----
    static juce::String paramId (int layer, const juce::String& name);
    static const juce::StringArray& fxNames();
    static const juce::StringArray& rateNames();
    static const juce::StringArray& gridNames();
    static const juce::StringArray& retrigNames();
    static const juce::StringArray& scFilterFreqNames();
    static int gridSteps (int gridIndex);
    static juce::String knobCaption (int fx, int knob);     // empty = this effect does not use that knob
    static juce::String knobText (int fx, int knob, float value);

    // ---- layers (message thread) ----
    bool layerUsed (int l) const   { return pUsed[l]->load() > 0.5f; }
    bool layerOn (int l) const     { return pOn[l]->load() > 0.5f; }
    int  layerFx (int l) const     { return juce::jlimit (0, numFxTypes - 1, (int) std::round (pFx[l]->load())); }
    int  layerMode (int l) const   { return (int) std::round (pMode[l]->load()); }
    int  layerSteps (int l) const  { return gridSteps ((int) std::round (pGrid[l]->load())); }
    int  numUsedLayers() const;
    int  addLayer (int fx);                 // returns the slot, or -1 when all are in use
    void removeLayer (int layer);
    int  duplicateLayer (int layer);        // returns the slot, or -1 when all are in use
    void setLayerFx (int layer, int fx);    // also sets that effect's default knob values

    // ---- patterns (message thread) ----
    Shape::Pattern getPattern (int layer) const;
    void setPattern (int layer, const Shape::Pattern&);
    void pushPatternUndo (int layer, const Shape::Pattern& before);   // records the change made since "before"
    Shape::Points getSegmentPoints (int layer, int step) const;
    void setSegmentPoints (int layer, int step, Shape::Points);

    // ---- presets (message thread) ----
    static const juce::StringArray& factoryPresetNames();
    void loadFactoryPreset (int index);
    juce::Array<juce::File> userPresetFiles() const;
    juce::File userPresetFolder() const;
    bool saveUserPreset (const juce::File&);
    bool loadUserPreset (const juce::File&);
    juce::String currentPresetName = "Init";

    // ---- live display values written by the audio thread ----
    std::atomic<int>   uiStep[Shape::numLayers];
    std::atomic<float> uiT[Shape::numLayers];
    std::atomic<float> uiEnv { 0.0f }, uiScLevel { 0.0f }, uiOutL { 0.0f }, uiOutR { 0.0f };
    std::atomic<float> uiBpm { 120.0f };
    std::atomic<int>   uiNum { 4 }, uiDen { 4 };
    std::atomic<bool>  uiHostPlaying { false }, uiScConnected { false };
    std::atomic<unsigned int> shapeRevision { 0 };

    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;

private:
    struct RtPattern
    {
        float lut[Shape::maxSteps][Shape::lutSize];
        int head[Shape::maxSteps];
        int nextStart[Shape::maxSteps];
        bool muted[Shape::maxSteps];
    };

    struct LayerRt
    {
        Fx::Svf svf;
        Fx::Crusher crush;
        Fx::Texture tex;
        Fx::PitchShifter pitch;
        Fx::Shaper shaper;
        Fx::NoiseGen noise;
        Fx::Widener widener;
        Fx::Echo echo;
        Fx::Chorus chorus;
        juce::Reverb reverb;
        juce::SmoothedValue<float> mixSm, outSm;
        float sm = 0.0f, last = 0.0f, noiseGate = 0.0f;
        double beatOffset = 0.0;
        int ctrl = 0;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::unique_ptr<RtPattern> buildRt (const Shape::Pattern&) const;
    void publishPattern (int layer);
    juce::ValueTree buildStateTree();
    void applyStateTree (juce::ValueTree);
    void setParam (const juce::String& id, float realValue);
    void resetAllParams();
    void setupLayer (int layer, int fx, int rate, int grid, float a, float b, float c, float depth, float mix);
    void setSegments (int layer, const std::vector<int>& lengths, const std::vector<int>& presets);

    Shape::Pattern patterns[Shape::numLayers];         // message-thread copy
    mutable juce::CriticalSection patternLock;         // never taken by the audio thread
    std::unique_ptr<RtPattern> rt[Shape::numLayers];   // what the audio thread reads
    juce::SpinLock rtLock;

    std::atomic<float>* pUsed[Shape::numLayers];
    std::atomic<float>* pOn[Shape::numLayers];
    std::atomic<float>* pFx[Shape::numLayers];
    std::atomic<float>* pMode[Shape::numLayers];
    std::atomic<float>* pGrid[Shape::numLayers];
    std::atomic<float>* pRate[Shape::numLayers];
    std::atomic<float>* pSwing[Shape::numLayers];
    std::atomic<float>* pRetrig[Shape::numLayers];
    std::atomic<float>* pA[Shape::numLayers];
    std::atomic<float>* pB[Shape::numLayers];
    std::atomic<float>* pC[Shape::numLayers];
    std::atomic<float>* pDepth[Shape::numLayers];
    std::atomic<float>* pLayerOut[Shape::numLayers];
    std::atomic<float>* pLayerMix[Shape::numLayers];

    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOut = nullptr;
    std::atomic<float>* pSmooth = nullptr;
    std::atomic<float>* pScOn = nullptr;
    std::atomic<float>* pScSrc = nullptr;
    std::atomic<float>* pScType = nullptr;
    std::atomic<float>* pScFreq = nullptr;
    std::atomic<float>* pScSlope = nullptr;
    std::atomic<float>* pScTrig = nullptr;
    std::atomic<float>* pScSens = nullptr;
    std::atomic<float>* pListen = nullptr;
    std::atomic<float>* pXLow = nullptr;
    std::atomic<float>* pXHigh = nullptr;
    std::atomic<float>* pBandOn[3];
    std::atomic<float>* pBandSolo[3];

    LayerRt layers[Shape::numLayers];
    juce::SmoothedValue<float> mixSm, outSm;

    Fx::Svf scFilter1, scFilter2;
    juce::dsp::LinkwitzRileyFilter<float> crossLow, crossHigh;
    float scEnv = 0.0f, scRms = 0.0f;
    bool trigArmed = true;
    int trigHoldoff = 0;

    double sr = 44100.0;
    double lastBpm = 120.0;
    double lastBeatsPerBar = 4.0;
    double freeBeat = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MotionProcessor)
};
