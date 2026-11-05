#include "MasterEditor.h"
#include "SharedRegistry.h"
#include <cstring>

SpaceMasterEditor::SpaceMasterEditor (SpaceMasterProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    // view switch: how the master draws the field
    viewSwitch.setMode3D (proc.view3D);
    viewSwitch.onChange = [this] (bool is3D) { proc.view3D = is3D; repaint(); };
    addAndMakeVisible (viewSwitch);

    // selected track: its own 2D/3D mode and height
    selModeSwitch.setEnabled (false);
    selModeSwitch.onChange = [this] (bool is3D)
    {
        if (selectionValid()) sendMode (selectedSlot, is3D);
    };
    addAndMakeVisible (selModeSwitch);

    const char* axisNames[3] = { "Left/Right", "Back/Front", "Height" };
    for (int a = 0; a < 3; ++a)
    {
        axisCaption[a].setText (axisNames[a], juce::dontSendNotification);
        axisCaption[a].setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (axisCaption[a]);

        auto& sl = axisSlider[a];
        sl.setSliderStyle (juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
        sl.setRange (-1.0, 1.0, 0.001);
        sl.setDoubleClickReturnValue (true, 0.0);
        sl.setEnabled (false);
        sl.onDragStart = [this]
        {
            juce::Component::unfocusAllComponents();
            if (selectionValid()) { sliderDragSlot = selectedSlot; setDragging (sliderDragSlot, true); }
        };
        sl.onDragEnd = [this]
        {
            if (sliderDragSlot >= 0) { setDragging (sliderDragSlot, false); sliderDragSlot = -1; }
        };
        sl.onValueChange = [this, a]
        {
            if (! selectionValid()) return;
            auto* reg = getSharedRegistry();
            if (reg == nullptr) return;
            auto& s = reg->slots[selectedSlot];
            float v[3] = { s.x.load(), s.y.load(), s.z.load() };
            v[a] = (float) axisSlider[a].getValue();
            sendPosition (selectedSlot, v[0], v[1], v[2]);
        };
        addAndMakeVisible (sl);

        auto& cb = axisCenter[a];
        cb.setButtonText ("C");
        cb.setEnabled (false);
        cb.onClick = [this, a]
        {
            juce::Component::unfocusAllComponents();
            if (! selectionValid()) return;
            auto* reg = getSharedRegistry();
            if (reg == nullptr) return;
            auto& s = reg->slots[selectedSlot];
            float v[3] = { s.x.load(), s.y.load(), s.z.load() };
            v[a] = 0.0f;                                   // center just this axis
            sendPosition (selectedSlot, v[0], v[1], v[2]);
        };
        addAndMakeVisible (cb);
    }

    centerSelectedButton.setEnabled (false);
    centerSelectedButton.onClick = [this]
    {
        juce::Component::unfocusAllComponents();
        if (selectionValid()) sendPosition (selectedSlot, 0.0f, 0.0f, 0.0f);
    };
    addAndMakeVisible (centerSelectedButton);

    centerAllButton.onClick = [this]
    {
        juce::Component::unfocusAllComponents();
        for (auto& d : readDots()) sendPosition (d.slot, 0.0f, 0.0f, 0.0f);
    };
    addAndMakeVisible (centerAllButton);

    // Resets the 3D camera to look straight from behind, so left/right is exactly horizontal
    cameraButton.onClick = [this]
    {
        juce::Component::unfocusAllComponents();
        proc.viewYaw = 0.0f;
        repaint();
    };
    addChildComponent (cameraButton);

    // One click for the whole song. A single track can still be set differently under SELECTED.
    allStereoButton.onClick = [this] { juce::Component::unfocusAllComponents(); setAllTracksMode (false); };
    all3DButton.onClick     = [this] { juce::Component::unfocusAllComponents(); setAllTracksMode (true); };
    addAndMakeVisible (allStereoButton);
    addAndMakeVisible (all3DButton);

    // Shared room: every track in 3D uses these. Read-only text boxes, so they never take the keyboard.
    const auto setupRoomSlider = [this] (juce::Slider& sl, juce::Label& cap, const char* text, float value, std::function<void (float)> apply)
    {
        cap.setText (text, juce::dontSendNotification);
        cap.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (cap);

        sl.setSliderStyle (juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle (juce::Slider::TextBoxRight, true, 42, 20);
        sl.setRange (0.0, 1.0, 0.01);
        sl.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        sl.setValue ((double) value, juce::dontSendNotification);
        sl.onDragStart = [] { juce::Component::unfocusAllComponents(); };
        sl.onValueChange = [this, &sl, apply]
        {
            apply ((float) sl.getValue());
            proc.publishRoom();
        };
        addAndMakeVisible (sl);
    };
    setupRoomSlider (roomSizeSlider, roomSizeCaption, "Room size", proc.roomSize, [this] (float v) { proc.roomSize = v; });
    setupRoomSlider (roomAmountSlider, roomAmountCaption, "Amount", proc.roomAmount, [this] (float v) { proc.roomAmount = v; });
    roomSizeSlider.setDoubleClickReturnValue (true, 0.4);
    roomAmountSlider.setDoubleClickReturnValue (true, 0.25);

    // Inline rename box: Enter or clicking away saves, Escape cancels.
    renameBox.setMultiLine (false);
    renameBox.setReturnKeyStartsNewLine (false);
    renameBox.setFont (juce::Font (juce::FontOptions (14.0f)));
    renameBox.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0d0f12));
    renameBox.setColour (juce::TextEditor::textColourId, juce::Colours::white);
    renameBox.setColour (juce::TextEditor::focusedOutlineColourId, SpaceUI::accent());
    renameBox.onReturnKey = [this] { commitRename(); };
    renameBox.onEscapeKey = [this] { cancelRename(); };
    renameBox.onFocusLost = [this] { commitRename(); };
    addChildComponent (renameBox);

    setResizeLimits (700, 540, 1600, 1400);
    setResizable (true, true);
    setSize (840, 680);
    startTimerHz (30);
}

// ======================= layout =======================
juce::Rectangle<int> SpaceMasterEditor::contentArea() const
{
    auto a = getLocalBounds().reduced (12);
    a.removeFromTop (titleH + 10);
    return a;
}

juce::Rectangle<int> SpaceMasterEditor::panelArea() const
{
    return contentArea().removeFromRight (panelW);
}

juce::Rectangle<int> SpaceMasterEditor::leftArea() const
{
    auto a = contentArea();
    a.removeFromRight (panelW + 10);
    return a;
}

juce::Rectangle<int> SpaceMasterEditor::selArea() const
{
    return panelArea().removeFromBottom (selH);
}

juce::Rectangle<int> SpaceMasterEditor::selRow (int index) const
{
    static const int heights[6] = { 18, 28, 24, 24, 24, 22 };
    auto s = selArea().reduced (10, 8);
    int y = s.getY();
    for (int i = 0; i < index; ++i) y += heights[i] + 6;
    return { s.getX(), y, s.getWidth(), heights[index] };
}

juce::Rectangle<int> SpaceMasterEditor::listArea() const
{
    auto p = panelArea();
    p.removeFromBottom (selH + 6);
    return p.withTrimmedTop (28).withTrimmedBottom (22);
}

juce::Rectangle<int> SpaceMasterEditor::rowRect (int index) const
{
    auto l = listArea();
    return { l.getX(), l.getY() + index * rowH - scrollY, l.getWidth(), rowH };
}

juce::Rectangle<float> SpaceMasterEditor::padRect() const
{
    auto a = leftArea();
    a.removeFromTop (34);       // view switch
    a.removeFromBottom (36);    // center buttons
    a.removeFromBottom (30);    // shared room
    return a.toFloat();
}

void SpaceMasterEditor::resized()
{
    auto a = leftArea();
    auto topRow = a.removeFromTop (28);
    viewSwitch.setBounds (topRow.withSizeKeepingCentre (130, 28));
    {
        auto allRow = topRow;
        allStereoButton.setBounds (allRow.removeFromLeft (68));
        allRow.removeFromLeft (2);
        all3DButton.setBounds (allRow.removeFromLeft (58));
    }
    cameraButton.setBounds (topRow.removeFromRight (120));

    auto row = a.removeFromBottom (28);
    centerSelectedButton.setBounds (row.removeFromLeft (row.getWidth() / 2 - 4));
    row.removeFromLeft (8);
    centerAllButton.setBounds (row);

    a.removeFromBottom (6);
    auto roomRow = a.removeFromBottom (24);
    auto roomLeft = roomRow.removeFromLeft (roomRow.getWidth() / 2 - 4);
    roomRow.removeFromLeft (8);
    roomSizeCaption.setBounds (roomLeft.removeFromLeft (66));
    roomSizeSlider.setBounds (roomLeft);
    roomAmountCaption.setBounds (roomRow.removeFromLeft (54));
    roomAmountSlider.setBounds (roomRow);

    selModeSwitch.setBounds (selRow (1).withSizeKeepingCentre (130, 28));
    for (int a = 0; a < 3; ++a)
    {
        auto row2 = selRow (2 + a);
        axisCaption[a].setBounds (row2.removeFromLeft (66));
        axisCenter[a].setBounds (row2.removeFromRight (26));
        row2.removeFromRight (4);
        axisSlider[a].setBounds (row2);
    }

    cancelRename();
}

// ======================= geometry =======================
juce::Point<float> SpaceMasterEditor::screenPos (const Dot& d) const
{
    auto r = padRect();
    if (proc.view3D)
        return SpaceUI::project (r, proc.viewYaw, d.x, d.y, d.mode3d ? d.z : 0.0f);
    return SpaceUI::sourcePos2D (r, d.x, d.y);
}

juce::Point<float> SpaceMasterEditor::fromScreen2D (juce::Point<float> p) const
{
    auto r = padRect();
    return { juce::jlimit (-1.0f, 1.0f, (p.x - r.getX()) / r.getWidth() * 2.0f - 1.0f),
             juce::jlimit (-1.0f, 1.0f, 1.0f - (p.y - r.getY()) / r.getHeight() * 2.0f) };
}

int SpaceMasterEditor::findDotAt (juce::Point<float> p, bool preferSelected) const
{
    const float hitRadius = 22.0f;
    auto dots = readDots();

    // In a cluster, the selected track keeps the click, so it never jumps to a neighbour.
    // (Ctrl-click, or the track list, picks a different one.)
    if (preferSelected && selectedSlot >= 0)
        for (auto& d : dots)
            if (d.slot == selectedSlot && screenPos (d).getDistanceFrom (p) < hitRadius)
                return selectedSlot;

    int best = -1;
    float bestDist = hitRadius;
    for (auto& d : dots)
    {
        const float dist = screenPos (d).getDistanceFrom (p);
        if (dist < bestDist) { bestDist = dist; best = d.slot; }
    }
    return best;
}

int SpaceMasterEditor::rowSlotAt (juce::Point<int> p) const
{
    auto list = listArea();
    if (! list.contains (p)) return -1;
    const int index = (p.y - list.getY() + scrollY) / rowH;
    auto dots = readDots();
    return (index >= 0 && index < (int) dots.size()) ? dots[(size_t) index].slot : -1;
}

void SpaceMasterEditor::updateDrag (juce::Point<float> p)
{
    float x, y;
    if (proc.view3D)
        SpaceUI::unprojectFloor (padRect(), proc.viewYaw, dragUnprojZ, p, x, y);
    else
    {
        auto v = fromScreen2D (p);
        x = v.x; y = v.y;
    }
    dragX = x;
    dragY = y;
    sendPosition (dragSlot, x, y, dragParamZ);
}

// ======================= shared memory access =======================
std::vector<SpaceMasterEditor::Dot> SpaceMasterEditor::readDots() const
{
    std::vector<Dot> out;
    auto* reg = getSharedRegistry();
    if (reg == nullptr) return out;

    const auto now = juce::Time::currentTimeMillis();
    for (int i = 0; i < SharedRegistry::maxSlots; ++i)
    {
        auto& s = reg->slots[i];
        if (s.owner.load() == 0 || now - s.heartbeat.load() > 5000) continue;

        char buf[32];
        std::memcpy (buf, s.name, 32);
        buf[31] = 0;
        juce::String n = juce::String::fromUTF8 (buf);
        if (n.isEmpty()) n = "Track " + juce::String (i + 1);

        Dot d { i, s.x.load(), s.y.load(), s.z.load(), s.mode3d.load() != 0,
                SpaceUI::resolveColour (s.color.load(), i), n };
        if (i == dragSlot) { d.x = dragX; d.y = dragY; }   // follow the mouse while dragging
        out.push_back (d);
    }
    return out;
}

bool SpaceMasterEditor::getDot (int slot, Dot& out) const
{
    for (auto& d : readDots())
        if (d.slot == slot) { out = d; return true; }
    return false;
}

void SpaceMasterEditor::sendPosition (int slot, float x, float y, float z)
{
    auto* reg = getSharedRegistry();
    if (reg == nullptr || slot < 0) return;
    auto& s = reg->slots[slot];
    s.cmdX.store (x);
    s.cmdY.store (y);
    s.cmdZ.store (z);
    s.cmdSeq.fetch_add (1);
}

void SpaceMasterEditor::sendMode (int slot, bool is3D)
{
    auto* reg = getSharedRegistry();
    if (reg == nullptr || slot < 0) return;
    auto& s = reg->slots[slot];
    s.cmdMode.store (is3D ? 1u : 0u);
    s.modeSeq.fetch_add (1);
}

void SpaceMasterEditor::sendColor (int slot, juce::uint32 argb)
{
    auto* reg = getSharedRegistry();
    if (reg == nullptr || slot < 0) return;
    auto& s = reg->slots[slot];
    s.cmdColor.store (argb);
    s.colorSeq.fetch_add (1);
}

void SpaceMasterEditor::sendName (int slot, const juce::String& name)
{
    auto* reg = getSharedRegistry();
    if (reg == nullptr || slot < 0) return;
    auto& s = reg->slots[slot];
    name.copyToUTF8 (s.cmdName, sizeof (s.cmdName));
    s.nameSeq.fetch_add (1);
}

void SpaceMasterEditor::setAllTracksMode (bool is3D)
{
    for (auto& d : readDots())
        sendMode (d.slot, is3D);

    // Show the matching field too. Positions are kept, so switching back loses nothing.
    proc.view3D = is3D;
    viewSwitch.setMode3D (is3D);
    repaint();
}

// A button lights up when every track is in that mode. Mixed tracks light neither.
void SpaceMasterEditor::refreshAllButtons()
{
    const auto dots = readDots();
    bool anyStereo = false, any3D = false;
    for (auto& d : dots)
        (d.mode3d ? any3D : anyStereo) = true;

    allStereoButton.setToggleState (anyStereo && ! any3D, juce::dontSendNotification);
    all3DButton.setToggleState (any3D && ! anyStereo, juce::dontSendNotification);
    allStereoButton.setEnabled (! dots.empty());
    all3DButton.setEnabled (! dots.empty());
}

// Tells the track a drag is in progress, so it records ONE undo step for the whole drag
void SpaceMasterEditor::setDragging (int slot, bool on)
{
    auto* reg = getSharedRegistry();
    if (reg == nullptr || slot < 0) return;
    auto& s = reg->slots[slot];
    s.dragBeat.store (juce::Time::currentTimeMillis());
    s.dragging.store (on ? 1u : 0u);
}

// ======================= selection =======================
void SpaceMasterEditor::select (int slot)
{
    auto* reg = getSharedRegistry();
    if (reg == nullptr || slot < 0) return;
    selectedSlot = slot;
    selectedOwner = reg->slots[slot].owner.load();
}

bool SpaceMasterEditor::selectionValid() const
{
    auto* reg = getSharedRegistry();
    if (reg == nullptr || selectedSlot < 0 || selectedOwner == 0) return false;
    auto& s = reg->slots[selectedSlot];
    return s.owner.load() == selectedOwner
        && juce::Time::currentTimeMillis() - s.heartbeat.load() <= 5000;
}

void SpaceMasterEditor::timerCallback()
{
    if (selectedSlot >= 0 && ! selectionValid())
        selectedSlot = -1;                                  // that track was removed

    auto* reg = getSharedRegistry();
    const auto now = juce::Time::currentTimeMillis();

    // keep the "drag in progress" flag fresh so the track knows we're still here
    if (reg != nullptr)
    {
        if (dragSlot >= 0)       reg->slots[dragSlot].dragBeat.store (now);
        if (sliderDragSlot >= 0) reg->slots[sliderDragSlot].dragBeat.store (now);
    }

    const bool hasSel = selectedSlot >= 0 && reg != nullptr;
    centerSelectedButton.setEnabled (hasSel);
    selModeSwitch.setEnabled (hasSel);
    cameraButton.setVisible (proc.view3D);
    refreshAllButtons();

    // Pick up a room restored after this window opened
    if (! roomSizeSlider.isMouseButtonDown()) roomSizeSlider.setValue ((double) proc.roomSize, juce::dontSendNotification);
    if (! roomAmountSlider.isMouseButtonDown()) roomAmountSlider.setValue ((double) proc.roomAmount, juce::dontSendNotification);

    if (hasSel)
    {
        auto& s = reg->slots[selectedSlot];
        const bool is3D = s.mode3d.load() != 0;
        selModeSwitch.setMode3D (is3D);
        for (int a = 0; a < 3; ++a)
        {
            const bool en = (a < 2) || is3D;                // height only matters in 3D
            axisSlider[a].setEnabled (en);
            axisCenter[a].setEnabled (en);
            axisCaption[a].setAlpha (en ? 1.0f : 0.4f);
        }
        if (sliderDragSlot < 0)
        {
            axisSlider[0].setValue ((double) s.x.load(), juce::dontSendNotification);
            axisSlider[1].setValue ((double) s.y.load(), juce::dontSendNotification);
            axisSlider[2].setValue ((double) s.z.load(), juce::dontSendNotification);
        }
    }
    else
    {
        for (int a = 0; a < 3; ++a)
        {
            axisSlider[a].setEnabled (false);
            axisCenter[a].setEnabled (false);
            axisCaption[a].setAlpha (0.4f);
        }
    }

    repaint();
}

// ======================= renaming =======================
void SpaceMasterEditor::startRename (int slot)
{
    auto dots = readDots();
    for (size_t i = 0; i < dots.size(); ++i)
    {
        if (dots[i].slot != slot) continue;

        auto rr = rowRect ((int) i);
        if (! listArea().contains (rr.getCentre())) return;

        if (auto* reg = getSharedRegistry())
            renamingOwner = reg->slots[slot].owner.load();
        renamingSlot = slot;
        renameBox.setBounds (rr.reduced (3, 3));
        renameBox.setText (dots[i].name, false);
        renameBox.setVisible (true);
        renameBox.grabKeyboardFocus();
        renameBox.selectAll();
        return;
    }
}

void SpaceMasterEditor::commitRename()
{
    if (renamingSlot < 0) return;               // also stops re-entry when the box is hidden
    const int slot = renamingSlot;
    renamingSlot = -1;

    const auto text = renameBox.getText().trim();
    renameBox.setVisible (false);
    juce::Component::unfocusAllComponents();    // give the keyboard back to the host

    if (text.isEmpty()) return;
    if (auto* reg = getSharedRegistry())
        if (reg->slots[slot].owner.load() == renamingOwner)
            sendName (slot, text);
}

void SpaceMasterEditor::cancelRename()
{
    if (renamingSlot < 0) return;
    renamingSlot = -1;
    renameBox.setVisible (false);
    juce::Component::unfocusAllComponents();
}

// ======================= painting =======================
void SpaceMasterEditor::paint (juce::Graphics& g)
{
    SpaceUI::drawBackground (g, getLocalBounds());
    SpaceUI::drawTitle (g, getLocalBounds().reduced (12).removeFromTop (titleH), "SpaceMaster");

    // ---- field ----
    auto r = padRect();
    if (proc.view3D) SpaceUI::drawField3D (g, r, proc.viewYaw);
    else             SpaceUI::drawField2D (g, r);

    auto dots = readDots();

    if (dots.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (14.0f);
        g.drawFittedText ("No SpaceTrack plugins found.\nAdd SpaceTrack to some mixer tracks.",
                          r.toNearestInt().reduced (30), juce::Justification::centred, 3);
    }

    for (auto& d : dots)
    {
        const bool sel = d.slot == selectedSlot;
        if (proc.view3D) SpaceUI::drawSource3D (g, r, proc.viewYaw, d.x, d.y, d.mode3d ? d.z : 0.0f, d.colour, sel);
        else             SpaceUI::drawSource2D (g, r, d.x, d.y, d.colour, sel);

        auto p = screenPos (d);
        g.setColour (juce::Colours::white);
        g.setFont (13.0f);
        g.drawText (d.name,
                    juce::Rectangle<float> (p.x - 60.0f, p.y + 18.0f, 120.0f, 16.0f).toNearestInt(),
                    juce::Justification::centred);
    }

    if (proc.view3D)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (11.0f);
        g.drawText ("Drag empty space (or right-drag) to rotate",
                    r.toNearestInt().reduced (10, 8).removeFromBottom (16), juce::Justification::centredLeft);
    }

    // ---- side panel: track list ----
    auto panel = panelArea();
    g.setColour (juce::Colour (0xff1b1e24));
    g.fillRoundedRectangle (panel.toFloat(), 8.0f);

    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.setFont (13.0f);
    g.drawText ("TRACKS (" + juce::String ((int) dots.size()) + ")",
                panel.withHeight (28).reduced (12, 0), juce::Justification::centredLeft);

    auto hintArea = panel;
    hintArea.removeFromBottom (selH + 6);
    hintArea = hintArea.removeFromBottom (22);
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.setFont (11.0f);
    g.drawText ("Double-click a track to rename", hintArea.reduced (12, 0), juce::Justification::centredLeft);

    auto list = listArea();
    g.saveState();
    g.reduceClipRegion (list);
    for (size_t i = 0; i < dots.size(); ++i)
    {
        auto row = rowRect ((int) i);
        if (! row.intersects (list)) continue;

        if (dots[i].slot == selectedSlot)
        {
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.fillRoundedRectangle (row.reduced (4, 1).toFloat(), 4.0f);
        }

        g.setColour (dots[i].colour);
        g.fillEllipse ((float) row.getX() + 12.0f, (float) row.getCentreY() - 6.0f, 12.0f, 12.0f);

        g.setColour (juce::Colours::white);
        g.setFont (14.0f);
        g.drawText (dots[i].name, row.withTrimmedLeft (32).withTrimmedRight (8), juce::Justification::centredLeft);

        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.setFont (11.0f);
        g.drawText (dots[i].mode3d ? "3D" : "2D", row.withTrimmedRight (10), juce::Justification::centredRight);
    }
    g.restoreState();

    // ---- selected track section ----
    auto sa = selArea();
    g.setColour (juce::Colour (0xff23272e));
    g.fillRoundedRectangle (sa.toFloat().reduced (4.0f, 0.0f), 6.0f);

    Dot sel;
    if (selectedSlot >= 0 && getDot (selectedSlot, sel))
    {
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText ("SELECTED: " + sel.name, selRow (0), juce::Justification::centredLeft);

        auto* reg = getSharedRegistry();
        const juce::uint32 stored = reg != nullptr ? reg->slots[selectedSlot].color.load() : 0u;
        SpaceUI::drawPalette (g, selRow (5).toFloat(), stored);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (13.0f);
        g.drawText ("Select a track to edit it", sa, juce::Justification::centred);
    }
}

// ======================= mouse =======================
void SpaceMasterEditor::mouseDown (const juce::MouseEvent& e)
{
    juce::Component::unfocusAllComponents();    // saves an open rename, frees the keyboard
    dragSlot = -1;
    rotating = false;

    if (selectionValid())
    {
        const int hit = SpaceUI::paletteHit (selRow (5).toFloat(), e.position);
        if (hit >= 0)
        {
            sendColor (selectedSlot, SpaceUI::paletteValue (hit));
            return;
        }
    }

    if (panelArea().contains (e.getPosition()))
    {
        const int slot = rowSlotAt (e.getPosition());
        if (slot >= 0) select (slot);
        return;
    }

    if (! padRect().contains (e.position)) return;

    if (e.mods.isRightButtonDown() && proc.view3D)
    {
        rotating = true;
        lastMouseX = e.position.x;
        return;
    }

    const int slot = findDotAt (e.position, ! e.mods.isCtrlDown());
    if (slot < 0)
    {
        if (proc.view3D) { rotating = true; lastMouseX = e.position.x; }   // empty space rotates the view
        return;
    }

    Dot d;
    if (! getDot (slot, d)) return;

    select (slot);
    dragSlot = slot;
    if (auto* reg = getSharedRegistry())
        dragOwner = reg->slots[slot].owner.load();
    dragParamZ = d.z;
    dragUnprojZ = d.mode3d ? d.z : 0.0f;

    setDragging (slot, true);
    updateDrag (e.position);
}

void SpaceMasterEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (rotating)
    {
        proc.viewYaw += (e.position.x - lastMouseX) * 0.01f;
        lastMouseX = e.position.x;
        return;
    }

    if (dragSlot < 0) return;

    auto* reg = getSharedRegistry();
    if (reg == nullptr || reg->slots[dragSlot].owner.load() != dragOwner)
    {
        dragSlot = -1;   // that track went away
        return;
    }
    updateDrag (e.position);
}

void SpaceMasterEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragSlot >= 0) setDragging (dragSlot, false);
    dragSlot = -1;
    rotating = false;
}

void SpaceMasterEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (panelArea().contains (e.getPosition()))
    {
        const int slot = rowSlotAt (e.getPosition());
        if (slot >= 0)
        {
            select (slot);
            startRename (slot);
        }
        return;
    }

    const int slot = findDotAt (e.position);
    if (slot >= 0)
    {
        select (slot);
        sendPosition (slot, 0.0f, 0.0f, 0.0f);   // double-click a dot to center it
    }
    else if (proc.view3D && padRect().contains (e.position))
    {
        proc.viewYaw = 0.0f;                      // double-click empty space = center camera
    }
}

void SpaceMasterEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! listArea().contains (e.getPosition())) return;

    commitRename();
    const int total = (int) readDots().size() * rowH;
    const int maxScroll = juce::jmax (0, total - listArea().getHeight());
    scrollY = juce::jlimit (0, maxScroll, scrollY - (int) (w.deltaY * 150.0f));
}
