#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

class GuiPluginExperimentAudioProcessor;

class GlassPot final : public juce::Component
{
public:
    GlassPot();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void setValueFromDrag (const juce::MouseEvent&);
    void drawSevenSegmentDigit (juce::Graphics&, int digit, juce::Rectangle<float> area, juce::Colour colour) const;

    float value = 0.60f;
    float dragStartValue = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlassPot)
};

class GuiPluginExperimentAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit GuiPluginExperimentAudioProcessorEditor (GuiPluginExperimentAudioProcessor&);
    ~GuiPluginExperimentAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    GuiPluginExperimentAudioProcessor& audioProcessor;
    GlassPot outputPot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuiPluginExperimentAudioProcessorEditor)
};
