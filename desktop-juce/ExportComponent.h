#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <vector>

// Modal export dialog body. Lets the user pick an output format (the source's
// native video, or an audio format to transcode to) and a destination path,
// then reports the resolved file plus whether to mux back into the source video
// via onExport. Does no encoding itself — MainComponent runs the offline render.
class ExportComponent : public juce::Component {
public:
    ExportComponent(const juce::File& sourceFile, bool sourceHasVideo);

    std::function<void(juce::File output, bool muxVideo)> onExport;
    std::function<void()> onCancel;

    void resized() override;

    // Natural size; used to size the hosting DialogWindow.
    static constexpr int kWidth = 500;
    static constexpr int kHeight = 184;

private:
    struct FormatOption {
        juce::String label;
        juce::String extension; // without the leading dot
        bool muxVideo = false;
    };

    const FormatOption& selectedFormat() const;
    void formatChanged();
    void browse();
    void closeDialog();

    std::vector<FormatOption> formats_;
    juce::File sourceFile_;

    juce::Label formatLabel_ { {}, "Format" };
    juce::ComboBox formatBox_;
    juce::Label pathLabel_ { {}, "Save to" };
    juce::TextEditor pathEditor_;
    juce::TextButton browseButton_ { "Browse..." };
    juce::TextButton exportButton_ { "Export" };
    juce::TextButton cancelButton_ { "Cancel" };
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportComponent)
};
