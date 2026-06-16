#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <unordered_set>

namespace vc {

// Shared base for the ported potentiometer looks. Holds the soft-disabled set
// (knobs that fade rather than hard-grey when disabled) and a helper that picks
// the transparency-layer alpha used to dim a disabled knob.
class KnobLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void markSoftDisabled(juce::Slider& s) { softDisabled_.insert(&s); }

protected:
    float layerAlphaFor(juce::Slider& s) const;
    std::unordered_set<juce::Slider*> softDisabled_;
};

// "Output"-style knob (ported GlassPot): LED dot ring inside, a 0.0-9.9
// seven-segment readout in the centre, subtle green glow. Used for Tone and the
// compact background-music encoders (size follows the slider bounds).
class BasicKnobLookAndFeel final : public KnobLookAndFeel {
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;
    // Knobs that show a separate label instead of the inner 0.0-9.9 readout.
    void markNoReadout(juce::Slider& s) { noReadout_.insert(&s); }

private:
    std::unordered_set<juce::Slider*> noReadout_;
};

// "Decay"-style knob (ported DecayPot): outer LED ring, glass face, pointer and
// a centre 0.0-9.9 readout. Used for the large Intensity control.
class IntensityKnobLookAndFeel final : public KnobLookAndFeel {
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;
};

// "Neural"-style knob (ported NeuralPot): knob body, LED ring, pointer and a
// soft glow. No numeric readout (the animated net conveys the value). Used for
// Noise Reduction, sitting on top of a NeuralNetworkPanel.
class NeuralKnobLookAndFeel final : public KnobLookAndFeel {
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;
};

// App-wide dark/green look applied as the default LookAndFeel. It cascades to
// every standard widget that doesn't have a more specific look set on it
// (combo boxes, progress bars, popup menus, text editors, the settings tabs,
// labels, scrollbars), so the whole UI shares the knob/button aesthetic.
class AppLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    AppLookAndFeel();
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void drawProgressBar(juce::Graphics&, juce::ProgressBar&, int width, int height,
                         double progress, const juce::String& textToShow) override;
};

// Dark, rounded, green-accented button to match the knob aesthetic. Toggle
// "on" state fills with the accent green; hover/press brighten slightly.
class PanelButtonLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    PanelButtonLookAndFeel();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&,
                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

// Subtle rounded backdrop + green-accented caption used to group the Intensity
// and Tone knobs into a single "area".
class EncoderGroupPanel final : public juce::Component {
public:
    EncoderGroupPanel();
    void paint(juce::Graphics&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EncoderGroupPanel)
};

} // namespace vc
