#include "FileDropComponent.h"

namespace {
const juce::StringArray kMediaExtensions {
    // video
    "mp4", "mov", "m4v", "mkv", "avi", "webm",
    // audio
    "wav", "mp3", "m4a", "aac", "flac", "aiff", "aif", "ogg"
};
}

FileDropComponent::FileDropComponent()
    : status_("Drag a video or audio file here\n(or click to browse)") {}

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
    auto r = getLocalBounds().toFloat().reduced(8.0f);

    g.setColour(juce::Colour(0xff2b2f36));
    g.fillRoundedRectangle(r, 10.0f);

    g.setColour(highlight_ ? juce::Colours::aqua : juce::Colour(0xff5a6270));
    const float dash[] = { 6.0f, 5.0f };
    juce::Path border;
    border.addRoundedRectangle(r, 10.0f);
    juce::PathStrokeType(highlight_ ? 2.5f : 1.5f).createDashedStroke(border, border, dash, 2);
    g.strokePath(border, juce::PathStrokeType(highlight_ ? 2.5f : 1.5f));

    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(juce::Font(juce::FontOptions(16.0f)));
    g.drawFittedText(status_, getLocalBounds().reduced(16), juce::Justification::centred, 4);
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
