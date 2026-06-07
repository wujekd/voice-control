#include "ExportComponent.h"

ExportComponent::ExportComponent(const juce::File& sourceFile, bool sourceHasVideo)
    : sourceFile_(sourceFile) {
    // A video source defaults to re-muxing into MP4; audio-only targets follow.
    // Audio codecs are inferred downstream from the extension by
    // FFmpeg::encodeAudio (wav is written directly).
    if (sourceHasVideo)
        formats_.push_back({ "Video \xE2\x80\x94 MP4 (H.264 + AAC)", "mp4", true });
    formats_.push_back({ "WAV (lossless)", "wav", false });
    formats_.push_back({ "MP3", "mp3", false });
    formats_.push_back({ "M4A (AAC)", "m4a", false });
    formats_.push_back({ "FLAC (lossless)", "flac", false });
    formats_.push_back({ "Opus", "opus", false });

    // The "keep original" target: same container/extension as the source, and
    // for a video source mux the processed audio back into the original video.
    const auto srcExt = sourceFile_.getFileExtension().removeCharacters(".").toLowerCase();
    originalFormat_ = { "Original (" + srcExt.toUpperCase() + ")", srcExt, sourceHasVideo };

    // Default combo selection (only used in "choose format" mode): video source
    // -> the MP4 item; audio source -> the item matching the source's own
    // extension, else WAV.
    int defaultIndex = 0;
    if (!sourceHasVideo) {
        for (int i = 0; i < (int) formats_.size(); ++i) {
            if (formats_[(size_t) i].extension == srcExt) {
                defaultIndex = i;
                break;
            }
        }
    }

    // Mode radio buttons: keep original (default) vs. choose a format.
    keepOriginalButton_.setRadioGroupId(1);
    chooseFormatButton_.setRadioGroupId(1);
    keepOriginalButton_.setToggleState(true, juce::dontSendNotification);
    keepOriginalButton_.onClick = [this] { modeChanged(); };
    chooseFormatButton_.onClick = [this] { modeChanged(); };
    addAndMakeVisible(keepOriginalButton_);
    addAndMakeVisible(chooseFormatButton_);

    formatLabel_.setJustificationType(juce::Justification::centredLeft);
    pathLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(formatLabel_);
    addAndMakeVisible(pathLabel_);

    for (int i = 0; i < (int) formats_.size(); ++i)
        formatBox_.addItem(formats_[(size_t) i].label, i + 1);
    formatBox_.setSelectedItemIndex(defaultIndex, juce::dontSendNotification);
    formatBox_.onChange = [this] { syncPathExtension(); };
    formatBox_.setEnabled(false); // disabled until "choose format" is selected
    addAndMakeVisible(formatBox_);

    pathEditor_.setMultiLine(false);
    pathEditor_.setReadOnly(true);
    pathEditor_.setCaretVisible(false);
    addAndMakeVisible(pathEditor_);

    browseButton_.onClick = [this] { browse(); };
    addAndMakeVisible(browseButton_);

    cancelButton_.onClick = [this] {
        auto cb = onCancel;
        closeDialog();
        if (cb) cb();
    };
    addAndMakeVisible(cancelButton_);

    exportButton_.onClick = [this] {
        const juce::File out(pathEditor_.getText());
        if (out == juce::File()) return;
        const bool mux = resolvedFormat().muxVideo;
        auto cb = onExport;
        closeDialog();
        if (cb) cb(out, mux);
    };
    addAndMakeVisible(exportButton_);

    // Seed the destination path from the source folder and current format.
    const auto dir = sourceFile_.getParentDirectory();
    const auto base = sourceFile_.getFileNameWithoutExtension() + "_enhanced";
    pathEditor_.setText(dir.getChildFile(base + "." + resolvedFormat().extension).getFullPathName(),
                        juce::dontSendNotification);

    setSize(kWidth, kHeight);
}

const ExportComponent::FormatOption& ExportComponent::selectedFormat() const {
    const int i = juce::jlimit(0, (int) formats_.size() - 1, formatBox_.getSelectedItemIndex());
    return formats_[(size_t) i];
}

const ExportComponent::FormatOption& ExportComponent::resolvedFormat() const {
    return keepOriginalButton_.getToggleState() ? originalFormat_ : selectedFormat();
}

void ExportComponent::modeChanged() {
    formatBox_.setEnabled(chooseFormatButton_.getToggleState());
    syncPathExtension();
}

void ExportComponent::syncPathExtension() {
    const juce::File current(pathEditor_.getText());
    if (current == juce::File()) return;
    pathEditor_.setText(current.withFileExtension(resolvedFormat().extension).getFullPathName(),
                        juce::dontSendNotification);
}

void ExportComponent::browse() {
    const juce::File current(pathEditor_.getText());
    const auto ext = resolvedFormat().extension;
    chooser_ = std::make_unique<juce::FileChooser>("Export to", current, "*." + ext);
    chooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, ext](const juce::FileChooser& fc) {
            const auto picked = fc.getResult();
            if (picked == juce::File()) return;
            pathEditor_.setText(picked.withFileExtension(ext).getFullPathName(),
                                juce::dontSendNotification);
        });
}

void ExportComponent::closeDialog() {
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(0);
}

void ExportComponent::resized() {
    auto area = getLocalBounds().reduced(16);

    auto modeRow = area.removeFromTop(26);
    keepOriginalButton_.setBounds(modeRow.removeFromLeft(200));
    chooseFormatButton_.setBounds(modeRow);

    area.removeFromTop(10);

    auto formatRow = area.removeFromTop(26);
    formatLabel_.setBounds(formatRow.removeFromLeft(70));
    formatBox_.setBounds(formatRow);

    area.removeFromTop(14);

    pathLabel_.setBounds(area.removeFromTop(20));
    auto pathRow = area.removeFromTop(28);
    browseButton_.setBounds(pathRow.removeFromRight(96));
    pathRow.removeFromRight(8);
    pathEditor_.setBounds(pathRow);

    auto buttonRow = area.removeFromBottom(32);
    exportButton_.setBounds(buttonRow.removeFromRight(110));
    buttonRow.removeFromRight(8);
    cancelButton_.setBounds(buttonRow.removeFromRight(96));
}
