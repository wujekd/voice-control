#pragma once

#include <juce_graphics/juce_graphics.h>

// Central design tokens for the desktop UI. One source of truth for colour,
// elevation and depth so panels, meters and knobs share a consistent visual
// language (single light source from the top, restrained accent hierarchy).
namespace vc::theme {

// --- Surfaces / elevation (light comes from the top) -------------------------
inline const juce::Colour windowTop    { 0xff1e2630 }; // app background, top (cool slate)
inline const juce::Colour windowBottom { 0xff131a20 }; // app background, bottom
inline const juce::Colour panelFill    { 0xff141b20 }; // grouped surface (inset)
inline const juce::Colour panelTopEdge { 0xffffffff }; // top highlight (use low alpha)
inline const juce::Colour panelShadow  { 0xff000000 }; // drop shadow (use low alpha)
inline const juce::Colour wellFill     { 0xff0a1311 }; // recessed readout / screen wells

// --- Accent (reserve bright tones for active state) --------------------------
inline const juce::Colour accent       { 0xff2dd4bf }; // primary teal
inline const juce::Colour accentBright { 0xff7ff0e4 }; // active / live highlight (cyan)
inline const juce::Colour accentDim    { 0xff4f7a76 }; // idle / secondary (desaturated teal)

// --- Text --------------------------------------------------------------------
inline const juce::Colour textPrimary   { 0xffe9f1ef };
inline const juce::Colour textSecondary { 0xff97a4ad };
inline const juce::Colour textMuted      { 0xff5c656d };
inline const juce::Colour readout        { 0xffd2fbf6 }; // numeric value digits (cyan-white)

// --- Status ------------------------------------------------------------------
inline const juce::Colour warn   { 0xffe0c050 }; // amber – caution / de-ess
inline const juce::Colour danger { 0xffe85858 }; // red – limiting / clip

// --- Meters ------------------------------------------------------------------
inline const juce::Colour ledOff { 0xff213030 }; // unlit LED / track (cool)

// Tabular numeral font for digital readouts – clean and monospaced so digits
// don't jitter as values change. Replaces the old seven-segment look.
inline juce::Font readoutFont(float height, bool bold = true) {
    auto opts = juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height,
                                  bold ? juce::Font::bold : juce::Font::plain);
    return juce::Font(opts);
}

// Paint a grouped surface with a single, consistent depth treatment: soft drop
// shadow below, inset fill, and a 1px top highlight catching the top light.
inline void paintPanel(juce::Graphics& g, juce::Rectangle<float> bounds,
                       float radius = 8.0f, float fillAlpha = 1.0f) {
    // Soft drop shadow underneath the panel.
    auto shadow = bounds.translated(0.0f, 1.5f);
    g.setColour(panelShadow.withAlpha(0.25f));
    g.fillRoundedRectangle(shadow, radius);

    g.setColour(panelFill.withAlpha(fillAlpha));
    g.fillRoundedRectangle(bounds, radius);

    // Top-edge highlight (light source above) fading down the upper rim.
    juce::ColourGradient topEdge(panelTopEdge.withAlpha(0.05f),
                                 bounds.getCentreX(), bounds.getY(),
                                 panelTopEdge.withAlpha(0.0f),
                                 bounds.getCentreX(), bounds.getY() + radius * 1.6f, false);
    g.setGradientFill(topEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
}

} // namespace vc::theme
