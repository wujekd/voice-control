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
    bool hitTest (int x, int y) override;

    float getValue() const { return value; }

private:
    void setValueFromDrag (const juce::MouseEvent&);
    void drawSevenSegmentDigit (juce::Graphics&, int digit, juce::Rectangle<float> area, juce::Colour colour) const;

    float value = 0.60f;
    float dragStartValue = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlassPot)
};

class DecayPot final : public juce::Component
{
public:
    DecayPot();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;

    float getValue() const { return value; }

private:
    void setValueFromDrag (const juce::MouseEvent&);
    void drawSevenSegmentDigit (juce::Graphics&, int digit, juce::Rectangle<float> area, juce::Colour colour) const;

    float value = 0.24f;
    float dragStartValue = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DecayPot)
};

class ParticleField final : public juce::Component,
                            private juce::Timer
{
public:
    ParticleField();
    ~ParticleField() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void setEnergySource (std::function<float()> energy);

private:
    struct Particle
    {
        juce::Point<float> position;
        juce::Point<float> velocity;
        juce::Colour colour;
        float radius = 1.0f;
        float phase = 0.0f;
        float twinkle = 1.0f;
        float depth = 1.0f;
        float life = 1.0f;
    };

    void timerCallback() override;
    void seedParticles();
    Particle makeParticle();

    std::vector<Particle> particles;
    std::function<float()> energySource;
    juce::Random random;
    float time = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParticleField)
};

class AnimatedBallPanel final : public juce::Component,
                                private juce::Timer
{
public:
    AnimatedBallPanel();
    ~AnimatedBallPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void setControlSources (std::function<float()> speed, std::function<float()> trail);

private:
    void timerCallback() override;

    juce::Point<float> position { 80.0f, 60.0f };
    juce::Point<float> velocity { 1.75f, 1.25f };
    std::vector<juce::Point<float>> trailPoints;
    std::function<float()> speedSource;
    std::function<float()> trailSource;
    float radius = 18.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnimatedBallPanel)
};

class NeuralPot final : public juce::Component
{
public:
    NeuralPot();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;

    float getValue() const { return value; }

private:
    void setValueFromDrag (const juce::MouseEvent&);

    float value = 0.0f;
    float dragStartValue = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuralPot)
};

class NeuralNetworkPanel final : public juce::Component,
                                 private juce::Timer
{
public:
    NeuralNetworkPanel();
    ~NeuralNetworkPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void setEnergySource (std::function<float()> energy);
    void setSourcePoint (juce::Point<float> source);

private:
    struct Connection
    {
        int layer = 0;
        int from = 0;
        int to = 0;
        float offset = 0.0f;
        float speed = 1.0f;
        float gate = 0.0f;
    };

    void timerCallback() override;
    void rebuild();

    std::vector<std::vector<juce::Point<float>>> layers;
    std::vector<float> nodePhase;
    std::vector<Connection> connections;
    std::function<float()> energySource;
    juce::Point<float> sourcePoint;
    juce::Random random;
    float time = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuralNetworkPanel)
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
    ParticleField particleField;
    GlassPot outputPot;
    DecayPot decayPot;
    AnimatedBallPanel ballPanel;
    NeuralPot neuralPot;
    NeuralNetworkPanel neuralNetwork;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuiPluginExperimentAudioProcessorEditor)
};
