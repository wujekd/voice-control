#include "MusicTimeline.h"

#include <algorithm>
#include <cmath>

void MusicTimeline::setVoice(const juce::AudioBuffer<float>* voice, double sampleRate) {
    voice_ = voice;
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    repaint();
}

void MusicTimeline::setClips(const std::vector<MusicClip>* clips, int selectedIndex) {
    clips_ = clips;
    selectedIndex_ = selectedIndex;
    repaint();
}

void MusicTimeline::setPlayheadSeconds(double seconds) {
    playheadSeconds_ = juce::jlimit(0.0, timelineDurationSeconds(), seconds);
    repaint();
}

double MusicTimeline::timelineDurationSeconds() const {
    return voice_ != nullptr && voice_->getNumSamples() > 0
        ? static_cast<double>(voice_->getNumSamples()) / sampleRate_
        : 60.0;
}

juce::Rectangle<float> MusicTimeline::timelineBounds() const {
    return getLocalBounds().reduced(10).toFloat();
}

juce::Rectangle<float> MusicTimeline::voiceLaneBounds() const {
    return timelineBounds().removeFromTop(58.0f);
}

juce::Rectangle<float> MusicTimeline::musicLaneBounds() const {
    return getLocalBounds().reduced(10).removeFromBottom(48).toFloat();
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
    return { x1, lane.getY() + 5.0f, std::max(8.0f, x2 - x1), lane.getHeight() - 10.0f };
}

int MusicTimeline::clipAt(juce::Point<float> p) const {
    if (clips_ == nullptr)
        return -1;
    for (int i = static_cast<int>(clips_->size()) - 1; i >= 0; --i)
        if (clipBounds(i).contains(p))
            return i;
    return -1;
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

void MusicTimeline::drawWaveform(juce::Graphics& g, juce::Rectangle<float> area) {
    g.setColour(juce::Colour(0xff303640));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(juce::Colour(0xff6bd0ff));

    if (voice_ == nullptr || voice_->getNumSamples() == 0)
        return;

    const int samples = voice_->getNumSamples();
    const int channels = voice_->getNumChannels();
    const int width = std::max(1, static_cast<int>(area.getWidth()));
    const float base = area.getBottom() - 4.0f;
    const float scale = area.getHeight() * 0.95f;

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
        const float px = area.getX() + static_cast<float>(x);
        g.drawVerticalLine(static_cast<int>(px), base - peak * scale, base);
    }
}

void MusicTimeline::drawClipWaveform(juce::Graphics& g, const MusicClip& clip, juce::Rectangle<float> area) {
    auto wave = area.reduced(7.0f, 8.0f);
    if (wave.getWidth() < 3.0f || wave.getHeight() < 3.0f || clip.waveformPeaks.empty())
        return;

    const int columns = std::min(clip.waveformProcessedColumns, static_cast<int>(clip.waveformPeaks.size()));
    if (columns <= 0)
        return;

    g.setColour(juce::Colour(0x55ffffff));
    const float midY = wave.getCentreY();
    const float halfH = wave.getHeight() * 0.46f;
    const int pixelColumns = std::max(1, static_cast<int>(wave.getWidth()));
    const int peakColumns = static_cast<int>(clip.waveformPeaks.size());
    const double sourceDuration = std::max(0.001, clip.sourceDurationSeconds());
    const double visibleFraction = juce::jlimit(0.0, 1.0, clip.durationSeconds() / sourceDuration);
    for (int x = 0; x < pixelColumns; ++x) {
        const double visiblePos = static_cast<double>(x) / static_cast<double>(pixelColumns);
        const int col = static_cast<int>(visiblePos * visibleFraction * static_cast<double>(peakColumns));
        if (col >= columns)
            break;
        const float peak = clip.waveformPeaks[static_cast<std::size_t>(col)];
        const float px = wave.getX() + static_cast<float>(x);
        g.drawVerticalLine(static_cast<int>(px), midY - peak * halfH, midY + peak * halfH);
    }
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

    double cursor = 0.0;
    bool drew = false;
    for (const auto& r : ranges) {
        if (r.first - cursor > 0.25) {
            const double center = (cursor + r.first) * 0.5;
            drawPlus(g, lane.withCentre({ static_cast<float>(secondsToX(center)), lane.getCentreY() })
                           .withSizeKeepingCentre(24.0f, 24.0f));
            drew = true;
        }
        cursor = std::max(cursor, r.second);
    }
    if (duration - cursor > 0.25) {
        const double center = (cursor + duration) * 0.5;
        drawPlus(g, lane.withCentre({ static_cast<float>(secondsToX(center)), lane.getCentreY() })
                       .withSizeKeepingCentre(24.0f, 24.0f));
        drew = true;
    }
    if (!drew && ranges.empty())
        drawPlus(g, lane.withCentre({ static_cast<float>(secondsToX(0.0)) + 18.0f, lane.getCentreY() })
                       .withSizeKeepingCentre(24.0f, 24.0f));
}

void MusicTimeline::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff252932));
    auto r = timelineBounds();
    auto voiceLane = r.removeFromTop(58.0f);
    auto musicLane = musicLaneBounds();

    g.setColour(juce::Colour(0xffa8b0bd));
    g.drawText("Voice waveform", voiceLane.removeFromTop(16.0f), juce::Justification::centredLeft);
    drawWaveform(g, voiceLane);

    g.setColour(juce::Colour(0xff303640));
    g.fillRoundedRectangle(musicLane, 4.0f);
    g.setColour(juce::Colour(0xffa8b0bd));
    g.drawText("Music", musicLane.removeFromLeft(50.0f), juce::Justification::centredLeft);
    drawGapPluses(g);

    if (clips_ != nullptr) {
        for (int i = 0; i < static_cast<int>(clips_->size()); ++i) {
            auto b = clipBounds(i);
            g.setColour(i == selectedIndex_ ? juce::Colour(0xffc7a84a) : juce::Colour(0xff6f7bd9));
            g.fillRoundedRectangle(b, 4.0f);
            drawClipWaveform(g, (*clips_)[static_cast<std::size_t>(i)], b);
            g.setColour(juce::Colours::white);
            g.drawText((*clips_)[static_cast<std::size_t>(i)].name, b.reduced(6, 0), juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xaa101217));
            g.fillRect(b.removeFromLeft(4.0f));
            g.fillRect(clipBounds(i).removeFromRight(4.0f));
        }
    }

    const float playheadX = static_cast<float>(secondsToX(playheadSeconds_));
    g.setColour(juce::Colour(0xffff5d5d));
    g.drawLine(playheadX, timelineBounds().getY() + 16.0f,
               playheadX, timelineBounds().getBottom(), 2.0f);
}

void MusicTimeline::mouseDown(const juce::MouseEvent& e) {
    const auto p = e.position;
    double addSeconds = 0.0;
    const int hit = clipAt(p);
    if (hit >= 0) {
        draggingIndex_ = hit;
        selectedIndex_ = hit;
        if (onSelectClip)
            onSelectClip(hit);
        const auto b = clipBounds(hit);
        const auto& clip = (*clips_)[static_cast<std::size_t>(hit)];
        dragStartSeconds_ = clip.startSeconds;
        dragLengthSeconds_ = clip.durationSeconds();
        dragMouseSeconds_ = xToSeconds(p.x);
        if (p.x < b.getX() + 8.0f)
            dragMode_ = DragMode::ResizeLeft;
        else if (p.x > b.getRight() - 8.0f)
            dragMode_ = DragMode::ResizeRight;
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
    double length = dragLengthSeconds_;

    if (dragMode_ == DragMode::Move) {
        start = juce::jlimit(0.0, std::max(0.0, timelineLength - length), dragStartSeconds_ + delta);
    } else if (dragMode_ == DragMode::ResizeRight) {
        length = juce::jlimit(0.1, std::min(sourceLength, timelineLength - start),
                              dragLengthSeconds_ + delta);
    } else if (dragMode_ == DragMode::ResizeLeft) {
        const double newStart = std::max(0.0, dragStartSeconds_ + delta);
        const double end = dragStartSeconds_ + dragLengthSeconds_;
        start = std::min(newStart, end - 0.1);
        length = juce::jlimit(0.1, std::min(sourceLength, timelineLength - start), end - start);
    }

    if (onMoveOrResizeClip)
        onMoveOrResizeClip(draggingIndex_, start, length);
}

void MusicTimeline::mouseUp(const juce::MouseEvent&) {
    if (draggingIndex_ >= 0 && onClipDragStateChanged)
        onClipDragStateChanged(draggingIndex_, false);
    draggingIndex_ = -1;
    dragMode_ = DragMode::None;
    scrubbing_ = false;
}
