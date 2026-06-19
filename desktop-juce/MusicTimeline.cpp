#include "MusicTimeline.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kClipEdgeGripTopInset = 16.0f;
constexpr float kClipEdgeGripWidth = 7.0f;
constexpr float kClipEdgeHitTopInset = 10.0f;
constexpr float kClipEdgeHitWidth = 16.0f;
constexpr float kClipEnvelopeTopInset = 13.0f;
constexpr float kClipEnvelopeLowBottomInset = 24.0f;
constexpr float kClipEnvelopeSilentBottomInset = 8.0f;
constexpr double kClipGainMinDb = -60.0;
constexpr double kClipGainMaxDb = 6.0;
constexpr float kVoiceLaneHeight = 96.0f;
constexpr int kMusicLaneHeight = 88;
constexpr float kLaneGap = 4.0f;

void drawLaneLabel(juce::Graphics& g, juce::Rectangle<float> lane, const juce::String& text) {
    auto label = juce::Rectangle<float>(lane.getX() + 8.0f, lane.getBottom() - 26.0f,
                                        150.0f, 20.0f);
    g.setColour(juce::Colour(0xffa8b0bd).withAlpha(0.88f));
    g.drawText(text, label, juce::Justification::centredLeft);
}

float clipGainVisualLevel(double gainDb) {
    const double clamped = juce::jlimit(kClipGainMinDb, kClipGainMaxDb, gainDb);
    return static_cast<float>((clamped - kClipGainMinDb) / (kClipGainMaxDb - kClipGainMinDb));
}

float clipEnvelopeTopY(juce::Rectangle<float> area, const MusicClip& clip) {
    const float highY = area.getY() + kClipEnvelopeTopInset;
    const float lowY = std::max(highY, area.getBottom() - kClipEnvelopeLowBottomInset);
    const float level = clipGainVisualLevel(clip.gainDb);
    return highY + (1.0f - level) * (lowY - highY);
}
}

void MusicTimeline::setVoice(const juce::AudioBuffer<float>* voice, double sampleRate) {
    voice_ = voice;
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    repaint();
}

void MusicTimeline::setVoiceWaveformPeaks(const std::vector<float>* dryPeaks,
                                          const std::vector<float>* processedPeaks,
                                          float displayGain) {
    voiceWaveformGain_ = displayGain;
    const bool hasProcessed = processedPeaks != nullptr && !processedPeaks->empty();
    if (hasProcessed && !hadProcessedVoicePeaks_)
        voiceWaveformTransition_ = 0.0f;
    if (!hasProcessed) {
        hadProcessedVoicePeaks_ = false;
        voiceWaveformTransition_ = 1.0f;
    }

    dryVoicePeaks_ = dryPeaks;
    processedVoicePeaks_ = processedPeaks;
    if (hasProcessed)
        hadProcessedVoicePeaks_ = true;
    repaint();
}

void MusicTimeline::setVoiceNoiseReduction(float amount01) {
    const float clamped = juce::jlimit(0.0f, 1.0f, amount01);
    if (std::abs(clamped - voiceNoiseReduction_) < 1e-4f)
        return;
    voiceNoiseReduction_ = clamped;
    repaint();
}

void MusicTimeline::setClips(const std::vector<MusicClip>* clips, int selectedIndex) {
    clips_ = clips;
    selectedIndex_ = selectedIndex;
    repaint();
}

void MusicTimeline::setMusicLaneVisible(bool visible) {
    if (musicLaneVisible_ == visible)
        return;
    musicLaneVisible_ = visible;
    repaint();
}

void MusicTimeline::setPlayheadSeconds(double seconds) {
    playheadSeconds_ = juce::jlimit(0.0, timelineDurationSeconds(), seconds);
    repaint();
}

bool MusicTimeline::tickAnimation() {
    if (voiceWaveformTransition_ >= 1.0f)
        return false;
    voiceWaveformTransition_ = juce::jmin(1.0f, voiceWaveformTransition_ + 0.08f);
    repaint();
    return voiceWaveformTransition_ < 1.0f;
}

double MusicTimeline::timelineDurationSeconds() const {
    return voice_ != nullptr && voice_->getNumSamples() > 0
        ? static_cast<double>(voice_->getNumSamples()) / sampleRate_
        : 60.0;
}

juce::Rectangle<float> MusicTimeline::timelineBounds() const {
    return getLocalBounds().toFloat();
}

juce::Rectangle<float> MusicTimeline::voiceLaneBounds() const {
    const auto b = timelineBounds();
    return { b.getX(), b.getY(), b.getWidth(), kVoiceLaneHeight };
}

juce::Rectangle<float> MusicTimeline::musicLaneBounds() const {
    if (!musicLaneVisible_)
        return {};
    const auto b = timelineBounds();
    return { b.getX(), b.getY() + kVoiceLaneHeight + kLaneGap, b.getWidth(),
             static_cast<float>(kMusicLaneHeight) };
}

double MusicTimeline::secondsToX(double seconds) const {
    const double duration = timelineDurationSeconds();
    return static_cast<double>(timelineBounds().getX())
        + juce::jlimit(0.0, 1.0, seconds / std::max(0.001, duration))
            * static_cast<double>(timelineBounds().getWidth());
}

double MusicTimeline::xToSeconds(double x) const {
    const auto r = timelineBounds();
    const double duration = timelineDurationSeconds();
    return juce::jlimit(0.0, duration,
        (x - r.getX()) / std::max(1.0f, r.getWidth()) * duration);
}

juce::Rectangle<float> MusicTimeline::clipBounds(int index) const {
    if (clips_ == nullptr || index < 0 || index >= static_cast<int>(clips_->size()))
        return {};
    const auto& clip = (*clips_)[static_cast<std::size_t>(index)];
    auto lane = musicLaneBounds();
    const double duration = timelineDurationSeconds();
    const double start = juce::jlimit(0.0, duration, clip.startSeconds);
    const double end = juce::jlimit(0.0, duration, clip.startSeconds + clip.durationSeconds());
    if (end <= start)
        return {};
    const float x1 = static_cast<float>(secondsToX(start));
    const float x2 = static_cast<float>(secondsToX(end));
    return { x1, lane.getY() + 1.0f, std::max(8.0f, x2 - x1), lane.getHeight() - 2.0f };
}

juce::Rectangle<float> MusicTimeline::removeButtonBounds(int index) const {
    auto b = clipBounds(index);
    if (b.isEmpty())
        return {};
    return { b.getRight() - 44.0f, b.getBottom() - 18.0f, 14.0f, 14.0f };
}

juce::Point<float> MusicTimeline::fadeInHandlePosition(int index) const {
    if (clips_ == nullptr || index < 0 || index >= static_cast<int>(clips_->size()))
        return {};
    const auto& clip = (*clips_)[static_cast<std::size_t>(index)];
    auto b = clipBounds(index);
    const double fade = juce::jlimit(0.0, clip.durationSeconds(), clip.fadeInSeconds);
    return { static_cast<float>(secondsToX(clip.startSeconds + fade)), clipEnvelopeTopY(b, clip) };
}

juce::Point<float> MusicTimeline::fadeOutHandlePosition(int index) const {
    if (clips_ == nullptr || index < 0 || index >= static_cast<int>(clips_->size()))
        return {};
    const auto& clip = (*clips_)[static_cast<std::size_t>(index)];
    auto b = clipBounds(index);
    const double length = clip.durationSeconds();
    const double fade = juce::jlimit(0.0, length, clip.fadeOutSeconds);
    return { static_cast<float>(secondsToX(clip.startSeconds + length - fade)), clipEnvelopeTopY(b, clip) };
}

int MusicTimeline::clipAt(juce::Point<float> p) const {
    if (clips_ == nullptr)
        return -1;
    for (int i = static_cast<int>(clips_->size()) - 1; i >= 0; --i)
        if (clipBounds(i).contains(p))
            return i;
    return -1;
}

bool MusicTimeline::removeButtonAt(juce::Point<float> p, int& index) const {
    if (clips_ == nullptr)
        return false;
    for (int i = static_cast<int>(clips_->size()) - 1; i >= 0; --i) {
        if (i == selectedIndex_ && removeButtonBounds(i).contains(p)) {
            index = i;
            return true;
        }
    }
    return false;
}

bool MusicTimeline::fadeHandleAt(juce::Point<float> p, int& index, DragMode& mode) const {
    if (clips_ == nullptr)
        return false;
    constexpr float handleRadius = 8.0f;
    for (int i = static_cast<int>(clips_->size()) - 1; i >= 0; --i) {
        if (i != selectedIndex_)
            continue;
        if (p.getDistanceFrom(fadeInHandlePosition(i)) <= handleRadius) {
            index = i;
            mode = DragMode::FadeIn;
            return true;
        }
        if (p.getDistanceFrom(fadeOutHandlePosition(i)) <= handleRadius) {
            index = i;
            mode = DragMode::FadeOut;
            return true;
        }
    }
    return false;
}

bool MusicTimeline::gainEnvelopeAt(juce::Point<float> p, int& index) const {
    if (clips_ == nullptr)
        return false;

    constexpr float hitRadius = 6.0f;
    for (int i = static_cast<int>(clips_->size()) - 1; i >= 0; --i) {
        const auto& clip = (*clips_)[static_cast<std::size_t>(i)];
        const auto b = clipBounds(i);
        if (b.isEmpty())
            continue;

        const double length = std::max(0.001, clip.durationSeconds());
        const double fadeIn = juce::jlimit(0.0, length, clip.fadeInSeconds);
        const double fadeOut = juce::jlimit(0.0, length, clip.fadeOutSeconds);
        const float x0 = static_cast<float>(secondsToX(clip.startSeconds + fadeIn));
        const float x1 = static_cast<float>(secondsToX(clip.startSeconds + length - fadeOut));
        if (x1 - x0 < 8.0f)
            continue;

        const float y = clipEnvelopeTopY(b, clip);
        if (p.x >= x0 && p.x <= x1 && std::abs(p.y - y) <= hitRadius) {
            index = i;
            return true;
        }
    }
    return false;
}

bool MusicTimeline::plusAt(juce::Point<float> p, double& seconds) const {
    auto lane = musicLaneBounds();
    if (!lane.contains(p))
        return false;
    const int hit = clipAt(p);
    if (hit >= 0)
        return false;
    if (clips_ == nullptr || clips_->empty()) {
        seconds = 0.0;
        return true;
    }

    const double duration = timelineDurationSeconds();
    const double clicked = xToSeconds(p.x);
    std::vector<std::pair<double, double>> ranges;
    for (const auto& clip : *clips_) {
        const double a = juce::jlimit(0.0, duration, clip.startSeconds);
        const double b = juce::jlimit(0.0, duration, clip.startSeconds + clip.durationSeconds());
        if (b > a)
            ranges.push_back({ a, b });
    }
    std::sort(ranges.begin(), ranges.end());

    double cursor = 0.0;
    for (const auto& r : ranges) {
        if (clicked < r.first) {
            seconds = cursor;
            return r.first - cursor > 0.25;
        }
        cursor = std::max(cursor, r.second);
    }

    seconds = cursor;
    return duration - cursor > 0.25;
}

void MusicTimeline::drawWaveformPeaks(juce::Graphics& g, juce::Rectangle<float> area,
                                      const std::vector<float>& peaks, float alpha) {
    if (peaks.empty() || alpha <= 0.01f)
        return;

    g.setColour(juce::Colour(0xff63d6b0).withAlpha(alpha));
    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.38f;
    const int width = std::max(1, static_cast<int>(area.getWidth()));

    // Scale to the normalized (heard) level on a fixed reference, not each clip's
    // own peak — so a quiet take and a loud take display at the level you hear,
    // instead of both being auto-stretched to full height.
    for (int x = 0; x < width; ++x) {
        const int index = static_cast<int>(
            static_cast<int64_t>(x) * static_cast<int64_t>(peaks.size()) / width);
        const float raw = peaks[static_cast<std::size_t>(std::min(index, static_cast<int>(peaks.size()) - 1))];
        const float peak = juce::jlimit(0.0f, 1.0f, raw * voiceWaveformGain_);
        const float px = area.getX() + static_cast<float>(x);
        g.drawVerticalLine(static_cast<int>(px), midY - peak * halfH, midY + peak * halfH);
    }
}

void MusicTimeline::drawWaveform(juce::Graphics& g, juce::Rectangle<float> area) {
    g.setColour(juce::Colour(0xff303640));
    g.fillRoundedRectangle(area, 4.0f);

    const bool hasDryPeaks = dryVoicePeaks_ != nullptr && !dryVoicePeaks_->empty();
    const bool hasProcessedPeaks = processedVoicePeaks_ != nullptr && !processedVoicePeaks_->empty();
    if (hasDryPeaks) {
        if (!hasProcessedPeaks) {
            drawWaveformPeaks(g, area, *dryVoicePeaks_, 1.0f);
            return;
        }
        // Blend dry -> denoised by the noise-reduction amount, so the waveform
        // shows what you hear: at 0% the full take (background and all), at 100%
        // only the cleaned voice. voiceWaveformTransition_ eases the denoised
        // peaks in when they first become available so it doesn't pop.
        const float weight = juce::jlimit(0.0f, 1.0f, voiceNoiseReduction_)
                           * juce::jlimit(0.0f, 1.0f, voiceWaveformTransition_);
        const auto& dry = *dryVoicePeaks_;
        const auto& proc = *processedVoicePeaks_;
        const std::size_t n = std::min(dry.size(), proc.size());
        std::vector<float> blended(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i)
            blended[i] = (1.0f - weight) * dry[i] + weight * proc[i];
        drawWaveformPeaks(g, area, blended, 1.0f);
        return;
    }

    g.setColour(juce::Colour(0xff63d6b0));

    if (voice_ == nullptr || voice_->getNumSamples() == 0)
        return;

    const int samples = voice_->getNumSamples();
    const int channels = voice_->getNumChannels();
    const int width = std::max(1, static_cast<int>(area.getWidth()));
    std::vector<float> peaks(static_cast<std::size_t>(width), 0.0f);
    float maxPeak = 0.001f;

    for (int x = 0; x < width; ++x) {
        const int s0 = static_cast<int>(static_cast<int64_t>(x) * samples / width);
        const int s1 = std::max(s0 + 1,
            static_cast<int>(static_cast<int64_t>(x + 1) * samples / width));
        float peak = 0.0f;
        for (int s = s0; s < std::min(samples, s1); ++s) {
            float mono = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                mono += voice_->getSample(ch, s);
            mono /= static_cast<float>(std::max(1, channels));
            peak = std::max(peak, std::abs(mono));
        }
        peaks[static_cast<std::size_t>(x)] = peak;
        maxPeak = std::max(maxPeak, peak);
    }

    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.38f;
    for (int x = 0; x < width; ++x) {
        const float peak = juce::jlimit(0.0f, 1.0f, peaks[static_cast<std::size_t>(x)] / maxPeak);
        const float px = area.getX() + static_cast<float>(x);
        g.drawVerticalLine(static_cast<int>(px), midY - peak * halfH, midY + peak * halfH);
    }
}

void MusicTimeline::drawClipWaveform(juce::Graphics& g, const MusicClip& clip, juce::Rectangle<float> area) {
    auto wave = area.reduced(0.0f, 2.0f);
    if (wave.getWidth() < 3.0f || wave.getHeight() < 3.0f || clip.waveformPeaks.empty())
        return;

    const int columns = std::min(clip.waveformProcessedColumns, static_cast<int>(clip.waveformPeaks.size()));
    if (columns <= 0)
        return;

    g.setColour(juce::Colour(0xa8ffffff));
    const float midY = wave.getCentreY();
    const float halfH = wave.getHeight() * 0.42f;
    const int pixelColumns = std::max(1, static_cast<int>(wave.getWidth()));
    const int peakColumns = static_cast<int>(clip.waveformPeaks.size());
    const double sourceDuration = std::max(0.001, clip.sourceDurationSeconds());
    const double offsetFraction = juce::jlimit(0.0, 1.0, clip.sourceOffsetSeconds / sourceDuration);
    const double visibleFraction = juce::jlimit(0.0, 1.0, clip.durationSeconds() / sourceDuration);

    float maxPeak = 0.001f;
    for (int x = 0; x < pixelColumns; ++x) {
        const double visiblePos = static_cast<double>(x) / static_cast<double>(pixelColumns);
        const int col = static_cast<int>((offsetFraction + visiblePos * visibleFraction)
                                         * static_cast<double>(peakColumns));
        if (col >= columns)
            break;
        maxPeak = std::max(maxPeak, clip.waveformPeaks[static_cast<std::size_t>(col)]);
    }

    for (int x = 0; x < pixelColumns; ++x) {
        const double visiblePos = static_cast<double>(x) / static_cast<double>(pixelColumns);
        const int col = static_cast<int>((offsetFraction + visiblePos * visibleFraction)
                                         * static_cast<double>(peakColumns));
        if (col >= columns)
            break;
        const float peak = clip.waveformPeaks[static_cast<std::size_t>(col)];
        const float normalizedPeak = juce::jlimit(0.0f, 1.0f, peak / maxPeak);
        const float px = wave.getX() + static_cast<float>(x);
        g.drawVerticalLine(static_cast<int>(px),
                           midY - normalizedPeak * halfH,
                           midY + normalizedPeak * halfH);
    }
}

void MusicTimeline::drawClipFadeOverlay(juce::Graphics& g, const MusicClip& clip,
                                        juce::Rectangle<float> area, bool selected) {
    const double length = std::max(0.001, clip.durationSeconds());
    const double fadeIn = juce::jlimit(0.0, length, clip.fadeInSeconds);
    const double fadeOut = juce::jlimit(0.0, length, clip.fadeOutSeconds);
    const float x0 = area.getX();
    const float x1 = area.getRight();
    const float yFull = clipEnvelopeTopY(area, clip);
    const float ySilent = area.getBottom() - kClipEnvelopeSilentBottomInset;
    const float inX = static_cast<float>(secondsToX(clip.startSeconds + fadeIn));
    const float outX = static_cast<float>(secondsToX(clip.startSeconds + length - fadeOut));

    g.setColour(juce::Colours::black.withAlpha(0.16f));
    if (fadeIn > 0.0) {
        juce::Path shade;
        shade.startNewSubPath(x0, ySilent);
        shade.lineTo(inX, yFull);
        shade.lineTo(x0, yFull);
        shade.closeSubPath();
        g.fillPath(shade);
    }
    if (fadeOut > 0.0) {
        juce::Path shade;
        shade.startNewSubPath(outX, yFull);
        shade.lineTo(x1, ySilent);
        shade.lineTo(x1, yFull);
        shade.closeSubPath();
        g.fillPath(shade);
    }

    juce::Path envelope;
    envelope.startNewSubPath(x0, fadeIn > 0.0 ? ySilent : yFull);
    envelope.lineTo(inX, yFull);
    envelope.lineTo(outX, yFull);
    envelope.lineTo(x1, fadeOut > 0.0 ? ySilent : yFull);
    g.setColour(juce::Colours::white.withAlpha(selected ? 0.82f : 0.48f));
    g.strokePath(envelope, juce::PathStrokeType(selected ? 2.0f : 1.4f));

    if (!selected)
        return;

    auto drawHandle = [&g](juce::Point<float> p) {
        g.setColour(juce::Colour(0xff15181d));
        g.fillEllipse(p.x - 5.5f, p.y - 5.5f, 11.0f, 11.0f);
        g.setColour(juce::Colour(0xffeaffed));
        g.fillEllipse(p.x - 3.8f, p.y - 3.8f, 7.6f, 7.6f);
        g.setColour(juce::Colour(0xff6ee07a));
        g.drawEllipse(p.x - 5.5f, p.y - 5.5f, 11.0f, 11.0f, 1.2f);
    };
    drawHandle(fadeInHandlePosition(selectedIndex_));
    drawHandle(fadeOutHandlePosition(selectedIndex_));
}

void MusicTimeline::drawClipRemoveButton(juce::Graphics& g, juce::Rectangle<float> bounds) {
    g.setColour(juce::Colour(0xdd15181d));
    g.fillEllipse(bounds);
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    const auto c = bounds.getCentre();
    g.drawLine(c.x - 4.0f, c.y - 4.0f, c.x + 4.0f, c.y + 4.0f, 1.6f);
    g.drawLine(c.x + 4.0f, c.y - 4.0f, c.x - 4.0f, c.y + 4.0f, 1.6f);
}

void MusicTimeline::drawPlus(juce::Graphics& g, juce::Rectangle<float> bounds) {
    g.setColour(juce::Colour(0xff2f7d52));
    g.fillEllipse(bounds);
    g.setColour(juce::Colours::white);
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    g.drawLine(cx - 6.0f, cy, cx + 6.0f, cy, 2.0f);
    g.drawLine(cx, cy - 6.0f, cx, cy + 6.0f, 2.0f);
}

void MusicTimeline::drawGapPluses(juce::Graphics& g) {
    auto lane = musicLaneBounds();
    const double duration = timelineDurationSeconds();
    std::vector<std::pair<double, double>> ranges;
    if (clips_ != nullptr) {
        for (const auto& clip : *clips_) {
            const double a = juce::jlimit(0.0, duration, clip.startSeconds);
            const double b = juce::jlimit(0.0, duration, clip.startSeconds + clip.durationSeconds());
            if (b > a)
                ranges.push_back({ a, b });
        }
    }
    std::sort(ranges.begin(), ranges.end());

    const float plusY = lane.getCentreY();
    double cursor = 0.0;
    bool drew = false;
    for (const auto& r : ranges) {
        if (r.first - cursor > 0.25) {
            const double center = (cursor + r.first) * 0.5;
            drawPlus(g, lane.withCentre({ static_cast<float>(secondsToX(center)), plusY })
                           .withSizeKeepingCentre(24.0f, 24.0f));
            drew = true;
        }
        cursor = std::max(cursor, r.second);
    }
    if (duration - cursor > 0.25) {
        const double center = (cursor + duration) * 0.5;
        drawPlus(g, lane.withCentre({ static_cast<float>(secondsToX(center)), plusY })
                       .withSizeKeepingCentre(24.0f, 24.0f));
        drew = true;
    }
    if (!drew && ranges.empty())
        drawPlus(g, lane.withCentre({ static_cast<float>(secondsToX(0.0)) + 18.0f, lane.getCentreY() })
                       .withSizeKeepingCentre(24.0f, 24.0f));
}

void MusicTimeline::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff252932));
    const auto voiceLane = voiceLaneBounds();
    const auto musicLane = musicLaneBounds();

    drawWaveform(g, voiceLane);
    drawLaneLabel(g, voiceLane, "Voice");

    if (!musicLaneVisible_) {
        const float playheadXOnly = static_cast<float>(secondsToX(playheadSeconds_));
        g.setColour(juce::Colour(0xff6ee07a));
        g.drawLine(playheadXOnly, voiceLane.getY(), playheadXOnly, voiceLane.getBottom(), 2.5f);
        return;
    }

    g.setColour(juce::Colour(0xff303640));
    g.fillRoundedRectangle(musicLane, 4.0f);
    drawLaneLabel(g, musicLane, "Background");
    drawGapPluses(g);

    if (clips_ != nullptr) {
        const int count = static_cast<int>(clips_->size());
        // Pass 1: clip bodies + waveforms + labels. Where a clip overlaps an
        // earlier (already-drawn) clip, its body is drawn translucent over the
        // overlap so the clip beneath shows through — the visual cue for a crossfade.
        for (int i = 0; i < count; ++i) {
            auto b = clipBounds(i);
            if (b.isEmpty())
                continue;

            float overlapL = b.getRight();
            float overlapR = b.getX();
            for (int j = 0; j < i; ++j) {
                auto bj = clipBounds(j);
                if (bj.isEmpty())
                    continue;
                const float overlapLeft = std::max(b.getX(), bj.getX());
                const float overlapRight = std::min(b.getRight(), bj.getRight());
                if (overlapRight > overlapLeft) {
                    overlapL = std::min(overlapL, overlapLeft);
                    overlapR = std::max(overlapR, overlapRight);
                }
            }
            const bool overlaps = overlapR > overlapL;

            g.setColour((i == selectedIndex_ ? juce::Colour(0xffc7a84a) : juce::Colour(0xff6f7bd9))
                            .withAlpha(i == selectedIndex_ ? 0.24f : 0.12f));
            if (overlaps) {
                const juce::Rectangle<int> overlapRect(
                    juce::Rectangle<float>(overlapL, b.getY(), overlapR - overlapL, b.getHeight())
                        .getSmallestIntegerContainer());
                {
                    juce::Graphics::ScopedSaveState s(g);
                    g.excludeClipRegion(overlapRect);
                    g.fillRoundedRectangle(b, 4.0f);
                }
                {
                    juce::Graphics::ScopedSaveState s(g);
                    g.reduceClipRegion(overlapRect);
                    g.setColour((i == selectedIndex_ ? juce::Colour(0xffc7a84a) : juce::Colour(0xff6f7bd9))
                                    .withAlpha(i == selectedIndex_ ? 0.14f : 0.06f));
                    g.fillRoundedRectangle(b, 4.0f);
                }
            } else {
                g.fillRoundedRectangle(b, 4.0f);
            }

            drawClipWaveform(g, (*clips_)[static_cast<std::size_t>(i)], b);
            auto labelBounds = b.reduced(6, 0);
            labelBounds.setY(b.getBottom() - 23.0f);
            labelBounds.setHeight(20.0f);
            float labelClearX = labelBounds.getX();
            for (int j = 0; j < count; ++j) {
                if (j == i)
                    continue;
                auto bj = clipBounds(j);
                if (bj.isEmpty())
                    continue;
                const bool clipStartIsInOverlap = bj.getX() < b.getX() + 1.0f
                    && bj.getRight() > b.getX() + 1.0f;
                if (clipStartIsInOverlap)
                    labelClearX = std::max(labelClearX, std::min(b.getRight(), bj.getRight()) + 6.0f);
            }
            if (i == selectedIndex_) {
                const auto removeBounds = removeButtonBounds(i);
                if (!removeBounds.isEmpty())
                    labelBounds.setRight(std::max(labelBounds.getX() + 8.0f, removeBounds.getX() - 6.0f));
            }
            if (labelClearX > labelBounds.getX() && labelBounds.getWidth() > 8.0f)
                labelBounds.setLeft(std::min(labelClearX, labelBounds.getRight() - 8.0f));
            g.setColour(juce::Colours::black.withAlpha(0.38f));
            g.fillRoundedRectangle(labelBounds.expanded(3.0f, 1.0f), 3.0f);
            g.setColour(juce::Colours::white.withAlpha(0.94f));
            g.drawText((*clips_)[static_cast<std::size_t>(i)].name, labelBounds, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xaa101217));
            const auto edgeGripY = std::min(b.getBottom(), b.getY() + kClipEdgeGripTopInset);
            const auto edgeGripHeight = std::max(0.0f, b.getBottom() - edgeGripY);
            g.fillRect(juce::Rectangle<float>(b.getX(), edgeGripY, kClipEdgeGripWidth, edgeGripHeight));
            g.fillRect(juce::Rectangle<float>(b.getRight() - kClipEdgeGripWidth, edgeGripY,
                                              kClipEdgeGripWidth, edgeGripHeight));
        }

        // Pass 2: fade envelopes + handles on top of every body, so overlapping
        // clips show both fade ramps crossing.
        for (int i = 0; i < count; ++i) {
            auto b = clipBounds(i);
            if (b.isEmpty())
                continue;
            drawClipFadeOverlay(g, (*clips_)[static_cast<std::size_t>(i)], b, i == selectedIndex_);
            if (i == selectedIndex_) {
                g.setColour(juce::Colour(0xaa101217));
                const auto edgeGripY = std::min(b.getBottom(), b.getY() + kClipEdgeGripTopInset);
                const auto edgeGripHeight = std::max(0.0f, b.getBottom() - edgeGripY);
                g.fillRect(juce::Rectangle<float>(b.getX(), edgeGripY, kClipEdgeGripWidth, edgeGripHeight));
                g.fillRect(juce::Rectangle<float>(b.getRight() - kClipEdgeGripWidth, edgeGripY,
                                                  kClipEdgeGripWidth, edgeGripHeight));
                drawClipRemoveButton(g, removeButtonBounds(i));
            }
        }
    }

    const float playheadX = static_cast<float>(secondsToX(playheadSeconds_));
    g.setColour(juce::Colour(0xff6ee07a));
    g.drawLine(playheadX, timelineBounds().getY(),
               playheadX, timelineBounds().getBottom(), 2.5f);
}

void MusicTimeline::mouseMove(const juce::MouseEvent& e) {
    int gainIndex = -1;
    if (gainEnvelopeAt(e.position, gainIndex))
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MusicTimeline::mouseExit(const juce::MouseEvent&) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MusicTimeline::mouseDown(const juce::MouseEvent& e) {
    const auto p = e.position;
    double addSeconds = 0.0;
    int removeIndex = -1;
    if (removeButtonAt(p, removeIndex)) {
        selectedIndex_ = removeIndex;
        if (onSelectClip)
            onSelectClip(removeIndex);
        if (onRemoveClip)
            onRemoveClip(removeIndex);
        repaint();
        return;
    }

    int fadeIndex = -1;
    DragMode fadeMode = DragMode::None;
    if (fadeHandleAt(p, fadeIndex, fadeMode)) {
        draggingIndex_ = fadeIndex;
        selectedIndex_ = fadeIndex;
        if (onSelectClip)
            onSelectClip(fadeIndex);
        if (onClipEditStarted)
            onClipEditStarted(fadeIndex);
        const auto& clip = (*clips_)[static_cast<std::size_t>(fadeIndex)];
        dragStartSeconds_ = clip.startSeconds;
        dragLengthSeconds_ = clip.durationSeconds();
        dragFadeInSeconds_ = clip.fadeInSeconds;
        dragFadeOutSeconds_ = clip.fadeOutSeconds;
        dragMouseSeconds_ = xToSeconds(p.x);
        dragMode_ = fadeMode;
        repaint();
        return;
    }

    int gainIndex = -1;
    if (gainEnvelopeAt(p, gainIndex)) {
        draggingIndex_ = gainIndex;
        selectedIndex_ = gainIndex;
        if (onSelectClip)
            onSelectClip(gainIndex);
        if (onClipEditStarted)
            onClipEditStarted(gainIndex);
        const auto& clip = (*clips_)[static_cast<std::size_t>(gainIndex)];
        dragStartSeconds_ = clip.startSeconds;
        dragSourceOffsetSeconds_ = clip.sourceOffsetSeconds;
        dragLengthSeconds_ = clip.durationSeconds();
        dragFadeInSeconds_ = clip.fadeInSeconds;
        dragFadeOutSeconds_ = clip.fadeOutSeconds;
        dragGainDb_ = clip.gainDb;
        dragMouseSeconds_ = xToSeconds(p.x);
        dragMouseY_ = p.y;
        dragMode_ = DragMode::Gain;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        repaint();
        return;
    }

    if (clips_ != nullptr && selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(clips_->size())) {
        const auto b = clipBounds(selectedIndex_);
        const bool inSelectedEdgeGripRow = !b.isEmpty()
            && p.y >= b.getY() + kClipEdgeHitTopInset
            && p.y <= b.getBottom();
        DragMode selectedEdgeMode = DragMode::None;
        if (inSelectedEdgeGripRow && p.x <= b.getX() + kClipEdgeHitWidth && p.x >= b.getX() - 1.0f)
            selectedEdgeMode = DragMode::ResizeLeft;
        else if (inSelectedEdgeGripRow && p.x >= b.getRight() - kClipEdgeHitWidth && p.x <= b.getRight() + 1.0f)
            selectedEdgeMode = DragMode::ResizeRight;

        if (selectedEdgeMode != DragMode::None) {
            draggingIndex_ = selectedIndex_;
            if (onSelectClip)
                onSelectClip(selectedIndex_);
            if (onClipEditStarted)
                onClipEditStarted(selectedIndex_);
            const auto& clip = (*clips_)[static_cast<std::size_t>(selectedIndex_)];
            dragStartSeconds_ = clip.startSeconds;
            dragSourceOffsetSeconds_ = clip.sourceOffsetSeconds;
            dragLengthSeconds_ = clip.durationSeconds();
            dragFadeInSeconds_ = clip.fadeInSeconds;
            dragFadeOutSeconds_ = clip.fadeOutSeconds;
            dragGainDb_ = clip.gainDb;
            dragMouseSeconds_ = xToSeconds(p.x);
            dragMode_ = selectedEdgeMode;
            if (onClipDragStateChanged)
                onClipDragStateChanged(selectedIndex_, true);
            repaint();
            return;
        }
    }

    const int hit = clipAt(p);
    if (hit >= 0) {
        draggingIndex_ = hit;
        selectedIndex_ = hit;
        if (onSelectClip)
            onSelectClip(hit);
        if (onClipEditStarted)
            onClipEditStarted(hit);
        const auto b = clipBounds(hit);
        const auto& clip = (*clips_)[static_cast<std::size_t>(hit)];
        dragStartSeconds_ = clip.startSeconds;
        dragSourceOffsetSeconds_ = clip.sourceOffsetSeconds;
        dragLengthSeconds_ = clip.durationSeconds();
        dragFadeInSeconds_ = clip.fadeInSeconds;
        dragFadeOutSeconds_ = clip.fadeOutSeconds;
        dragGainDb_ = clip.gainDb;
        dragMouseSeconds_ = xToSeconds(p.x);
        const bool inEdgeGripRow = p.y >= b.getY() + kClipEdgeHitTopInset && p.y <= b.getBottom();
        if (inEdgeGripRow && p.x <= b.getX() + kClipEdgeHitWidth)
            dragMode_ = DragMode::ResizeLeft;
        else if (inEdgeGripRow && p.x >= b.getRight() - kClipEdgeHitWidth)
            dragMode_ = DragMode::ResizeRight;
        else if (e.mods.isShiftDown())
            dragMode_ = DragMode::Slip;
        else
            dragMode_ = DragMode::Move;
        if (onClipDragStateChanged)
            onClipDragStateChanged(hit, true);
        repaint();
        return;
    }

    if (voiceLaneBounds().contains(p)) {
        scrubbing_ = true;
        const double seconds = xToSeconds(p.x);
        setPlayheadSeconds(seconds);
        if (onSeek)
            onSeek(seconds);
        return;
    }

    if (plusAt(p, addSeconds) && onAddAt)
        onAddAt(addSeconds);
}

void MusicTimeline::mouseDrag(const juce::MouseEvent& e) {
    if (scrubbing_) {
        const double seconds = xToSeconds(e.position.x);
        setPlayheadSeconds(seconds);
        if (onSeek)
            onSeek(seconds);
        return;
    }

    if (draggingIndex_ < 0 || clips_ == nullptr || draggingIndex_ >= static_cast<int>(clips_->size()))
        return;

    const double current = xToSeconds(e.position.x);
    const double delta = current - dragMouseSeconds_;
    const auto& clip = (*clips_)[static_cast<std::size_t>(draggingIndex_)];
    const double sourceLength = std::max(0.1, clip.sourceDurationSeconds());
    const double timelineLength = timelineDurationSeconds();
    double start = dragStartSeconds_;
    double sourceOffset = clip.sourceOffsetSeconds;
    double length = dragLengthSeconds_;

    if (dragMode_ == DragMode::Move) {
        start = juce::jlimit(0.0, std::max(0.0, timelineLength - length), dragStartSeconds_ + delta);
        sourceOffset = dragSourceOffsetSeconds_;
    } else if (dragMode_ == DragMode::Slip) {
        start = dragStartSeconds_;
        length = dragLengthSeconds_;
        const double maxOffset = std::max(0.0, sourceLength - length);
        sourceOffset = juce::jlimit(0.0, maxOffset, dragSourceOffsetSeconds_ - delta);
    } else if (dragMode_ == DragMode::ResizeRight) {
        sourceOffset = dragSourceOffsetSeconds_;
        length = juce::jlimit(0.1, std::min(sourceLength - sourceOffset, timelineLength - start),
                              dragLengthSeconds_ + delta);
    } else if (dragMode_ == DragMode::ResizeLeft) {
        const double minDelta = std::max(-dragStartSeconds_, -dragSourceOffsetSeconds_);
        const double maxDelta = dragLengthSeconds_ - 0.1;
        const double trimDelta = juce::jlimit(minDelta, maxDelta, delta);
        const double newStart = dragStartSeconds_ + trimDelta;
        const double end = dragStartSeconds_ + dragLengthSeconds_;
        start = std::min(newStart, end - 0.1);
        sourceOffset = juce::jlimit(0.0, sourceLength - 0.1,
                                    dragSourceOffsetSeconds_ + trimDelta);
        length = juce::jlimit(0.1, std::min(sourceLength - sourceOffset, timelineLength - start),
                              end - start);
    }

    if (dragMode_ == DragMode::FadeIn) {
        const double maxFade = std::max(0.0, dragLengthSeconds_ - juce::jlimit(0.0, dragLengthSeconds_, clip.fadeOutSeconds));
        const double fadeIn = juce::jlimit(0.0, maxFade, dragFadeInSeconds_ + delta);
        if (onAdjustClipFades)
            onAdjustClipFades(draggingIndex_, fadeIn, clip.fadeOutSeconds);
        return;
    }

    if (dragMode_ == DragMode::FadeOut) {
        const double maxFade = std::max(0.0, dragLengthSeconds_ - juce::jlimit(0.0, dragLengthSeconds_, clip.fadeInSeconds));
        const double fadeOut = juce::jlimit(0.0, maxFade, dragFadeOutSeconds_ - delta);
        if (onAdjustClipFades)
            onAdjustClipFades(draggingIndex_, clip.fadeInSeconds, fadeOut);
        return;
    }

    if (dragMode_ == DragMode::Gain) {
        constexpr double dbPerPixel = 0.35;
        const double gainDb = juce::jlimit(-60.0, 6.0,
            dragGainDb_ - static_cast<double>(e.position.y - dragMouseY_) * dbPerPixel);
        if (onAdjustClipGain)
            onAdjustClipGain(draggingIndex_, gainDb);
        return;
    }

    if (onMoveOrResizeClip)
        onMoveOrResizeClip(draggingIndex_, start, sourceOffset, length);
}

void MusicTimeline::mouseUp(const juce::MouseEvent&) {
    if (draggingIndex_ >= 0 && onClipDragStateChanged)
        onClipDragStateChanged(draggingIndex_, false);
    draggingIndex_ = -1;
    dragMode_ = DragMode::None;
    scrubbing_ = false;
}
