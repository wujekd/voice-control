#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "MusicClip.h"

#include <functional>

class MusicTimeline : public juce::Component {
public:
    std::function<void(double)> onAddAt;
    std::function<void(int)> onSelectClip;
    std::function<void(int, double, double)> onMoveOrResizeClip;

    void setVoice(const juce::AudioBuffer<float>* voice, double sampleRate);
    void setClips(const std::vector<MusicClip>* clips, int selectedIndex);
    void setPlayheadSeconds(double seconds);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    enum class DragMode { None, Move, ResizeLeft, ResizeRight };

    double secondsToX(double seconds) const;
    double xToSeconds(double x) const;
    double timelineDurationSeconds() const;
    juce::Rectangle<float> timelineBounds() const;
    juce::Rectangle<float> musicLaneBounds() const;
    juce::Rectangle<float> clipBounds(int index) const;
    int clipAt(juce::Point<float> p) const;
    bool plusAt(juce::Point<float> p, double& seconds) const;
    void drawWaveform(juce::Graphics& g, juce::Rectangle<float> area);
    void drawPlus(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawGapPluses(juce::Graphics& g);

    const juce::AudioBuffer<float>* voice_ = nullptr;
    const std::vector<MusicClip>* clips_ = nullptr;
    double sampleRate_ = 48000.0;
    double playheadSeconds_ = 0.0;
    int selectedIndex_ = -1;
    int draggingIndex_ = -1;
    DragMode dragMode_ = DragMode::None;
    double dragStartSeconds_ = 0.0;
    double dragLengthSeconds_ = 0.0;
    double dragMouseSeconds_ = 0.0;
};
