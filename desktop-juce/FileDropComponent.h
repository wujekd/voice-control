#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>

// Drag-and-drop (and click-to-browse) target for video files. Reports the
// chosen file via the onFile callback; it does no processing itself.
class FileDropComponent : public juce::Component,
                          public juce::FileDragAndDropTarget {
public:
    FileDropComponent();

    std::function<void(const juce::File&)> onFile;

    void setStatus(const juce::String& text);

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    static bool looksLikeVideo(const juce::String& path);
    void deliver(const juce::File& f);

    bool highlight_ = false;
    juce::String status_;
    std::unique_ptr<juce::FileChooser> chooser_;
};
