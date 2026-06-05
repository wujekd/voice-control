#include "FileDropComponent.h"

namespace {
const juce::StringArray kMediaExtensions {
    // video
    "mp4", "mov", "m4v", "mkv", "avi", "webm",
    // audio
    "wav", "mp3", "m4a", "aac", "flac", "aiff", "aif", "ogg"
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

void FileDropComponent::deliver(const juce::File& f) {
    if (f.existsAsFile() && onFile)
        onFile(f);
}

void FileDropComponent::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();

    // Only the plus button: no frame, no instructions. Clicking anywhere in the
    // field (the whole component) opens the file selector. Status text is shown
    // only for transient messages (e.g. "Analyzing...").
    const bool showText = status_.isNotEmpty() && status_ != kDefaultStatus;

    auto icon = r.withSizeKeepingCentre(30.0f, 30.0f);
    if (showText)
        icon.setY(r.getCentreY() - 20.0f);

    g.setColour(highlight_ ? juce::Colours::aqua : juce::Colour(0xff2f7d52));
    g.fillEllipse(icon);
    g.setColour(juce::Colours::white);
    const float cx = icon.getCentreX();
    const float cy = icon.getCentreY();
    g.drawLine(cx - 7.0f, cy, cx + 7.0f, cy, 2.0f);
    g.drawLine(cx, cy - 7.0f, cx, cy + 7.0f, 2.0f);

    if (showText) {
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        auto textArea = getLocalBounds().reduced(12);
        textArea.removeFromTop(textArea.getHeight() / 2 + 4);
        g.drawFittedText(status_, textArea, juce::Justification::centred, 2);
    }
}

void FileDropComponent::mouseUp(const juce::MouseEvent&) {
    chooser_ = std::make_unique<juce::FileChooser>(
        "Choose a video or audio file", juce::File(),
        "*.mp4;*.mov;*.m4v;*.mkv;*.avi;*.webm;*.wav;*.mp3;*.m4a;*.aac;*.flac;*.aiff;*.aif;*.ogg");
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
