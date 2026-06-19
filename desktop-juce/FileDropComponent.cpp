#include "FileDropComponent.h"

#include <cmath>

namespace {
const juce::StringArray kMediaExtensions {
    // video
    "mp4", "mov", "m4v", "mkv", "avi", "webm",
    // audio
    "wav", "mp3", "m4a", "aac", "flac", "aiff", "aif", "ogg", "opus"
};
const juce::String kDefaultStatus { "Add voice audio or video\nDrag here, or click to browse" };
}

FileDropComponent::FileDropComponent()
    : status_(kDefaultStatus) {}

bool FileDropComponent::looksLikeVideo(const juce::String& path) {
    return kMediaExtensions.contains(juce::File(path).getFileExtension()
                                         .removeCharacters(".").toLowerCase());
}

void FileDropComponent::setStatus(const juce::String& text) {
    status_ = text;
    repaint();
}

void FileDropComponent::setBusy(bool busy) {
    if (busy_ == busy)
        return;

    busy_ = busy;
    busyPhase_ = 0.0f;
    if (busy_)
        startTimerHz(30);
    else
        stopTimer();
    repaint();
}

void FileDropComponent::deliver(const juce::File& f) {
    if (f.existsAsFile() && onFile)
        onFile(f);
}

void FileDropComponent::timerCallback() {
    busyPhase_ = std::fmod(busyPhase_ + 1.0f / 30.0f, 1.0f);
    repaint();
}

void FileDropComponent::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();

    // Only the plus button: no frame, no instructions. Clicking anywhere in the
    // field (the whole component) opens the file selector. Status text is shown
    // only for transient messages (e.g. "Analyzing...").
    const bool showText = status_.isNotEmpty() && status_ != kDefaultStatus;

    auto icon = r.withSizeKeepingCentre(30.0f, 30.0f);
    if (showText)
        icon.setY(r.getCentreY() - 24.0f);

    const float cx = icon.getCentreX();
    const float cy = icon.getCentreY();

    if (busy_) {
        const auto green = juce::Colour(0xff6ee07a);
        g.setColour(juce::Colour(0xff15181d));
        g.fillEllipse(icon);
        g.setColour(green.withAlpha(0.22f));
        g.drawEllipse(icon, 1.4f);

        constexpr int kLedCount = 18;
        const float ledRadius = icon.getWidth() * 0.5f + 7.0f;
        const float head = busyPhase_ * static_cast<float>(kLedCount);
        for (int i = 0; i < kLedCount; ++i) {
            const float a = -juce::MathConstants<float>::halfPi
                + juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(kLedCount);
            const auto p = juce::Point<float>(cx + std::cos(a) * ledRadius,
                                              cy + std::sin(a) * ledRadius);
            float dist = head - static_cast<float>(i);
            while (dist < 0.0f)
                dist += static_cast<float>(kLedCount);
            while (dist >= static_cast<float>(kLedCount))
                dist -= static_cast<float>(kLedCount);

            const float trail = juce::jlimit(0.0f, 1.0f, 1.0f - dist / 5.0f);
            const auto led = green.interpolatedWith(juce::Colour(0xff253026), 1.0f - trail);
            const float dot = 2.1f + trail * 1.0f;
            if (trail > 0.45f) {
                g.setColour(green.withAlpha(0.14f * trail));
                g.fillEllipse(p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
            }
            g.setColour(led.withAlpha(0.36f + trail * 0.62f));
            g.fillEllipse(p.x - dot, p.y - dot, dot * 2.0f, dot * 2.0f);
        }
    } else {
        g.setColour(highlight_ ? juce::Colours::aqua : juce::Colour(0xff2f7d52));
        g.fillEllipse(icon);
        g.setColour(juce::Colours::white);
        g.drawLine(cx - 7.0f, cy, cx + 7.0f, cy, 2.0f);
        g.drawLine(cx, cy - 7.0f, cx, cy + 7.0f, 2.0f);
    }

    if (showText) {
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        auto textArea = getLocalBounds().reduced(12);
        textArea.removeFromTop(static_cast<int>(icon.getBottom()) + 10);
        auto text = status_;
        if (busy_)
            text = text.upToFirstOccurrenceOf("\n", false, false);
        g.drawFittedText(text, textArea, juce::Justification::centred, 1);
    }
}

void FileDropComponent::mouseUp(const juce::MouseEvent&) {
    chooser_ = std::make_unique<juce::FileChooser>(
        "Choose a video or audio file", juce::File(),
        "*.mp4;*.mov;*.m4v;*.mkv;*.avi;*.webm;*.wav;*.mp3;*.m4a;*.aac;*.flac;*.aiff;*.aif;*.ogg;*.opus");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) { deliver(fc.getResult()); });
}

bool FileDropComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files)
        if (looksLikeVideo(f)) return true;
    return false;
}

void FileDropComponent::fileDragEnter(const juce::StringArray&, int, int) {
    highlight_ = true;
    repaint();
}

void FileDropComponent::fileDragExit(const juce::StringArray&) {
    highlight_ = false;
    repaint();
}

void FileDropComponent::filesDropped(const juce::StringArray& files, int, int) {
    highlight_ = false;
    repaint();
    for (const auto& f : files) {
        if (looksLikeVideo(f)) {
            deliver(juce::File(f));
            return;
        }
    }
}
