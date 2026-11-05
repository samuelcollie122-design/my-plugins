#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EditorWidgets.h"

class MotionEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MotionEditor (MotionProcessor&);
    ~MotionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void selectLayer (int layer);

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void rebindLayer();
    void selectStep (int step);
    void showLayerMenu (int layer, juce::Component* target);
    void showAddMenu (juce::Component* target);
    void showStepMenu (int step, juce::Component* target);
    void showPresetMenu();
    void showMainMenu();
    void stepPreset (int delta);
    void loadPresetByIndex (int index);
    void savePresetAs();
    void afterPresetChange();
    void editPattern (const std::function<void (Shape::Pattern&)>&);
    void setUiChoice (const juce::Identifier&, int value);

    int currentStep() const { return selectedStep[curLayer]; }

    Ui::LookAndFeel laf;
    MotionProcessor& proc;

    Ui::LayerList layerList;
    Ui::ShapeView shapeView;
    Ui::StepLanes stepLanes;

    int curLayer = 0;
    int boundFx = -1;
    int selectedStep[Shape::numLayers] {};

    // top bar
    juce::TextButton presetName, presetSave { "save" }, scButton { "SC" };
    Ui::IconButton presetPrev, presetNext, menuButton;
    juce::ComboBox scSourceTop;
    juce::Slider globalMix;

    // centre header
    juce::TextButton shapeBtn { "shape" }, followBtn { "follow" };
    juce::ComboBox snapBox, viewGridBox;
    Ui::IconButton focusBtn;

    // sequencer panel
    juce::ComboBox gridBox, rateBox, retrigBox;
    juce::Slider swingSlider;
    juce::TextButton randomBtn { "random" };

    // right panel
    Ui::IconButton powerBtn;
    Ui::Knob knobA, knobB, knobC, knobOut, knobMix;
    juce::ComboBox scSource, scType, scFreq, scSlope, scTrig;
    juce::Slider scSens, globalOut;
    juce::TextButton listenBtn { "listen" };
    std::unique_ptr<Ui::CrossoverBar> crossover;
    juce::TextButton bandSolo[3];
    std::unique_ptr<Ui::IconButton> bandPower[3];

    // bottom bar
    Ui::IconButton undoBtn, redoBtn;
    juce::TooltipWindow tooltips { this, 500 };

    juce::Rectangle<int> topBar, leftPanel, seqPanel, centerPanel, rightPanel;
    juce::Rectangle<int> meterRect, bpmRect, sigRect, ledRect, titleRect;

    Shape::Pattern clipboard;
    bool hasClipboard = false;
    std::unique_ptr<juce::FileChooser> chooser;
    float meterL = 0.0f, meterR = 0.0f;

    // attachments last: destroyed before the widgets they point at
    std::unique_ptr<ComboAtt> scSourceTopAtt, scSourceAtt, scTypeAtt, scFreqAtt, scSlopeAtt, scTrigAtt;
    std::unique_ptr<SliderAtt> globalMixAtt, globalOutAtt, scSensAtt;
    std::unique_ptr<ButtonAtt> scOnAtt, listenAtt;
    std::unique_ptr<ButtonAtt> bandSoloAtt[3], bandPowerAtt[3];

    std::unique_ptr<ComboAtt> gridAtt, rateAtt, retrigAtt;
    std::unique_ptr<SliderAtt> swingAtt, aAtt, bAtt, cAtt, depthAtt, layerOutAtt, layerMixAtt;
    std::unique_ptr<ButtonAtt> powerAtt;
    std::unique_ptr<juce::ParameterAttachment> modeAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MotionEditor)
};
