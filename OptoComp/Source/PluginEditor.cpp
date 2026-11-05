#include "PluginEditor.h"

namespace
{
    constexpr int kWidth = 990, kHeight = 405, kBarH = 38, kPanelBottom = 332;

    void disableKeyboardFocus (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        for (auto* child : c.getChildren())
            disableKeyboardFocus (*child);
    }

    void setupKnob (juce::Slider& s, const char* kind, int dragPixels)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setMouseDragSensitivity (dragPixels);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.getProperties().set ("kind", kind);
    }

    void drawArrow (juce::Graphics& g, juce::Rectangle<float> r, bool left)
    {
        const auto c = r.getCentre();
        const float d = left ? 1.0f : -1.0f;
        juce::Path p;
        p.addTriangle (c.x - 8.0f * d, c.y, c.x - 1.0f * d, c.y - 6.0f, c.x - 1.0f * d, c.y + 6.0f);
        p.addRectangle (juce::jmin (c.x - 1.0f * d, c.x + 8.0f * d), c.y - 2.0f, 9.0f, 4.0f);
        g.fillPath (p);
    }

    void drawCurvedArrow (juce::Graphics& g, juce::Rectangle<float> r, bool left)
    {
        const auto c = r.getCentre();
        const float s = left ? 1.0f : -1.0f;
        juce::Path p;
        p.startNewSubPath (c.x - 7.0f * s, c.y - 1.0f);
        p.quadraticTo (c.x, c.y - 7.0f, c.x + 7.0f * s, c.y + 4.0f);
        p.startNewSubPath (c.x - 7.5f * s, c.y - 6.0f);
        p.lineTo (c.x - 7.0f * s, c.y - 1.0f);
        p.lineTo (c.x - 2.0f * s, c.y - 1.5f);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }

    void drawMenuBars (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto c = r.getCentre();
        for (int i = -1; i <= 1; ++i)
            g.fillRect (c.x - 8.0f, c.y + (float) i * 5.0f - 0.75f, 16.0f, 1.5f);
    }
}

OptoEditor::OptoEditor (OptoProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      undoBtn ([] (juce::Graphics& g, juce::Rectangle<float> r) { drawCurvedArrow (g, r, true); }),
      redoBtn ([] (juce::Graphics& g, juce::Rectangle<float> r) { drawCurvedArrow (g, r, false); }),
      prevBtn ([] (juce::Graphics& g, juce::Rectangle<float> r) { drawArrow (g, r, true); }),
      nextBtn ([] (juce::Graphics& g, juce::Rectangle<float> r) { drawArrow (g, r, false); }),
      menuBtn (drawMenuBars)
{
    setLookAndFeel (&laf);
    auto& apvts = proc.apvts;

    // ---- main knobs ----
    setupKnob (gainKnob, "big", 240);
    setupKnob (peakKnob, "big", 240);
    setupKnob (hiFreqKnob, "dial", 180);
    setupKnob (mixKnob, "small", 180);
    setupKnob (trimKnob, "small", 180);
    hiFreqKnob.onValueChange = [this] { repaint (hiFreqKnob.getRight(), kPanelBottom, 90, getHeight() - kPanelBottom); };
    for (auto* k : { &gainKnob, &peakKnob, &hiFreqKnob, &mixKnob, &trimKnob })
        addAndMakeVisible (*k);

    gainAtt   = std::make_unique<SliderAtt> (apvts, "gain", gainKnob);
    peakAtt   = std::make_unique<SliderAtt> (apvts, "peak", peakKnob);
    hiFreqAtt = std::make_unique<SliderAtt> (apvts, "hifreq", hiFreqKnob);
    mixAtt    = std::make_unique<SliderAtt> (apvts, "mix", mixKnob);
    trimAtt   = std::make_unique<SliderAtt> (apvts, "trim", trimKnob);

    // ---- switches ----
    limitSwitch.setTooltip ("Compress: gentle. Limit: firmer.");
    autoSwitch.setTooltip ("Auto Makeup: slowly raises the level by about what the compressor took away.");
    addAndMakeVisible (limitSwitch);
    addAndMakeVisible (autoSwitch);
    limitAtt = std::make_unique<ButtonAtt> (apvts, "limit", limitSwitch);
    autoAtt  = std::make_unique<ButtonAtt> (apvts, "automakeup", autoSwitch);
    limitSwitch.onStateChange = [this] { repaint (0, kBarH, 200, kPanelBottom - kBarH); };
    autoSwitch.onStateChange  = [this] { repaint (800, kBarH, 190, kPanelBottom - kBarH); };

    selector.mode = (int) apvts.state.getProperty ("meterMode", 1);
    selector.onChange = [this] (int m) { proc.apvts.state.setProperty ("meterMode", m, nullptr); };
    addAndMakeVisible (selector);
    addAndMakeVisible (meter);

    // ---- analog buttons ----
    const char* analogText[3] = { "50Hz", "60Hz", "OFF" };
    const float analogValue[3] = { 1.0f, 2.0f, 0.0f };
    for (int i = 0; i < 3; ++i)
    {
        analogBtn[i].setButtonText (analogText[i]);
        analogBtn[i].getProperties().set ("kind", "analog");
        const float value = analogValue[i];
        analogBtn[i].onClick = [this, value] { if (analogAtt) analogAtt->setValueAsCompleteGesture (value); };
        addAndMakeVisible (analogBtn[i]);
    }
    analogBtn[0].setTooltip ("Adds tube-style harmonics, plus a very quiet 50 Hz hum and hiss that sound even when the track is silent.");
    analogBtn[1].setTooltip ("Adds tube-style harmonics, plus a very quiet 60 Hz hum and hiss that sound even when the track is silent.");
    analogBtn[2].setTooltip ("Clean: no harmonics, hum or hiss.");
    analogAtt = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter ("analog"),
        [this] (float v)
        {
            const int mode = juce::roundToInt (v);
            analogBtn[0].setToggleState (mode == 1, juce::dontSendNotification);
            analogBtn[1].setToggleState (mode == 2, juce::dontSendNotification);
            analogBtn[2].setToggleState (mode == 0, juce::dontSendNotification);
        }, &proc.undo);
    analogAtt->sendInitialUpdate();

    // ---- top bar ----
    undoBtn.onClick = [this] { proc.undo.undo(); };
    redoBtn.onClick = [this] { proc.undo.redo(); };
    undoBtn.setTooltip ("Undo");
    redoBtn.setTooltip ("Redo");
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    prevBtn.setTooltip ("Previous preset");
    nextBtn.setTooltip ("Next preset");
    presetBtn.getProperties().set ("dropdown", true);
    presetBtn.onClick = [this] { showPresetMenu(); };
    abBtn.onClick = [this] { proc.copyCurrentSetupToOther(); };
    setupBtn.onClick = [this]
    {
        proc.toggleSetup();
        updateSetupLabels();
    };
    saveBtn.setTooltip ("Save the current sound as your own preset");
    saveBtn.onClick = [this] { savePresetAs(); };
    menuBtn.onClick = [this] { showMainMenu(); };
    for (auto* b : std::initializer_list<juce::Button*> { &undoBtn, &redoBtn, &presetBtn, &prevBtn, &nextBtn, &abBtn, &setupBtn, &saveBtn, &menuBtn })
        addAndMakeVisible (*b);

    updateSetupLabels();

    addMouseListener (this, true);     // every mouse press starts one undo step

    setSize (kWidth, kHeight);
    disableKeyboardFocus (*this);      // keys belong to the host
    startTimerHz (30);
}

OptoEditor::~OptoEditor()
{
    removeMouseListener (this);
    stopTimer();
    setLookAndFeel (nullptr);
}

void OptoEditor::mouseDown (const juce::MouseEvent&)
{
    proc.undo.beginNewTransaction();
}

void OptoEditor::updateSetupLabels()
{
    abBtn.setButtonText (proc.getCurrentSetup() == 0 ? "A -> B" : "B -> A");
    abBtn.setTooltip (proc.getCurrentSetup() == 0 ? "Copy setup A to B" : "Copy setup B to A");
    setupBtn.setButtonText (proc.getCurrentSetup() == 0 ? "Setup A" : "Setup B");
    setupBtn.setTooltip ("Switch between two setups to compare them");
    updatePresetLabel();
}

void OptoEditor::updatePresetLabel()
{
    const auto text = juce::String (proc.getCurrentSetup() == 0 ? "A: " : "B: ") + proc.getPresetName();
    if (presetBtn.getButtonText() != text)
        presetBtn.setButtonText (text);
}

void OptoEditor::timerCallback()
{
    undoBtn.setEnabled (proc.undo.canUndo());
    redoBtn.setEnabled (proc.undo.canRedo());
    updatePresetLabel();

    // The input and output readings are placed so that -18 dBFS shows 0 on the meter
    inputSmooth = juce::jmax (proc.uiInputLevel.load(), inputSmooth * 0.9f);
    outputSmooth = juce::jmax (proc.uiOutputLevel.load(), outputSmooth * 0.9f);

    switch (selector.mode)
    {
        case 0:  meter.setValueDb (juce::Decibels::gainToDecibels (inputSmooth, -60.0f) + 18.0f); break;
        case 2:  meter.setValueDb (juce::Decibels::gainToDecibels (outputSmooth, -60.0f) + 18.0f); break;
        default: meter.setValueDb (-proc.uiReductionDb.load()); break;
    }
}

// ======================= painting =======================
void OptoEditor::buildPanelImage()
{
    panelImage = juce::Image (juce::Image::RGB, kWidth, kPanelBottom - kBarH, true);
    juce::Graphics g (panelImage);
    const auto w = (float) panelImage.getWidth(), h = (float) panelImage.getHeight();

    juce::ColourGradient base (juce::Colour (0xffc6c6c3), 0.0f, 0.0f, juce::Colour (0xffa9a9a6), 0.0f, h, false);
    base.addColour (0.5, juce::Colour (0xffbdbdba));
    g.setGradientFill (base);
    g.fillAll();

    // Fine brushed lines and a few light scratches, the same every time
    juce::Random rng (2024);
    for (int y = 0; y < panelImage.getHeight(); ++y)
    {
        g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (rng.nextFloat() * 0.035f));
        g.fillRect (0, y, panelImage.getWidth(), 1);
    }
    for (int i = 0; i < 90; ++i)
    {
        const float x = rng.nextFloat() * w, y = rng.nextFloat() * h;
        const float len = 6.0f + rng.nextFloat() * 42.0f, slope = (rng.nextFloat() - 0.5f) * 0.5f;
        g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.05f + rng.nextFloat() * 0.09f));
        g.drawLine (x, y, x + len, y + len * slope, 0.7f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillRect (0.0f, 0.0f, w, 1.5f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (0.0f, h - 2.0f, w, 2.0f);
}

void OptoEditor::paint (juce::Graphics& g)
{
    // ---- top bar ----
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3b3e), 0.0f, 0.0f, juce::Colour (0xff161618), 0.0f, (float) kBarH, false));
    g.fillRect (0, 0, kWidth, kBarH);

    {
        const juce::Rectangle<float> plate (6.0f, 6.0f, 322.0f, 26.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8e8e6), 0.0f, plate.getY(), juce::Colour (0xffa9a9a7), 0.0f, plate.getBottom(), false));
        g.fillRect (plate);
        g.setColour (juce::Colour (0xff27272a));
        g.setFont (OptoUi::font (15.0f));
        g.drawText ("OPTO CLASSIC COMPRESSOR", plate.toNearestInt(), juce::Justification::centred);
    }

    // ---- faceplate ----
    if (panelImage.isValid())
        g.drawImageAt (panelImage, 0, kBarH);

    // Own emblem in place of a company logo: a small lamp over a light cell
    {
        const juce::Point<float> c (100.0f, 92.0f);
        g.setColour (OptoUi::redLabel);
        g.fillEllipse (c.x - 15.0f, c.y - 19.0f, 30.0f, 30.0f);
        g.setColour (OptoUi::metal);
        g.fillEllipse (c.x - 9.0f, c.y - 13.0f, 18.0f, 18.0f);
        g.setColour (OptoUi::redLabel);
        g.fillEllipse (c.x - 4.0f, c.y - 8.0f, 8.0f, 8.0f);
        g.fillRect (c.x - 8.0f, c.y + 14.0f, 16.0f, 4.0f);
        g.setFont (OptoUi::font (13.5f));
        g.drawText ("OPTO CELL", 40, 114, 120, 18, juce::Justification::centred);
        g.drawText ("AUDIO", 40, 130, 120, 18, juce::Justification::centred);
    }

    const auto label = [&g] (const juce::String& s, int x, int y, int w, juce::Colour c = OptoUi::label, float size = 12.5f)
    {
        g.setColour (c);
        g.setFont (OptoUi::font (size));
        g.drawText (s, x, y, w, 18, juce::Justification::centred);
    };

    label ("COMPRESS", 50, 220, 100, limitSwitch.getToggleState() ? OptoUi::label : OptoUi::redLabel, 12.0f);
    label ("LIMIT", 50, 298, 100, limitSwitch.getToggleState() ? OptoUi::redLabel : OptoUi::label, 12.0f);

    label ("GAIN", gainKnob.getX(), 283, gainKnob.getWidth());
    label ("PEAK REDUCTION", peakKnob.getX(), 283, peakKnob.getWidth());

    label ("VU DISPLAY", 830, 72, 124);
    label ("AUTO", 830, 196, 124);
    label ("MAKEUP", 830, 209, 124);
    label ("OFF", 830, 300, 124, autoSwitch.getToggleState() ? OptoUi::label : OptoUi::redLabel, 12.0f);

    // Stamped title under the meter
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.setFont (OptoUi::font (31.0f));
    g.drawText ("OPTOCOMP", meter.getX(), meter.getBottom() + 5, meter.getWidth(), 40, juce::Justification::centred);
    g.setColour (juce::Colour (0xff222224));
    g.drawText ("OPTOCOMP", meter.getX(), meter.getBottom() + 4, meter.getWidth(), 40, juce::Justification::centred);

    // ---- bottom strip ----
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff36363a), 0.0f, (float) kPanelBottom, OptoUi::barDark, 0.0f, (float) kHeight, false));
    g.fillRect (0, kPanelBottom, kWidth, kHeight - kPanelBottom);

    g.setColour (OptoUi::barText);
    g.setFont (OptoUi::font (15.5f, false));
    g.drawText ("ANALOG", 100, kPanelBottom, 90, kHeight - kPanelBottom, juce::Justification::centredLeft);
    g.drawText ("HI FREQ", 405, kPanelBottom, 70, kHeight - kPanelBottom, juce::Justification::centredLeft);
    g.drawText ("MIX", 645, kPanelBottom, 40, kHeight - kPanelBottom, juce::Justification::centredLeft);
    g.drawText ("TRIM", 775, kPanelBottom, 50, kHeight - kPanelBottom, juce::Justification::centredLeft);

    g.setFont (OptoUi::font (11.5f, false));
    const auto hiFreq = (float) hiFreqKnob.getValue();
    g.drawText (hiFreq < 0.005f ? "[ FLAT ]" : "[ " + juce::String (juce::roundToInt (hiFreq * 100.0f)) + "% ]",
                hiFreqKnob.getRight() + 4, kPanelBottom, 70, kHeight - kPanelBottom, juce::Justification::centredLeft);

    g.setFont (OptoUi::font (10.0f, false));
    g.drawText ("0", mixKnob.getX() - 2, kHeight - 16, 20, 14, juce::Justification::centred);
    g.drawText ("100", mixKnob.getRight() - 24, kHeight - 16, 26, 14, juce::Justification::centred);
    g.drawText ("-18", trimKnob.getX() - 4, kHeight - 16, 24, 14, juce::Justification::centred);
    g.drawText ("18", trimKnob.getRight() - 20, kHeight - 16, 22, 14, juce::Justification::centred);
}

void OptoEditor::resized()
{
    buildPanelImage();

    // top bar
    int x = 334;
    const auto place = [&x] (juce::Component& c, int w) { c.setBounds (x, 6, w, 26); x += w + 3; };
    place (undoBtn, 46);
    place (redoBtn, 46);
    place (presetBtn, 218);
    place (prevBtn, 46);
    place (nextBtn, 46);
    place (abBtn, 62);
    place (setupBtn, 64);
    place (saveBtn, 62);
    place (menuBtn, 26);

    // faceplate
    gainKnob.setBounds (194, 92, 160, 160);
    peakKnob.setBounds (647, 92, 160, 160);
    meter.setBounds (372, 72, 256, 170);
    selector.setBounds (800, 88, 184, 96);
    limitSwitch.setBounds (80, 240, 40, 56);
    autoSwitch.setBounds (872, 240, 40, 56);

    // bottom strip
    const int cy = (kPanelBottom + kHeight) / 2;
    for (int i = 0; i < 3; ++i)
        analogBtn[i].setBounds (190 + i * 56, cy - 11, 53, 22);
    hiFreqKnob.setBounds (477, cy - 30, 60, 60);
    mixKnob.setBounds (680, cy - 32, 64, 64);
    trimKnob.setBounds (817, cy - 32, 64, 64);
}

// ======================= presets =======================
void OptoEditor::loadPresetByIndex (int index)
{
    const auto& factory = OptoProcessor::factoryPresetNames();
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
    updatePresetLabel();
}

void OptoEditor::stepPreset (int delta)
{
    const auto& factory = OptoProcessor::factoryPresetNames();
    const auto files = proc.userPresetFiles();
    const int total = factory.size() + files.size();

    int current = factory.indexOf (proc.getPresetName());
    if (current < 0)
        for (int i = 0; i < files.size(); ++i)
            if (files[i].getFileNameWithoutExtension() == proc.getPresetName())
                current = factory.size() + i;

    loadPresetByIndex ((juce::jmax (0, current) + delta + total) % total);
}

void OptoEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& factory = OptoProcessor::factoryPresetNames();
    for (int i = 0; i < factory.size(); ++i)
        menu.addItem (i + 1, factory[i], true, factory[i] == proc.getPresetName());

    const auto files = proc.userPresetFiles();
    if (! files.isEmpty())
    {
        menu.addSectionHeader ("My presets");
        for (int i = 0; i < files.size(); ++i)
        {
            const auto name = files[i].getFileNameWithoutExtension();
            menu.addItem (factory.size() + i + 1, name, true, name == proc.getPresetName());
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn),
                        [safe = juce::Component::SafePointer<OptoEditor> (this)] (int r)
                        {
                            if (safe != nullptr && r != 0) safe->loadPresetByIndex (r - 1);
                        });
}

void OptoEditor::savePresetAs()
{
    proc.userPresetFolder().createDirectory();
    const bool isFactory = OptoProcessor::factoryPresetNames().contains (proc.getPresetName());
    const auto suggested = isFactory ? juce::String ("My Preset") : proc.getPresetName();
    chooser = std::make_unique<juce::FileChooser> ("Name your preset",
                                                   proc.userPresetFolder().getChildFile (suggested + ".optopreset"),
                                                   "*.optopreset");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file != juce::File())
                                  proc.saveUserPreset (file.withFileExtension ("optopreset"));
                          });
}

void OptoEditor::showMainMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Save preset...");
    menu.addItem (2, "Load preset from file...");
    menu.addItem (3, "Open preset folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuBtn),
                        [safe = juce::Component::SafePointer<OptoEditor> (this)] (int r)
                        {
                            if (safe == nullptr) return;
                            if (r == 1)
                            {
                                safe->savePresetAs();
                            }
                            else if (r == 2)
                            {
                                safe->proc.userPresetFolder().createDirectory();
                                safe->chooser = std::make_unique<juce::FileChooser> ("Load preset", safe->proc.userPresetFolder(), "*.optopreset");
                                safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                            [safe] (const juce::FileChooser& fc)
                                                            {
                                                                const auto file = fc.getResult();
                                                                if (safe != nullptr && file.existsAsFile())
                                                                    safe->proc.loadUserPreset (file);
                                                            });
                            }
                            else if (r == 3)
                            {
                                safe->proc.userPresetFolder().createDirectory();
                                safe->proc.userPresetFolder().startAsProcess();
                            }
                        });
}
