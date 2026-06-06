#include "SettingsComponent.h"

SettingsComponent::SettingsComponent() {
    setSize(kWidth, kHeight);

    tabs_.setOutline(0);
    tabs_.setTabBarDepth(30);
    const auto pageColour = juce::Colour(0xff2b2f36);
    tabs_.addTab("Audio", pageColour, &audioPage_, false);
    tabs_.addTab("Pro", pageColour, &proPage_, false);
    addAndMakeVisible(tabs_);
}

void SettingsComponent::setAudioControls(juce::ToggleButton& followSystem,
                                         juce::Label& outputLabel, juce::ComboBox& outputBox) {
    audioPage_.addAndMakeVisible(followSystem);
    audioPage_.addAndMakeVisible(outputLabel);
    audioPage_.addAndMakeVisible(outputBox);

    audioPage_.layout = [&followSystem, &outputLabel, &outputBox](juce::Rectangle<int> r) {
        r.reduce(16, 16);
        followSystem.setBounds(r.removeFromTop(24));
        r.removeFromTop(10);
        auto devRow = r.removeFromTop(26);
        outputLabel.setBounds(devRow.removeFromLeft(100));
        outputBox.setBounds(devRow);
    };
}

void SettingsComponent::setProControls(std::vector<ProPair> pairs, juce::Button& resetButton) {
    for (const auto& p : pairs) {
        if (p.label != nullptr)  proPage_.addAndMakeVisible(*p.label);
        if (p.slider != nullptr) proPage_.addAndMakeVisible(*p.slider);
    }
    proPage_.addAndMakeVisible(resetButton);

    proPage_.layout = [pairs = std::move(pairs), &resetButton](juce::Rectangle<int> r) {
        r.reduce(16, 12);

        auto placeCell = [](juce::Rectangle<int> cell, const ProPair& pair) {
            if (pair.label != nullptr)
                pair.label->setBounds(cell.removeFromLeft(118).reduced(0, 10));
            if (pair.slider != nullptr)
                pair.slider->setBounds(cell);
        };

        for (std::size_t i = 0; i < pairs.size(); i += 2) {
            auto row = r.removeFromTop(50);
            placeCell(row.removeFromLeft(row.getWidth() / 2).reduced(4, 0), pairs[i]);
            if (i + 1 < pairs.size())
                placeCell(row.reduced(4, 0), pairs[i + 1]);
            r.removeFromTop(4);
        }

        resetButton.setBounds(r.removeFromBottom(28).removeFromRight(90));
    };
}

void SettingsComponent::resized() {
    tabs_.setBounds(getLocalBounds());
}
