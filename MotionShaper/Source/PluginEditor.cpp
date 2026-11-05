#include "PluginEditor.h"

namespace
{
    const juce::StringArray kSnapNames { "off", "1/4", "1/8", "1/16", "1/32" };
    const int kSnapDivs[5] { 0, 4, 8, 16, 32 };
    const juce::StringArray kViewGridNames { "off", "1/4", "1/8", "1/16", "1/32" };

    void disableKeyboardFocus (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        for (auto* child : c.getChildren())
            disableKeyboardFocus (*child);
    }

    void headerText (juce::Graphics& g, const juce::String& s, int x, int y)
    {
        Ui::drawGrip (g, (float) x, (float) y + 4.0f);
        g.setColour (Ui::dim);
        g.setFont (Ui::font (12.0f));
        g.drawText (s, x + 18, y, 180, 18, juce::Justification::centredLeft);
    }

    void smallText (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Colour c = Ui::dim, float size = 12.0f)
    {
        g.setColour (c);
        g.setFont (Ui::font (size));
        g.drawText (s, r, juce::Justification::centredLeft);
    }
}

MotionEditor::MotionEditor (MotionProcessor& p)
    : AudioProcessorEditor (&p), proc (p), layerList (p), shapeView (p), stepLanes (p),
      presetPrev ([] (juce::Graphics& g, juce::Rectangle<float> r) { Ui::drawChevron (g, r, true); }),
      presetNext ([] (juce::Graphics& g, juce::Rectangle<float> r) { Ui::drawChevron (g, r, false); }),
      menuButton (Ui::drawMenuIcon),
      focusBtn (Ui::drawFocusIcon),
      powerBtn (Ui::drawPower),
      undoBtn ([] (juce::Graphics& g, juce::Rectangle<float> r) { Ui::drawCurvedArrow (g, r, true); }),
      redoBtn ([] (juce::Graphics& g, juce::Rectangle<float> r) { Ui::drawCurvedArrow (g, r, false); })
{
    setLookAndFeel (&laf);
    auto& apvts = proc.apvts;

    // ---- list + centre ----
    layerList.onSelect = [this] (int l) { selectLayer (l); };
    layerList.onMenu   = [this] (int l, juce::Component* c) { showLayerMenu (l, c); };
    layerList.onAdd    = [this] (juce::Component* c) { showAddMenu (c); };
    addAndMakeVisible (layerList);

    shapeView.onPickStep = [this] (int s)
    {
        selectStep (s);
        shapeView.focus = true;
        focusBtn.setToggleState (true, juce::dontSendNotification);
        setUiChoice ("uiFocus", 1);
    };
    addAndMakeVisible (shapeView);

    stepLanes.onSelect = [this] (int s) { selectStep (s); };
    stepLanes.onContextMenu = [this] (int s, juce::Component* c) { showStepMenu (s, c); };
    addAndMakeVisible (stepLanes);

    // ---- top bar ----
    presetName.getProperties().set ("flat", true);
    presetName.setButtonText (proc.currentPresetName);
    presetName.setTooltip ("Choose a preset");
    presetName.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetName);
    presetSave.getProperties().set ("round", true);
    presetSave.setTooltip ("Save the current sound as your own preset");
    presetSave.onClick = [this] { savePresetAs(); };
    addAndMakeVisible (presetSave);
    presetPrev.setTooltip ("Previous preset");
    presetNext.setTooltip ("Next preset");
    presetPrev.onClick = [this] { stepPreset (-1); };
    presetNext.onClick = [this] { stepPreset (1); };
    addAndMakeVisible (presetPrev);
    addAndMakeVisible (presetNext);

    scButton.getProperties().set ("flat", true);
    scButton.setClickingTogglesState (true);
    scButton.setTooltip ("Sidechain detection on or off");
    addAndMakeVisible (scButton);
    scOnAtt = std::make_unique<ButtonAtt> (apvts, "sc_on", scButton);

    for (auto* box : { &scSourceTop, &scSource })
    {
        box->addItemList (juce::StringArray { "External", "Internal" }, 1);
        addAndMakeVisible (*box);
    }
    scSourceTop.getProperties().set ("flat", true);
    scSourceTopAtt = std::make_unique<ComboAtt> (apvts, "sc_src", scSourceTop);
    scSourceAtt    = std::make_unique<ComboAtt> (apvts, "sc_src", scSource);

    globalMix.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    globalMix.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    globalMix.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    globalMix.setTooltip ("Overall dry / wet mix");
    addAndMakeVisible (globalMix);
    globalMixAtt = std::make_unique<SliderAtt> (apvts, "mix", globalMix);

    menuButton.offColour = Ui::text;
    menuButton.onClick = [this] { showMainMenu(); };
    addAndMakeVisible (menuButton);

    // ---- centre header ----
    for (auto* b : { &shapeBtn, &followBtn })
    {
        b->getProperties().set ("round", true);
        addAndMakeVisible (*b);
    }
    shapeBtn.onClick  = [this] { if (modeAtt) modeAtt->setValueAsCompleteGesture (0.0f); };
    followBtn.onClick = [this] { if (modeAtt) modeAtt->setValueAsCompleteGesture (1.0f); };

    snapBox.addItemList (kSnapNames, 1);
    viewGridBox.addItemList (kViewGridNames, 1);
    addAndMakeVisible (snapBox);
    addAndMakeVisible (viewGridBox);

    snapBox.setSelectedItemIndex ((int) apvts.state.getProperty ("uiSnap", 3), juce::dontSendNotification);
    viewGridBox.setSelectedItemIndex ((int) apvts.state.getProperty ("uiGrid", 4), juce::dontSendNotification);
    shapeView.snapDiv = kSnapDivs[juce::jlimit (0, 4, snapBox.getSelectedItemIndex())];
    shapeView.gridDiv = kSnapDivs[juce::jlimit (0, 4, viewGridBox.getSelectedItemIndex())];
    shapeView.focus = (int) apvts.state.getProperty ("uiFocus", 1) != 0;

    snapBox.onChange = [this]
    {
        const int i = snapBox.getSelectedItemIndex();
        shapeView.snapDiv = kSnapDivs[juce::jlimit (0, 4, i)];
        setUiChoice ("uiSnap", i);
    };
    viewGridBox.onChange = [this]
    {
        const int i = viewGridBox.getSelectedItemIndex();
        shapeView.gridDiv = kSnapDivs[juce::jlimit (0, 4, i)];
        setUiChoice ("uiGrid", i);
        shapeView.repaint();
    };

    focusBtn.setToggleState (shapeView.focus, juce::dontSendNotification);
    focusBtn.setClickingTogglesState (true);
    focusBtn.setTooltip ("On: edit one segment. Off: see the whole pattern, click a segment to edit it.");
    focusBtn.onClick = [this]
    {
        shapeView.focus = focusBtn.getToggleState();
        setUiChoice ("uiFocus", shapeView.focus ? 1 : 0);
        shapeView.repaint();
    };
    addAndMakeVisible (focusBtn);

    // ---- sequencer panel ----
    gridBox.addItemList (MotionProcessor::gridNames(), 1);
    rateBox.addItemList (MotionProcessor::rateNames(), 1);
    retrigBox.addItemList (MotionProcessor::retrigNames(), 1);
    gridBox.setTooltip ("Pattern length: 1/32 is 32 steps, 1/16 is 16 steps ...");
    rateBox.setTooltip ("How long one step lasts");
    retrigBox.setTooltip ("Restart the pattern on a sidechain hit, or every 1 or 2 bars");
    for (auto* box : { &gridBox, &rateBox, &retrigBox })
        addAndMakeVisible (*box);

    swingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    swingSlider.onValueChange = [this] { repaint (seqPanel); };
    addAndMakeVisible (swingSlider);

    randomBtn.getProperties().set ("round", true);
    randomBtn.setTooltip ("Make a new random pattern for this layer. Undo brings the old one back.");
    randomBtn.onClick = [this]
    {
        const int steps = proc.layerSteps (curLayer);
        editPattern ([steps] (Shape::Pattern& p) { Shape::randomisePattern (p, steps); });
        proc.currentPresetName = "Custom";
        selectStep (0);
    };
    addAndMakeVisible (randomBtn);

    // ---- right panel ----
    powerBtn.setClickingTogglesState (true);
    powerBtn.setTooltip ("Turn this layer on or off");
    addAndMakeVisible (powerBtn);

    for (auto* k : { &knobA, &knobB, &knobC, &knobOut, &knobMix })
        addAndMakeVisible (*k);

    knobOut.caption = "output";
    knobMix.caption = "mix";
    knobOut.format = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
    knobMix.format = knobOut.format;

    scType.addItemList (juce::StringArray { "off", "hp", "lp" }, 1);
    scFreq.addItemList (MotionProcessor::scFilterFreqNames(), 1);
    scSlope.addItemList (juce::StringArray { "12 db", "24 db" }, 1);
    scTrig.addItemList (juce::StringArray { "peak", "rms" }, 1);
    scSource.setTooltip ("External: the track sent to this plugin's sidechain input. Internal: this track's own audio.");
    for (auto* box : { &scType, &scFreq, &scSlope, &scTrig })
        addAndMakeVisible (*box);
    scTypeAtt  = std::make_unique<ComboAtt> (apvts, "sc_type", scType);
    scFreqAtt  = std::make_unique<ComboAtt> (apvts, "sc_freq", scFreq);
    scSlopeAtt = std::make_unique<ComboAtt> (apvts, "sc_slope", scSlope);
    scTrigAtt  = std::make_unique<ComboAtt> (apvts, "sc_trig", scTrig);

    listenBtn.getProperties().set ("round", true);
    listenBtn.setClickingTogglesState (true);
    listenBtn.setTooltip ("Hear the sidechain signal, after its filter and bands");
    addAndMakeVisible (listenBtn);
    listenAtt = std::make_unique<ButtonAtt> (apvts, "sc_listen", listenBtn);

    for (auto* s : { &scSens, &globalOut })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->onValueChange = [this] { repaint (rightPanel); };
        addAndMakeVisible (*s);
    }
    scSensAtt    = std::make_unique<SliderAtt> (apvts, "sc_sens", scSens);
    globalOutAtt = std::make_unique<SliderAtt> (apvts, "out", globalOut);

    crossover = std::make_unique<Ui::CrossoverBar> (*apvts.getParameter ("x_low"), *apvts.getParameter ("x_high"), &proc.undo);
    addAndMakeVisible (*crossover);

    const char* bands[3] = { "low", "mid", "high" };
    const juce::Colour bandColours[3] { juce::Colour (0xfff5a54a), juce::Colour (0xff9ccf7a), juce::Colour (0xff4cc9b0) };
    for (int b = 0; b < 3; ++b)
    {
        bandSolo[b].setButtonText ("S");
        bandSolo[b].setClickingTogglesState (true);
        bandSolo[b].getProperties().set ("round", true);
        bandSolo[b].setTooltip (juce::String ("Use only the ") + bands[b] + " band");
        addAndMakeVisible (bandSolo[b]);
        bandSoloAtt[b] = std::make_unique<ButtonAtt> (apvts, juce::String ("b_solo_") + bands[b], bandSolo[b]);

        bandPower[b] = std::make_unique<Ui::IconButton> (Ui::drawPower);
        bandPower[b]->onColour = bandColours[b];
        bandPower[b]->setClickingTogglesState (true);
        bandPower[b]->setTooltip (juce::String ("Include the ") + bands[b] + " band in detection");
        addAndMakeVisible (*bandPower[b]);
        bandPowerAtt[b] = std::make_unique<ButtonAtt> (apvts, juce::String ("b_on_") + bands[b], *bandPower[b]);
    }

    // ---- bottom bar ----
    undoBtn.offColour = Ui::text;
    redoBtn.offColour = Ui::text;
    undoBtn.setTooltip ("Undo");
    redoBtn.setTooltip ("Redo");
    undoBtn.onClick = [this] { proc.undo.undo(); };
    redoBtn.onClick = [this] { proc.undo.redo(); };
    addAndMakeVisible (undoBtn);
    addAndMakeVisible (redoBtn);

    // Every mouse press inside the window starts one undo step
    addMouseListener (this, true);

    // Nothing in this window should ever hold on to the keyboard: keys belong to the host.
    setSize (1107, 740);
    disableKeyboardFocus (*this);

    int first = 0;
    for (int l = Shape::numLayers - 1; l >= 0; --l)
        if (proc.layerUsed (l)) first = l;
    selectLayer (first);

    startTimerHz (30);
}

MotionEditor::~MotionEditor()
{
    removeMouseListener (this);
    stopTimer();
    setLookAndFeel (nullptr);
}

void MotionEditor::mouseDown (const juce::MouseEvent&)
{
    proc.undo.beginNewTransaction();
}

void MotionEditor::setUiChoice (const juce::Identifier& id, int value)
{
    proc.apvts.state.setProperty (id, value, nullptr);
}

void MotionEditor::selectLayer (int layer)
{
    curLayer = juce::jlimit (0, Shape::numLayers - 1, layer);
    layerList.selected = curLayer;
    laf.accent = Ui::layerColour (curLayer);
    powerBtn.onColour = laf.accent;
    focusBtn.onColour = laf.accent;
    scButton.repaint();
    rebindLayer();
    stepLanes.setLayer (curLayer, currentStep());
    shapeView.setTarget (curLayer, currentStep());
    repaint();
}

void MotionEditor::selectStep (int step)
{
    selectedStep[curLayer] = step;
    stepLanes.setLayer (curLayer, step);
    shapeView.setTarget (curLayer, step);
}

void MotionEditor::rebindLayer()
{
    auto& apvts = proc.apvts;
    const auto id = [this] (const char* name) { return MotionProcessor::paramId (curLayer, name); };
    const int fx = proc.layerFx (curLayer);
    boundFx = fx;

    gridAtt.reset(); rateAtt.reset(); retrigAtt.reset(); swingAtt.reset();
    aAtt.reset(); bAtt.reset(); cAtt.reset(); depthAtt.reset();
    layerOutAtt.reset(); layerMixAtt.reset(); powerAtt.reset(); modeAtt.reset();

    gridAtt   = std::make_unique<ComboAtt> (apvts, id ("grid"), gridBox);
    rateAtt   = std::make_unique<ComboAtt> (apvts, id ("rate"), rateBox);
    retrigAtt = std::make_unique<ComboAtt> (apvts, id ("retrig"), retrigBox);
    swingAtt  = std::make_unique<SliderAtt> (apvts, id ("swing"), swingSlider);
    aAtt      = std::make_unique<SliderAtt> (apvts, id ("a"), knobA.slider);
    bAtt      = std::make_unique<SliderAtt> (apvts, id ("b"), knobB.slider);
    cAtt      = std::make_unique<SliderAtt> (apvts, id ("c"), knobC.slider);
    depthAtt  = std::make_unique<SliderAtt> (apvts, id ("depth"), knobB.ring);
    layerOutAtt = std::make_unique<SliderAtt> (apvts, id ("out"), knobOut.slider);
    layerMixAtt = std::make_unique<SliderAtt> (apvts, id ("mix"), knobMix.slider);
    powerAtt  = std::make_unique<ButtonAtt> (apvts, id ("on"), powerBtn);

    modeAtt = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (id ("mode")),
        [this] (float v)
        {
            shapeBtn.setToggleState (v < 0.5f, juce::dontSendNotification);
            followBtn.setToggleState (v >= 0.5f, juce::dontSendNotification);
        }, &proc.undo);
    modeAtt->sendInitialUpdate();

    knobA.caption = MotionProcessor::knobCaption (fx, 0);
    knobB.caption = MotionProcessor::knobCaption (fx, 1);
    knobC.caption = MotionProcessor::knobCaption (fx, 2);
    knobA.format = [fx] (float v) { return MotionProcessor::knobText (fx, 0, v); };
    knobB.format = [fx] (float v) { return MotionProcessor::knobText (fx, 1, v); };
    knobC.format = [fx] (float v) { return MotionProcessor::knobText (fx, 2, v); };
    knobB.ringLabel = "S" + juce::String (curLayer + 1);
    knobB.setRingVisible (true);

    knobA.setVisible (knobA.caption.isNotEmpty());
    knobC.setVisible (knobC.caption.isNotEmpty());

    for (auto* k : { &knobA, &knobB, &knobC, &knobOut, &knobMix })
        k->repaint();
    repaint (rightPanel);
}

void MotionEditor::timerCallback()
{
    if (! proc.layerUsed (curLayer))
    {
        for (int l = 0; l < Shape::numLayers; ++l)
            if (proc.layerUsed (l)) { selectLayer (l); break; }
    }
    else if (proc.layerFx (curLayer) != boundFx)
    {
        rebindLayer();
    }

    if (currentStep() >= proc.layerSteps (curLayer))
        selectStep (0);

    if (presetName.getButtonText() != proc.currentPresetName)
        presetName.setButtonText (proc.currentPresetName);

    undoBtn.setEnabled (proc.undo.canUndo());
    redoBtn.setEnabled (proc.undo.canRedo());

    meterL = juce::jmax (proc.uiOutL.load(), meterL * 0.85f);
    meterR = juce::jmax (proc.uiOutR.load(), meterR * 0.85f);

    layerList.repaint();
    shapeView.tick();
    stepLanes.tick();
    repaint (meterRect);
    repaint (bpmRect);
    repaint (sigRect);
    repaint (ledRect);
}

// ======================= painting =======================
void MotionEditor::paint (juce::Graphics& g)
{
    g.fillAll (Ui::bg);

    // panel backgrounds and dividers
    g.setColour (juce::Colour (0xff0f0f11));
    g.fillRect (topBar);
    g.setColour (Ui::line);
    g.fillRect (topBar.getX(), topBar.getBottom() - 1, topBar.getWidth(), 1);
    g.fillRect (leftPanel.getRight(), topBar.getBottom(), 1, getHeight() - topBar.getBottom());
    g.fillRect (rightPanel.getX(), topBar.getBottom(), 1, getHeight() - topBar.getBottom());
    g.fillRect (0, seqPanel.getY(), leftPanel.getWidth(), 1);
    g.fillRect (0, getHeight() - 28, leftPanel.getWidth(), 1);

    // title
    g.setColour (juce::Colours::white);
    g.setFont (Ui::font (19.0f, true));
    g.drawText ("MotionShaper", titleRect, juce::Justification::centredLeft);

    // preset box
    auto presetBox = juce::Rectangle<int> (presetPrev.getX() - presetName.getWidth() - 8, 28, presetName.getWidth() + 8 + 56, 34).toFloat();
    g.setColour (Ui::raised.withAlpha (0.7f));
    g.fillRoundedRectangle (presetBox, 4.0f);
    g.setColour (Ui::line);
    g.drawRoundedRectangle (presetBox.reduced (0.5f), 4.0f, 1.0f);
    g.fillRect (presetPrev.getX() - 1, 28, 1, 34);
    g.fillRect (presetNext.getX(), 28, 1, 34);

    // bpm
    g.setColour (Ui::text);
    g.setFont (Ui::font (15.0f));
    g.drawText (juce::String (proc.uiBpm.load(), 2), bpmRect.withWidth (56).withHeight (24).translated (0, 4), juce::Justification::centred);
    g.setColour (Ui::dim);
    g.setFont (Ui::font (10.0f));
    g.drawText ("BPM", bpmRect.withWidth (56).withHeight (14).translated (0, 28), juce::Justification::centred);
    const bool playing = proc.uiHostPlaying.load();
    g.setColour (playing ? laf.accent : laf.accent.withAlpha (0.45f));
    g.fillEllipse ((float) bpmRect.getX() + 65.0f, (float) bpmRect.getY() + 11.0f, 10.0f, 10.0f);

    // time signature
    g.setColour (Ui::text);
    g.setFont (Ui::font (13.0f));
    g.drawText (juce::String (proc.uiNum.load()) + " / " + juce::String (proc.uiDen.load()), sigRect, juce::Justification::centred);

    // wave icon beside the source box
    {
        const float x = (float) scSourceTop.getX() - 16.0f, y = (float) scSourceTop.getBounds().getCentreY();
        juce::Path wave;
        wave.startNewSubPath (x, y);
        wave.lineTo (x + 3.0f, y);
        wave.lineTo (x + 5.5f, y - 5.0f);
        wave.lineTo (x + 8.5f, y + 5.0f);
        wave.lineTo (x + 11.0f, y);
        wave.lineTo (x + 14.0f, y);
        g.setColour (Ui::text);
        g.strokePath (wave, juce::PathStrokeType (1.1f));
    }

    // output meter
    {
        auto m = meterRect.toFloat();
        const float h = 4.0f;
        const float levels[3] { meterL, juce::jmax (meterL, meterR), meterR };
        const juce::Colour cols[3] { Ui::text, Ui::layerColour (1), Ui::layerColour (0) };
        for (int i = 0; i < 3; ++i)
        {
            const float y = m.getY() + (float) i * (h + 2.0f);
            g.setColour (Ui::knobBg);
            g.fillRect (m.getX(), y, m.getWidth(), h - 1.0f);
            const float db = juce::Decibels::gainToDecibels (levels[i], -60.0f);
            g.setColour (cols[i]);
            g.fillRect (m.getX(), y, m.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f), h - 1.0f);
        }
    }

    smallText (g, "MIX", { globalMix.getRight() + 8, 34, 34, 22 });

    // centre header captions
    smallText (g, "MODE", { centerPanel.getX() + 20, 100, 50, 26 });
    smallText (g, "snap", { snapBox.getX() - 46, 100, 40, 26 }, Ui::dim, 11.0f);
    smallText (g, "GRID SIZE", { viewGridBox.getX() - 72, 100, 66, 26 });
    smallText (g, "FOCUS", { focusBtn.getX() - 46, 100, 44, 26 });

    // sequencer panel captions
    const int sx = 18;
    headerText (g, "SEQUENCER", sx, seqPanel.getY() + 12);
    smallText (g, "GRID SIZE", { sx, gridBox.getY(), 80, gridBox.getHeight() });
    smallText (g, "rate", { sx, rateBox.getY(), 80, rateBox.getHeight() });
    smallText (g, "swing", { sx, swingSlider.getY(), 80, swingSlider.getHeight() });
    smallText (g, juce::String (juce::roundToInt (swingSlider.getValue() * 100.0)) + "%",
               { swingSlider.getRight() + 2, swingSlider.getY(), 36, swingSlider.getHeight() }, Ui::dim, 11.0f);
    smallText (g, "retrigger", { sx, retrigBox.getY(), 80, retrigBox.getHeight() });

    // SC activity light
    {
        const float lvl = proc.uiScLevel.load();
        const bool active = lvl > 0.0005f;
        g.setColour (active ? juce::Colours::white : Ui::dim.withAlpha (0.5f));
        g.fillEllipse ((float) ledRect.getX(), (float) ledRect.getCentreY() - 4.0f, 8.0f, 8.0f);
        smallText (g, "SC", ledRect.withTrimmedLeft (30), Ui::text, 11.0f);
    }

    // right panel
    const int rx = rightPanel.getX() + 14;
    const auto fxName = MotionProcessor::fxNames()[proc.layerFx (curLayer)];
    smallText (g, fxName.toLowerCase(), { rx, 96, 160, 16 }, Ui::dim, 11.0f);
    smallText (g, fxName, { rx, 114, 170, 26 }, Ui::text, 17.0f);

    headerText (g, "PARAMETER", rx, 158);
    g.setColour (Ui::line);
    g.fillRect (rightPanel.getX() + 1, 442, rightPanel.getWidth() - 1, 1);
    headerText (g, "SIDECHAIN", rx, 454);

    smallText (g, "source", { rx, scSource.getY(), 52, scSource.getHeight() });
    smallText (g, "filter", { rx, scType.getY(), 52, scType.getHeight() });
    smallText (g, "trigger", { rx, scTrig.getY(), 52, scTrig.getHeight() });
    smallText (g, "sensitivity", { scTrig.getRight() + 8, scTrig.getY(), 62, scTrig.getHeight() }, Ui::dim, 11.0f);
    smallText (g, juce::String (juce::roundToInt (scSens.getValue() * 100.0)) + "%",
               { scSens.getRight() + 2, scTrig.getY(), 30, scTrig.getHeight() }, Ui::dim, 10.0f);

    g.setColour (Ui::line);
    g.fillRect (rightPanel.getX() + 1, 702, rightPanel.getWidth() - 1, 1);
    smallText (g, "OUTPUT", { rx, 708, 60, 28 }, Ui::text, 12.0f);
    smallText (g, juce::String (juce::roundToInt (globalOut.getValue() * 100.0)) + "%", { rx + 62, 708, 40, 28 }, Ui::dim, 11.0f);

    if (! proc.uiScConnected.load() && (int) std::round (proc.apvts.getRawParameterValue ("sc_src")->load()) == 0)
        smallText (g, "no sidechain routed", { rx + 124, scSource.getBottom() + 1, 110, 12 }, Ui::dim, 9.0f);
}

void MotionEditor::resized()
{
    topBar = { 0, 0, getWidth(), 88 };
    leftPanel = { 0, 88, 222, getHeight() - 88 };
    rightPanel = { 858, 88, getWidth() - 858, getHeight() - 88 };
    centerPanel = { 222, 88, 636, getHeight() - 88 };
    seqPanel = { 0, 574, 222, 138 };

    // top bar
    titleRect = { 16, 24, 180, 40 };
    presetName.setBounds (190, 28, 118, 34);
    presetPrev.setBounds (316, 28, 28, 34);
    presetNext.setBounds (344, 28, 28, 34);
    presetSave.setBounds (380, 32, 50, 26);
    bpmRect = { 448, 18, 84, 50 };
    sigRect = { 560, 30, 70, 30 };
    scButton.setBounds (660, 32, 36, 26);
    scSourceTop.setBounds (726, 32, 94, 26);
    meterRect = { 866, 33, 50, 18 };
    globalMix.setBounds (950, 24, 44, 44);
    menuButton.setBounds (1058, 26, 36, 36);

    // list
    layerList.setBounds (leftPanel.withBottom (seqPanel.getY()));

    // sequencer panel
    const int comboX = 118, comboW = 84;
    randomBtn.setBounds (comboX + 18, seqPanel.getY() + 9, comboW - 18, 22);
    gridBox.setBounds (comboX, seqPanel.getY() + 36, comboW, 22);
    rateBox.setBounds (comboX, seqPanel.getY() + 60, comboW, 22);
    swingSlider.setBounds (82, seqPanel.getY() + 84, 96, 22);
    retrigBox.setBounds (comboX, seqPanel.getY() + 108, comboW, 22);

    // bottom left
    ledRect = { 14, getHeight() - 22, 50, 16 };
    undoBtn.setBounds (92, getHeight() - 25, 26, 22);
    redoBtn.setBounds (122, getHeight() - 25, 26, 22);

    // centre header
    const int cx = centerPanel.getX();
    shapeBtn.setBounds (cx + 66, 100, 60, 26);
    followBtn.setBounds (cx + 128, 100, 60, 26);
    snapBox.setBounds (cx + 262, 102, 66, 22);
    viewGridBox.setBounds (cx + 414, 102, 66, 22);
    focusBtn.setBounds (cx + 572, 100, 26, 26);

    shapeView.setBounds (cx + 18, 134, centerPanel.getWidth() - 34, 320);
    stepLanes.setBounds (cx + 18, 462, centerPanel.getWidth() - 34, 244);

    // right panel
    const int rx = rightPanel.getX() + 14;
    const int rw = rightPanel.getWidth() - 24;
    powerBtn.setBounds (rightPanel.getRight() - 42, 110, 30, 30);

    const int kw = rw / 3;
    knobA.setBounds (rx, 180, kw, 118);
    knobB.setBounds (rx + kw, 180, kw, 118);
    knobC.setBounds (rx + 2 * kw, 180, kw, 118);
    knobOut.setBounds (rx + kw / 2, 310, kw, 118);
    knobMix.setBounds (rx + kw / 2 + kw, 310, kw, 118);

    scSource.setBounds (rx + 54, 478, 108, 24);
    listenBtn.setBounds (rx + 170, 478, 56, 24);

    scType.setBounds (rx + 54, 510, 52, 24);
    scFreq.setBounds (rx + 110, 510, 62, 24);
    scSlope.setBounds (rx + 176, 510, 54, 24);

    scTrig.setBounds (rx + 54, 542, 52, 24);
    scSens.setBounds (rx + 170, 542, 36, 24);

    crossover->setBounds (rx, 574, rw, 66);

    const int bw = rw / 3;
    for (int b = 0; b < 3; ++b)
    {
        auto cell = juce::Rectangle<int> (rx + b * bw, 648, bw, 26).withSizeKeepingCentre (56, 26);
        bandSolo[b].setBounds (cell.removeFromLeft (26));
        cell.removeFromLeft (4);
        bandPower[b]->setBounds (cell);
    }

    globalOut.setBounds (rx + 104, 708, rw - 110, 28);
}

// ======================= menus =======================
void MotionEditor::editPattern (const std::function<void (Shape::Pattern&)>& fn)
{
    proc.undo.beginNewTransaction();
    const auto before = proc.getPattern (curLayer);
    auto after = before;
    fn (after);
    proc.setPattern (curLayer, after);
    proc.pushPatternUndo (curLayer, before);
    shapeView.reload();
}

void MotionEditor::showAddMenu (juce::Component* target)
{
    juce::PopupMenu menu;
    const auto& names = MotionProcessor::fxNames();
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i]);

    Ui::showMenu (menu, target, [this] (int r)
    {
        proc.undo.beginNewTransaction();
        const int l = proc.addLayer (r - 1);
        if (l >= 0) selectLayer (l);
    });
}

void MotionEditor::showLayerMenu (int layer, juce::Component* target)
{
    juce::PopupMenu fxMenu;
    const auto& names = MotionProcessor::fxNames();
    for (int i = 0; i < names.size(); ++i)
        fxMenu.addItem (100 + i, names[i], true, proc.layerFx (layer) == i);

    juce::PopupMenu menu;
    menu.addSubMenu ("Effect", fxMenu);
    menu.addItem (1, proc.layerOn (layer) ? "Turn off" : "Turn on");
    menu.addItem (2, "Duplicate", proc.numUsedLayers() < Shape::numLayers);
    menu.addItem (3, "Clear shapes");
    menu.addSeparator();
    menu.addItem (4, "Remove", proc.numUsedLayers() > 1);

    Ui::showMenu (menu, target, [this, layer] (int r)
    {
        proc.undo.beginNewTransaction();
        if (r >= 100)
        {
            proc.setLayerFx (layer, r - 100);
            selectLayer (layer);
        }
        else if (r == 1)
        {
            auto* p = proc.apvts.getParameter (MotionProcessor::paramId (layer, "on"));
            p->setValueNotifyingHost (proc.layerOn (layer) ? 0.0f : 1.0f);
        }
        else if (r == 2)
        {
            const int l = proc.duplicateLayer (layer);
            if (l >= 0) selectLayer (l);
        }
        else if (r == 3)
        {
            const auto before = proc.getPattern (layer);
            proc.setPattern (layer, Shape::Pattern());
            proc.pushPatternUndo (layer, before);
            if (layer == curLayer) selectLayer (layer);
        }
        else if (r == 4)
        {
            proc.removeLayer (layer);
        }
    });
}

void MotionEditor::showStepMenu (int step, juce::Component* target)
{
    juce::PopupMenu presets;
    for (int i = 0; i < Shape::numPresets; ++i)
        presets.addItem (100 + i, Shape::presetName (i));

    const auto pattern = proc.getPattern (curLayer);
    const int steps = proc.layerSteps (curLayer);
    const int head = Shape::segmentHead (pattern, step);
    const int len = Shape::segmentLength (pattern, head, steps);

    juce::PopupMenu menu;
    menu.addSubMenu ("Shape preset", presets);
    menu.addItem (1, "Randomize");
    menu.addSeparator();
    menu.addItem (2, "Copy");
    menu.addItem (3, "Paste", hasClipboard);
    menu.addItem (4, "Apply to all steps");
    menu.addSeparator();
    menu.addItem (5, "Merge with next", head + len < steps);
    menu.addItem (6, "Split into steps", len > 1);
    menu.addItem (7, pattern.muted[(size_t) step] ? "Unmute step" : "Mute step");

    Ui::showMenu (menu, target, [this, head, len, step, steps] (int r)
    {
        if (r >= 100)
            editPattern ([&] (Shape::Pattern& p) { p.pts[(size_t) head] = Shape::makePreset (r - 100); });
        else if (r == 1)
            editPattern ([&] (Shape::Pattern& p) { p.pts[(size_t) head] = Shape::makePreset (Shape::presetRandom); });
        else if (r == 2)
        {
            clipboard = proc.getPattern (curLayer);
            clipboard.pts[0] = clipboard.pts[(size_t) head];
            hasClipboard = true;
        }
        else if (r == 3)
            editPattern ([&] (Shape::Pattern& p) { p.pts[(size_t) head] = clipboard.pts[0]; });
        else if (r == 4)
            editPattern ([&] (Shape::Pattern& p) { const auto src = p.pts[(size_t) head]; for (auto& q : p.pts) q = src; });
        else if (r == 5)
            editPattern ([&] (Shape::Pattern& p)
            {
                const int nextHead = head + len;
                Shape::setSegmentEnd (p, head, nextHead + Shape::segmentLength (p, nextHead, steps) - 1, steps);
            });
        else if (r == 6)
            editPattern ([&] (Shape::Pattern& p)
            {
                for (int i = head + 1; i < head + len; ++i)
                {
                    p.start[(size_t) i] = true;
                    p.pts[(size_t) i] = p.pts[(size_t) head];
                }
            });
        else if (r == 7)
            editPattern ([&] (Shape::Pattern& p) { p.muted[(size_t) step] = ! p.muted[(size_t) step]; });

        stepLanes.setLayer (curLayer, currentStep());
    });
}

void MotionEditor::loadPresetByIndex (int index)
{
    const auto& factory = MotionProcessor::factoryPresetNames();
    if (index < factory.size())
    {
        proc.loadFactoryPreset (index);
    }
    else
    {
        const auto files = proc.userPresetFiles();
        const int u = index - factory.size();
        if (u < files.size()) proc.loadUserPreset (files[u]);
    }
    afterPresetChange();
}

void MotionEditor::afterPresetChange()
{
    for (auto& s : selectedStep) s = 0;

    int first = 0;
    for (int l = Shape::numLayers - 1; l >= 0; --l)
        if (proc.layerUsed (l)) first = l;
    selectLayer (first);
}

void MotionEditor::stepPreset (int delta)
{
    const auto& factory = MotionProcessor::factoryPresetNames();
    const auto files = proc.userPresetFiles();
    const int total = factory.size() + files.size();

    int current = factory.indexOf (proc.currentPresetName);
    if (current < 0)
        for (int i = 0; i < files.size(); ++i)
            if (files[i].getFileNameWithoutExtension() == proc.currentPresetName)
                current = factory.size() + i;

    loadPresetByIndex ((juce::jmax (0, current) + delta + total) % total);
}

void MotionEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& factory = MotionProcessor::factoryPresetNames();
    for (int i = 0; i < factory.size(); ++i)
        menu.addItem (i + 1, factory[i], true, factory[i] == proc.currentPresetName);

    const auto files = proc.userPresetFiles();
    if (! files.isEmpty())
    {
        menu.addSectionHeader ("My presets");
        for (int i = 0; i < files.size(); ++i)
        {
            const auto name = files[i].getFileNameWithoutExtension();
            menu.addItem (factory.size() + i + 1, name, true, name == proc.currentPresetName);
        }
    }

    Ui::showMenu (menu, &presetName, [this] (int r) { loadPresetByIndex (r - 1); });
}

void MotionEditor::savePresetAs()
{
    proc.userPresetFolder().createDirectory();
    const bool isFactory = MotionProcessor::factoryPresetNames().contains (proc.currentPresetName)
                            || proc.currentPresetName == "Custom";
    const auto suggested = isFactory ? juce::String ("My Preset") : proc.currentPresetName;
    chooser = std::make_unique<juce::FileChooser> ("Name your preset",
                                                   proc.userPresetFolder().getChildFile (suggested + ".mspreset"),
                                                   "*.mspreset");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file != juce::File())
                                  proc.saveUserPreset (file.withFileExtension ("mspreset"));
                          });
}

void MotionEditor::showMainMenu()
{
    juce::PopupMenu smoothing;
    const float current = proc.apvts.getRawParameterValue ("smooth")->load();
    const int values[7] = { 0, 1, 3, 5, 10, 20, 50 };
    for (int i = 0; i < 7; ++i)
        smoothing.addItem (100 + i, juce::String (values[i]) + " ms", true, juce::roundToInt (current) == values[i]);

    juce::PopupMenu menu;
    menu.addItem (1, "Save preset...");
    menu.addItem (2, "Load preset from file...");
    menu.addItem (3, "Open preset folder");
    menu.addSeparator();
    menu.addItem (4, "Reset to Init");
    menu.addSubMenu ("Shape smoothing", smoothing);

    Ui::showMenu (menu, &menuButton, [this, values] (int r)
    {
        if (r >= 100)
        {
            auto* p = proc.apvts.getParameter ("smooth");
            p->setValueNotifyingHost (p->convertTo0to1 ((float) values[r - 100]));
        }
        else if (r == 1)
        {
            savePresetAs();
        }
        else if (r == 2)
        {
            proc.userPresetFolder().createDirectory();
            chooser = std::make_unique<juce::FileChooser> ("Load preset", proc.userPresetFolder(), "*.mspreset");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto file = fc.getResult();
                                      if (file.existsAsFile() && proc.loadUserPreset (file))
                                          afterPresetChange();
                                  });
        }
        else if (r == 3)
        {
            proc.userPresetFolder().createDirectory();
            proc.userPresetFolder().startAsProcess();
        }
        else if (r == 4)
        {
            loadPresetByIndex (0);
        }
    });
}
