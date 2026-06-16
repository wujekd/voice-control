#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace vc {

// Remap raw control energy (0–1) into a subdued visual range for secondary
// animations. Noise reduction stays off at 0; any active amount jumps to ~35%
// visibility and ramps to ~75% at full strength.
float mapNoiseReductionEnergy(float rawEnergy);

// Intensity flow lines stay visible across the whole slider range (~35% at
// minimum intensity, ~65% at full) — audio always flows once intensity is "on".
float mapIntensityFlowEnergy(float rawEnergy);

// Animated neural-network backdrop for the Noise-Reduction area. It draws a
// small feed-forward network whose "source" node is the knob placed on top of
// it: connections fan out from the knob and energy pulses flow through them.
// At energy 0 the web is dim ("offline"); as the energy rises the connections
// brighten and more pulses fire. Recoloured to the app's green accent.
class NeuralNetworkPanel final : public juce::Component,
                                 private juce::Timer {
public:
    NeuralNetworkPanel();
    ~NeuralNetworkPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Energy in [0, 1] drives brightness and the number of active pulses.
    void setEnergySource(std::function<float()> energy);
    // Knob centre in panel-local coordinates; the network fans out from here.
    void setSourcePoint(juce::Point<float> source);
    // Continuous animation only while audio is playing.
    void setPlaybackActive(bool active);
    // Brief activity flash when NR is adjusted while stopped.
    void triggerPreviewBurst(float seconds = 1.5f);

private:
    struct Connection {
        int layer = 0;
        int from = 0;
        int to = 0;
        float offset = 0.0f;
        float speed = 1.0f;
        float gate = 0.0f;
    };

    void timerCallback() override;
    void rebuild();
    float animationEnergy(float baseEnergy) const;
    void updateTimerState();

    std::vector<std::vector<juce::Point<float>>> layers_;
    std::vector<float> nodePhase_;
    std::vector<Connection> connections_;
    std::function<float()> energySource_;
    juce::Point<float> sourcePoint_;
    juce::Random random_;
    float time_ = 0.0f;
    bool playbackActive_ = false;
    float previewRemaining_ = 0.0f;
    float previewDuration_ = 1.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeuralNetworkPanel)
};

// Thin animated bridge drawn in the gap between the Intensity/Tone area and the
// Noise-Reduction net: green connection lines (same style as the net) carrying
// flowing pulses from the left-area knobs into the network on the right.
class SignalFlowConnector final : public juce::Component,
                                  private juce::Timer {
public:
    SignalFlowConnector();
    ~SignalFlowConnector() override;

    void paint(juce::Graphics&) override;
    void setEnergySource(std::function<float()> energy);
    // Pulse animation only while audio is playing; lines stay static otherwise.
    void setPlaybackActive(bool active);
    // Parallel wires: leftPoints[i] -> rightPoints[i], in panel-local coords.
    void setEndpoints(std::vector<juce::Point<float>> leftPoints,
                      std::vector<juce::Point<float>> rightPoints);

private:
    struct Pulse {
        int line = 0;
        float pos = 0.0f;
        float speed = 0.5f;
    };

    void timerCallback() override;
    void updateTimerState();

    std::vector<juce::Point<float>> leftPoints_;
    std::vector<juce::Point<float>> rightPoints_;
    std::function<float()> energySource_;
    std::vector<Pulse> pulses_;
    juce::Random rng_;
    bool playbackActive_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SignalFlowConnector)
};

} // namespace vc
