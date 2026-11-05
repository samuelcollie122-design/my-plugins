#pragma once
#include <JuceHeader.h>
#include "MasterProcessor.h"
#include "SpatialUI.h"

class SpaceMasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpaceMasterEditor (SpaceMasterProcessor&);
    ~SpaceMasterEditor() override { stopTimer(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Switches the SOUND of every track: flat stereo for speakers, or 3D for headphones
    void setAllTracksMode (bool is3D);
    void refreshAllButtons();

private:
    struct Dot { int slot; float x, y, z; bool mode3d; juce::Colour colour; juce::String name; };

    static constexpr int titleH = 44;
    static constexpr int panelW = 270;
    static constexpr int rowH = 30;
    static constexpr int selH = 190;

    std::vector<Dot> readDots() const;
    bool getDot (int slot, Dot& out) const;

    // layout
    juce::Rectangle<int> contentArea() const;
    juce::Rectangle<int> leftArea() const;
    juce::Rectangle<int> panelArea() const;
    juce::Rectangle<int> selArea() const;
    juce::Rectangle<int> selRow (int index) const;   // 0 caption, 1 mode, 2 L/R, 3 F/B, 4 height, 5 palette
    juce::Rectangle<int> listArea() const;
    juce::Rectangle<int> rowRect (int index) const;
    juce::Rectangle<float> padRect() const;

    // geometry
    juce::Point<float> screenPos (const Dot& d) const;
    juce::Point<float> fromScreen2D (juce::Point<float> p) const;
    int findDotAt (juce::Point<float> p, bool preferSelected = true) const;
    int rowSlotAt (juce::Point<int> p) const;
    void updateDrag (juce::Point<float> p);

    // selection
    void select (int slot);
    bool selectionValid() const;

    // commands to the tracks
    void sendPosition (int slot, float x, float y, float z);
    void sendMode (int slot, bool is3D);
    void sendColor (int slot, juce::uint32 argb);
    void sendName (int slot, const juce::String& name);
    void setDragging (int slot, bool on);

    // renaming
    void startRename (int slot);
    void commitRename();
    void cancelRename();

    void timerCallback() override;

    SpaceUI::SpaceLookAndFeel laf;
    SpaceMasterProcessor& proc;

    SpaceUI::ModeSwitch viewSwitch, selModeSwitch;
    juce::Slider axisSlider[3];          // 0 = left/right, 1 = back/front, 2 = height
    juce::Label axisCaption[3];
    juce::TextButton axisCenter[3];
    juce::TextButton centerSelectedButton { "Center selected" };
    juce::TextButton centerAllButton { "Center all" };
    juce::TextButton cameraButton { "Center camera" };   // 3D view only
    juce::TextButton allStereoButton { "All stereo" }, all3DButton { "All 3D" };
    juce::Slider roomSizeSlider, roomAmountSlider;     // shared room, heard in 3D only
    juce::Label roomSizeCaption, roomAmountCaption;
    juce::TextEditor renameBox;

    int selectedSlot = -1;
    uint64_t selectedOwner = 0;

    int dragSlot = -1;
    uint64_t dragOwner = 0;
    float dragX = 0.0f, dragY = 0.0f, dragParamZ = 0.0f, dragUnprojZ = 0.0f;
    int sliderDragSlot = -1;

    bool rotating = false;
    float lastMouseX = 0.0f;

    int renamingSlot = -1;
    uint64_t renamingOwner = 0;

    int scrollY = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceMasterEditor)
};
