#include "NeuralNetworkPanel.h"

#include <algorithm>

namespace vc {

namespace {
const juce::Colour kAccent(0xff6ee07a);     // app green
const juce::Colour kAccentBright(0xffaef0b5);
const juce::Colour kPulse(0xffd6ffdc);
const juce::Colour kDim(0xff1f2a22);

constexpr float kActiveEnergyFloor = 0.35f;
} // namespace

float mapNoiseReductionEnergy(float rawEnergy) {
    if (rawEnergy <= 0.001f)
        return 0.0f;
    constexpr float kMax = 0.75f;
    return kActiveEnergyFloor + rawEnergy * (kMax - kActiveEnergyFloor);
}

float mapIntensityFlowEnergy(float rawEnergy) {
    // Intensity is always "on" within its slider range (minimum = 10%). Even at
    // the lowest setting the bridge stays visible as a subtle background flow.
    constexpr float kMax = 0.65f;
    const float t = juce::jlimit(0.0f, 1.0f, rawEnergy);
    return kActiveEnergyFloor + t * (kMax - kActiveEnergyFloor);
}

NeuralNetworkPanel::NeuralNetworkPanel() {
    setOpaque(false);
    setInterceptsMouseClicks(false, false);
}

NeuralNetworkPanel::~NeuralNetworkPanel() {
    stopTimer();
}

void NeuralNetworkPanel::setPlaybackActive(bool active) {
    if (playbackActive_ == active)
        return;
    playbackActive_ = active;
    updateTimerState();
    repaint();
}

void NeuralNetworkPanel::triggerPreviewBurst(float seconds) {
    previewDuration_ = juce::jmax(0.4f, seconds);
    previewRemaining_ = previewDuration_;
    updateTimerState();
    repaint();
}

void NeuralNetworkPanel::updateTimerState() {
    if (playbackActive_ || previewRemaining_ > 0.0f) {
        if (!isTimerRunning())
            startTimerHz(50);
    } else {
        stopTimer();
    }
}

float NeuralNetworkPanel::animationEnergy(float baseEnergy) const {
    if (playbackActive_)
        return baseEnergy;
    if (previewRemaining_ > 0.0f && previewDuration_ > 0.0f)
        return baseEnergy * (previewRemaining_ / previewDuration_);
    return 0.0f;
}

void NeuralNetworkPanel::resized() {
    rebuild();
}

void NeuralNetworkPanel::setEnergySource(std::function<float()> energy) {
    energySource_ = std::move(energy);
}

void NeuralNetworkPanel::setSourcePoint(juce::Point<float> source) {
    sourcePoint_ = source;
    rebuild();
}

void NeuralNetworkPanel::rebuild() {
    // The knob is the source node (layer 0); these layers fan out from it.
    static constexpr int rightSizes[] = { 5, 6, 4 };
    const int numRight = static_cast<int>(std::size(rightSizes));

    layers_.clear();
    nodePhase_.clear();
    connections_.clear();

    auto area = getLocalBounds().toFloat();
    if (area.isEmpty())
        area = juce::Rectangle<float>(0.0f, 0.0f, 360.0f, 150.0f);

    layers_.push_back({ sourcePoint_ });
    nodePhase_.push_back(0.0f);

    const float left = sourcePoint_.x + 76.0f;
    const float right = juce::jmax(left + 48.0f, area.getRight() - 18.0f);
    const float top = area.getY() + 24.0f;
    const float bottom = area.getBottom() - 24.0f;

    for (int l = 0; l < numRight; ++l) {
        std::vector<juce::Point<float>> nodes;
        const float x = numRight > 1
            ? left + (right - left) * static_cast<float>(l) / static_cast<float>(numRight - 1)
            : right;
        const int count = rightSizes[l];
        for (int n = 0; n < count; ++n) {
            const float y = count > 1
                ? top + (bottom - top) * static_cast<float>(n) / static_cast<float>(count - 1)
                : (top + bottom) * 0.5f;
            nodes.push_back({ x, y });
            nodePhase_.push_back(random_.nextFloat() * juce::MathConstants<float>::twoPi);
        }
        layers_.push_back(std::move(nodes));
    }

    const int total = static_cast<int>(layers_.size());
    for (int l = 0; l + 1 < total; ++l)
        for (int a = 0; a < static_cast<int>(layers_[static_cast<size_t>(l)].size()); ++a)
            for (int b = 0; b < static_cast<int>(layers_[static_cast<size_t>(l + 1)].size()); ++b)
                connections_.push_back({ l, a, b,
                                         random_.nextFloat(),
                                         0.45f + random_.nextFloat() * 0.85f,
                                         random_.nextFloat() });
}

void NeuralNetworkPanel::timerCallback() {
    if (playbackActive_ || previewRemaining_ > 0.0f) {
        time_ += 0.02f;
        if (!playbackActive_)
            previewRemaining_ = juce::jmax(0.0f, previewRemaining_ - 0.02f);
    }
    updateTimerState();
    repaint();
}

void NeuralNetworkPanel::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float baseEnergy = energySource_ != nullptr ? juce::jlimit(0.0f, 1.0f, energySource_()) : 0.0f;
    const float energy = animationEnergy(baseEnergy);

    // Subtle grouped-panel chrome so the area reads as a distinct zone.
    g.setColour(juce::Colour(0xff121712).withAlpha(0.55f));
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(kAccent.withAlpha(0.08f + energy * 0.10f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    if (layers_.empty())
        return;

    // Energy halo radiating out of the knob/source.
    if (energy > 0.01f) {
        for (int i = 0; i < 5; ++i) {
            const float t = static_cast<float>(i) / 4.0f;
            const float size = 58.0f + t * 62.0f + energy * 34.0f;
            const float alpha = 0.018f * energy * std::pow(1.0f - t, 2.0f);
            g.setColour(kAccent.interpolatedWith(kAccentBright, 0.30f + t * 0.22f).withAlpha(alpha));
            g.fillEllipse(juce::Rectangle<float>(size, size).withCentre(sourcePoint_));
        }
    }

    const juce::Colour live = kAccent.interpolatedWith(kAccentBright, 0.35f);

    for (const auto& c : connections_) {
        const auto a = layers_[static_cast<size_t>(c.layer)][static_cast<size_t>(c.from)];
        const auto b = layers_[static_cast<size_t>(c.layer + 1)][static_cast<size_t>(c.to)];

        const auto base = kDim.interpolatedWith(live, energy);
        g.setColour(base.withAlpha(0.07f + energy * 0.28f));
        g.drawLine(a.x, a.y, b.x, b.y, 0.7f + energy * 0.75f);

        if (c.gate < energy) {
            const float pos = std::fmod(time_ * c.speed + c.offset, 1.0f);
            const auto pt = a + (b - a) * pos;
            g.setColour(kPulse.withAlpha(0.05f * energy));
            g.fillEllipse(pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
            g.setColour(kPulse.withAlpha(0.48f * energy));
            g.fillEllipse(pt.x - 1.4f, pt.y - 1.4f, 2.8f, 2.8f);
        }
    }

    int phaseIndex = static_cast<int>(layers_[0].size());
    for (size_t li = 1; li < layers_.size(); ++li) {
        for (const auto& node : layers_[li]) {
            const float phase = nodePhase_[static_cast<size_t>(phaseIndex++)];
            const float activation = juce::jlimit(0.0f, 1.0f,
                energy * (0.55f + 0.45f * std::sin(time_ * 2.4f + phase)));
            const float r = 4.5f + activation * 1.6f;
            const auto fill = juce::Colour(0xff20281f).interpolatedWith(live, activation * 0.85f);

            if (activation > 0.08f) {
                g.setColour(kAccentBright.withAlpha(0.05f * activation));
                g.fillEllipse(juce::Rectangle<float>(r * 2.4f, r * 2.4f).withCentre(node));
            }

            g.setColour(fill);
            g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(node));
            g.setColour(kAccent.withAlpha(0.12f + activation * 0.34f));
            g.drawEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(node), 1.0f);

            if (activation > 0.2f) {
                g.setColour(juce::Colours::white.withAlpha(0.28f * activation));
                g.fillEllipse(node.x - 1.2f, node.y - 1.2f, 2.4f, 2.4f);
            }
        }
    }
}

SignalFlowConnector::SignalFlowConnector() {
    setOpaque(false);
    setInterceptsMouseClicks(false, false);
}

SignalFlowConnector::~SignalFlowConnector() {
    stopTimer();
}

void SignalFlowConnector::setPlaybackActive(bool active) {
    if (playbackActive_ == active)
        return;
    playbackActive_ = active;
    if (!playbackActive_)
        pulses_.clear();
    updateTimerState();
    repaint();
}

void SignalFlowConnector::updateTimerState() {
    if (playbackActive_) {
        if (!isTimerRunning())
            startTimerHz(50);
    } else {
        stopTimer();
    }
}

void SignalFlowConnector::setEnergySource(std::function<float()> energy) {
    energySource_ = std::move(energy);
}

void SignalFlowConnector::setEndpoints(std::vector<juce::Point<float>> leftPoints,
                                       std::vector<juce::Point<float>> rightPoints) {
    leftPoints_ = std::move(leftPoints);
    rightPoints_ = std::move(rightPoints);
    pulses_.clear(); // stale line indices otherwise
    repaint();
}

void SignalFlowConnector::timerCallback() {
    if (!playbackActive_)
        return;

    const float energy = energySource_ != nullptr ? juce::jlimit(0.0f, 1.0f, energySource_()) : 0.5f;

    for (auto& p : pulses_)
        p.pos += p.speed * 0.02f;
    pulses_.erase(std::remove_if(pulses_.begin(), pulses_.end(),
                                 [](const Pulse& p) { return p.pos > 1.05f; }),
                  pulses_.end());

    if (!leftPoints_.empty()) {
        const float spawnChance = 0.04f + energy * 0.12f;
        if (rng_.nextFloat() < spawnChance) {
            Pulse p;
            p.line = rng_.nextInt(static_cast<int>(leftPoints_.size()));
            p.pos = 0.0f;
            p.speed = 0.45f + rng_.nextFloat() * 0.6f;
            pulses_.push_back(p);
        }
    }

    repaint();
}

void SignalFlowConnector::paint(juce::Graphics& g) {
    if (leftPoints_.empty())
        return;

    const float energy = energySource_ != nullptr ? juce::jlimit(0.0f, 1.0f, energySource_()) : 0.5f;
    const juce::Colour live = kAccent.interpolatedWith(kAccentBright, 0.35f);
    const auto base = kDim.interpolatedWith(live, energy);
    const size_t count = juce::jmin(leftPoints_.size(), rightPoints_.size());

    for (size_t i = 0; i < count; ++i) {
        g.setColour(base.withAlpha(0.07f + energy * 0.26f));
        g.drawLine(leftPoints_[i].x, leftPoints_[i].y, rightPoints_[i].x, rightPoints_[i].y, 0.7f + energy * 0.65f);
    }

    for (const auto& p : pulses_) {
        if (!playbackActive_)
            break;
        if (p.line < 0 || p.line >= static_cast<int>(count))
            continue;
        const auto a = leftPoints_[static_cast<size_t>(p.line)];
        const auto b = rightPoints_[static_cast<size_t>(p.line)];
        const auto pt = a + (b - a) * juce::jlimit(0.0f, 1.0f, p.pos);
        g.setColour(kPulse.withAlpha(0.05f * energy));
        g.fillEllipse(pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
        g.setColour(kPulse.withAlpha(0.45f * energy));
        g.fillEllipse(pt.x - 1.4f, pt.y - 1.4f, 2.8f, 2.8f);
    }
}

} // namespace vc
