#include "PluginEditor.h"

SpaceTrackEditor::SpaceTrackEditor (SpaceTrackProcessor& p)
    : AudioProcessorEditor (&p), proc (p), pad (p)
{
    setLookAndFeel (&laf);

    // Name: double-click to edit, Enter or clicking away saves and releases the keyboard.
    nameLabel.setEditable (false, true, false);
    nameLabel.setText (p.apvts.state.getProperty ("trackName").toString(), juce::dontSendNotification);
    nameLabel.setFont (juce::Font (juce::FontOptions (16.0f)));
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    nameLabel.setBorderSize (juce::BorderSize<int> (0, 28, 0, 8));
    nameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    nameLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    nameLabel.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    nameLabel.setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
    nameLabel.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff0d0f12));
    nameLabel.setColour (juce::Label::outlineWhenEditingColourId, SpaceUI::accent());
    nameLabel.onTextChange = [this]
    {
        proc.apvts.state.setProperty ("trackName", nameLabel.getText().trim(), nullptr);
        juce::Component::unfocusAllComponents();
    };
    addAndMakeVisible (nameLabel);

    // 2D / 3D switch, bound to the plugin parameter
    modeAtt = std::make_unique<juce::ParameterAttachment> (
        *p.apvts.getParameter ("mode3d"),
        [this] (float v) { modeSwitch.setMode3D (v > 0.5f); updateModeUI(); });
    modeSwitch.onChange = [this] (bool is3D) { modeAtt->setValueAsCompleteGesture (is3D ? 1.0f : 0.0f); };
    addAndMakeVisible (modeSwitch);

    addAndMakeVisible (pad);

    const char* captions[3] = { "Left / Right", "Back / Front", "Height" };
    juce::Slider* sliders[3] = { &xSlider, &ySlider, &zSlider };
    juce::Label* labels[3]   = { &xCaption, &yCaption, &zCaption };
    for (int i = 0; i < 3; ++i)
    {
        sliders[i]->setSliderStyle (juce::Slider::LinearHorizontal);
        sliders[i]->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
        sliders[i]->onDragStart = [] { juce::Component::unfocusAllComponents(); };
        addAndMakeVisible (*sliders[i]);

        labels[i]->setText (captions[i], juce::dontSendNotification);
        labels[i]->setFont (juce::Font (juce::FontOptions (13.0f)));
        addAndMakeVisible (*labels[i]);

        axisCenter[i].setButtonText ("C");
        axisCenter[i].onClick = [this, i]
        {
            static const char* const ids[3] = { "x", "y", "z" };
            juce::Component::unfocusAllComponents();
            auto* param = proc.apvts.getParameter (ids[i]);
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (0.0f));
            param->endChangeGesture();
        };
        addAndMakeVisible (axisCenter[i]);
    }
    xAtt = std::make_unique<Attach> (p.apvts, "x", xSlider);
    yAtt = std::make_unique<Attach> (p.apvts, "y", ySlider);
    zAtt = std::make_unique<Attach> (p.apvts, "z", zSlider);

    centerButton.onClick = [this]
    {
        juce::Component::unfocusAllComponents();
        for (auto* id : { "x", "y", "z" })
        {
            auto* param = proc.apvts.getParameter (id);
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (0.0f));
            param->endChangeGesture();
        }
    };
    addAndMakeVisible (centerButton);

    modeAtt->sendInitialUpdate();
    updateModeUI();

    setSize (380, 640);
    startTimerHz (10);
}

void SpaceTrackEditor::updateModeUI()
{
    const bool is3D = modeSwitch.getMode3D();
    zSlider.setEnabled (is3D);               // height only matters in 3D
    axisCenter[2].setEnabled (is3D);
    zCaption.setAlpha (is3D ? 1.0f : 0.4f);
    repaint();
}

void SpaceTrackEditor::timerCallback()
{
    // Pick up renames / colour changes made from the master
    if (! nameLabel.isBeingEdited())
    {
        auto n = proc.apvts.state.getProperty ("trackName").toString();
        if (nameLabel.getText() != n)
            nameLabel.setText (n, juce::dontSendNotification);
    }

    const auto stored = (juce::uint32) (int) proc.apvts.state.getProperty ("trackColor", 0);
    if (stored != lastStoredColour)
    {
        lastStoredColour = stored;
        repaint();
    }
}

void SpaceTrackEditor::mouseDown (const juce::MouseEvent& e)
{
    juce::Component::unfocusAllComponents();    // clicking empty space ends a name edit

    const int hit = SpaceUI::paletteHit (paletteRect.toFloat(), e.position);
    if (hit >= 0)
    {
        proc.apvts.state.setProperty ("trackColor", (int) SpaceUI::paletteValue (hit), nullptr);
        repaint();
    }
}

void SpaceTrackEditor::paint (juce::Graphics& g)
{
    SpaceUI::drawBackground (g, getLocalBounds());
    SpaceUI::drawTitle (g, titleRect, "SpaceTrack");

    // name field
    auto nf = nameRect.toFloat();
    g.setColour (juce::Colour (0xff12151a));
    g.fillRoundedRectangle (nf, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawRoundedRectangle (nf, 6.0f, 1.0f);
    g.setColour (proc.getTrackColour());
    g.fillEllipse (nf.getX() + 9.0f, nf.getCentreY() - 6.0f, 12.0f, 12.0f);

    SpaceUI::drawPalette (g, paletteRect.toFloat(), lastStoredColour);
}

void SpaceTrackEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    titleRect = r.removeFromTop (44);
    r.removeFromTop (10);
    nameRect = r.removeFromTop (30);
    nameLabel.setBounds (nameRect);
    r.removeFromTop (8);
    paletteRect = r.removeFromTop (22);
    r.removeFromTop (10);
    modeSwitch.setBounds (r.removeFromTop (28).withSizeKeepingCentre (130, 28));
    r.removeFromTop (10);

    auto bottom = r.removeFromBottom (126);
    r.removeFromBottom (10);
    pad.setBounds (r);

    juce::Slider* sliders[3] = { &xSlider, &ySlider, &zSlider };
    juce::Label* labels[3]   = { &xCaption, &yCaption, &zCaption };
    for (int i = 0; i < 3; ++i)
    {
        auto row = bottom.removeFromTop (26);
        labels[i]->setBounds (row.removeFromLeft (84));
        axisCenter[i].setBounds (row.removeFromRight (28));
        row.removeFromRight (4);
        sliders[i]->setBounds (row);
        bottom.removeFromTop (6);
    }
    bottom.removeFromTop (2);
    centerButton.setBounds (bottom.removeFromTop (28));
}
