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

    // General tab (the default tab): app-wide preferences plus audio output —
    // the background-music behaviour combo, the "follow system" toggle and the
    // output-device chooser.
    void setGeneralControls(juce::Label& musicModeLabel, juce::ComboBox& musicModeBox,
                            juce::ToggleButton& followSystem,
                            juce::Label& outputLabel, juce::ComboBox& outputBox);

    // Pro tab: grouped sections of compact encoders plus a reset button in a
    // dedicated footer row (so it never overlaps the control grid).
    struct ProPair { juce::Label* label = nullptr; juce::Slider* slider = nullptr; };
    struct ProSection {
        juce::String title;
        std::vector<ProPair> pairs;
        int columns = 2;
    };
    void setProControls(std::vector<ProSection> sections, juce::Button& resetButton);

    // Project tab: a read-only report of what was analysed and decided for the
    // loaded media (loudness, detected fundamental, auto-EQ, etc.). Owned here —
    // unlike the other tabs it is plain text, not re-parented MainComponent
    // controls.
    void setProjectInfo(const juce::String& text);

    // About tab: app name/version, a short blurb, and credits/attribution for
    // the bundled third-party works (DeepFilterNet, JUCE, FFmpeg) with clickable
    // links. Owned here, like the Project tab. Call once with the running
    // version string.
    void setAboutInfo(const juce::String& version);

    void resized() override;

    // Natural size; used to size the hosting DialogWindow.
    static constexpr int kWidth = 520;
    // Pro tab drives height: tab bar (30) + page pad (24) + footer (36) + three
    // encoder rows (88 each) + two section separators (21 each) = 396.
    static constexpr int kHeight = 396;

private:
    // A bare page whose layout is delegated to a std::function so the two very
    // different tab layouts can live next to the controls they arrange.
    class Page : public juce::Component {
    public:
        std::function<void(juce::Rectangle<int>)> layout;
        void resized() override { if (layout) layout(getLocalBounds()); }
    };

    // A thin horizontal rule used to separate groups within a page.
    class Separator : public juce::Component {
    public:
        void paint(juce::Graphics& g) override {
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            const float y = getHeight() * 0.5f;
            g.drawLine(0.0f, y, static_cast<float>(getWidth()), y, 1.0f);
        }
    };

    // One credit row on the About tab: a description label, a clickable link to
    // the project, and a small license/attribution caption.
    struct CreditRow {
        std::unique_ptr<juce::Label> title;
        std::unique_ptr<juce::Label> detail;
        std::unique_ptr<juce::HyperlinkButton> link;
    };

    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };
    Page projectPage_;
    Page generalPage_;
    Page proPage_;
    Page aboutPage_;
    juce::TextEditor projectInfo_;
    juce::Label aboutTitle_, aboutBlurb_, creditsHeader_, aboutLicenseNote_;
    Separator aboutSeparator_;
    std::vector<CreditRow> creditRows_;
    juce::Label audioHeader_, musicHeader_;
    Separator generalSeparator_;
    std::vector<ProSection> proSections_;
    std::vector<std::unique_ptr<juce::Label>> proSectionHeaders_;
    std::vector<std::unique_ptr<Separator>> proSectionSeparators_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};
