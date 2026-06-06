#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <vector>

// Unified settings dialog body, shown in a DialogWindow popup (like Export).
// Hosts one tab per settings group. The controls themselves are owned by
// MainComponent and merely re-parented into the tab pages here, so closing the
// window leaves every control (and its value, callbacks and project-state
// wiring) untouched.
class SettingsComponent : public juce::Component {
public:
    SettingsComponent();

    // Audio Output tab: the "follow system" toggle plus the output-device
    // chooser (label + combo box).
    void setAudioControls(juce::ToggleButton& followSystem,
                          juce::Label& outputLabel, juce::ComboBox& outputBox);

    // Pro tab: label/slider pairs laid out in two columns, with the reset
    // button pinned to the bottom-right. Pairs are taken in row-major order
    // (left cell, right cell, left cell, ...); a null label/slider leaves that
    // half of the row empty.
    struct ProPair { juce::Label* label = nullptr; juce::Slider* slider = nullptr; };
    void setProControls(std::vector<ProPair> pairs, juce::Button& resetButton);

    void resized() override;

    // Natural size; used to size the hosting DialogWindow.
    static constexpr int kWidth = 520;
    static constexpr int kHeight = 392;

private:
    // A bare page whose layout is delegated to a std::function so the two very
    // different tab layouts can live next to the controls they arrange.
    class Page : public juce::Component {
    public:
        std::function<void(juce::Rectangle<int>)> layout;
        void resized() override { if (layout) layout(getLocalBounds()); }
    };

    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };
    Page audioPage_;
    Page proPage_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};
