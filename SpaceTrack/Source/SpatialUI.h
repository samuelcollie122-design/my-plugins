#pragma once
#include <JuceHeader.h>
#include <cmath>

// Look, 2D/3D field drawing and small widgets shared by SpaceTrack and SpaceMaster.
namespace SpaceUI
{
    inline juce::Colour accent() { return juce::Colour (0xff4fc3f7); }

    // ---------------- colours ----------------
    constexpr int paletteSize = 12;

    inline const juce::uint32* paletteColours()
    {
        static const juce::uint32 c[paletteSize] = {
            0xff4fc3f7, 0xff5c7cfa, 0xffb06cff, 0xffff6bb5, 0xffff5d5d, 0xffff9f43,
            0xffffd93d, 0xffa6e22e, 0xff3ddc84, 0xff2dd4bf, 0xffe6e9ef, 0xff8a93a3 };
        return c;
    }

    inline juce::Colour slotColour (int slot)
    {
        return juce::Colour::fromHSV (std::fmod ((float) slot * 0.137f, 1.0f), 0.6f, 0.95f, 1.0f);
    }

    // stored == 0 means "automatic"
    inline juce::Colour resolveColour (juce::uint32 stored, int slot)
    {
        return stored != 0 ? juce::Colour (stored) : slotColour (slot);
    }

    inline juce::Rectangle<float> paletteCell (juce::Rectangle<float> r, int i)
    {
        const float gap = 4.0f;
        const float cell = juce::jmin (r.getHeight(),
                                       (r.getWidth() - gap * (float) paletteSize) / (float) (paletteSize + 1));
        const float totalW = cell * (float) (paletteSize + 1) + gap * (float) paletteSize;
        const float x0 = r.getX() + (r.getWidth() - totalW) * 0.5f;
        return { x0 + (float) i * (cell + gap), r.getCentreY() - cell * 0.5f, cell, cell };
    }

    // cell 0 = "Auto", cells 1..12 = palette colours
    inline void drawPalette (juce::Graphics& g, juce::Rectangle<float> r, juce::uint32 current)
    {
        for (int i = 0; i <= paletteSize; ++i)
        {
            auto c = paletteCell (r, i);
            const juce::uint32 value = (i == 0) ? 0u : paletteColours()[i - 1];

            if (i == 0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.fillRoundedRectangle (c, 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                g.setFont (11.0f);
                g.drawText ("A", c.toNearestInt(), juce::Justification::centred);
            }
            else
            {
                g.setColour (juce::Colour (value));
                g.fillRoundedRectangle (c, 4.0f);
            }

            if (value == current)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (c.expanded (2.0f), 5.0f, 1.8f);
            }
        }
    }

    inline int paletteHit (juce::Rectangle<float> r, juce::Point<float> p)
    {
        for (int i = 0; i <= paletteSize; ++i)
            if (paletteCell (r, i).expanded (2.0f).contains (p)) return i;
        return -1;
    }

    inline juce::uint32 paletteValue (int hit) { return hit <= 0 ? 0u : paletteColours()[hit - 1]; }

    // ---------------- look & feel ----------------
    class SpaceLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        SpaceLookAndFeel()
        {
            setColour (juce::Slider::thumbColourId, accent());
            setColour (juce::Slider::trackColourId, accent().withAlpha (0.55f));
            setColour (juce::Slider::backgroundColourId, juce::Colour (0xff0d0f12));
            setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
            setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff12151a));
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::white.withAlpha (0.12f));
            setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2f3540));
            setColour (juce::TextButton::buttonOnColourId, accent());
            setColour (juce::TextButton::textColourOffId, juce::Colours::white);
            setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
            setColour (juce::TextEditor::highlightColourId, accent().withAlpha (0.4f));
        }
    };

    inline void drawBackground (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2f38), 0.0f, (float) bounds.getY(),
                                                 juce::Colour (0xff181b21), 0.0f, (float) bounds.getBottom(), false));
        g.fillRect (bounds);
    }

    // Centered plugin title with an accent underline
    inline void drawTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
    {
        auto f = area.toFloat();
        g.setColour (juce::Colour (0xff1b1e24));
        g.fillRoundedRectangle (f, 8.0f);

        const float y = f.getBottom() - 2.0f;
        juce::ColourGradient line (accent().withAlpha (0.0f), f.getX(), y,
                                   accent().withAlpha (0.0f), f.getRight(), y, false);
        line.addColour (0.5, accent());
        g.setGradientFill (line);
        g.fillRect (f.getX() + 10.0f, y, f.getWidth() - 20.0f, 2.0f);

        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
        g.drawText (text, area, juce::Justification::centred);
    }

    // ---------------- 2D / 3D switch ----------------
    class ModeSwitch : public juce::Component
    {
    public:
        std::function<void (bool)> onChange;

        void setMode3D (bool v) { if (v != is3D) { is3D = v; repaint(); } }
        bool getMode3D() const { return is3D; }

        void paint (juce::Graphics& g) override
        {
            const float alpha = isEnabled() ? 1.0f : 0.4f;
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (juce::Colour (0xff0d0f12));
            g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.0f);

            auto half = b.withWidth (b.getWidth() * 0.5f);
            auto thumb = (is3D ? half.translated (b.getWidth() * 0.5f, 0.0f) : half).reduced (2.0f);
            g.setColour (accent().withAlpha (alpha));
            g.fillRoundedRectangle (thumb, thumb.getHeight() * 0.5f);

            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.setColour (! is3D ? juce::Colour (0xff0d0f12) : juce::Colours::white.withAlpha (0.7f * alpha));
            g.drawText ("2D", half.toNearestInt(), juce::Justification::centred);
            g.setColour (is3D ? juce::Colour (0xff0d0f12) : juce::Colours::white.withAlpha (0.7f * alpha));
            g.drawText ("3D", half.translated (b.getWidth() * 0.5f, 0.0f).toNearestInt(), juce::Justification::centred);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (! isEnabled()) return;
            juce::Component::unfocusAllComponents();
            is3D = ! is3D;
            repaint();
            if (onChange) onChange (is3D);
        }

    private:
        bool is3D = false;
    };

    // ---------------- 3D projection ----------------
    // World: x = right, y = front, z = up, all in [-1, 1]. The camera sits behind the listener.
    constexpr float camDist = 6.0f;
    constexpr float camPitch = 0.5236f;   // 30 degrees

    struct Proj { juce::Point<float> c; float s; };

    inline Proj makeProj (juce::Rectangle<float> r)
    {
        return { r.getCentre(), 0.24f * juce::jmin (r.getWidth(), r.getHeight()) };
    }

    inline juce::Point<float> project (juce::Rectangle<float> r, float yaw, float x, float y, float z,
                                       float* scaleOut = nullptr)
    {
        auto pr = makeProj (r);
        const float cs = std::cos (yaw), sn = std::sin (yaw);
        const float xr = x * cs - y * sn;
        const float yr = x * sn + y * cs;
        const float scale = camDist / (camDist + yr);
        const float u = xr * scale;
        const float v = (yr * std::sin (camPitch) + z * std::cos (camPitch)) * scale;
        if (scaleOut != nullptr) *scaleOut = scale;
        return { pr.c.x + u * pr.s, pr.c.y - v * pr.s };
    }

    // Finds the floor position (at height z) that appears under screen point p
    inline void unprojectFloor (juce::Rectangle<float> r, float yaw, float z, juce::Point<float> p,
                                float& x, float& y)
    {
        auto pr = makeProj (r);
        const float u = (p.x - pr.c.x) / pr.s;
        const float v = (pr.c.y - p.y) / pr.s;
        const float denom = v / camDist - std::sin (camPitch);   // always negative, never zero
        const float yr = (z * std::cos (camPitch) - v) / denom;
        const float xr = u * (camDist + yr) / camDist;
        const float cs = std::cos (yaw), sn = std::sin (yaw);
        x = juce::jlimit (-1.0f, 1.0f, xr * cs + yr * sn);
        y = juce::jlimit (-1.0f, 1.0f, -xr * sn + yr * cs);
    }

    // ---------------- field drawing ----------------
    inline void drawField2D (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (juce::Colour (0xff12151a));
        g.fillRoundedRectangle (r, 8.0f);

        g.setColour (juce::Colours::white.withAlpha (0.08f));
        for (float k : { 0.5f, 1.0f })
            g.drawEllipse (r.getCentreX() - r.getWidth() * 0.5f * k, r.getCentreY() - r.getHeight() * 0.5f * k,
                           r.getWidth() * k, r.getHeight() * k, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawLine (r.getCentreX(), r.getY(), r.getCentreX(), r.getBottom());
        g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY());
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);

        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (12.0f);
        auto ri = r.toNearestInt();
        g.drawText ("FRONT", ri.withHeight (20), juce::Justification::centred);
        g.drawText ("BACK",  ri.withTrimmedTop (ri.getHeight() - 20), juce::Justification::centred);
        g.drawText ("L", ri.reduced (6), juce::Justification::centredLeft);
        g.drawText ("R", ri.reduced (6), juce::Justification::centredRight);

        g.setColour (juce::Colours::white.withAlpha (0.35f));      // listener
        g.fillEllipse (r.getCentreX() - 4.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
    }

    inline void drawField3D (juce::Graphics& g, juce::Rectangle<float> r, float yaw)
    {
        g.setColour (juce::Colour (0xff12151a));
        g.fillRoundedRectangle (r, 8.0f);

        auto P = [&] (float x, float y, float z) { return project (r, yaw, x, y, z); };

        g.setColour (juce::Colours::white.withAlpha (0.07f));
        for (float t : { -0.5f, 0.0f, 0.5f })
        {
            g.drawLine (juce::Line<float> (P (t, -1.0f, 0.0f), P (t, 1.0f, 0.0f)), 1.0f);
            g.drawLine (juce::Line<float> (P (-1.0f, t, 0.0f), P (1.0f, t, 0.0f)), 1.0f);
        }

        for (float rad : { 0.5f, 1.0f })
        {
            juce::Path ring;
            for (int i = 0; i <= 64; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 64.0f;
                auto p = P (std::sin (a) * rad, std::cos (a) * rad, 0.0f);
                if (i == 0) ring.startNewSubPath (p); else ring.lineTo (p);
            }
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.strokePath (ring, juce::PathStrokeType (1.0f));
        }

        juce::Path box;
        box.startNewSubPath (P (-1, -1, 0));
        box.lineTo (P (1, -1, 0));
        box.lineTo (P (1, 1, 0));
        box.lineTo (P (-1, 1, 0));
        box.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.strokePath (box, juce::PathStrokeType (1.2f));

        g.setColour (juce::Colours::white.withAlpha (0.10f));      // vertical axis
        g.drawLine (juce::Line<float> (P (0, 0, -1), P (0, 0, 1)), 1.0f);

        auto head = P (0, 0, 0);                                    // listener, nose points front
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawLine (juce::Line<float> (head, P (0, 0.22f, 0)), 2.0f);
        g.fillEllipse (head.x - 5.0f, head.y - 5.0f, 10.0f, 10.0f);

        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (12.0f);
        auto label = [&] (const char* t, juce::Point<float> p)
        {
            g.drawText (t, juce::Rectangle<float> (p.x - 30.0f, p.y - 8.0f, 60.0f, 16.0f).toNearestInt(),
                        juce::Justification::centred);
        };
        label ("FRONT", P (0, 1.16f, 0));
        label ("BACK",  P (0, -1.16f, 0));
        label ("L",     P (-1.16f, 0, 0));
        label ("R",     P (1.16f, 0, 0));
        label ("UP",    P (0, 0, 1.12f));

        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
    }

    // ---------------- sound sources ----------------
    inline juce::Point<float> sourcePos2D (juce::Rectangle<float> r, float x, float y)
    {
        return { r.getX() + (x + 1.0f) * 0.5f * r.getWidth(), r.getY() + (1.0f - y) * 0.5f * r.getHeight() };
    }

    inline void drawSource2D (juce::Graphics& g, juce::Rectangle<float> r, float x, float y,
                              juce::Colour colour, bool selected)
    {
        auto p = sourcePos2D (r, x, y);
        const float radius = 10.0f + 4.0f * y;                       // bigger = closer
        const float bright = 0.55f + 0.45f * (y + 1.0f) * 0.5f;      // brighter = closer
        g.setColour (colour.withMultipliedBrightness (bright));
        g.fillEllipse (p.x - radius, p.y - radius, radius * 2.0f, radius * 2.0f);
        if (selected)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (p.x - radius - 3.0f, p.y - radius - 3.0f, (radius + 3.0f) * 2.0f, (radius + 3.0f) * 2.0f, 2.0f);
        }
    }

    inline void drawSource3D (juce::Graphics& g, juce::Rectangle<float> r, float yaw, float x, float y, float z,
                              juce::Colour colour, bool selected)
    {
        float scale = 1.0f;
        auto top  = project (r, yaw, x, y, z, &scale);
        auto base = project (r, yaw, x, y, 0.0f);

        g.setColour (colour.withAlpha (0.45f));                      // stem to the floor
        g.drawLine (juce::Line<float> (base, top), 1.5f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));         // floor shadow
        g.fillEllipse (base.x - 7.0f * scale, base.y - 3.5f * scale, 14.0f * scale, 7.0f * scale);

        const float radius = 11.0f * scale;
        const float bright = 0.6f + 0.4f * juce::jlimit (0.0f, 1.0f, (scale - 0.8f) / 0.5f);
        g.setColour (colour.withMultipliedBrightness (bright));
        g.fillEllipse (top.x - radius, top.y - radius, radius * 2.0f, radius * 2.0f);
        if (selected)
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (top.x - radius - 3.0f, top.y - radius - 3.0f, (radius + 3.0f) * 2.0f, (radius + 3.0f) * 2.0f, 2.0f);
        }
    }
}
