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

    auto display = juce::Rectangle<float> (0.0f, 0.0f, 58.0f, 31.0f).withCentre (centre.translated (0.0f, 3.0f));
    g.setColour (juce::Colour (0xff090712));
    g.fillRoundedRectangle (display.expanded (6.0f, 5.0f), 4.0f);
    g.setColour (juce::Colour (0xff332456).withAlpha (0.72f));
    g.drawRoundedRectangle (display.expanded (6.0f, 5.0f), 4.0f, 1.0f);

    g.setColour (juce::Colour (0xff8a65ff).withAlpha (0.10f));
    for (int y = 0; y < 6; ++y)
        g.fillRect (display.getX() - 4.0f, display.getY() + static_cast<float> (y * 5), display.getWidth() + 8.0f, 1.0f);

    const int displayValue = juce::roundToInt (value * 99.0f);
    const auto digitColour = juce::Colour (0xff9dffff);
    drawSevenSegmentDigit (g, displayValue / 10, display.removeFromLeft (15.0f), digitColour);
    g.setColour (digitColour.withAlpha (0.95f));
    g.fillEllipse (display.getX() + 1.0f, display.getBottom() - 5.0f, 3.0f, 3.0f);
    display.removeFromLeft (4.0f);
    drawSevenSegmentDigit (g, displayValue % 10, display.removeFromLeft (15.0f), digitColour);

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

GuiPluginExperimentAudioProcessorEditor::GuiPluginExperimentAudioProcessorEditor (
    GuiPluginExperimentAudioProcessor& ownerProcessor)
    : AudioProcessorEditor (&ownerProcessor),
      audioProcessor (ownerProcessor)
{
    juce::ignoreUnused (audioProcessor);

    addAndMakeVisible (outputPot);

    setSize (360, 300);
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
    outputPot.setBounds (getLocalBounds().withSizeKeepingCentre (190, 205));
}
