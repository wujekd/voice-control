#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <vector>

// Modal export dialog body. Defaults to "keep original format" — exporting in
// the source's own format/container. The user can switch to "choose format" to
// pick an audio target (or the source's native video). Reports the resolved
// file plus whether to mux back into the source video via onExport. Does no
// encoding itself — MainComponent runs the offline render.
class ExportComponent : public juce::Component {
public:
    ExportComponent(const juce::File& sourceFile, bool sourceHasVideo);

    std::function<void(juce::File output, bool muxVideo)> onExport;
    std::function<void()> onCancel;

    void resized() override;

    // Natural size; used to size the hosting DialogWindow.
    static constexpr int kWidth = 500;
    static constexpr int kHeight = 224;

private:
    struct FormatOption {
        juce::String label;
        juce::String extension; // without the leading dot
        bool muxVideo = false;
    };

    const FormatOption& selectedFormat() const;
    // The format that will actually be written: the original when "keep
    // original" is selected, otherwise the format chosen in the combo box.
    const FormatOption& resolvedFormat() const;
    void modeChanged();
    void syncPathExtension();
    void browse();
    void closeDialog();

    std::vector<FormatOption> formats_;
    FormatOption originalFormat_;
    juce::File sourceFile_;

    juce::ToggleButton keepOriginalButton_ { "Keep original format" };
    juce::ToggleButton chooseFormatButton_ { "Choose format" };
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
