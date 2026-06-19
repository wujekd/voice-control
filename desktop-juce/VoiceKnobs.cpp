#include "VoiceKnobs.h"

#include <cmath>

namespace vc {

namespace {
const juce::Colour kAccent(0xff6ee07a);      // app green
const juce::Colour kAccentBright(0xff9effae);
const juce::Colour kReadout(0xffd6ffdc);     // green-white digits

juce::Path roundedSegment(juce::Rectangle<float> r) {
    juce::Path p;
    p.addRoundedRectangle(r, juce::jmin(r.getWidth(), r.getHeight()) * 0.45f);
    return p;
}

void drawSevenSegmentDigit(juce::Graphics& g, int digit, juce::Rectangle<float> area,
                           juce::Colour colour) {
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

    digit = juce::jlimit(0, 9, digit);
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

    for (int i = 0; i < 7; ++i) {
        g.setColour(segments[digit][i] ? colour : colour.withAlpha(0.08f));
        g.fillPath(roundedSegment(s[i]));
    }
}

// Two-digit "X.X" readout (0.0-9.9) centred in a small display window.
void drawReadout(juce::Graphics& g, juce::Rectangle<float> display, float proportional,
                 juce::Colour colour) {
    g.setColour(juce::Colour(0xff070b07).withAlpha(0.82f));
    g.fillRoundedRectangle(display.expanded(5.0f, 4.0f), 4.0f);
    g.setColour(kAccent.withAlpha(0.32f));
    g.drawRoundedRectangle(display.expanded(5.0f, 4.0f), 4.0f, 1.0f);

    const int value = juce::roundToInt(juce::jlimit(0.0f, 1.0f, proportional) * 99.0f);
    auto digits = juce::Rectangle<float>(0.0f, 0.0f, 35.0f, display.getHeight()).withCentre(display.getCentre());
    drawSevenSegmentDigit(g, value / 10, digits.removeFromLeft(15.0f), colour);
    g.setColour(colour.withAlpha(0.95f));
    g.fillEllipse(digits.getX() + 1.0f, digits.getBottom() - 5.0f, 3.0f, 3.0f);
    digits.removeFromLeft(4.0f);
    drawSevenSegmentDigit(g, value % 10, digits.removeFromLeft(15.0f), colour);
}

// Map slider bounds onto a fixed-size reference square so each ported paint can
// keep its absolute coordinates and simply scale into whatever space it gets.
juce::AffineTransform refTransform(juce::Rectangle<float> bounds, float ref) {
    const float s = juce::jmin(bounds.getWidth(), bounds.getHeight()) / ref;
    return juce::AffineTransform::translation(-ref * 0.5f, -ref * 0.5f)
        .scaled(s)
        .translated(bounds.getCentreX(), bounds.getCentreY());
}

// Glass reflections that live around the rim of the face, leaving the centre
// (where the readout sits) clear. `variant` mirrors them to the opposite
// shoulders so two knobs of the same design read as a matched pair.
void drawEdgeReflections(juce::Graphics& g, juce::Rectangle<float> face, float pos, int variant) {
    const auto fc = face.getCentre();
    const float fw = face.getWidth();
    const float fh = face.getHeight();
    const float rr = fw * 0.5f;
    const float dir = variant == 0 ? 1.0f : -1.0f; // mirror the glints for the variation

    // Clip everything to the glass so the highlights hug its rim.
    juce::Graphics::ScopedSaveState save(g);
    juce::Path clip;
    clip.addEllipse(face);
    g.reduceClipRegion(clip);

    // Main reflection: a radial hot spot sitting right on the upper-shoulder rim
    // and falling off across the face, so the glass catches light all the way to
    // the edge while the centre (the readout) stays darker.
    {
        const auto hotspot = fc.translated(dir * -fw * 0.26f, -fh * 0.34f);
        juce::ColourGradient grad(juce::Colours::white.withAlpha(0.32f + pos * 0.08f),
                                  hotspot.x, hotspot.y,
                                  juce::Colours::white.withAlpha(0.0f),
                                  hotspot.x + fw * 0.55f, hotspot.y + fh * 0.55f, true);
        grad.addColour(0.45, juce::Colours::white.withAlpha(0.09f));
        g.setGradientFill(grad);
        g.fillEllipse(face);
    }

    // Crisp specular streak running along the upper rim, right to the edge.
    {
        juce::Path p;
        const float a0 = dir * -2.10f;
        const float a1 = dir * -0.55f;
        p.addCentredArc(fc.x, fc.y, rr - 2.0f, rr - 2.0f, 0.0f, juce::jmin(a0, a1), juce::jmax(a0, a1), true);
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.strokePath(p, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Soft secondary glint on the opposite lower shoulder.
    {
        const auto hotspot2 = fc.translated(dir * fw * 0.26f, fh * 0.30f);
        juce::ColourGradient grad(juce::Colours::white.withAlpha(0.14f + pos * 0.05f),
                                  hotspot2.x, hotspot2.y,
                                  juce::Colours::white.withAlpha(0.0f),
                                  hotspot2.x + fw * 0.34f, hotspot2.y + fh * 0.34f, true);
        g.setGradientFill(grad);
        g.fillEllipse(face);
    }
}

// The "Decay" knob body (outer LED ring, glass face, edge reflections, pointer
// and centre 0.0-9.9 readout), drawn in the reference space. Intensity and Noise
// Reduction share it; `variant` only changes the reflection arrangement.
void paintDecayKnobBody(juce::Graphics& g, float ref, float pos, int variant) {
    auto knobArea = juce::Rectangle<float>(0.0f, 0.0f, 122.0f, 122.0f).withCentre({ ref * 0.5f, ref * 0.5f });
    const auto centre = knobArea.getCentre();
    const float radius = knobArea.getWidth() * 0.5f;

    // Green glow when active.
    if (pos > 0.02f) {
        const auto glowCentre = centre.translated(0.0f, -2.0f);
        for (int i = 0; i < 6; ++i) {
            const float t = static_cast<float>(i) / 5.0f;
            const float size = knobArea.getWidth() + 4.0f + t * (8.0f + pos * 6.0f);
            const float alpha = (0.03f + pos * 0.08f) * std::pow(1.0f - t, 2.1f);
            g.setColour(kAccent.interpolatedWith(kAccentBright, 0.35f + t * 0.20f).withAlpha(alpha));
            g.fillEllipse(juce::Rectangle<float>(size, size).withCentre(glowCentre));
        }
    }

    g.setColour(juce::Colours::black.withAlpha(0.12f));
    g.fillEllipse(knobArea.translated(0.0f, 7.0f).expanded(18.0f, 10.0f));

    // Outer LED ring (sits just outside the knob body).
    const int totalDots = 42;
    const float start = juce::MathConstants<float>::pi * 0.77f;
    const float sweep = juce::MathConstants<float>::pi * 1.50f;
    const int activeDots = juce::roundToInt(pos * static_cast<float>(totalDots - 1));
    const float ledRadius = radius + 5.0f;
    for (int i = 0; i < totalDots; ++i) {
        const float a = start + sweep * static_cast<float>(i) / static_cast<float>(totalDots - 1);
        const auto p = centre + juce::Point<float>(std::cos(a), std::sin(a)) * ledRadius;
        const bool active = i <= activeDots;
        if (active) {
            const auto glow = i == activeDots ? kAccentBright : kAccent;
            g.setColour(glow.withAlpha(0.10f));
            g.fillEllipse(p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
            g.setColour(i > activeDots - 4 ? kAccentBright : kAccent);
            g.fillEllipse(p.x - 2.4f, p.y - 2.4f, 4.8f, 4.8f);
        } else {
            g.setColour(juce::Colours::black.withAlpha(0.60f));
            g.fillEllipse(p.x - 2.6f, p.y - 2.6f, 5.2f, 5.2f);
            g.setColour(juce::Colours::white.withAlpha(0.035f));
            g.drawEllipse(p.x - 2.6f, p.y - 2.6f, 5.2f, 5.2f, 0.8f);
        }
    }

    // Body.
    g.setColour(juce::Colour(0xff050606));
    g.fillEllipse(knobArea.expanded(2.0f));
    juce::ColourGradient outerRim(juce::Colour(0xff19211b), knobArea.getX() + 18.0f, knobArea.getY() + 8.0f,
                                  juce::Colour(0xff010202), knobArea.getRight() - 12.0f, knobArea.getBottom() - 10.0f, true);
    outerRim.addColour(0.35, juce::Colour(0xff0a0f0b));
    g.setGradientFill(outerRim);
    g.fillEllipse(knobArea);
    g.setColour(kAccent.withAlpha(0.34f));
    g.drawEllipse(knobArea.reduced(4.0f), 1.1f);

    // Glass face.
    auto face = knobArea.reduced(24.0f);
    juce::ColourGradient faceGrad(juce::Colour(0xff121613), face.getX() + 14.0f, face.getY() + 10.0f,
                                  juce::Colour(0xff010202), face.getRight() - 4.0f, face.getBottom() - 2.0f, true);
    faceGrad.addColour(0.46, juce::Colour(0xff06080a));
    g.setGradientFill(faceGrad);
    g.fillEllipse(face);

    // Reflections around the rim (kept clear of the centre readout).
    drawEdgeReflections(g, face, pos, variant);

    // Pointer marker.
    const float pointerAngle = start + pos * sweep;
    const auto marker = centre + juce::Point<float>(std::cos(pointerAngle), std::sin(pointerAngle)) * (radius - 16.0f);
    g.setColour(kAccent.withAlpha(0.24f));
    g.fillEllipse(marker.x - 8.0f, marker.y - 8.0f, 16.0f, 16.0f);
    juce::Path pointer;
    pointer.addRoundedRectangle(marker.x - 4.0f, marker.y - 10.0f, 8.0f, 16.0f, 2.5f);
    g.setColour(kReadout);
    g.fillPath(pointer, juce::AffineTransform::rotation(pointerAngle + juce::MathConstants<float>::halfPi, marker.x, marker.y));

    // Centre digital readout.
    auto display = juce::Rectangle<float>(0.0f, 0.0f, 42.0f, 31.0f).withCentre(centre.translated(0.0f, 2.0f));
    drawReadout(g, display, pos, kReadout);
}
} // namespace

float KnobLookAndFeel::layerAlphaFor(juce::Slider& s) const {
    if (s.isEnabled())
        return 1.0f;
    return softDisabled_.count(&s) > 0 ? 0.5f : 0.28f;
}

void BasicKnobLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width,
                                            int height, float pos, float, float, juce::Slider& slider) {
    const float layerAlpha = layerAlphaFor(slider);
    const bool fade = layerAlpha < 0.999f;
    if (fade)
        g.beginTransparencyLayer(layerAlpha);

    {
        juce::Graphics::ScopedSaveState save(g);
        const float ref = 150.0f;
        g.addTransform(refTransform(juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height), ref));

        auto knobArea = juce::Rectangle<float>(0.0f, 0.0f, 128.0f, 128.0f).withCentre({ ref * 0.5f, ref * 0.5f });
        const auto centre = knobArea.getCentre();
        const float radius = knobArea.getWidth() * 0.5f;
        const float intensity = juce::jlimit(0.0f, 1.0f, pos);

        // Soft drop shadow.
        g.setColour(juce::Colours::black.withAlpha(0.12f));
        g.fillEllipse(knobArea.translated(0.0f, 8.0f).expanded(18.0f, 10.0f));

        // Green glow when active (kept inside the reference box).
        if (pos > 0.02f) {
            for (int i = 0; i < 5; ++i) {
                const float t = static_cast<float>(i) / 4.0f;
                const float size = knobArea.getWidth() + 2.0f + t * (8.0f + intensity * 6.0f);
                const float alpha = (0.03f + intensity * 0.06f) * std::pow(1.0f - t, 2.1f);
                g.setColour(kAccent.withAlpha(alpha));
                g.fillEllipse(juce::Rectangle<float>(size, size).withCentre(centre));
            }
        }

        g.setColour(juce::Colour(0xff050606));
        g.fillEllipse(knobArea.expanded(3.0f));

        juce::ColourGradient rim(juce::Colour(0xff2c352e), knobArea.getX() + 18.0f, knobArea.getY() + 6.0f,
                                 juce::Colour(0xff050706), knobArea.getRight() - 12.0f, knobArea.getBottom() - 8.0f, true);
        rim.addColour(0.4, juce::Colour(0xff121a14));
        g.setGradientFill(rim);
        g.fillEllipse(knobArea);
        g.setColour(kAccent.withAlpha(0.38f));
        g.drawEllipse(knobArea.reduced(1.0f), 1.2f);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawEllipse(knobArea.reduced(5.0f), 1.0f);

        // LED dot ring inside the knob face.
        const int totalDots = 24;
        const float start = juce::MathConstants<float>::pi * 0.80f;
        const float sweep = juce::MathConstants<float>::pi * 1.40f;
        const int activeDots = juce::roundToInt(pos * static_cast<float>(totalDots - 1));
        const float ledRadius = radius - 18.0f;
        for (int i = 0; i < totalDots; ++i) {
            const float a = start + sweep * static_cast<float>(i) / static_cast<float>(totalDots - 1);
            const auto p = centre + juce::Point<float>(std::cos(a), std::sin(a)) * ledRadius;
            const bool active = pos > 0.001f && i <= activeDots;
            const auto led = active ? kAccentBright : juce::Colour(0xff253026);
            const float dot = active ? 2.9f : 2.2f;
            if (active) {
                g.setColour(led.withAlpha(0.22f));
                g.fillEllipse(p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
            }
            g.setColour(led);
            g.fillEllipse(p.x - dot, p.y - dot, dot * 2.0f, dot * 2.0f);
        }

        // Centre digital readout (unless this knob shows a separate label).
        if (noReadout_.count(&slider) == 0) {
            auto display = juce::Rectangle<float>(0.0f, 0.0f, 42.0f, 31.0f).withCentre(centre);
            drawReadout(g, display, pos, kReadout);
        }

        // Top gloss.
        juce::Path gloss;
        gloss.addPieSegment(knobArea.reduced(11.0f), juce::MathConstants<float>::pi * 1.08f,
                            juce::MathConstants<float>::pi * 1.92f, 0.28f);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.12f), centre.x - 22.0f, knobArea.getY() + 14.0f,
                                               juce::Colours::white.withAlpha(0.0f), centre.x + 34.0f, centre.y + 34.0f, false));
        g.fillPath(gloss);
    }

    if (fade)
        g.endTransparencyLayer();
}

void IntensityKnobLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width,
                                                int height, float pos, float, float, juce::Slider& slider) {
    const float layerAlpha = layerAlphaFor(slider);
    const bool fade = layerAlpha < 0.999f;
    if (fade)
        g.beginTransparencyLayer(layerAlpha);

    {
        juce::Graphics::ScopedSaveState save(g);
        const float ref = 150.0f;
        g.addTransform(refTransform(juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height), ref));
        paintDecayKnobBody(g, ref, pos, /*variant*/ 0);
    }

    if (fade)
        g.endTransparencyLayer();
}

void NeuralKnobLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width,
                                             int height, float pos, float, float, juce::Slider& slider) {
    const float layerAlpha = layerAlphaFor(slider);
    const bool fade = layerAlpha < 0.999f;
    if (fade)
        g.beginTransparencyLayer(layerAlpha);

    {
        juce::Graphics::ScopedSaveState save(g);
        const float ref = 150.0f;
        g.addTransform(refTransform(juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height), ref));
        // Same "Decay" body as Intensity, with the reflections mirrored.
        paintDecayKnobBody(g, ref, pos, /*variant*/ 1);
    }

    if (fade)
        g.endTransparencyLayer();
}

AppLookAndFeel::AppLookAndFeel() {
    auto scheme = getDarkColourScheme();
    using CS = juce::LookAndFeel_V4::ColourScheme;
    scheme.setUIColour(CS::windowBackground, juce::Colour(0xff181d18));
    scheme.setUIColour(CS::widgetBackground, juce::Colour(0xff1b211c));
    scheme.setUIColour(CS::menuBackground, juce::Colour(0xff161b16));
    scheme.setUIColour(CS::outline, kAccent.withAlpha(0.40f));
    scheme.setUIColour(CS::defaultFill, kAccent);
    scheme.setUIColour(CS::highlightedFill, kAccent);
    scheme.setUIColour(CS::defaultText, kReadout);
    scheme.setUIColour(CS::menuText, kReadout);
    scheme.setUIColour(CS::highlightedText, juce::Colour(0xff0c140d));
    setColourScheme(scheme);

    setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff181d18));
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1b211c));
    setColour(juce::ComboBox::textColourId, kReadout);
    setColour(juce::ComboBox::outlineColourId, kAccent.withAlpha(0.40f));
    setColour(juce::ComboBox::arrowColourId, kAccent);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff161b16));
    setColour(juce::PopupMenu::textColourId, kReadout);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.85f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xff0c140d));
    setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xff121712));
    setColour(juce::ProgressBar::foregroundColourId, kAccent);
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff141914));
    setColour(juce::TextEditor::textColourId, kReadout);
    setColour(juce::TextEditor::highlightColourId, kAccent.withAlpha(0.30f));
    setColour(juce::TextEditor::outlineColourId, kAccent.withAlpha(0.30f));
    setColour(juce::TextEditor::focusedOutlineColourId, kAccent.withAlpha(0.55f));
    setColour(juce::Label::textColourId, juce::Colour(0xffcfe8d2));
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b211c));
    setColour(juce::TextButton::textColourOffId, kReadout);
    setColour(juce::ToggleButton::textColourId, juce::Colour(0xffcfe8d2));
    setColour(juce::ToggleButton::tickColourId, kAccent);
    setColour(juce::TabbedComponent::backgroundColourId, juce::Colour(0xff181d18));
    setColour(juce::TabbedButtonBar::tabOutlineColourId, kAccent.withAlpha(0.30f));
    setColour(juce::TabbedButtonBar::frontOutlineColourId, kAccent.withAlpha(0.55f));
    setColour(juce::TabbedButtonBar::frontTextColourId, kReadout);
    setColour(juce::TabbedButtonBar::tabTextColourId, juce::Colour(0xff8aa890));
}

void AppLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                  int, int, int, int, juce::ComboBox& box) {
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    const float radius = 6.0f;
    const bool enabled = box.isEnabled();

    juce::ColourGradient grad(juce::Colour(0xff222a23), bounds.getX(), bounds.getY(),
                              juce::Colour(0xff141914), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(kAccent.withAlpha(enabled ? 0.40f : 0.18f));
    g.drawRoundedRectangle(bounds, radius, 1.2f);

    // Arrow.
    const float arrowX = static_cast<float>(width) - 16.0f;
    const float cy = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath(arrowX, cy - 2.0f);
    arrow.lineTo(arrowX + 5.0f, cy + 3.0f);
    arrow.lineTo(arrowX + 10.0f, cy - 2.0f);
    g.setColour(kAccent.withAlpha(enabled ? 0.9f : 0.4f));
    g.strokePath(arrow, juce::PathStrokeType(1.6f));
}

void AppLookAndFeel::drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                                     double progress, const juce::String& textToShow) {
    juce::ignoreUnused(bar);
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    const float radius = juce::jmin(6.0f, bounds.getHeight() * 0.5f);
    g.setColour(juce::Colour(0xff121712));
    g.fillRoundedRectangle(bounds, radius);

    if (progress >= 0.0 && progress <= 1.0) {
        auto fill = bounds.reduced(1.5f);
        fill = fill.withWidth(juce::jmax(0.0f, static_cast<float>(fill.getWidth() * progress)));
        g.setColour(kAccent.withAlpha(0.85f));
        g.fillRoundedRectangle(fill, juce::jmax(0.0f, radius - 1.5f));
    }

    g.setColour(kAccent.withAlpha(0.35f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);

    if (textToShow.isNotEmpty()) {
        g.setColour(kReadout);
        g.setFont(juce::Font(juce::FontOptions(juce::jmin(12.0f, bounds.getHeight() * 0.7f))));
        g.drawText(textToShow, bounds, juce::Justification::centred, false);
    }
}

PanelButtonLookAndFeel::PanelButtonLookAndFeel() {
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b211c));
    setColour(juce::TextButton::buttonOnColourId, kAccent);
    setColour(juce::TextButton::textColourOffId, kReadout);
    setColour(juce::TextButton::textColourOnId, juce::Colour(0xff0c140d));
}

void PanelButtonLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour&, bool isOver, bool isDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = 6.0f;
    const bool on = button.getToggleState();
    const bool enabled = button.isEnabled();

    juce::Colour base = on ? button.findColour(juce::TextButton::buttonOnColourId)
                           : button.findColour(juce::TextButton::buttonColourId);
    if (base.isTransparent())
        base = on ? kAccent : juce::Colour(0xff1b211c);
    if (isDown)
        base = base.brighter(0.12f);
    else if (isOver)
        base = base.brighter(0.06f);
    if (!enabled)
        base = base.withMultipliedAlpha(0.45f);

    if (on) {
        g.setColour(base);
        g.fillRoundedRectangle(bounds, radius);
    } else {
        juce::ColourGradient grad(base.brighter(0.06f), bounds.getX(), bounds.getY(),
                                  base.darker(0.35f), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bounds, radius);
    }

    g.setColour(kAccent.withAlpha(on ? 0.9f : (enabled ? 0.40f : 0.20f)));
    g.drawRoundedRectangle(bounds, radius, 1.2f);
}

void PanelButtonLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                            bool, bool) {
    const bool on = button.getToggleState();
    auto colour = button.findColour(on ? juce::TextButton::textColourOnId
                                       : juce::TextButton::textColourOffId);
    if (!button.isEnabled())
        colour = colour.withMultipliedAlpha(0.5f);
    g.setColour(colour);
    g.setFont(juce::Font(juce::FontOptions(juce::jmin(15.0f, static_cast<float>(button.getHeight()) * 0.42f))));
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, true);
}

EncoderGroupPanel::EncoderGroupPanel() {
    setInterceptsMouseClicks(false, false);
}

void EncoderGroupPanel::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff121712).withAlpha(0.55f));
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(kAccent.withAlpha(0.16f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
}

} // namespace vc
