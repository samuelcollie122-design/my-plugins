#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SpatialUI.h"

// The field: top-down in 2D mode, a rotatable 3D room in 3D mode.
class XYPad : public juce::Component, private juce::Timer
{
public:
    explicit XYPad (SpaceTrackProcessor& p) : proc (p), apvts (p.apvts) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const bool is3D = *apvts.getRawParameterValue ("mode3d") > 0.5f;
        const float x = *apvts.getRawParameterValue ("x");
        const float y = *apvts.getRawParameterValue ("y");
        const float z = *apvts.getRawParameterValue ("z");
        const auto colour = proc.getTrackColour();

        if (is3D)
        {
            SpaceUI::drawField3D (g, b, yaw);
            SpaceUI::drawSource3D (g, b, yaw, x, y, z, colour, false);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.setFont (11.0f);
            g.drawText ("Right-drag to rotate", getLocalBounds().reduced (8).removeFromBottom (16),
                        juce::Justification::centredLeft);
        }
        else
        {
            SpaceUI::drawField2D (g, b);
            SpaceUI::drawSource2D (g, b, x, y, colour, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::Component::unfocusAllComponents();   // end any name edit, give keys back to the host
        if (e.mods.isRightButtonDown() && is3DMode())
        {
            rotating = true;
            lastMouseX = e.position.x;
            return;
        }
        apvts.getParameter ("x")->beginChangeGesture();   // one gesture per drag = one undo step
        apvts.getParameter ("y")->beginChangeGesture();
        setFromMouse (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (rotating)
        {
            yaw += (e.position.x - lastMouseX) * 0.01f;
            lastMouseX = e.position.x;
            repaint();
            return;
        }
        setFromMouse (e);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (rotating) { rotating = false; return; }
        apvts.getParameter ("x")->endChangeGesture();
        apvts.getParameter ("y")->endChangeGesture();
    }

private:
    bool is3DMode() const { return *apvts.getRawParameterValue ("mode3d") > 0.5f; }

    void setFromMouse (const juce::MouseEvent& e)
    {
        float x, y;
        if (is3DMode())
        {
            const float z = *apvts.getRawParameterValue ("z");
            SpaceUI::unprojectFloor (getLocalBounds().toFloat(), yaw, z, e.position, x, y);
        }
        else
        {
            x = juce::jlimit (-1.0f, 1.0f, (float) e.x / (float) getWidth() * 2.0f - 1.0f);
            y = juce::jlimit (-1.0f, 1.0f, 1.0f - (float) e.y / (float) getHeight() * 2.0f);
        }
        auto* px = apvts.getParameter ("x");
        auto* py = apvts.getParameter ("y");
        px->setValueNotifyingHost (px->convertTo0to1 (x));
        py->setValueNotifyingHost (py->convertTo0to1 (y));
    }

    void timerCallback() override { repaint(); }

    SpaceTrackProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    float yaw = 0.6f;
    bool rotating = false;
    float lastMouseX = 0.0f;
};

class SpaceTrackEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpaceTrackEditor (SpaceTrackProcessor&);
    ~SpaceTrackEditor() override { stopTimer(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void updateModeUI();

    using Attach = juce::AudioProcessorValueTreeState::SliderAttachment;

    SpaceUI::SpaceLookAndFeel laf;
    SpaceTrackProcessor& proc;

    juce::Label nameLabel;                 // double-click to edit
    SpaceUI::ModeSwitch modeSwitch;
    XYPad pad;
    juce::Slider xSlider, ySlider, zSlider;
    juce::Label xCaption, yCaption, zCaption;
    juce::TextButton centerButton { "Center" };
    juce::TextButton axisCenter[3];

    std::unique_ptr<Attach> xAtt, yAtt, zAtt;
    std::unique_ptr<juce::ParameterAttachment> modeAtt;

    juce::Rectangle<int> titleRect, nameRect, paletteRect;
    juce::uint32 lastStoredColour = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceTrackEditor)
};
