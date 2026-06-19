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
    std::function<void(int, double)> onAdjustClipGain;
    std::function<void(double)> onSeek;

    void setVoice(const juce::AudioBuffer<float>* voice, double sampleRate);
    void setVoiceWaveformPeaks(const std::vector<float>* dryPeaks,
                               const std::vector<float>* processedPeaks,
                               float displayGain = 1.0f);
    // Current noise-reduction blend (0 = dry, 1 = fully denoised). The displayed
    // voice waveform blends the dry and denoised peaks by this amount so it
    // tracks what you hear as the slider moves.
    void setVoiceNoiseReduction(float amount01);
    void setClips(const std::vector<MusicClip>* clips, int selectedIndex);
    // When false, only the voice lane is drawn/interactive and the component is
    // expected to be sized to the voice lane alone (backing-music section folded).
    void setMusicLaneVisible(bool visible);
    void setPlayheadSeconds(double seconds);
    bool tickAnimation();

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    enum class DragMode { None, Move, Slip, ResizeLeft, ResizeRight, FadeIn, FadeOut, Gain };

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
    bool gainEnvelopeAt(juce::Point<float> p, int& index) const;
    bool plusAt(juce::Point<float> p, double& seconds) const;
    void drawWaveform(juce::Graphics& g, juce::Rectangle<float> area);
    void drawWaveformPeaks(juce::Graphics& g, juce::Rectangle<float> area,
                           const std::vector<float>& peaks, float alpha);
    void drawClipWaveform(juce::Graphics& g, const MusicClip& clip, juce::Rectangle<float> area);
    void drawClipFadeOverlay(juce::Graphics& g, const MusicClip& clip, juce::Rectangle<float> area, bool selected);
    void drawClipRemoveButton(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawPlus(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawGapPluses(juce::Graphics& g);

    const juce::AudioBuffer<float>* voice_ = nullptr;
    const std::vector<float>* dryVoicePeaks_ = nullptr;
    const std::vector<float>* processedVoicePeaks_ = nullptr;
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
    double dragGainDb_ = 0.0;
    double dragMouseSeconds_ = 0.0;
    float dragMouseY_ = 0.0f;
    bool scrubbing_ = false;
    bool hadProcessedVoicePeaks_ = false;
    float voiceWaveformTransition_ = 1.0f;
    float voiceWaveformGain_ = 1.0f; // maps raw peaks to the normalized (heard) level
    float voiceNoiseReduction_ = 0.0f; // 0 = dry, 1 = fully denoised
    bool musicLaneVisible_ = true;
};
