#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace OptoUi
{
    const juce::Colour metal     (0xffbdbdba);
    const juce::Colour ink       (0xff17171a);
    const juce::Colour label     (0xff1d1d1f);
    const juce::Colour redLabel  (0xffa8201c);
    const juce::Colour barDark   (0xff232326);
    const juce::Colour barMid    (0xff3a3a3d);
    const juce::Colour barText   (0xffe8e8e4);

    inline juce::Font font (float size, bool bold = true)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline void drawMetalCap (juce::Graphics& g, juce::Point<float> c, float r)
    {
        juce::ColourGradient cap (juce::Colour (0xffededeb), c.x - r * 0.6f, c.y - r * 0.8f,
                                  juce::Colour (0xff77777a), c.x + r * 0.6f, c.y + r * 0.8f, false);
        cap.addColour (0.5, juce::Colour (0xffbcbcbb));
        g.setGradientFill (cap);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);
    }

    // Rotary sliders come in three looks, set with the "kind" property: big (default), small, dial
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setDefaultSansSerifTypefaceName ("Segoe UI");
            setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff333336));
            setColour (juce::PopupMenu::textColourId, juce::Colours::white);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff4b4b4f));
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        }

        juce::Font getPopupMenuFont() override { return font (14.0f, false); }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
        {
            const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
            const auto c = bounds.getCentre();
            const float side = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const float angle = start + pos * (end - start);
            const auto kind = s.getProperties()["kind"].toString();

            if (kind == "small")      drawSmall (g, c, side, angle, start, end);
            else if (kind == "dial")  drawDial (g, c, side, pos, angle, start, end);
            else                      drawBig (g, c, side, angle, pos, start, end);
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const auto kind = b.getProperties()["kind"].toString();

            if (kind == "analog")
            {
                const bool on = b.getToggleState();
                g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xfffff4d6) : juce::Colour (0xffbdb18d), 0.0f, r.getY(),
                                                         on ? juce::Colour (0xffe6d7a8) : juce::Colour (0xff8f8566), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (juce::Colours::black.withAlpha (0.65f));
                g.drawRoundedRectangle (r, 3.0f, 1.0f);
                return;
            }

            g.setColour (over ? juce::Colour (0xff46464a) : juce::Colour (0xff2c2c2f));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (juce::Colour (0xff77777b));
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            const bool analog = b.getProperties()["kind"].toString() == "analog";
            g.setColour (analog ? juce::Colour (0xff1c1a12) : barText);
            g.setFont (font (analog ? 12.5f : 14.0f, false));
            g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1);

            if (b.getProperties()["dropdown"])
            {
                const float x = (float) b.getWidth() - 14.0f, y = (float) b.getHeight() * 0.5f;
                juce::Path triangle;
                triangle.addTriangle (x - 4.0f, y - 2.5f, x + 4.0f, y - 2.5f, x, y + 3.0f);
                g.fillPath (triangle);
            }
        }

    private:
        static void drawBig (juce::Graphics& g, juce::Point<float> c, float side, float angle, float pos, float start, float end)
        {
            const float knobR = side * 0.62f;
            const float tickIn = knobR + 2.0f, tickOut = knobR + 7.0f, numberR = knobR + 18.0f;

            for (int i = 0; i <= 50; ++i)
            {
                const float a = start + (end - start) * (float) i / 50.0f;
                const bool major = i % 5 == 0;
                g.setColour (label.withAlpha (major ? 0.95f : 0.6f));
                g.drawLine ({ c.getPointOnCircumference (tickIn, a), c.getPointOnCircumference (major ? tickOut + 1.0f : tickOut - 2.0f, a) }, major ? 1.3f : 0.7f);
            }

            const int nearest = juce::roundToInt (pos * 10.0f);
            for (int i = 0; i <= 10; ++i)
            {
                const auto p = c.getPointOnCircumference (numberR, start + (end - start) * (float) i / 10.0f);
                g.setColour (i == nearest ? redLabel : label);
                g.setFont (font (12.5f));
                g.drawText (juce::String (i * 10), juce::Rectangle<float> (p.x - 15.0f, p.y - 8.0f, 30.0f, 16.0f), juce::Justification::centred);
            }

            // ribbed black body
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (c.x - knobR + 2.0f, c.y - knobR + 5.0f, knobR * 2.0f, knobR * 2.0f);

            juce::Path ribs;
            const int teeth = 36;
            for (int i = 0; i < teeth * 2; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / (float) (teeth * 2);
                const auto p = c.getPointOnCircumference (i % 2 == 0 ? knobR : knobR - 2.0f, a);
                if (i == 0) ribs.startNewSubPath (p); else ribs.lineTo (p);
            }
            ribs.closeSubPath();
            g.setColour (juce::Colour (0xff0d0d0e));
            g.fillPath (ribs);

            const float faceR = knobR * 0.82f;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3c), c.x, c.y - faceR, juce::Colour (0xff09090a), c.x, c.y + faceR, false));
            g.fillEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);

            g.setColour (juce::Colour (0xffececea));
            g.drawLine ({ c.getPointOnCircumference (knobR * 0.45f, angle), c.getPointOnCircumference (knobR * 0.97f, angle) }, 2.4f);

            drawMetalCap (g, c, knobR * 0.45f);
        }

        static void drawSmall (juce::Graphics& g, juce::Point<float> c, float side, float angle, float start, float end)
        {
            const float knobR = side * 0.62f;
            for (int i = 0; i <= 10; ++i)
            {
                const float a = start + (end - start) * (float) i / 10.0f;
                g.setColour (juce::Colour (0xffd8d8d4).withAlpha (i % 5 == 0 ? 0.95f : 0.55f));
                g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (c.getPointOnCircumference (knobR + 6.0f, a)));
            }

            g.setColour (juce::Colour (0xff0a0a0b));
            g.fillEllipse (c.x - knobR, c.y - knobR, knobR * 2.0f, knobR * 2.0f);
            g.setColour (juce::Colour (0xffe8e8e4));
            g.drawLine ({ c.getPointOnCircumference (knobR * 0.55f, angle), c.getPointOnCircumference (knobR * 0.98f, angle) }, 2.0f);
            drawMetalCap (g, c, knobR * 0.52f);
        }

        static void drawDial (juce::Graphics& g, juce::Point<float> c, float side, float pos, float angle, float start, float end)
        {
            const float r = side * 0.68f;
            juce::ignoreUnused (pos);
            drawMetalCap (g, c, r);

            juce::Path arc;
            arc.addCentredArc (c.x, c.y, r + 5.0f, r + 5.0f, 0.0f, start, end, true);
            g.setColour (juce::Colour (0xffd8d8d4));
            g.strokePath (arc, juce::PathStrokeType (1.5f));

            juce::Path head;
            const auto tip = c.getPointOnCircumference (r + 5.0f, end);
            head.addTriangle (tip.translated (-4.0f, -3.0f), tip.translated (4.0f, -3.0f), tip.translated (0.0f, 5.0f));
            g.fillPath (head);

            g.setColour (juce::Colour (0xff1a1a1c));
            g.drawLine ({ c.getPointOnCircumference (r * 0.25f, angle), c.getPointOnCircumference (r * 0.95f, angle) }, 2.2f);
        }
    };

    // Metal toggle. "Up" means the toggle state equals upIsOn.
    class BatSwitch : public juce::Button
    {
    public:
        explicit BatSwitch (bool upIsOnState) : juce::Button ({}), upIsOn (upIsOnState)
        {
            setClickingTogglesState (true);
        }

        void paintButton (juce::Graphics& g, bool, bool) override
        {
            const auto c = getLocalBounds().toFloat().getCentre();
            const bool up = getToggleState() == upIsOn;

            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (c.x - 11.0f, c.y - 9.0f, 24.0f, 24.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffdededb), c.x, c.y - 11.0f, juce::Colour (0xff77777a), c.x, c.y + 11.0f, false));
            g.fillEllipse (c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f);
            g.setColour (juce::Colour (0xff2a2a2c));
            g.fillEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f);

            const float dir = up ? -1.0f : 1.0f;
            juce::Path bat;
            bat.startNewSubPath (c.x - 3.0f, c.y);
            bat.lineTo (c.x + 3.0f, c.y);
            bat.lineTo (c.x + 5.5f, c.y + dir * 20.0f);
            bat.quadraticTo (c.x, c.y + dir * 25.0f, c.x - 5.5f, c.y + dir * 20.0f);
            bat.closeSubPath();
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f2ef), c.x - 5.0f, c.y, juce::Colour (0xff6f6f72), c.x + 5.0f, c.y, false));
            g.fillPath (bat);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.strokePath (bat, juce::PathStrokeType (0.8f));
        }

    private:
        bool upIsOn;
    };

    // A button drawn by a small function, for the top bar
    class IconButton : public juce::Button
    {
    public:
        using Draw = std::function<void (juce::Graphics&, juce::Rectangle<float>)>;
        explicit IconButton (Draw d) : juce::Button ({}), draw (std::move (d)) {}

        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (over && isEnabled() ? juce::Colour (0xff46464a) : juce::Colour (0xff2c2c2f));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (juce::Colour (0xff77777b));
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
            g.setColour (barText.withAlpha (isEnabled() ? 1.0f : 0.35f));
            draw (g, r);
        }

    private:
        Draw draw;
    };

    // Three-position pointer knob: input, reduction, output
    class MeterSelector : public juce::Component
    {
    public:
        int mode = 1;       // 0 input, 1 GR, 2 output
        std::function<void (int)> onChange;

        void paint (juce::Graphics& g) override
        {
            const auto c = getLocalBounds().toFloat().getCentre().translated (0.0f, 8.0f);
            const float r = 27.0f;
            const float angle = (float) (mode - 1) * 0.9f;

            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillEllipse (c.x - r + 3.0f, c.y - r + 6.0f, r * 2.0f, r * 2.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2e30), c.x, c.y - r, juce::Colour (0xff070708), c.x, c.y + r, false));
            g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

            juce::Path pointer;
            pointer.addRoundedRectangle (-5.0f, -r - 2.0f, 10.0f, r * 2.0f + 4.0f, 5.0f);
            g.setColour (juce::Colour (0xff121213));
            g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
            g.setColour (juce::Colour (0xffececea));
            g.drawLine ({ c.getPointOnCircumference (r * 0.25f, angle), c.getPointOnCircumference (r + 1.0f, angle) }, 2.2f);

            const auto text = [&] (const char* s, int m, juce::Rectangle<int> area)
            {
                g.setColour (mode == m ? redLabel : label);
                g.setFont (font (12.0f));
                g.drawText (s, area, juce::Justification::centred);
            };
            const int cx = (int) c.x, cy = (int) c.y;
            text ("INPUT",  0, { cx - 90, cy - 50, 62, 18 });
            text ("GR",     1, { cx - 20, cy - 58, 40, 18 });
            text ("OUTPUT", 2, { cx + 28, cy - 50, 66, 18 });
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const float dx = e.position.x - (float) getWidth() * 0.5f;
            const int next = dx < -18.0f ? 0 : (dx > 18.0f ? 2 : 1);
            if (next != mode)
            {
                mode = next;
                repaint();
                if (onChange) onChange (mode);
            }
        }

    private:
    };

    // Framed needle meter
    class VuMeter : public juce::Component
    {
    public:
        void setValueDb (float db)
        {
            position += 0.22f * (scalePosition (db) - position);    // close to the slow swing of a real needle at 30 updates a second
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            const auto window = b.reduced (26.0f, 26.0f);

            // bevelled frame
            const auto face = [&] (juce::Point<float> a, juce::Point<float> c, juce::Point<float> d, juce::Point<float> e, juce::Colour col)
            {
                juce::Path p;
                p.addQuadrilateral (a.x, a.y, c.x, c.y, d.x, d.y, e.x, e.y);
                g.setColour (col);
                g.fillPath (p);
            };
            const auto tl = b.getTopLeft(), tr = b.getTopRight(), bl = b.getBottomLeft(), br = b.getBottomRight();
            const auto wtl = window.getTopLeft(), wtr = window.getTopRight(), wbl = window.getBottomLeft(), wbr = window.getBottomRight();
            face (tl, tr, wtr, wtl, juce::Colour (0xff6c6c6d));
            face (bl, br, wbr, wbl, juce::Colour (0xff353536));
            face (tl, bl, wbl, wtl, juce::Colour (0xff535354));
            face (tr, br, wbr, wtr, juce::Colour (0xff454546));
            g.setColour (juce::Colour (0xff0d0d0e));
            g.drawRect (window.expanded (2.0f), 2.0f);

            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff5e9b9), window.getCentreX(), window.getY() + 10.0f,
                                                     juce::Colour (0xffd5b868), window.getCentreX(), window.getBottom(), false));
            g.fillRect (window);

            g.saveState();
            g.reduceClipRegion (window.toNearestInt());

            const juce::Point<float> pivot (window.getCentreX(), window.getBottom() + 44.0f);
            const float radius = window.getHeight() * 1.03f;
            const float span = 0.70f;
            const auto angleFor = [span] (float p) { return -span + 2.0f * span * p; };
            const float zero = scalePosition (0.0f);

            juce::Path scale;
            scale.addCentredArc (pivot.x, pivot.y, radius - 7.0f, radius - 7.0f, 0.0f, angleFor (0.0f), angleFor (zero), true);
            scale.addCentredArc (pivot.x, pivot.y, radius - 2.0f, radius - 2.0f, 0.0f, angleFor (0.0f), angleFor (zero), true);
            g.setColour (juce::Colour (0xff2a2015));
            g.strokePath (scale, juce::PathStrokeType (1.1f));

            juce::Path red;
            red.addCentredArc (pivot.x, pivot.y, radius - 5.0f, radius - 5.0f, 0.0f, angleFor (zero), angleFor (1.0f), true);
            g.setColour (juce::Colour (0xffb5301e));
            g.strokePath (red, juce::PathStrokeType (6.0f));

            for (int db : { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 })
            {
                const float a = angleFor (scalePosition ((float) db));
                const auto p0 = pivot.getPointOnCircumference (radius - 2.0f, a);
                const auto p1 = pivot.getPointOnCircumference (radius + 6.0f, a);
                const auto text = pivot.getPointOnCircumference (radius + 16.0f, a);
                const auto col = db > 0 ? juce::Colour (0xffb5301e) : juce::Colour (0xff2a2015);
                g.setColour (col);
                g.drawLine ({ p0, p1 }, 1.3f);
                g.setFont (font (db > 0 ? 13.0f : 11.5f));
                g.drawText (db > 0 ? juce::String (db) : (db == 0 ? juce::String ("0") : juce::String (db)),
                            juce::Rectangle<float> (text.x - 14.0f, text.y - 8.0f, 28.0f, 16.0f), juce::Justification::centred);
            }

            // own small mark in place of a logo
            g.setColour (juce::Colour (0xff4a3a22));
            g.setFont (font (9.5f));
            g.drawText ("OPTO", window.withTrimmedBottom (14.0f), juce::Justification::centredBottom);

            const auto tip = pivot.getPointOnCircumference (radius + 3.0f, angleFor (position));
            g.setColour (juce::Colour (0xffa32a1a));
            g.drawLine ({ pivot, tip }, 1.5f);
            g.restoreState();

            // pilot lamp
            const juce::Point<float> lamp (window.getRight() - 12.0f, window.getY() + 12.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff77777a), lamp.x - 2.0f, lamp.y - 3.0f, juce::Colour (0xff0b0b0c), lamp.x + 3.0f, lamp.y + 4.0f, true));
            g.fillEllipse (lamp.x - 5.0f, lamp.y - 5.0f, 10.0f, 10.0f);
        }

    private:
        // A VU scale is spaced by amplitude, not by dB: the low end is squeezed together and 0 sits about 70% across
        static float scalePosition (float db)
        {
            return juce::jlimit (0.0f, 1.0f, std::pow (10.0f, db / 20.0f) / std::pow (10.0f, 3.0f / 20.0f));
        }

        float position = scalePosition (0.0f);
    };
}

class OptoEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OptoEditor (OptoProcessor&);
    ~OptoEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void buildPanelImage();
    void showPresetMenu();
    void showMainMenu();
    void stepPreset (int delta);
    void loadPresetByIndex (int index);
    void savePresetAs();
    void updateSetupLabels();
    void updatePresetLabel();

    OptoUi::LookAndFeel laf;
    OptoProcessor& proc;

    juce::Slider gainKnob, peakKnob, hiFreqKnob, mixKnob, trimKnob;
    OptoUi::BatSwitch limitSwitch { false }, autoSwitch { true };
    OptoUi::MeterSelector selector;
    OptoUi::VuMeter meter;
    juce::TextButton analogBtn[3];

    OptoUi::IconButton undoBtn, redoBtn, prevBtn, nextBtn, menuBtn;
    juce::TextButton presetBtn, abBtn, setupBtn, saveBtn { "Save" };
    juce::TooltipWindow tooltips { this, 500 };

    juce::Image panelImage;
    float inputSmooth = 0.0f, outputSmooth = 0.0f;
    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<SliderAtt> gainAtt, peakAtt, hiFreqAtt, mixAtt, trimAtt;
    std::unique_ptr<ButtonAtt> limitAtt, autoAtt;
    std::unique_ptr<juce::ParameterAttachment> analogAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptoEditor)
};
