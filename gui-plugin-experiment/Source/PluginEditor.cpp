#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
juce::Path roundedSegment (juce::Rectangle<float> r)
{
    juce::Path p;
    p.addRoundedRectangle (r, juce::jmin (r.getWidth(), r.getHeight()) * 0.45f);
    return p;
}
}

GlassPot::GlassPot()
{
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void GlassPot::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto labelArea = bounds.removeFromBottom (34.0f);
    auto knobArea = bounds.withSizeKeepingCentre (132.0f, 132.0f).translated (0.0f, 2.0f);
    auto centre = knobArea.getCentre();
    const float radius = knobArea.getWidth() * 0.5f;

    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.fillEllipse (knobArea.translated (0.0f, 10.0f).expanded (28.0f, 16.0f));
    g.setColour (juce::Colours::black.withAlpha (0.08f));
    g.fillEllipse (knobArea.translated (0.0f, 16.0f).expanded (40.0f, 24.0f));

    if (value > 0.02f)
    {
        const float intensity = juce::jlimit (0.0f, 1.0f, value);
        const auto glowCentre = centre.translated (-4.0f, -6.0f);
        const float baseSize = knobArea.getWidth() + 10.0f + intensity * 16.0f;

        for (int i = 0; i < 7; ++i)
        {
            const float t = static_cast<float> (i) / 6.0f;
            const float size = baseSize + t * (32.0f + intensity * 10.0f);
            const float alpha = (0.060f + intensity * 0.085f) * std::pow (1.0f - t, 2.15f);
            const auto colour = juce::Colour (0xff7d63ff).interpolatedWith (juce::Colour (0xff8fffff), 0.28f + t * 0.24f);

            g.setColour (colour.withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (glowCentre));
        }

        g.setColour (juce::Colour (0xffa5ffff).withAlpha (0.025f + intensity * 0.040f));
        g.fillEllipse (juce::Rectangle<float> (baseSize * 0.78f, baseSize * 0.78f).withCentre (glowCentre.translated (2.0f, 3.0f)));
    }

    g.setColour (juce::Colour (0xff050506));
    g.fillEllipse (knobArea.expanded (3.0f));

    juce::ColourGradient rim (juce::Colour (0xff3e3250), knobArea.getX() + 20.0f, knobArea.getY() + 6.0f,
                              juce::Colour (0xff030304), knobArea.getRight() - 14.0f, knobArea.getBottom() - 8.0f, true);
    rim.addColour (0.35, juce::Colour (0xff171020));
    rim.addColour (0.78, juce::Colour (0xff09090c));
    g.setGradientFill (rim);
    g.fillEllipse (knobArea);

    g.setColour (juce::Colour (0xff6c558b).withAlpha (0.42f));
    g.drawEllipse (knobArea.reduced (1.0f), 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (knobArea.reduced (5.0f), 1.0f);

    const int totalDots = 24;
    const float start = juce::MathConstants<float>::pi * 0.80f;
    const float sweep = juce::MathConstants<float>::pi * 1.40f;
    const int activeDots = juce::roundToInt (value * static_cast<float> (totalDots - 1));
    const float ledRadius = radius - 18.0f;

    for (int i = 0; i < totalDots; ++i)
    {
        const float a = start + sweep * static_cast<float> (i) / static_cast<float> (totalDots - 1);
        const auto dotCentre = centre + juce::Point<float> (std::cos (a), std::sin (a)) * ledRadius;
        const bool active = i <= activeDots;
        const auto led = active ? juce::Colour (0xff95a8ff) : juce::Colour (0xff262138);
        const float dot = active ? 2.9f : 2.2f;

        if (active)
        {
            g.setColour (led.withAlpha (0.22f));
            g.fillEllipse (dotCentre.x - 5.0f, dotCentre.y - 5.0f, 10.0f, 10.0f);
        }

        g.setColour (led);
        g.fillEllipse (dotCentre.x - dot, dotCentre.y - dot, dot * 2.0f, dot * 2.0f);
    }

    for (const auto& marker : { juce::MathConstants<float>::pi * 0.80f,
                                juce::MathConstants<float>::pi * 2.20f })
    {
        const auto p = centre + juce::Point<float> (std::cos (marker), std::sin (marker)) * (ledRadius + 8.0f);
        g.setColour (juce::Colour (0xffffd28a).withAlpha (0.20f));
        g.fillEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
        g.setColour (juce::Colour (0xffffd28a));
        g.fillEllipse (p.x - 2.8f, p.y - 2.8f, 5.6f, 5.6f);
    }

    auto display = juce::Rectangle<float> (0.0f, 0.0f, 42.0f, 31.0f).withCentre (centre.translated (0.0f, 3.0f));
    g.setColour (juce::Colour (0xff090712));
    g.fillRoundedRectangle (display.expanded (6.0f, 5.0f), 4.0f);
    g.setColour (juce::Colour (0xff332456).withAlpha (0.72f));
    g.drawRoundedRectangle (display.expanded (6.0f, 5.0f), 4.0f, 1.0f);

    g.setColour (juce::Colour (0xff8a65ff).withAlpha (0.10f));
    for (int y = 0; y < 6; ++y)
        g.fillRect (display.getX() - 4.0f, display.getY() + static_cast<float> (y * 5), display.getWidth() + 8.0f, 1.0f);

    const int displayValue = juce::roundToInt (value * 99.0f);
    const auto digitColour = juce::Colour (0xff9dffff);
    auto digits = juce::Rectangle<float> (0.0f, 0.0f, 35.0f, display.getHeight()).withCentre (display.getCentre());
    drawSevenSegmentDigit (g, displayValue / 10, digits.removeFromLeft (15.0f), digitColour);
    g.setColour (digitColour.withAlpha (0.95f));
    g.fillEllipse (digits.getX() + 1.0f, digits.getBottom() - 5.0f, 3.0f, 3.0f);
    digits.removeFromLeft (4.0f);
    drawSevenSegmentDigit (g, displayValue % 10, digits.removeFromLeft (15.0f), digitColour);

    juce::Path gloss;
    gloss.addPieSegment (knobArea.reduced (11.0f), juce::MathConstants<float>::pi * 1.08f,
                          juce::MathConstants<float>::pi * 1.92f, 0.28f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.13f), centre.x - 22.0f, knobArea.getY() + 14.0f,
                                             juce::Colours::white.withAlpha (0.0f), centre.x + 34.0f, centre.y + 34.0f, false));
    g.fillPath (gloss);

    juce::Path softGloss;
    softGloss.addPieSegment (knobArea.reduced (18.0f), juce::MathConstants<float>::pi * 1.02f,
                              juce::MathConstants<float>::pi * 1.98f, 0.46f);
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.fillPath (softGloss);

    g.setColour (juce::Colour (0xffffd28a));
    g.setFont (juce::FontOptions (24.0f, juce::Font::plain));
    g.drawText ("Output", labelArea, juce::Justification::centredTop);
}

void GlassPot::mouseDown (const juce::MouseEvent&)
{
    dragStartValue = value;
}

void GlassPot::mouseDrag (const juce::MouseEvent& e)
{
    setValueFromDrag (e);
}

void GlassPot::mouseDoubleClick (const juce::MouseEvent&)
{
    value = 0.60f;
    repaint();
}

bool GlassPot::hitTest (int x, int y)
{
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromBottom (34.0f);
    auto knobArea = bounds.withSizeKeepingCentre (132.0f, 132.0f).translated (0.0f, 2.0f);
    return juce::Point<float> (static_cast<float> (x), static_cast<float> (y))
               .getDistanceFrom (knobArea.getCentre()) <= knobArea.getWidth() * 0.5f + 18.0f;
}

void GlassPot::setValueFromDrag (const juce::MouseEvent& e)
{
    value = juce::jlimit (0.0f, 1.0f, dragStartValue - static_cast<float> (e.getDistanceFromDragStartY()) * 0.006f);
    repaint();
}

void GlassPot::drawSevenSegmentDigit (juce::Graphics& g, int digit, juce::Rectangle<float> area, juce::Colour colour) const
{
    static constexpr bool segments[10][7] = {
        { true,  true,  true,  true,  true,  true,  false },
        { false, true,  true,  false, false, false, false },
        { true,  true,  false, true,  true,  false, true  },
        { true,  true,  true,  true,  false, false, true  },
        { false, true,  true,  false, false, true,  true  },
        { true,  false, true,  true,  false, true,  true  },
        { true,  false, true,  true,  true,  true,  true  },
        { true,  true,  true,  false, false, false, false },
        { true,  true,  true,  true,  true,  true,  true  },
        { true,  true,  true,  true,  false, true,  true  },
    };

    digit = juce::jlimit (0, 9, digit);
    const float t = 3.0f;
    const auto lit = colour;
    const auto dim = colour.withAlpha (0.08f);

    const juce::Rectangle<float> s[7] = {
        { area.getX() + 3.0f, area.getY(), area.getWidth() - 6.0f, t },
        { area.getRight() - t, area.getY() + 3.0f, t, area.getHeight() * 0.5f - 4.0f },
        { area.getRight() - t, area.getCentreY() + 1.0f, t, area.getHeight() * 0.5f - 4.0f },
        { area.getX() + 3.0f, area.getBottom() - t, area.getWidth() - 6.0f, t },
        { area.getX(), area.getCentreY() + 1.0f, t, area.getHeight() * 0.5f - 4.0f },
        { area.getX(), area.getY() + 3.0f, t, area.getHeight() * 0.5f - 4.0f },
        { area.getX() + 3.0f, area.getCentreY() - t * 0.5f, area.getWidth() - 6.0f, t },
    };

    for (int i = 0; i < 7; ++i)
    {
        g.setColour (segments[digit][i] ? lit : dim);
        g.fillPath (roundedSegment (s[i]));
    }
}

DecayPot::DecayPot()
{
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void DecayPot::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto labelArea = bounds.removeFromBottom (36.0f);
    auto knobArea = bounds.withSizeKeepingCentre (156.0f, 156.0f).translated (0.0f, 3.0f);
    auto centre = knobArea.getCentre();
    const float radius = knobArea.getWidth() * 0.5f;
    const float reactiveAngle = (value - 0.5f) * 0.42f;
    const float reflectionScale = 0.72f + value * 0.28f;
    const auto reactiveTransform = juce::AffineTransform::translation (-centre.x, -centre.y)
                                       .scaled (reflectionScale)
                                       .rotated (reactiveAngle)
                                       .translated (centre.x + (value - 0.5f) * 5.0f,
                                                    centre.y + std::sin (value * juce::MathConstants<float>::twoPi) * 2.5f);

    if (value > 0.02f)
    {
        const auto glowCentre = centre.translated (2.0f, -4.0f);
        for (int i = 0; i < 6; ++i)
        {
            const float t = static_cast<float> (i) / 5.0f;
            const float size = knobArea.getWidth() + 14.0f + t * (26.0f + value * 12.0f);
            const float alpha = (0.030f + value * 0.090f) * std::pow (1.0f - t, 2.1f);
            g.setColour (juce::Colour (0xff7d63ff).interpolatedWith (juce::Colour (0xff8fffff), 0.35f + t * 0.20f)
                              .withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (glowCentre));
        }
    }

    g.setColour (juce::Colours::black.withAlpha (0.10f));
    g.fillEllipse (knobArea.translated (0.0f, 11.0f).expanded (42.0f, 28.0f));
    g.setColour (juce::Colours::black.withAlpha (0.08f));
    g.fillEllipse (knobArea.translated (0.0f, 19.0f).expanded (58.0f, 38.0f));

    const int totalDots = 42;
    const float start = juce::MathConstants<float>::pi * 0.77f;
    const float sweep = juce::MathConstants<float>::pi * 1.50f;
    const int activeDots = juce::roundToInt (value * static_cast<float> (totalDots - 1));
    const float ledRadius = radius + 12.0f;

    for (int i = 0; i < totalDots; ++i)
    {
        const float a = start + sweep * static_cast<float> (i) / static_cast<float> (totalDots - 1);
        const auto p = centre + juce::Point<float> (std::cos (a), std::sin (a)) * ledRadius;
        const bool active = i <= activeDots;

        if (active)
        {
            const auto glow = i == activeDots ? juce::Colour (0xff9d77ff) : juce::Colour (0xff56bfff);
            g.setColour (glow.withAlpha (0.18f));
            g.fillEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f);
            g.setColour (i > activeDots - 4 ? juce::Colour (0xff9b8cff) : juce::Colour (0xff82ceff));
            g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.62f));
            g.fillEllipse (p.x - 2.7f, p.y - 2.7f, 5.4f, 5.4f);
            g.setColour (juce::Colours::white.withAlpha (0.035f));
            g.drawEllipse (p.x - 2.7f, p.y - 2.7f, 5.4f, 5.4f, 0.8f);
        }
    }

    g.setColour (juce::Colour (0xff050506));
    g.fillEllipse (knobArea.expanded (2.0f));

    juce::ColourGradient outerRim (juce::Colour (0xff16111e), knobArea.getX() + 18.0f, knobArea.getY() + 8.0f,
                                   juce::Colour (0xff010102), knobArea.getRight() - 12.0f, knobArea.getBottom() - 10.0f, true);
    outerRim.addColour (0.35, juce::Colour (0xff09070d));
    outerRim.addColour (0.72, juce::Colour (0xff050507));
    g.setGradientFill (outerRim);
    g.fillEllipse (knobArea);

    g.setColour (juce::Colour (0xff6d5f82).withAlpha (0.38f));
    g.drawEllipse (knobArea.reduced (4.0f), 1.1f);
    g.setColour (juce::Colour (0xffffcf82).withAlpha (0.45f));
    const float dash[] = { 1.0f, 3.0f };
    for (int i = 0; i < 64; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * static_cast<float> (i) / 64.0f;
        const auto p1 = centre + juce::Point<float> (std::cos (a), std::sin (a)) * (radius - 9.0f);
        const auto p2 = centre + juce::Point<float> (std::cos (a), std::sin (a)) * (radius - 8.0f);
        if ((i % 2) == 0)
            g.drawDashedLine ({ p1, p2 }, dash, 2, 1.0f);
    }

    auto face = knobArea.reduced (30.0f);
    juce::ColourGradient faceGrad (juce::Colour (0xff111116), face.getX() + 14.0f, face.getY() + 10.0f,
                                   juce::Colour (0xff010102), face.getRight() - 4.0f, face.getBottom() - 2.0f, true);
    faceGrad.addColour (0.46, juce::Colour (0xff06070b));
    g.setGradientFill (faceGrad);
    g.fillEllipse (face);

    juce::Path lowerGlass;
    lowerGlass.addPieSegment (face.reduced (1.0f), juce::MathConstants<float>::pi * 0.06f + reactiveAngle * 0.45f,
                              juce::MathConstants<float>::pi * 0.96f + reactiveAngle * 0.45f, 0.22f);
    g.setColour (juce::Colour (0xffcdd7ff).withAlpha (0.055f + value * 0.045f));
    g.fillPath (lowerGlass);

    juce::Path leftFacet;
    leftFacet.startNewSubPath (face.getX() + 1.0f, centre.y - 7.0f);
    leftFacet.lineTo (centre.x - 29.0f, centre.y - 6.0f);
    leftFacet.lineTo (centre.x - 26.0f, centre.y + 42.0f);
    leftFacet.lineTo (face.getX() + 2.0f, centre.y + 48.0f);
    leftFacet.closeSubPath();
    g.setColour (juce::Colours::white.withAlpha (0.11f + (1.0f - value) * 0.09f));
    g.fillPath (leftFacet, reactiveTransform);

    juce::Path bottomFacet;
    bottomFacet.startNewSubPath (centre.x - 20.0f, centre.y + 40.0f);
    bottomFacet.lineTo (centre.x + 24.0f, centre.y + 36.0f);
    bottomFacet.lineTo (centre.x + 45.0f, centre.y + 50.0f);
    bottomFacet.lineTo (centre.x - 30.0f, centre.y + 53.0f);
    bottomFacet.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.12f), centre.x - 20.0f, centre.y + 38.0f,
                                             juce::Colours::white.withAlpha (0.0f), centre.x + 35.0f, centre.y + 56.0f, false));
    g.fillPath (bottomFacet, reactiveTransform);

    juce::Path movingSheen;
    movingSheen.startNewSubPath (centre.x - 7.0f, centre.y + 48.0f);
    movingSheen.lineTo (centre.x + 35.0f, centre.y + 38.0f);
    movingSheen.lineTo (centre.x + 48.0f, centre.y + 45.0f);
    movingSheen.lineTo (centre.x + 2.0f, centre.y + 56.0f);
    movingSheen.closeSubPath();
    g.setColour (juce::Colours::white.withAlpha (0.035f + value * 0.065f));
    g.fillPath (movingSheen, juce::AffineTransform::translation (-centre.x, -centre.y)
                                 .scaled (reflectionScale)
                                 .rotated (reactiveAngle * -1.8f)
                                 .translated (centre.x, centre.y));

    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (face.withTrimmedTop (face.getHeight() * 0.48f).withTrimmedBottom (face.getHeight() * 0.15f));

    auto pointerAngle = start + value * sweep;
    const auto marker = centre + juce::Point<float> (std::cos (pointerAngle), std::sin (pointerAngle)) * (radius - 20.0f);
    g.setColour (juce::Colour (0xffffd15f).withAlpha (0.24f));
    g.fillEllipse (marker.x - 11.0f, marker.y - 11.0f, 22.0f, 22.0f);
    juce::Path pointer;
    pointer.addRoundedRectangle (marker.x - 5.0f, marker.y - 12.0f, 10.0f, 20.0f, 3.0f);
    g.setColour (juce::Colour (0xfffff1b6));
    g.fillPath (pointer, juce::AffineTransform::rotation (pointerAngle + juce::MathConstants<float>::halfPi, marker.x, marker.y));

    auto display = juce::Rectangle<float> (0.0f, 0.0f, 42.0f, 31.0f).withCentre (centre.translated (-2.0f, 8.0f));
    g.setColour (juce::Colour (0xff07060f).withAlpha (0.82f));
    g.fillRoundedRectangle (display.expanded (5.0f, 4.0f), 4.0f);
    g.setColour (juce::Colour (0xff38285f).withAlpha (0.72f));
    g.drawRoundedRectangle (display.expanded (5.0f, 4.0f), 4.0f, 1.0f);

    const int displayValue = juce::roundToInt (value * 99.0f);
    const auto digitColour = juce::Colour (0xff9fffff);
    auto digits = juce::Rectangle<float> (0.0f, 0.0f, 35.0f, display.getHeight()).withCentre (display.getCentre());
    drawSevenSegmentDigit (g, displayValue / 10, digits.removeFromLeft (15.0f), digitColour);
    g.setColour (digitColour.withAlpha (0.95f));
    g.fillEllipse (digits.getX() + 1.0f, digits.getBottom() - 5.0f, 3.0f, 3.0f);
    digits.removeFromLeft (4.0f);
    drawSevenSegmentDigit (g, displayValue % 10, digits.removeFromLeft (15.0f), digitColour);

    g.setColour (juce::Colour (0xffffd28a));
    g.setFont (juce::FontOptions (24.0f, juce::Font::plain));
    g.drawText ("Decay", labelArea, juce::Justification::centredTop);
}

void DecayPot::mouseDown (const juce::MouseEvent&)
{
    dragStartValue = value;
}

void DecayPot::mouseDrag (const juce::MouseEvent& e)
{
    setValueFromDrag (e);
}

void DecayPot::mouseDoubleClick (const juce::MouseEvent&)
{
    value = 0.24f;
    repaint();
}

bool DecayPot::hitTest (int x, int y)
{
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromBottom (36.0f);
    auto knobArea = bounds.withSizeKeepingCentre (156.0f, 156.0f).translated (0.0f, 3.0f);
    return juce::Point<float> (static_cast<float> (x), static_cast<float> (y))
               .getDistanceFrom (knobArea.getCentre()) <= knobArea.getWidth() * 0.5f + 24.0f;
}

void DecayPot::setValueFromDrag (const juce::MouseEvent& e)
{
    value = juce::jlimit (0.0f, 1.0f, dragStartValue - static_cast<float> (e.getDistanceFromDragStartY()) * 0.006f);
    repaint();
}

void DecayPot::drawSevenSegmentDigit (juce::Graphics& g, int digit, juce::Rectangle<float> area, juce::Colour colour) const
{
    static constexpr bool segments[10][7] = {
        { true,  true,  true,  true,  true,  true,  false },
        { false, true,  true,  false, false, false, false },
        { true,  true,  false, true,  true,  false, true  },
        { true,  true,  true,  true,  false, false, true  },
        { false, true,  true,  false, false, true,  true  },
        { true,  false, true,  true,  false, true,  true  },
        { true,  false, true,  true,  true,  true,  true  },
        { true,  true,  true,  false, false, false, false },
        { true,  true,  true,  true,  true,  true,  true  },
        { true,  true,  true,  true,  false, true,  true  },
    };

    digit = juce::jlimit (0, 9, digit);
    const float t = 3.0f;
    const juce::Rectangle<float> s[7] = {
        { area.getX() + 3.0f, area.getY(), area.getWidth() - 6.0f, t },
        { area.getRight() - t, area.getY() + 3.0f, t, area.getHeight() * 0.5f - 4.0f },
        { area.getRight() - t, area.getCentreY() + 1.0f, t, area.getHeight() * 0.5f - 4.0f },
        { area.getX() + 3.0f, area.getBottom() - t, area.getWidth() - 6.0f, t },
        { area.getX(), area.getCentreY() + 1.0f, t, area.getHeight() * 0.5f - 4.0f },
        { area.getX(), area.getY() + 3.0f, t, area.getHeight() * 0.5f - 4.0f },
        { area.getX() + 3.0f, area.getCentreY() - t * 0.5f, area.getWidth() - 6.0f, t },
    };

    for (int i = 0; i < 7; ++i)
    {
        g.setColour (segments[digit][i] ? colour : colour.withAlpha (0.08f));
        g.fillPath (roundedSegment (s[i]));
    }
}

ParticleField::ParticleField()
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);
    particles.reserve (220);
    startTimerHz (50);
}

ParticleField::~ParticleField()
{
    stopTimer();
}

void ParticleField::resized()
{
    seedParticles();
}

void ParticleField::setEnergySource (std::function<float()> energy)
{
    energySource = std::move (energy);
}

void ParticleField::seedParticles()
{
    particles.clear();
    const int count = 185;
    for (int i = 0; i < count; ++i)
        particles.push_back (makeParticle());
}

ParticleField::Particle ParticleField::makeParticle()
{
    auto area = getLocalBounds().toFloat().reduced (34.0f, 26.0f);
    if (area.isEmpty())
        area = juce::Rectangle<float> (0.0f, 0.0f, 640.0f, 320.0f);

    const float palettePick = random.nextFloat();
    juce::Colour colour;
    if (palettePick < 0.30f)
        colour = juce::Colour (0xff8fffff);
    else if (palettePick < 0.54f)
        colour = juce::Colour (0xff8068ff);
    else if (palettePick < 0.74f)
        colour = juce::Colour (0xffffd26f);
    else if (palettePick < 0.88f)
        colour = juce::Colour (0xffff5c5c);
    else
        colour = juce::Colour (0xfff7f7ff);

    const float depth = 0.25f + random.nextFloat() * 0.75f;
    const float speed = 0.10f + depth * 0.45f;
    const float angle = random.nextFloat() * juce::MathConstants<float>::twoPi;

    Particle p;
    p.position = { area.getX() + random.nextFloat() * area.getWidth(),
                   area.getY() + random.nextFloat() * area.getHeight() };
    p.velocity = { std::cos (angle) * speed, std::sin (angle) * speed };
    p.colour = colour;
    p.radius = 0.65f + std::pow (random.nextFloat(), 2.0f) * 2.9f + depth * 1.25f;
    p.phase = random.nextFloat() * juce::MathConstants<float>::twoPi;
    p.twinkle = 0.45f + random.nextFloat() * 1.1f;
    p.depth = depth;
    p.life = 0.45f + random.nextFloat() * 0.55f;
    return p;
}

void ParticleField::timerCallback()
{
    time += 0.022f;
    auto area = getLocalBounds().toFloat().reduced (26.0f, 20.0f);
    if (area.isEmpty())
        return;

    const float energy = energySource != nullptr ? juce::jlimit (0.0f, 1.0f, energySource()) : 1.0f;
    const float motion = 0.075f + energy * 0.78f;

    for (auto& p : particles)
    {
        const float swirl = std::sin (time * 0.7f + p.phase) * 0.018f * p.depth * motion;
        const float cs = std::cos (swirl);
        const float sn = std::sin (swirl);
        p.velocity = { p.velocity.x * cs - p.velocity.y * sn,
                       p.velocity.x * sn + p.velocity.y * cs };
        p.position += p.velocity * motion;

        if (p.position.x < area.getX() - 20.0f)       p.position.x = area.getRight() + 20.0f;
        if (p.position.x > area.getRight() + 20.0f)   p.position.x = area.getX() - 20.0f;
        if (p.position.y < area.getY() - 20.0f)       p.position.y = area.getBottom() + 20.0f;
        if (p.position.y > area.getBottom() + 20.0f)  p.position.y = area.getY() - 20.0f;
    }

    repaint();
}

void ParticleField::paint (juce::Graphics& g)
{
    auto field = getLocalBounds().toFloat().reduced (34.0f, 30.0f);
    if (field.isEmpty())
        return;

    const float energy = energySource != nullptr ? juce::jlimit (0.0f, 1.0f, energySource()) : 1.0f;
    const float visibleFraction = 0.075f + energy * 0.775f;
    const size_t visibleCount = static_cast<size_t> (juce::roundToInt (static_cast<float> (particles.size()) * visibleFraction));
    const float glowScale = 0.18f + energy * 0.82f;

    juce::Path capsule;
    capsule.addRoundedRectangle (field, field.getHeight() * 0.46f);

    g.saveState();
    g.reduceClipRegion (capsule);

    juce::ColourGradient bed (juce::Colour (0xff050507), field.getX(), field.getY(),
                              juce::Colour (0xff111020), field.getRight(), field.getBottom(), false);
    bed.addColour (0.45, juce::Colour (0xff08070d));
    g.setGradientFill (bed);
    g.fillPath (capsule);

    g.setColour (juce::Colour (0xff1a1235).withAlpha (0.06f + energy * 0.16f));
    g.fillEllipse (field.withSizeKeepingCentre (field.getWidth() * 0.95f, field.getHeight() * 0.78f));
    g.setColour (juce::Colour (0xff074055).withAlpha (0.025f + energy * 0.075f));
    g.fillEllipse (field.withSizeKeepingCentre (field.getWidth() * 0.62f, field.getHeight() * 0.52f).translated (field.getWidth() * 0.12f, -8.0f));

    for (size_t particleIndex = 0; particleIndex < visibleCount; ++particleIndex)
    {
        const auto& p = particles[particleIndex];
        const float twinkle = 0.55f + 0.45f * std::sin (time * (1.4f + p.twinkle) + p.phase);
        const float alpha = juce::jlimit (0.0f, 1.0f, (0.05f + energy * 0.95f) * (0.20f + p.depth * 0.80f) * twinkle * p.life);
        const float r = p.radius * (0.55f + glowScale * 0.20f + p.depth * 0.65f);

        if (p.depth > 0.56f && energy > 0.08f)
        {
            g.setColour (p.colour.withAlpha (0.055f * alpha * glowScale));
            g.fillEllipse (juce::Rectangle<float> (r * (4.0f + glowScale * 4.0f), r * (4.0f + glowScale * 4.0f)).withCentre (p.position));
            g.setColour (p.colour.withAlpha (0.12f * alpha * glowScale));
            g.fillEllipse (juce::Rectangle<float> (r * (2.3f + glowScale * 1.7f), r * (2.3f + glowScale * 1.7f)).withCentre (p.position));
        }

        g.setColour (p.colour.withAlpha (0.80f * alpha));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (p.position));

        if (p.depth > 0.78f && energy > 0.18f)
        {
            const auto tail = p.position - p.velocity * (10.0f + p.depth * 18.0f);
            g.setColour (p.colour.withAlpha (0.10f * alpha * energy));
            g.drawLine (tail.x, tail.y, p.position.x, p.position.y, 0.6f + p.depth * energy);
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.025f + energy * 0.055f));
    g.drawLine (field.getX() + 40.0f, field.getY() + 8.0f, field.getRight() - 70.0f, field.getY() + 18.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.010f + energy * 0.025f));
    g.fillEllipse (field.reduced (10.0f, 18.0f).withTrimmedBottom (field.getHeight() * 0.58f));

    g.restoreState();

    g.setColour (juce::Colours::black.withAlpha (0.70f));
    g.drawRoundedRectangle (field.expanded (1.5f), field.getHeight() * 0.46f, 2.2f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (field.reduced (1.0f), field.getHeight() * 0.45f, 1.0f);
}

AnimatedBallPanel::AnimatedBallPanel()
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);
    startTimerHz (60);
}

AnimatedBallPanel::~AnimatedBallPanel()
{
    stopTimer();
}

void AnimatedBallPanel::resized()
{
    auto r = getLocalBounds().toFloat().reduced (24.0f);
    if (! r.contains (position))
        position = r.getCentre();
}

void AnimatedBallPanel::setControlSources (std::function<float()> speed, std::function<float()> trail)
{
    speedSource = std::move (speed);
    trailSource = std::move (trail);
}

void AnimatedBallPanel::timerCallback()
{
    auto area = getLocalBounds().toFloat().reduced (16.0f + radius);
    if (area.isEmpty())
        return;

    const float speedValue = speedSource != nullptr ? juce::jlimit (0.0f, 1.0f, speedSource()) : 0.6f;
    const float speedScale = 0.34f + speedValue * 1.70f;
    position += velocity * speedScale;

    trailPoints.push_back (position);
    const float trailValue = trailSource != nullptr ? juce::jlimit (0.0f, 1.0f, trailSource()) : 0.24f;
    const size_t maxTrail = static_cast<size_t> (juce::roundToInt (6.0f + trailValue * 52.0f));
    while (trailPoints.size() > maxTrail)
        trailPoints.erase (trailPoints.begin());

    if (position.x <= area.getX() || position.x >= area.getRight())
    {
        velocity.x *= -1.0f;
        position.x = juce::jlimit (area.getX(), area.getRight(), position.x);
    }

    if (position.y <= area.getY() || position.y >= area.getBottom())
    {
        velocity.y *= -1.0f;
        position.y = juce::jlimit (area.getY(), area.getBottom(), position.y);
    }

    repaint();
}

void AnimatedBallPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff07070b).withAlpha (0.92f));
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (juce::Colour (0xff242331));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);

    g.setColour (juce::Colour (0xff9dffff).withAlpha (0.045f));
    for (int x = 18; x < getWidth(); x += 18)
        g.drawVerticalLine (x, 0.0f, static_cast<float> (getHeight()));
    for (int y = 18; y < getHeight(); y += 18)
        g.drawHorizontalLine (y, 0.0f, static_cast<float> (getWidth()));

    g.setColour (juce::Colour (0xff8068ff).withAlpha (0.09f));
    g.fillEllipse (bounds.withSizeKeepingCentre (bounds.getWidth() * 0.70f, bounds.getHeight() * 0.72f));
    g.setColour (juce::Colours::white.withAlpha (0.055f));
    g.drawLine (bounds.getX() + 18.0f, bounds.getY() + 10.0f,
                bounds.getRight() - 28.0f, bounds.getY() + 16.0f, 1.0f);

    const float trailValue = trailSource != nullptr ? juce::jlimit (0.0f, 1.0f, trailSource()) : 0.24f;
    const float trailIntensity = 0.15f + trailValue * 0.85f;

    if (trailPoints.size() > 1)
    {
        for (size_t i = 0; i < trailPoints.size(); ++i)
        {
            const float age = static_cast<float> (i) / static_cast<float> (trailPoints.size() - 1);
            const float alpha = std::pow (age, 2.2f) * 0.22f * trailIntensity;
            const float size = radius * (0.45f + age * 0.95f) + trailValue * 9.0f;
            const auto colour = juce::Colour (0xff6fffff).interpolatedWith (juce::Colour (0xff8b65ff), 1.0f - age * 0.5f);

            g.setColour (colour.withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (size * 2.0f, size * 2.0f).withCentre (trailPoints[i]));
        }

        juce::Path streak;
        streak.startNewSubPath (trailPoints.front());
        for (size_t i = 1; i < trailPoints.size(); ++i)
            streak.lineTo (trailPoints[i]);
        g.setColour (juce::Colour (0xff9dffff).withAlpha (0.06f + trailValue * 0.16f));
        g.strokePath (streak, juce::PathStrokeType (1.0f + trailValue * 3.5f, juce::PathStrokeType::curved));
    }

    auto ball = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (position);

    for (int i = 0; i < 6; ++i)
    {
        const float t = static_cast<float> (i) / 5.0f;
        const float size = radius * 2.0f + 14.0f + trailValue * 14.0f + t * (24.0f + trailValue * 16.0f);
        const float alpha = (0.10f + trailValue * 0.12f) * std::pow (1.0f - t, 2.0f);
        g.setColour (juce::Colour (0xff6fffff).withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (position));
    }

    juce::ColourGradient body (juce::Colour (0xffbfffff), ball.getX() + 8.0f, ball.getY() + 5.0f,
                               juce::Colour (0xff003238), ball.getRight() - 4.0f, ball.getBottom() - 2.0f, true);
    body.addColour (0.34, juce::Colour (0xff16d6df));
    body.addColour (0.72, juce::Colour (0xff061416));
    g.setGradientFill (body);
    g.fillEllipse (ball);

    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.fillEllipse (ball.reduced (5.0f).withTrimmedBottom (radius * 0.9f).translated (-3.0f, -2.0f));

    auto core = ball.reduced (radius * 0.58f);
    g.setColour (juce::Colour (0xff061518));
    g.fillRoundedRectangle (core.expanded (3.0f, 2.0f), 3.0f);
    g.setColour (juce::Colour (0xff9dffff));
    g.drawRoundedRectangle (core.expanded (3.0f, 2.0f), 3.0f, 1.0f);
    g.setColour (juce::Colour (0xff9dffff).withAlpha (0.9f));
    g.fillEllipse (core.withSizeKeepingCentre (5.0f, 5.0f));

    g.setColour (juce::Colour (0xff9dffff).withAlpha (0.28f));
    g.drawEllipse (ball.expanded (1.0f), 1.1f);
}

NeuralPot::NeuralPot()
{
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void NeuralPot::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto labelArea = bounds.removeFromBottom (34.0f);
    auto knobArea = bounds.withSizeKeepingCentre (122.0f, 122.0f);
    const auto centre = knobArea.getCentre();
    const float radius = knobArea.getWidth() * 0.5f;
    const float intensity = juce::jlimit (0.0f, 1.0f, value);

    if (value > 0.02f)
    {
        for (int i = 0; i < 5; ++i)
        {
            const float t = static_cast<float> (i) / 4.0f;
            const float size = knobArea.getWidth() + 2.0f + t * (8.0f + intensity * 5.0f);
            const float alpha = (0.030f + intensity * 0.06f) * std::pow (1.0f - t, 2.1f);
            g.setColour (juce::Colour (0xff7d63ff).interpolatedWith (juce::Colour (0xff8fffff), 0.30f + t * 0.22f)
                              .withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (centre));
        }
    }

    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.fillEllipse (knobArea.translated (0.0f, 9.0f).expanded (26.0f, 14.0f));

    g.setColour (juce::Colour (0xff050506));
    g.fillEllipse (knobArea.expanded (3.0f));

    juce::ColourGradient rim (juce::Colour (0xff3a2f4c), knobArea.getX() + 16.0f, knobArea.getY() + 6.0f,
                              juce::Colour (0xff040406), knobArea.getRight() - 12.0f, knobArea.getBottom() - 8.0f, true);
    rim.addColour (0.40, juce::Colour (0xff140e1d));
    g.setGradientFill (rim);
    g.fillEllipse (knobArea);
    g.setColour (juce::Colour (0xff6c558b).withAlpha (0.42f));
    g.drawEllipse (knobArea.reduced (1.0f), 1.2f);

    const int totalDots = 22;
    const float start = juce::MathConstants<float>::pi * 0.80f;
    const float sweep = juce::MathConstants<float>::pi * 1.40f;
    const int activeDots = juce::roundToInt (value * static_cast<float> (totalDots - 1));
    const float ledRadius = radius - 14.0f;

    for (int i = 0; i < totalDots; ++i)
    {
        const float a = start + sweep * static_cast<float> (i) / static_cast<float> (totalDots - 1);
        const auto p = centre + juce::Point<float> (std::cos (a), std::sin (a)) * ledRadius;
        const bool active = value > 0.001f && i <= activeDots;

        if (active)
        {
            const auto led = juce::Colour (0xff8fffff).interpolatedWith (juce::Colour (0xff9b8cff), static_cast<float> (i) / static_cast<float> (totalDots - 1));
            g.setColour (led.withAlpha (0.22f));
            g.fillEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
            g.setColour (led);
            g.fillEllipse (p.x - 2.6f, p.y - 2.6f, 5.2f, 5.2f);
        }
        else
        {
            g.setColour (juce::Colour (0xff262138));
            g.fillEllipse (p.x - 2.1f, p.y - 2.1f, 4.2f, 4.2f);
        }
    }

    auto face = knobArea.reduced (20.0f);
    juce::ColourGradient faceGrad (juce::Colour (0xff15131c), face.getX() + 12.0f, face.getY() + 8.0f,
                                   juce::Colour (0xff020203), face.getRight() - 4.0f, face.getBottom() - 2.0f, true);
    faceGrad.addColour (0.5, juce::Colour (0xff09080f));
    g.setGradientFill (faceGrad);
    g.fillEllipse (face);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawEllipse (face.reduced (1.0f), 1.0f);

    const float pointerAngle = start + value * sweep;
    const auto tip = centre + juce::Point<float> (std::cos (pointerAngle), std::sin (pointerAngle)) * (radius - 16.0f);
    const auto pointerColour = juce::Colour (0xff2a2536).interpolatedWith (juce::Colour (0xff9dffff), intensity);
    g.setColour (pointerColour.withAlpha (0.14f + intensity * 0.24f));
    g.fillEllipse (tip.x - 6.0f, tip.y - 6.0f, 12.0f, 12.0f);
    g.setColour (pointerColour);
    g.drawLine (centre.x, centre.y, tip.x, tip.y, 2.6f);
    g.fillEllipse (tip.x - 3.2f, tip.y - 3.2f, 6.4f, 6.4f);

    g.setColour (juce::Colour (0xffffd28a));
    g.setFont (juce::FontOptions (22.0f, juce::Font::plain));
    g.drawText ("Neural", labelArea, juce::Justification::centredTop);
}

void NeuralPot::mouseDown (const juce::MouseEvent&)
{
    dragStartValue = value;
}

void NeuralPot::mouseDrag (const juce::MouseEvent& e)
{
    setValueFromDrag (e);
}

void NeuralPot::mouseDoubleClick (const juce::MouseEvent&)
{
    value = 0.0f;
    repaint();
}

bool NeuralPot::hitTest (int x, int y)
{
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromBottom (34.0f);
    auto knobArea = bounds.withSizeKeepingCentre (122.0f, 122.0f);
    return juce::Point<float> (static_cast<float> (x), static_cast<float> (y))
               .getDistanceFrom (knobArea.getCentre()) <= knobArea.getWidth() * 0.5f + 16.0f;
}

void NeuralPot::setValueFromDrag (const juce::MouseEvent& e)
{
    value = juce::jlimit (0.0f, 1.0f, dragStartValue - static_cast<float> (e.getDistanceFromDragStartY()) * 0.006f);
    repaint();
}

NeuralNetworkPanel::NeuralNetworkPanel()
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);
    startTimerHz (50);
}

NeuralNetworkPanel::~NeuralNetworkPanel()
{
    stopTimer();
}

void NeuralNetworkPanel::resized()
{
    rebuild();
}

void NeuralNetworkPanel::setEnergySource (std::function<float()> energy)
{
    energySource = std::move (energy);
}

void NeuralNetworkPanel::setSourcePoint (juce::Point<float> source)
{
    sourcePoint = source;
    rebuild();
}

void NeuralNetworkPanel::rebuild()
{
    // The knob is the source node (layer 0); these are the layers fanning out from it.
    static constexpr int rightSizes[] = { 5, 6, 4 };
    const int numRight = static_cast<int> (std::size (rightSizes));

    layers.clear();
    nodePhase.clear();
    connections.clear();

    auto area = getLocalBounds().toFloat();
    if (area.isEmpty())
        area = juce::Rectangle<float> (0.0f, 0.0f, 560.0f, 200.0f);

    layers.push_back ({ sourcePoint });
    nodePhase.push_back (0.0f);

    const float left   = sourcePoint.x + 90.0f;
    const float right  = juce::jmin (area.getRight() - 44.0f, left + 116.0f);
    const float top    = area.getY() + 44.0f;
    const float bottom = area.getBottom() - 44.0f;

    for (int l = 0; l < numRight; ++l)
    {
        std::vector<juce::Point<float>> nodes;
        const float x = numRight > 1
                            ? left + (right - left) * static_cast<float> (l) / static_cast<float> (numRight - 1)
                            : right;
        const int count = rightSizes[l];
        for (int n = 0; n < count; ++n)
        {
            const float y = count > 1
                                ? top + (bottom - top) * static_cast<float> (n) / static_cast<float> (count - 1)
                                : (top + bottom) * 0.5f;
            nodes.push_back ({ x, y });
            nodePhase.push_back (random.nextFloat() * juce::MathConstants<float>::twoPi);
        }
        layers.push_back (std::move (nodes));
    }

    const int total = static_cast<int> (layers.size());
    for (int l = 0; l + 1 < total; ++l)
        for (int a = 0; a < static_cast<int> (layers[static_cast<size_t> (l)].size()); ++a)
            for (int b = 0; b < static_cast<int> (layers[static_cast<size_t> (l + 1)].size()); ++b)
                connections.push_back ({ l, a, b,
                                         random.nextFloat(),
                                         0.45f + random.nextFloat() * 0.85f,
                                         random.nextFloat() });
}

void NeuralNetworkPanel::timerCallback()
{
    time += 0.02f;
    repaint();
}

void NeuralNetworkPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float energy = energySource != nullptr ? juce::jlimit (0.0f, 1.0f, energySource()) : 0.0f;

    if (layers.empty())
        return;

    // Energy halo radiating out of the knob into the field.
    if (energy > 0.01f)
    {
        for (int i = 0; i < 5; ++i)
        {
            const float t = static_cast<float> (i) / 4.0f;
            const float size = 80.0f + t * 90.0f + energy * 55.0f;
            const float alpha = 0.030f * energy * std::pow (1.0f - t, 2.0f);
            g.setColour (juce::Colour (0xff8068ff).interpolatedWith (juce::Colour (0xff8fffff), 0.30f + t * 0.22f)
                              .withAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (sourcePoint));
        }
    }

    const juce::Colour dim (0xff1c2030);
    const juce::Colour live = juce::Colour (0xff8fffff).interpolatedWith (juce::Colour (0xff9b8cff), 0.35f);

    for (const auto& c : connections)
    {
        const auto a = layers[static_cast<size_t> (c.layer)][static_cast<size_t> (c.from)];
        const auto b = layers[static_cast<size_t> (c.layer + 1)][static_cast<size_t> (c.to)];

        const auto base = dim.interpolatedWith (live, energy);
        g.setColour (base.withAlpha (0.10f + energy * 0.45f));
        g.drawLine (a.x, a.y, b.x, b.y, 0.8f + energy * 1.4f);

        if (c.gate < energy)
        {
            const float pos = std::fmod (time * c.speed + c.offset, 1.0f);
            const auto pt = a + (b - a) * pos;
            const auto pulse = juce::Colour (0xff9dffff);
            g.setColour (pulse.withAlpha (0.09f * energy));
            g.fillEllipse (pt.x - 3.0f, pt.y - 3.0f, 6.0f, 6.0f);
            g.setColour (pulse.withAlpha (0.9f * energy));
            g.fillEllipse (pt.x - 1.6f, pt.y - 1.6f, 3.2f, 3.2f);
        }
    }

    int phaseIndex = static_cast<int> (layers[0].size());
    for (size_t li = 1; li < layers.size(); ++li)
    {
        for (const auto& node : layers[li])
        {
            const float phase = nodePhase[static_cast<size_t> (phaseIndex++)];
            const float activation = juce::jlimit (0.0f, 1.0f,
                                                   energy * (0.55f + 0.45f * std::sin (time * 2.4f + phase)));
            const float r = 6.0f + activation * 2.0f;
            const auto fill = juce::Colour (0xff23222e).interpolatedWith (live, activation);

            if (activation > 0.08f)
            {
                g.setColour (juce::Colour (0xff9dffff).withAlpha (0.08f * activation));
                g.fillEllipse (juce::Rectangle<float> (r * 2.6f, r * 2.6f).withCentre (node));
            }

            g.setColour (fill);
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (node));
            g.setColour (juce::Colour (0xff8fffff).withAlpha (0.18f + activation * 0.55f));
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (node), 1.1f);

            if (activation > 0.2f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.5f * activation));
                g.fillEllipse (node.x - 1.6f, node.y - 1.6f, 3.2f, 3.2f);
            }
        }
    }

    const bool online = energy > 0.03f;
    g.setColour ((online ? juce::Colour (0xff9dffff) : juce::Colour (0xff4a4a58))
                     .withAlpha (online ? 0.45f + energy * 0.45f : 0.5f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::plain));
    g.drawText (online ? "NETWORK ACTIVE" : "NETWORK OFFLINE",
                bounds.reduced (16.0f, 11.0f), juce::Justification::bottomRight);
}

GuiPluginExperimentAudioProcessorEditor::GuiPluginExperimentAudioProcessorEditor (
    GuiPluginExperimentAudioProcessor& ownerProcessor)
    : AudioProcessorEditor (&ownerProcessor),
      audioProcessor (ownerProcessor)
{
    juce::ignoreUnused (audioProcessor);

    addAndMakeVisible (particleField);
    addAndMakeVisible (outputPot);
    addAndMakeVisible (decayPot);
    addAndMakeVisible (ballPanel);
    addAndMakeVisible (neuralNetwork);
    addAndMakeVisible (neuralPot);
    particleField.setEnergySource ([this] { return decayPot.getValue(); });
    ballPanel.setControlSources ([this] { return outputPot.getValue(); },
                                 [this] { return decayPot.getValue(); });
    neuralNetwork.setEnergySource ([this] { return neuralPot.getValue(); });

    setSize (660, 760);
}

void GuiPluginExperimentAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff24222b));

    auto bounds = getLocalBounds().toFloat().reduced (18.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2d2b35), bounds.getX(), bounds.getY(),
                                             juce::Colour (0xff181720), bounds.getRight(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

void GuiPluginExperimentAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (28, 26);
    auto top = area.removeFromTop (272);
    outputPot.setBounds (top.removeFromLeft (285).withSizeKeepingCentre (190, 205));
    decayPot.setBounds (top.withSizeKeepingCentre (250, 260));
    particleField.setBounds (getLocalBounds());
    particleField.toBack();
    area.removeFromTop (12);

    auto neuralSection = area.removeFromBottom (236);
    area.removeFromBottom (14);
    ballPanel.setBounds (area.reduced (20, 0));

    neuralNetwork.setBounds (neuralSection);
    const auto panelOrigin = neuralSection.getTopLeft();
    auto knobBounds = neuralSection.removeFromLeft (196).withSizeKeepingCentre (180, 200);
    neuralPot.setBounds (knobBounds);
    neuralNetwork.setSourcePoint ((knobBounds.getCentre() - panelOrigin).toFloat());
}
