#include "SettingsComponent.h"

SettingsComponent::SettingsComponent() {
    setSize(kWidth, kHeight);

    tabs_.setOutline(0);
    tabs_.setTabBarDepth(30);
    const auto pageColour = juce::Colour(0xff2b2f36);
    tabs_.addTab("General", pageColour, &generalPage_, false);
    tabs_.addTab("Project", pageColour, &projectPage_, false);
    tabs_.addTab("Pro", pageColour, &proPage_, false);
    tabs_.addTab("About", pageColour, &aboutPage_, false);
    tabs_.setCurrentTabIndex(0); // General is the default tab
    addAndMakeVisible(tabs_);

    projectInfo_.setMultiLine(true);
    projectInfo_.setReadOnly(true);
    projectInfo_.setCaretVisible(false);
    projectInfo_.setScrollbarsShown(true);
    projectInfo_.setPopupMenuEnabled(false);
    projectInfo_.setFont(juce::Font(juce::FontOptions(13.0f)));
    projectInfo_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff14161b));
    projectInfo_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    projectInfo_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    projectInfo_.setColour(juce::TextEditor::textColourId, juce::Colours::white.withAlpha(0.85f));
    projectInfo_.setText("No project loaded.", juce::dontSendNotification);
    projectPage_.addAndMakeVisible(projectInfo_);
    projectPage_.layout = [this](juce::Rectangle<int> r) {
        projectInfo_.setBounds(r.reduced(12));
    };
    projectPage_.resized();
}

void SettingsComponent::setProjectInfo(const juce::String& text) {
    projectInfo_.setText(text, juce::dontSendNotification);
}

void SettingsComponent::setAboutInfo(const juce::String& version) {
    const auto dim = juce::Colours::white.withAlpha(0.55f);
    const auto bright = juce::Colours::white.withAlpha(0.85f);

    aboutTitle_.setText("Voice Control " + version, juce::dontSendNotification);
    aboutTitle_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    aboutTitle_.setColour(juce::Label::textColourId, bright);

    aboutBlurb_.setText("Clean up voice recordings and add background music, "
                        "then export back to audio or video.",
                        juce::dontSendNotification);
    aboutBlurb_.setFont(juce::Font(juce::FontOptions(13.0f)));
    aboutBlurb_.setColour(juce::Label::textColourId, dim);
    aboutBlurb_.setJustificationType(juce::Justification::topLeft);

    creditsHeader_.setText("CREDITS", juce::dontSendNotification);
    creditsHeader_.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    creditsHeader_.setColour(juce::Label::textColourId, dim);

    aboutLicenseNote_.setText("Bundled under their respective open-source "
                              "licenses. See the included third-party license "
                              "file for full terms.",
                              juce::dontSendNotification);
    aboutLicenseNote_.setFont(juce::Font(juce::FontOptions(11.0f)));
    aboutLicenseNote_.setColour(juce::Label::textColourId,
                                juce::Colours::white.withAlpha(0.4f));
    aboutLicenseNote_.setJustificationType(juce::Justification::topLeft);

    for (auto* c : { static_cast<juce::Component*>(&aboutTitle_),
                     static_cast<juce::Component*>(&aboutBlurb_),
                     static_cast<juce::Component*>(&aboutSeparator_),
                     static_cast<juce::Component*>(&creditsHeader_),
                     static_cast<juce::Component*>(&aboutLicenseNote_) })
        aboutPage_.addAndMakeVisible(*c);

    // One row per bundled third-party work: what it is, where it lives, and the
    // license it ships under.
    struct CreditSpec {
        const char* title;
        const char* detail;
        const char* linkText;
        const char* url;
    };
    const CreditSpec specs[] = {
        { "Noise reduction - DeepFilterNet",
          "Real-time speech enhancement by H. Schroeter et al., MIT / Apache-2.0",
          "github.com/Rikorose/DeepFilterNet",
          "https://github.com/Rikorose/DeepFilterNet" },
        { "Audio app framework - JUCE",
          "UI and audio engine, juce.com",
          "juce.com",
          "https://juce.com" },
        { "Audio/video I/O - FFmpeg",
          "Media import and export, ffmpeg.org",
          "ffmpeg.org",
          "https://ffmpeg.org" },
    };

    creditRows_.clear();
    for (const auto& spec : specs) {
        CreditRow row;

        row.title = std::make_unique<juce::Label>();
        row.title->setText(spec.title, juce::dontSendNotification);
        row.title->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        row.title->setColour(juce::Label::textColourId, bright);
        aboutPage_.addAndMakeVisible(*row.title);

        row.detail = std::make_unique<juce::Label>();
        row.detail->setText(spec.detail, juce::dontSendNotification);
        row.detail->setFont(juce::Font(juce::FontOptions(11.0f)));
        row.detail->setColour(juce::Label::textColourId, dim);
        aboutPage_.addAndMakeVisible(*row.detail);

        row.link = std::make_unique<juce::HyperlinkButton>(
            spec.linkText, juce::URL(spec.url));
        row.link->setFont(juce::Font(juce::FontOptions(12.0f)), false,
                          juce::Justification::centredLeft);
        row.link->setColour(juce::HyperlinkButton::textColourId,
                            juce::Colour(0xff5aa9ff));
        aboutPage_.addAndMakeVisible(*row.link);

        creditRows_.push_back(std::move(row));
    }

    aboutPage_.layout = [this](juce::Rectangle<int> r) {
        r.reduce(16, 16);

        aboutTitle_.setBounds(r.removeFromTop(22));
        r.removeFromTop(4);
        aboutBlurb_.setBounds(r.removeFromTop(34));

        r.removeFromTop(12);
        aboutSeparator_.setBounds(r.removeFromTop(1));
        r.removeFromTop(12);

        creditsHeader_.setBounds(r.removeFromTop(16));
        r.removeFromTop(6);

        for (auto& row : creditRows_) {
            row.title->setBounds(r.removeFromTop(18));
            row.detail->setBounds(r.removeFromTop(15));
            row.link->setBounds(r.removeFromTop(18));
            r.removeFromTop(10);
        }

        r.removeFromTop(4);
        aboutLicenseNote_.setBounds(r.removeFromTop(34));
    };
    aboutPage_.resized();
}

void SettingsComponent::setGeneralControls(juce::Label& musicModeLabel,
                                           juce::ComboBox& musicModeBox,
                                           juce::ToggleButton& muteWhenHidden,
                                           juce::ToggleButton& followSystem,
                                           juce::Label& outputLabel,
                                           juce::ComboBox& outputBox) {
    auto styleHeader = [](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
    };
    styleHeader(audioHeader_, "AUDIO OUTPUT");
    styleHeader(musicHeader_, "BACKGROUND MUSIC");

    for (auto* c : { static_cast<juce::Component*>(&audioHeader_),
                     static_cast<juce::Component*>(&followSystem),
                     static_cast<juce::Component*>(&outputLabel),
                     static_cast<juce::Component*>(&outputBox),
                     static_cast<juce::Component*>(&generalSeparator_),
                     static_cast<juce::Component*>(&musicHeader_),
                     static_cast<juce::Component*>(&musicModeLabel),
                     static_cast<juce::Component*>(&musicModeBox),
                     static_cast<juce::Component*>(&muteWhenHidden) })
        generalPage_.addAndMakeVisible(*c);

    generalPage_.layout = [this, &musicModeLabel, &musicModeBox, &muteWhenHidden, &followSystem,
                           &outputLabel, &outputBox](juce::Rectangle<int> r) {
        r.reduce(16, 16);

        // Audio output first.
        audioHeader_.setBounds(r.removeFromTop(20));
        r.removeFromTop(6);
        followSystem.setBounds(r.removeFromTop(24));
        r.removeFromTop(10);
        auto devRow = r.removeFromTop(26);
        outputLabel.setBounds(devRow.removeFromLeft(100));
        outputBox.setBounds(devRow);

        // Clear separation, then background-music behaviour.
        r.removeFromTop(16);
        generalSeparator_.setBounds(r.removeFromTop(1));
        r.removeFromTop(16);
        musicHeader_.setBounds(r.removeFromTop(20));
        r.removeFromTop(6);
        auto modeRow = r.removeFromTop(26);
        musicModeLabel.setBounds(modeRow.removeFromLeft(150));
        musicModeBox.setBounds(modeRow.removeFromLeft(220));
        r.removeFromTop(8);
        muteWhenHidden.setBounds(r.removeFromTop(24));
    };

    // The page already has its bounds (set when added to the tabs); lay the
    // controls out now that the layout callback exists.
    generalPage_.resized();
}

void SettingsComponent::setProControls(std::vector<ProSection> sections, juce::Button& resetButton) {
    proSections_ = std::move(sections);
    proSectionHeaders_.clear();
    proSectionSeparators_.clear();

    auto styleSectionHeader = [](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
    };

    for (std::size_t si = 0; si < proSections_.size(); ++si) {
        if (si > 0) {
            auto sep = std::make_unique<Separator>();
            proPage_.addAndMakeVisible(*sep);
            proSectionSeparators_.push_back(std::move(sep));
        }

        auto header = std::make_unique<juce::Label>();
        styleSectionHeader(*header, proSections_[si].title);
        proPage_.addAndMakeVisible(*header);
        proSectionHeaders_.push_back(std::move(header));

        for (const auto& pair : proSections_[si].pairs) {
            if (pair.label != nullptr)
                proPage_.addAndMakeVisible(*pair.label);
            if (pair.slider != nullptr)
                proPage_.addAndMakeVisible(*pair.slider);
        }
    }
    proPage_.addAndMakeVisible(resetButton);

    proPage_.layout = [this, &resetButton](juce::Rectangle<int> r) {
        r.reduce(16, 12);

        constexpr int kFooterHeight = 36;
        constexpr int kSectionHeaderHeight = 14;
        constexpr int kCaptionHeight = 12;
        constexpr int kRowHeight = 68;
        constexpr int kRowGap = 2;
        constexpr int kSectionTopGap = 10;

        auto footer = r.removeFromBottom(kFooterHeight);
        resetButton.setBounds(footer.removeFromRight(90).withSizeKeepingCentre(90, 28));

        auto placeKnob = [&](juce::Rectangle<int> cell, const ProPair& pair) {
            if (pair.label != nullptr)
                pair.label->setBounds(cell.removeFromTop(kCaptionHeight));
            if (pair.slider != nullptr)
                pair.slider->setBounds(cell.reduced(1, 0));
        };

        for (std::size_t si = 0; si < proSections_.size(); ++si) {
            if (si > 0) {
                r.removeFromTop(kSectionTopGap);
                if (si - 1 < proSectionSeparators_.size())
                    proSectionSeparators_[si - 1]->setBounds(r.removeFromTop(1));
                r.removeFromTop(kSectionTopGap);
            }

            if (si < proSectionHeaders_.size())
                proSectionHeaders_[si]->setBounds(r.removeFromTop(kSectionHeaderHeight));
            r.removeFromTop(4);

            const auto& section = proSections_[si];
            const int columns = juce::jmax(1, section.columns);
            for (std::size_t pi = 0; pi < section.pairs.size(); pi += static_cast<std::size_t>(columns)) {
                auto row = r.removeFromTop(kRowHeight);
                const int itemsInRow =
                    static_cast<int>(juce::jmin(static_cast<std::size_t>(columns), section.pairs.size() - pi));

                if (itemsInRow == 1) {
                    auto cell = row.withSizeKeepingCentre(juce::jmin(88, row.getWidth()), row.getHeight())
                                    .reduced(2, 0);
                    placeKnob(cell, section.pairs[pi]);
                } else {
                    const int cellWidth = row.getWidth() / itemsInRow;
                    for (int col = 0; col < itemsInRow; ++col) {
                        auto cell = row.removeFromLeft(cellWidth).reduced(2, 0);
                        placeKnob(cell, section.pairs[pi + static_cast<std::size_t>(col)]);
                    }
                }
                r.removeFromTop(kRowGap);
            }
        }
    };

    proPage_.resized();
}

void SettingsComponent::resized() {
    tabs_.setBounds(getLocalBounds());
}
