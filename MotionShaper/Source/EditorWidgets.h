#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace Ui
{
    const juce::Colour bg      (0xff141416);
    const juce::Colour panel   (0xff1a1a1d);
    const juce::Colour raised  (0xff232327);
    const juce::Colour line    (0xff2e2e33);
    const juce::Colour text    (0xffdcdce0);
    const juce::Colour dim     (0xff85858c);
    const juce::Colour knobBg  (0xff34343a);

    inline juce::Colour layerColour (int layer)
    {
        static const juce::uint32 c[Shape::numLayers] = { 0xfff5a54a, 0xff5b93dc, 0xffa56be0,
                                                          0xff4cc9b0, 0xffe86fa0, 0xffd9c94f };
        return juce::Colour (c[juce::jlimit (0, Shape::numLayers - 1, layer)]);
    }

    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline void drawGrip (juce::Graphics& g, float x, float y)
    {
        g.setColour (dim);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                g.fillEllipse (x + 4.0f * (float) c, y + 4.0f * (float) r, 2.0f, 2.0f);
    }

    template <typename Fn>
    void showMenu (juce::PopupMenu& menu, juce::Component* target, Fn&& fn)
    {
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                            [safe = juce::Component::SafePointer<juce::Component> (target),
                             fn = std::forward<Fn> (fn)] (int result)
                            {
                                if (safe != nullptr && result != 0) fn (result);
                            });
    }

    // =========================================================================
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        juce::Colour accent = layerColour (0);

        LookAndFeel()
        {
            setDefaultSansSerifTypefaceName ("Segoe UI");
            setColour (juce::PopupMenu::backgroundColourId, raised);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, line);
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::Label::textColourId, text);
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
        {
            const bool mini = s.getProperties()["mini"];
            const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (mini ? 2.0f : 6.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto c = bounds.getCentre();
            const float angle = start + pos * (end - start);
            const float thick = mini ? 1.5f : 2.0f;

            juce::Path track;
            track.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, end, true);
            g.setColour (knobBg);
            g.strokePath (track, juce::PathStrokeType (thick));

            const bool bipolar = s.getMinimum() < 0.0;
            const float from = bipolar ? (start + end) * 0.5f : start;
            if (std::abs (angle - from) > 0.001f)
            {
                juce::Path value;
                value.addCentredArc (c.x, c.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
                g.setColour (mini ? juce::Colour (0xff6fb4e8) : accent);
                g.strokePath (value, juce::PathStrokeType (thick));
            }

            if (! mini)
            {
                const auto dot = c.getPointOnCircumference (radius, angle);
                g.setColour (juce::Colours::white);
                g.fillEllipse (dot.x - 5.0f, dot.y - 5.0f, 10.0f, 10.0f);
            }
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) override
        {
            const float cy = (float) y + (float) h * 0.5f;
            g.setColour (knobBg);
            g.fillRoundedRectangle ((float) x, cy - 1.5f, (float) w, 3.0f, 1.5f);
            g.setColour (accent);
            g.fillRoundedRectangle ((float) x, cy - 1.5f, pos - (float) x, 3.0f, 1.5f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (pos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
        }

        int getSliderThumbRadius (juce::Slider& s) override
        {
            return s.isRotary() ? 0 : 5;
        }

        void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
        {
            auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
            const bool flat = box.getProperties()["flat"];
            if (! flat)
            {
                g.setColour (raised);
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (line);
                g.drawRoundedRectangle (r, 4.0f, 1.0f);
            }

            juce::Path chevron;
            const float cx = (float) w - 11.0f, cy = (float) h * 0.5f;
            chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
            chevron.lineTo (cx, cy + 2.0f);
            chevron.lineTo (cx + 3.5f, cy - 1.5f);
            g.setColour (text);
            g.strokePath (chevron, juce::PathStrokeType (1.3f));
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f); }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (2, 0, box.getWidth() - 18, box.getHeight());
            label.setFont (getComboBoxFont (box));
        }

        juce::Font getPopupMenuFont() override { return font (13.0f); }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            if (b.getProperties()["flat"]) return;

            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const float corner = b.getProperties()["round"] ? r.getHeight() * 0.5f : 5.0f;

            if (b.getToggleState())
            {
                g.setColour (accent);
                g.fillRoundedRectangle (r, corner);
            }
            else
            {
                g.setColour (over || down ? line : raised.withAlpha (0.6f));
                g.fillRoundedRectangle (r, corner);
                g.setColour (dim.withAlpha (0.55f));
                g.drawRoundedRectangle (r, corner, 1.0f);
            }
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            const bool flat = b.getProperties()["flat"];
            if (flat) g.setColour (b.getToggleState() ? accent : text);
            else      g.setColour (b.getToggleState() ? juce::Colour (0xff17130d) : text);
            g.setFont (font (juce::jmin (13.0f, (float) b.getHeight() * 0.62f)));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (2, 0), juce::Justification::centred, 1);
        }
    };

    // =========================================================================
    // A button drawn by a small function (power, menu, arrows, focus ...)
    class IconButton : public juce::Button
    {
    public:
        using Draw = std::function<void (juce::Graphics&, juce::Rectangle<float>)>;

        explicit IconButton (Draw d) : juce::Button ({}), draw (std::move (d)) {}

        juce::Colour onColour = layerColour (0), offColour = dim;

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto c = getToggleState() ? onColour : offColour;
            if (! isEnabled()) c = c.withAlpha (0.35f);
            else if (over || down) c = c.brighter (0.4f);
            g.setColour (c);
            draw (g, getLocalBounds().toFloat());
        }

    private:
        Draw draw;
    };

    inline void drawPower (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto c = r.getCentre();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.34f;
        juce::Path p;
        p.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        p.startNewSubPath (c.x, c.y - rad * 1.25f);
        p.lineTo (c.x, c.y - rad * 0.3f);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    inline void drawMenuIcon (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto c = r.getCentre();
        for (int i = -1; i <= 1; ++i)
            g.fillRect (c.x - 10.0f, c.y + (float) i * 6.0f - 0.75f, 20.0f, 1.5f);
    }

    inline void drawChevron (juce::Graphics& g, juce::Rectangle<float> r, bool left)
    {
        const auto c = r.getCentre();
        const float d = left ? 2.5f : -2.5f;
        juce::Path p;
        p.startNewSubPath (c.x + d, c.y - 5.0f);
        p.lineTo (c.x - d, c.y);
        p.lineTo (c.x + d, c.y + 5.0f);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    inline void drawCurvedArrow (juce::Graphics& g, juce::Rectangle<float> r, bool left)
    {
        const auto c = r.getCentre();
        const float s = left ? 1.0f : -1.0f;
        juce::Path p;
        p.startNewSubPath (c.x - 6.0f * s, c.y - 1.0f);
        p.quadraticTo (c.x, c.y - 6.5f, c.x + 6.0f * s, c.y + 3.5f);
        p.startNewSubPath (c.x - 6.5f * s, c.y - 5.5f);
        p.lineTo (c.x - 6.0f * s, c.y - 1.0f);
        p.lineTo (c.x - 1.5f * s, c.y - 1.5f);
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }

    inline void drawFocusIcon (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto c = r.getCentre();
        g.drawEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f, 1.2f);
        g.fillEllipse (c.x - 1.5f, c.y - 1.5f, 3.0f, 3.0f);
        for (auto d : { juce::Point<float> (0.0f, -7.5f), juce::Point<float> (0.0f, 5.0f),
                        juce::Point<float> (-7.5f, 0.0f), juce::Point<float> (5.0f, 0.0f) })
            g.fillRect (c.x + d.x - (d.x == 0.0f ? 0.5f : 0.0f), c.y + d.y - (d.y == 0.0f ? 0.5f : 0.0f),
                        d.x == 0.0f ? 1.0f : 2.5f, d.x == 0.0f ? 2.5f : 1.0f);
    }

    // =========================================================================
    // Round knob with a caption and a value line below it. Optionally a small depth ring at its corner.
    class Knob : public juce::Component
    {
    public:
        Knob()
        {
            slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            slider.setMouseDragSensitivity (180);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                        juce::MathConstants<float>::pi * 2.75f, true);
            slider.onValueChange = [this] { repaint(); };
            addAndMakeVisible (slider);

            ring.getProperties().set ("mini", true);
            ring.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            ring.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            ring.setMouseDragSensitivity (200);
            ring.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                      juce::MathConstants<float>::pi * 2.75f, true);
            ring.setTooltip ("How far this layer's shape moves the knob. Drag to change, double-click to reset.");
            ring.onValueChange = [this] { repaint(); };
            addChildComponent (ring);
        }

        juce::Slider slider, ring;
        juce::String caption, ringLabel;
        std::function<juce::String (float)> format;

        void setRingVisible (bool v) { ring.setVisible (v); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            r.removeFromTop (r.getHeight() - 36);
            g.setColour (text);
            g.setFont (font (12.0f));
            g.drawText (caption, r.removeFromTop (18), juce::Justification::centred);
            g.setColour (dim);
            g.setFont (font (11.0f));
            if (format) g.drawText (format ((float) slider.getValue()), r, juce::Justification::centred);

            if (ring.isVisible())
            {
                g.setColour (dim);
                g.setFont (font (9.0f));
                g.drawText (ringLabel, ring.getX(), ring.getBottom() - 1, ring.getWidth(), 11, juce::Justification::centred);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromBottom (36);
            const int side = juce::jmin (r.getWidth(), r.getHeight()) - 12;
            slider.setBounds (r.withSizeKeepingCentre (side, side));
            ring.setBounds (slider.getRight() - 10, slider.getY() - 5, 18, 18);
        }
    };

    // =========================================================================
    // Low / mid / high crossover line with two draggable points
    class CrossoverBar : public juce::Component
    {
    public:
        CrossoverBar (juce::RangedAudioParameter& low, juce::RangedAudioParameter& high, juce::UndoManager* um)
            : lowParam (low), highParam (high),
              lowAtt (low, [this] (float v) { lowHz = v; repaint(); }, um),
              highAtt (high, [this] (float v) { highHz = v; repaint(); }, um)
        {
            lowAtt.sendInitialUpdate();
            highAtt.sendInitialUpdate();
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            const float y = lineY();
            const float xL = xFromHz (lowHz), xH = xFromHz (highHz);

            g.setColour (dim);
            g.setFont (font (11.0f));
            g.drawText ("low", juce::Rectangle<float> (r.getX(), 0.0f, xL, 16.0f), juce::Justification::centred);
            g.drawText ("mid", juce::Rectangle<float> (xL, 0.0f, xH - xL, 16.0f), juce::Justification::centred);
            g.drawText ("high", juce::Rectangle<float> (xH, 0.0f, r.getRight() - xH, 16.0f), juce::Justification::centred);

            const juce::Colour lowC (0xfff5a54a), midC (0xff9ccf7a), highC (0xff4cc9b0);
            g.setColour (lowC);  g.fillRoundedRectangle (r.getX() + 4.0f, y - 1.5f, xL - 4.0f, 3.0f, 1.5f);
            g.setColour (midC);  g.fillRect (xL, y - 1.5f, xH - xL, 3.0f);
            g.setColour (highC); g.fillRoundedRectangle (xH, y - 1.5f, r.getRight() - 4.0f - xH, 3.0f, 1.5f);

            g.setColour (juce::Colours::white);
            g.fillEllipse (xL - 6.0f, y - 6.0f, 12.0f, 12.0f);
            g.fillEllipse (xH - 6.0f, y - 6.0f, 12.0f, 12.0f);

            g.setColour (dim);
            g.setFont (font (10.5f));
            g.drawText (hzText (lowHz), juce::Rectangle<float> (xL - 30.0f, y + 8.0f, 60.0f, 14.0f), juce::Justification::centred);
            g.drawText (hzText (highHz), juce::Rectangle<float> (xH - 30.0f, y + 8.0f, 60.0f, 14.0f), juce::Justification::centred);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            draggingLow = std::abs (e.position.x - xFromHz (lowHz)) < std::abs (e.position.x - xFromHz (highHz));
            (draggingLow ? lowAtt : highAtt).beginGesture();
            mouseDrag (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            float hz = hzFromX (e.position.x);
            if (draggingLow)
                lowAtt.setValueAsPartOfGesture (juce::jlimit (lowParam.getNormalisableRange().start,
                                                              juce::jmin (lowParam.getNormalisableRange().end, highHz * 0.8f), hz));
            else
                highAtt.setValueAsPartOfGesture (juce::jlimit (juce::jmax (highParam.getNormalisableRange().start, lowHz * 1.25f),
                                                               highParam.getNormalisableRange().end, hz));
        }

        void mouseUp (const juce::MouseEvent&) override { (draggingLow ? lowAtt : highAtt).endGesture(); }

    private:
        float lineY() const { return 26.0f; }
        float xFromHz (float hz) const
        {
            return 4.0f + (float) (getWidth() - 8) * std::log (hz / 20.0f) / std::log (1000.0f);
        }
        float hzFromX (float x) const
        {
            return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - 4.0f) / (float) (getWidth() - 8)));
        }
        static juce::String hzText (float hz)
        {
            return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        }

        juce::RangedAudioParameter& lowParam;
        juce::RangedAudioParameter& highParam;
        juce::ParameterAttachment lowAtt, highAtt;
        float lowHz = 180.0f, highHz = 3200.0f;
        bool draggingLow = true;
    };

    // =========================================================================
    // Layer list on the left
    class LayerList : public juce::Component
    {
    public:
        explicit LayerList (MotionProcessor& p) : proc (p) {}

        std::function<void (int)> onSelect;
        std::function<void (int, juce::Component*)> onMenu;
        std::function<void (juce::Component*)> onAdd;
        int selected = 0;

        void paint (juce::Graphics& g) override
        {
            g.setColour (text);
            g.setFont (font (13.0f));
            g.drawText ("layers", 18, 8, 80, 22, juce::Justification::centredLeft);

            const auto plus = plusRect().toFloat().getCentre();
            g.fillRect (plus.x - 6.0f, plus.y - 0.75f, 12.0f, 1.5f);
            g.fillRect (plus.x - 0.75f, plus.y - 6.0f, 1.5f, 12.0f);

            int row = 0;
            for (int l = 0; l < Shape::numLayers; ++l)
            {
                if (! proc.layerUsed (l)) continue;

                auto r = rowRect (row++).toFloat();
                const auto col = layerColour (l);
                const bool sel = l == selected;
                const bool on = proc.layerOn (l);

                if (sel)
                {
                    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.82f), r.getX(), r.getY(),
                                                             col.withAlpha (0.55f), r.getRight(), r.getBottom(), false));
                    g.fillRoundedRectangle (r, 4.0f);
                }
                else
                {
                    g.setColour (col.withAlpha (on ? 0.9f : 0.35f));
                    g.fillRect (r.getX(), r.getY() + 3.0f, 2.0f, r.getHeight() - 6.0f);
                }

                const auto primary = sel ? juce::Colours::white : text.withAlpha (on ? 1.0f : 0.45f);
                g.setColour (sel ? juce::Colours::white : col.withAlpha (on ? 1.0f : 0.45f));
                g.setFont (font (16.0f));
                g.drawText (juce::String (l + 1), r.withWidth (40.0f).translated (4.0f, 0.0f), juce::Justification::centred);

                const auto name = MotionProcessor::fxNames()[proc.layerFx (l)];
                g.setColour (primary);
                g.setFont (font (13.0f));
                g.drawText (name, (int) r.getX() + 54, (int) r.getY() + 7, 110, 18, juce::Justification::centredLeft);
                g.setColour (sel ? juce::Colours::white.withAlpha (0.75f) : dim);
                g.setFont (font (10.5f));
                g.drawText (name.toLowerCase(), (int) r.getX() + 54, (int) r.getY() + 25, 110, 14, juce::Justification::centredLeft);

                const auto dots = menuRect (r.toNearestInt()).toFloat().getCentre();
                g.setColour (primary);
                for (int d = -1; d <= 1; ++d)
                    g.fillEllipse (dots.x + 5.0f * (float) d - 1.2f, dots.y - 1.2f, 2.4f, 2.4f);
            }

            if (proc.numUsedLayers() < Shape::numLayers)
            {
                auto r = addRect().toFloat();
                juce::Path outline, dashed;
                outline.addRoundedRectangle (r.reduced (0.5f), 5.0f);
                const float dash[2] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
                g.setColour (dim.withAlpha (0.6f));
                g.fillPath (dashed);
                g.setColour (text);
                g.setFont (font (11.0f));
                g.drawText ("+ ADD LAYER", r, juce::Justification::centred);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto pos = e.getPosition();
            if (plusRect().expanded (6).contains (pos) || (addRect().contains (pos) && proc.numUsedLayers() < Shape::numLayers))
            {
                if (onAdd) onAdd (this);
                return;
            }

            int row = 0;
            for (int l = 0; l < Shape::numLayers; ++l)
            {
                if (! proc.layerUsed (l)) continue;
                const auto r = rowRect (row++);
                if (r.contains (pos))
                {
                    if (menuRect (r).expanded (4).contains (pos) || e.mods.isRightButtonDown())
                    {
                        if (onMenu) onMenu (l, this);
                    }
                    else if (onSelect)
                    {
                        onSelect (l);
                    }
                    return;
                }
            }
        }

    private:
        juce::Rectangle<int> plusRect() const { return { getWidth() - 44, 10, 20, 20 }; }
        juce::Rectangle<int> rowRect (int row) const { return { 16, 44 + row * 50, getWidth() - 20, 48 }; }
        juce::Rectangle<int> menuRect (juce::Rectangle<int> row) const { return row.removeFromRight (36); }
        juce::Rectangle<int> addRect() const { return { 16, getHeight() - 40, getWidth() - 20, 30 }; }

        MotionProcessor& proc;
    };

    // =========================================================================
    // Big curve view.
    //   Double-click: add / remove a point     drag: move a point
    //   Right-drag or mouse wheel on a segment: bend it      Shift: toggle snapping
    // With focus off it shows the whole pattern; clicking a segment focuses it.
    // In follow mode it shows the sidechain level instead.
    class ShapeView : public juce::Component
    {
    public:
        explicit ShapeView (MotionProcessor& p) : proc (p) { history.fill (0.0f); }

        int snapDiv = 16, gridDiv = 32;
        bool focus = true;
        std::function<void (int)> onPickStep;

        void setTarget (int layer, int step)
        {
            curLayer = layer;
            curHead = Shape::segmentHead (proc.getPattern (layer), step);
            reload();
        }

        void reload()
        {
            pattern = proc.getPattern (curLayer);
            curHead = Shape::segmentHead (pattern, curHead);
            pts = pattern.pts[(size_t) curHead];
            lastRev = proc.shapeRevision.load();
            selected = -1;
            dragging = false;
            curveSeg = -1;
            repaint();
        }

        void tick()
        {
            history[(size_t) historyPos] = proc.uiEnv.load();
            historyPos = (historyPos + 1) % (int) history.size();

            const auto rev = proc.shapeRevision.load();
            if (rev != lastRev && ! dragging && curveSeg < 0)
                reload();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            g.setColour (panel);
            g.fillRoundedRectangle (b, 3.0f);

            const auto r = plotRect();
            const auto col = layerColour (curLayer);
            const bool follow = proc.layerMode (curLayer) == 1;
            const int steps = proc.layerSteps (curLayer);

            if (gridDiv > 0)
            {
                const int majorEvery = juce::jmax (1, gridDiv / 4);
                for (int i = 0; i <= gridDiv; ++i)
                {
                    const float x = r.getX() + r.getWidth() * (float) i / (float) gridDiv;
                    g.setColour (juce::Colours::white.withAlpha (i % majorEvery == 0 ? 0.05f : 0.022f));
                    g.fillRect (x, r.getY(), 1.0f, r.getHeight());
                }
            }
            for (int i = 0; i <= 4; ++i)
            {
                const float y = r.getY() + r.getHeight() * (float) i / 4.0f;
                g.setColour (juce::Colours::white.withAlpha (0.03f));
                g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
            }

            if (follow)
            {
                paintFollow (g, r, col);
                return;
            }

            juce::Path path;
            const int N = 320;
            if (focus)
            {
                for (int i = 0; i <= N; ++i)
                {
                    const float t = (float) i / (float) N;
                    const juce::Point<float> p (r.getX() + t * r.getWidth(), r.getBottom() - Shape::evaluate (pts, t) * r.getHeight());
                    if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
                }
            }
            else
            {
                bool first = true;
                for (int i = 0; i <= N * 2; ++i)
                {
                    const float pos = (float) i / (float) (N * 2) * (float) steps;
                    const int s = juce::jlimit (0, steps - 1, (int) pos);
                    const int head = Shape::segmentHead (pattern, s);
                    const int len = Shape::segmentLength (pattern, head, steps);
                    const float u = (pos - (float) head) / (float) len;
                    const float v = pattern.muted[(size_t) s] ? 0.0f : Shape::evaluate (pattern.pts[(size_t) head], u);
                    const juce::Point<float> p (r.getX() + pos / (float) steps * r.getWidth(), r.getBottom() - v * r.getHeight());
                    if (first) { path.startNewSubPath (p); first = false; } else path.lineTo (p);
                }
            }

            juce::Path fill (path);
            fill.lineTo (r.getRight(), r.getBottom());
            fill.lineTo (r.getX(), r.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.20f), 0.0f, r.getY(), col.withAlpha (0.015f), 0.0f, r.getBottom(), false));
            g.fillPath (fill);

            g.setColour (col.withAlpha (0.16f));
            g.strokePath (path, juce::PathStrokeType (6.0f));
            g.setColour (col);
            g.strokePath (path, juce::PathStrokeType (2.0f));

            if (focus)
                for (int i = 0; i < (int) pts.size(); ++i)
                {
                    const auto p = toScreen (pts[(size_t) i]);
                    const float rad = 7.0f;
                    g.setColour (col.withAlpha (0.28f));
                    g.fillEllipse (p.x - rad - 3.0f, p.y - rad - 3.0f, (rad + 3.0f) * 2.0f, (rad + 3.0f) * 2.0f);
                    if (i == selected)
                    {
                        g.setColour (juce::Colours::white);
                        g.fillEllipse (p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);
                    }
                    else
                    {
                        g.setColour (panel);
                        g.fillEllipse (p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);
                        g.setColour (juce::Colours::white);
                        g.drawEllipse (p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f, 1.8f);
                    }
                }

            // live playhead
            const int playing = proc.uiStep[curLayer].load();
            if (playing >= 0)
            {
                const float stepPos = (float) playing + proc.uiT[curLayer].load();
                float x = -1.0f;
                if (! focus)
                {
                    x = r.getX() + stepPos / (float) steps * r.getWidth();
                }
                else if (Shape::segmentHead (pattern, playing) == curHead)
                {
                    const int len = Shape::segmentLength (pattern, curHead, steps);
                    x = r.getX() + (stepPos - (float) curHead) / (float) len * r.getWidth();
                }
                if (x >= 0.0f)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.45f));
                    g.fillRect (x, r.getY() - 6.0f, 1.0f, r.getHeight() + 12.0f);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            juce::Component::unfocusAllComponents();
            if (proc.layerMode (curLayer) == 1) return;

            if (! focus)
            {
                const auto r = plotRect();
                const int steps = proc.layerSteps (curLayer);
                const int s = juce::jlimit (0, steps - 1, (int) ((e.position.x - r.getX()) / r.getWidth() * (float) steps));
                if (onPickStep) onPickStep (s);
                return;
            }

            dragging = false;
            curveSeg = -1;
            proc.undo.beginNewTransaction();
            undoBefore = proc.getPattern (curLayer);
            edited = false;

            if (e.mods.isRightButtonDown())
            {
                const int seg = segmentAt (fromScreen (e.position, false).x);
                if (seg >= 0)
                {
                    curveSeg = seg;
                    curveStartY = e.position.y;
                    curveStartC = pts[(size_t) seg].c;
                }
                return;
            }

            selected = findPoint (e.position);
            dragging = selected >= 0;
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! focus) return;

            if (curveSeg >= 0 && curveSeg + 1 < (int) pts.size())
            {
                const float dir = pts[(size_t) curveSeg + 1].y >= pts[(size_t) curveSeg].y ? 1.0f : -1.0f;
                pts[(size_t) curveSeg].c = juce::jlimit (-1.0f, 1.0f, curveStartC + dir * (e.position.y - curveStartY) / 80.0f);
                commit();
                return;
            }

            if (! dragging || selected < 0 || selected >= (int) pts.size()) return;

            const bool snapNow = (snapDiv > 0) != e.mods.isShiftDown();
            const auto np = fromScreen (e.position, snapNow);
            auto& p = pts[(size_t) selected];
            p.y = np.y;

            if (selected > 0 && selected < (int) pts.size() - 1)       // end points keep x = 0 and x = 1
            {
                const float lo = pts[(size_t) selected - 1].x + 0.004f;
                const float hi = pts[(size_t) selected + 1].x - 0.004f;
                if (lo <= hi) p.x = juce::jlimit (lo, hi, np.x);
            }
            commit();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            dragging = false;
            curveSeg = -1;
            pushUndoIfEdited();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (! focus || proc.layerMode (curLayer) == 1) return;

            dragging = false;
            curveSeg = -1;
            undoBefore = proc.getPattern (curLayer);
            edited = false;

            const int hit = findPoint (e.position);
            if (hit > 0 && hit < (int) pts.size() - 1)                 // remove an inner point
            {
                pts.erase (pts.begin() + hit);
                selected = -1;
                commit();
                pushUndoIfEdited();
                return;
            }
            if (hit >= 0 || (int) pts.size() >= Shape::maxPoints) return;   // end points stay

            auto np = fromScreen (e.position, (snapDiv > 0) != e.mods.isShiftDown());
            np.x = juce::jlimit (0.01f, 0.99f, np.x);
            for (auto& q : pts)
                if (std::abs (q.x - np.x) < 0.01f) return;

            auto it = std::find_if (pts.begin(), pts.end(), [&] (const Shape::Point& q) { return q.x > np.x; });
            selected = (int) (it - pts.begin());
            pts.insert (it, np);
            commit();
            pushUndoIfEdited();
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (! focus || proc.layerMode (curLayer) == 1) return;

            const int seg = segmentAt (fromScreen (e.position, false).x);
            if (seg < 0 || seg + 1 >= (int) pts.size()) return;
            proc.undo.beginNewTransaction();
            undoBefore = proc.getPattern (curLayer);
            const float dir = pts[(size_t) seg + 1].y >= pts[(size_t) seg].y ? 1.0f : -1.0f;
            pts[(size_t) seg].c = juce::jlimit (-1.0f, 1.0f, pts[(size_t) seg].c - dir * w.deltaY * 0.5f);
            commit();
            pushUndoIfEdited();
        }

    private:
        juce::Rectangle<float> plotRect() const { return getLocalBounds().toFloat().reduced (9.0f, 28.0f); }

        void paintFollow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour col)
        {
            juce::Path path;
            const int size = (int) history.size();
            for (int i = 0; i < size; ++i)
            {
                const float v = history[(size_t) ((historyPos + i) % size)];
                const juce::Point<float> p (r.getX() + (float) i / (float) (size - 1) * r.getWidth(), r.getBottom() - v * r.getHeight());
                if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
            }
            juce::Path fill (path);
            fill.lineTo (r.getRight(), r.getBottom());
            fill.lineTo (r.getX(), r.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.25f), 0.0f, r.getY(), col.withAlpha (0.02f), 0.0f, r.getBottom(), false));
            g.fillPath (fill);
            g.setColour (col);
            g.strokePath (path, juce::PathStrokeType (2.0f));

            g.setColour (dim);
            g.setFont (font (11.0f));
            g.drawText ("following the sidechain level", r.withHeight (18.0f), juce::Justification::centredLeft);
        }

        juce::Point<float> toScreen (const Shape::Point& p) const
        {
            const auto r = plotRect();
            return { r.getX() + p.x * r.getWidth(), r.getBottom() - p.y * r.getHeight() };
        }

        Shape::Point fromScreen (juce::Point<float> s, bool snapNow) const
        {
            const auto r = plotRect();
            float x = juce::jlimit (0.0f, 1.0f, (s.x - r.getX()) / r.getWidth());
            float y = juce::jlimit (0.0f, 1.0f, (r.getBottom() - s.y) / r.getHeight());
            if (snapNow)
            {
                const float div = (float) (snapDiv > 0 ? snapDiv : 16);
                x = std::round (x * div) / div;
                y = std::round (y * 8.0f) / 8.0f;
            }
            return Shape::Point { x, y, 0.0f };
        }

        int findPoint (juce::Point<float> pos) const
        {
            int best = -1;
            float bestDist = 13.0f;
            for (int i = 0; i < (int) pts.size(); ++i)
            {
                const float d = toScreen (pts[(size_t) i]).getDistanceFrom (pos);
                if (d < bestDist) { bestDist = d; best = i; }
            }
            return best;
        }

        int segmentAt (float x) const
        {
            for (int i = 0; i + 1 < (int) pts.size(); ++i)
                if (x >= pts[(size_t) i].x && x <= pts[(size_t) i + 1].x) return i;
            return -1;
        }

        void commit()
        {
            proc.setSegmentPoints (curLayer, curHead, pts);
            pattern = proc.getPattern (curLayer);
            lastRev = proc.shapeRevision.load();
            edited = true;
            repaint();
        }

        void pushUndoIfEdited()
        {
            if (edited)
                proc.pushPatternUndo (curLayer, undoBefore);
            lastRev = proc.shapeRevision.load();
            edited = false;
        }

        MotionProcessor& proc;
        Shape::Pattern pattern, undoBefore;
        Shape::Points pts;
        int curLayer = 0, curHead = 0, selected = -1, curveSeg = -1;
        bool dragging = false, edited = false;
        float curveStartY = 0.0f, curveStartC = 0.0f;
        unsigned int lastRev = 0;
        std::array<float, 300> history;
        int historyPos = 0;
    };

    // =========================================================================
    // Ruler, segment bars, step lanes and step switches.
    //   Click a lane: select that segment       Drag a bar's right end: merge / split steps
    //   Click a circle: mute / unmute the step  Right-click a lane: shape menu
    class StepLanes : public juce::Component
    {
    public:
        explicit StepLanes (MotionProcessor& p) : proc (p) {}

        std::function<void (int)> onSelect;
        std::function<void (int, juce::Component*)> onContextMenu;

        void setLayer (int l, int step)
        {
            layer = l;
            selected = step;
            refresh();
        }

        void tick()
        {
            const auto rev = proc.shapeRevision.load();
            if (rev != lastRev) refresh();
            else repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto col = layerColour (layer);
            const int steps = proc.layerSteps (layer);
            const float cw = cellWidth (steps);
            const float w = (float) getWidth();
            const int selHead = Shape::segmentHead (pattern, selected);
            const int everyLabel = cw < 13.0f ? 2 : 1;

            // ruler
            g.setColour (dim.withAlpha (0.7f));
            g.fillRect (0.0f, 8.0f, w, 1.0f);
            g.setFont (font (cw < 17.0f ? 10.0f : 11.0f));
            for (int s = 0; s < steps; ++s)
            {
                const float x = (float) s * cw;
                const bool major = s % 8 == 0 && s > 0;
                g.setColour (dim.withAlpha (major ? 0.8f : 0.4f));
                g.fillRect (x, 8.0f, 1.0f, major ? 8.0f : 4.0f);
                if (s % everyLabel == 0)
                {
                    g.setColour (text.withAlpha (0.85f));
                    g.drawText (juce::String (s + 1), juce::Rectangle<float> (x, 16.0f, cw, 15.0f), juce::Justification::centred);
                }
            }

            const auto lanes = laneRect();

            // lanes
            for (int s = 0; s < steps; ++s)
            {
                const auto cell = juce::Rectangle<float> ((float) s * cw + 1.0f, lanes.getY(), cw - 2.0f, lanes.getHeight());
                const bool inSel = Shape::segmentHead (pattern, s) == selHead;
                g.setColour (inSel ? raised.brighter (0.05f).overlaidWith (layerColour (layer).withAlpha (0.07f)) : raised);
                g.fillRoundedRectangle (cell, 3.0f);
            }

            // one bar and one mini shape per segment
            for (int head = 0; head < steps; )
            {
                const int len = Shape::segmentLength (pattern, head, steps);
                const float x0 = (float) head * cw, x1 = (float) (head + len) * cw;

                g.setColour (head == selHead ? col.brighter (0.2f) : col.withAlpha (0.85f));
                g.fillRoundedRectangle (x0 + 1.5f, barY(), x1 - x0 - 3.0f, 4.0f, 2.0f);

                auto inner = lanes.reduced (0.0f, 42.0f);
                inner.setX (x0 + 1.0f);
                inner.setWidth (x1 - x0 - 2.0f);
                juce::Path path;
                const int N = juce::jmax (16, (int) (inner.getWidth() * 0.75f));
                for (int i = 0; i <= N; ++i)
                {
                    const float t = (float) i / (float) N;
                    const float v = Shape::evaluate (pattern.pts[(size_t) head], t);
                    const juce::Point<float> p (inner.getX() + t * inner.getWidth(), inner.getBottom() - v * inner.getHeight());
                    if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
                }
                g.setColour (col);
                g.strokePath (path, juce::PathStrokeType (1.3f));
                head += len;
            }

            // step switches
            for (int s = 0; s < steps; ++s)
            {
                const auto c = circleRect (s, steps);
                if (pattern.muted[(size_t) s])
                {
                    g.setColour (dim.withAlpha (0.35f));
                    g.fillEllipse (c.reduced (2.5f));
                }
                else
                {
                    g.setColour (s == selHead ? col : text.withAlpha (0.9f));
                    g.drawEllipse (c, 1.3f);
                }
            }

            // playhead
            const int playing = proc.uiStep[layer].load();
            if (playing >= 0)
            {
                const float x = ((float) playing + proc.uiT[layer].load()) * cw;
                juce::Path tri;
                tri.addTriangle (x - 4.5f, 1.0f, x + 4.5f, 1.0f, x, 7.0f);
                g.setColour (juce::Colours::white);
                g.fillPath (tri);
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                g.fillRect (x, lanes.getY() - 8.0f, 1.0f, lanes.getHeight() + 8.0f);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            setMouseCursor (barHandleAt (e.position) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            juce::Component::unfocusAllComponents();
            const int steps = proc.layerSteps (layer);
            const int s = juce::jlimit (0, steps - 1, (int) (e.position.x / cellWidth (steps)));
            dragHead = -1;

            if (circleRowContains (e.position.y))
            {
                proc.undo.beginNewTransaction();
                const auto before = proc.getPattern (layer);
                auto after = before;
                after.muted[(size_t) s] = ! after.muted[(size_t) s];
                proc.setPattern (layer, after);
                proc.pushPatternUndo (layer, before);
                refresh();
                return;
            }

            const int handle = barHandleAt (e.position);
            if (handle >= 0 && ! e.mods.isRightButtonDown())
            {
                proc.undo.beginNewTransaction();
                dragHead = handle;
                dragBefore = proc.getPattern (layer);
                dragged = false;
                return;
            }

            selectStep (s);

            if (e.mods.isRightButtonDown() && onContextMenu)
                onContextMenu (s, this);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const int steps = proc.layerSteps (layer);
            if (dragHead >= 0)
            {
                const int end = juce::jlimit (dragHead, steps - 1, (int) std::round (e.position.x / cellWidth (steps)) - 1);
                auto p = dragBefore;
                Shape::setSegmentEnd (p, dragHead, end, steps);
                proc.setPattern (layer, p);
                dragged = true;
                refresh();
            }
            else if (! e.mods.isRightButtonDown() && ! circleRowContains (e.getMouseDownPosition().toFloat().y))
            {
                selectStep (juce::jlimit (0, steps - 1, (int) (e.position.x / cellWidth (steps))));
            }
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragHead >= 0 && dragged)
                proc.pushPatternUndo (layer, dragBefore);
            dragHead = -1;
        }

    private:
        float cellWidth (int steps) const { return (float) getWidth() / (float) juce::jmax (1, steps); }
        float barY() const { return 34.0f; }
        juce::Rectangle<float> laneRect() const { return { 0.0f, 42.0f, (float) getWidth(), (float) getHeight() - 42.0f - 26.0f }; }
        juce::Rectangle<float> circleRect (int s, int steps) const
        {
            const float cw = cellWidth (steps);
            return { (float) s * cw + cw * 0.5f - 5.0f, (float) getHeight() - 16.0f, 10.0f, 10.0f };
        }
        bool circleRowContains (float y) const { return y >= (float) getHeight() - 22.0f; }

        int barHandleAt (juce::Point<float> pos) const
        {
            if (pos.y < barY() - 5.0f || pos.y > barY() + 9.0f) return -1;
            const int steps = proc.layerSteps (layer);
            const float cw = cellWidth (steps);
            for (int head = 0; head < steps; )
            {
                const int len = Shape::segmentLength (pattern, head, steps);
                if (std::abs (pos.x - (float) (head + len) * cw) < juce::jmin (6.0f, cw * 0.4f)) return head;
                head += len;
            }
            return -1;
        }

        void selectStep (int s)
        {
            if (s == selected) return;
            selected = s;
            if (onSelect) onSelect (s);
            repaint();
        }

        void refresh()
        {
            pattern = proc.getPattern (layer);
            lastRev = proc.shapeRevision.load();
            repaint();
        }

        MotionProcessor& proc;
        Shape::Pattern pattern, dragBefore;
        int layer = 0, selected = 0, dragHead = -1;
        bool dragged = false;
        unsigned int lastRev = 0;
    };
}
