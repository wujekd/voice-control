#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "MusicClip.h"

#include <functional>

class MusicTimeline : public juce::Component {
public:
    std::function<void(double)> onAddAt;
    std::function<void(int)> onSelectClip;
    std::function<void(int)> onRemoveClip;
    std::function<void(int)> onClipEditStarted;
    std::function<void(int, bool)> onClipDragStateChanged;
    std::function<void(int, double, double, double)> onMoveOrResizeClip;
    std::function<void(int, double, double)> onAdjustClipFades;
    std::function<void(double)> onSeek;

    void setVoice(const juce::AudioBuffer<float>* voice, double sampleRate);
    void setClips(const std::vector<MusicClip>* clips, int selectedIndex);
    void setPlayheadSeconds(double seconds);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    enum class DragMode { None, Move, Slip, ResizeLeft, ResizeRight, FadeIn, FadeOut };

    double secondsToX(double seconds) const;
    double xToSeconds(double x) const;
    double timelineDurationSeconds() const;
    juce::Rectangle<float> timelineBounds() const;
    juce::Rectangle<float> voiceLaneBounds() const;
    juce::Rectangle<float> musicLaneBounds() const;
    juce::Rectangle<float> clipBounds(int index) const;
    juce::Rectangle<float> removeButtonBounds(int index) const;
    juce::Point<float> fadeInHandlePosition(int index) const;
    juce::Point<float> fadeOutHandlePosition(int index) const;
    int clipAt(juce::Point<float> p) const;
    bool removeButtonAt(juce::Point<float> p, int& index) const;
    bool fadeHandleAt(juce::Point<float> p, int& index, DragMode& mode) const;
    bool plusAt(juce::Point<float> p, double& seconds) const;
    void drawWaveform(juce::Graphics& g, juce::Rectangle<float> area);
    void drawClipWaveform(juce::Graphics& g, const MusicClip& clip, juce::Rectangle<float> area);
    void drawClipFadeOverlay(juce::Graphics& g, const MusicClip& clip, juce::Rectangle<float> area, bool selected);
    void drawClipRemoveButton(juce::Graphics& g, juce::Rectangle<float> bounds);
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
    double dragSourceOffsetSeconds_ = 0.0;
    double dragLengthSeconds_ = 0.0;
    double dragFadeInSeconds_ = 0.0;
    double dragFadeOutSeconds_ = 0.0;
    double dragMouseSeconds_ = 0.0;
    bool scrubbing_ = false;
};
