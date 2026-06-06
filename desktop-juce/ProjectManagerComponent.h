#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <utility>

// Project library browser. Lists the .vcproj files in the managed projects
// folder and lets the user create, open, or delete projects without ever
// touching a file dialog. All actions are reported through callbacks so
// MainComponent stays the single owner of project state; call refresh() after
// the owner mutates the folder to re-read it.
class ProjectManagerComponent : public juce::Component,
                                private juce::ListBoxModel {
public:
    std::function<void()> onNew;
    std::function<void(juce::File)> onOpen;
    std::function<void(juce::File)> onDelete;
    std::function<void()> onClose;

    ProjectManagerComponent(juce::File folder, juce::File current)
        : folder_(std::move(folder)), current_(std::move(current)) {
        list_.setModel(this);
        list_.setRowHeight(30);
        list_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff14161b));
        addAndMakeVisible(list_);

        newButton_.onClick = [this] { if (onNew) onNew(); };
        openButton_.onClick = [this] { openSelected(); };
        deleteButton_.onClick = [this] { deleteSelected(); };
        closeButton_.onClick = [this] { if (onClose) onClose(); };
        for (auto* b : { &newButton_, &openButton_, &deleteButton_, &closeButton_ }) {
            b->setWantsKeyboardFocus(false);
            addAndMakeVisible(b);
        }

        refresh();
        setSize(440, 360);
    }

    // Re-read the folder; mark which project is currently open for highlighting.
    void setCurrent(juce::File current) { current_ = std::move(current); refresh(); }

    void refresh() {
        files_ = folder_.findChildFiles(juce::File::findFiles, false, "*.vcproj");
        files_.sort();
        list_.updateContent();
        list_.repaint();
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff20232a)); }

    void resized() override {
        auto r = getLocalBounds().reduced(12);
        auto buttons = r.removeFromBottom(34);
        r.removeFromBottom(8);
        list_.setBounds(r);
        const int n = 4;
        const int gap = 6;
        const int bw = (buttons.getWidth() - gap * (n - 1)) / n;
        for (auto* b : { &newButton_, &openButton_, &deleteButton_, &closeButton_ }) {
            b->setBounds(buttons.removeFromLeft(bw));
            buttons.removeFromLeft(gap);
        }
    }

private:
    int getNumRows() override { return files_.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                          bool selected) override {
        if (row < 0 || row >= files_.size())
            return;
        if (selected) {
            g.setColour(juce::Colour(0xff2f7d52));
            g.fillRect(0, 0, width, height);
        }
        const auto f = files_[row];
        const bool isCurrent = f == current_;
        g.setColour(juce::Colours::white.withAlpha(isCurrent ? 1.0f : 0.85f));
        g.setFont(juce::Font(juce::FontOptions(14.0f,
                                               isCurrent ? juce::Font::bold : juce::Font::plain)));
        auto name = f.getFileNameWithoutExtension();
        if (isCurrent)
            name += "   (open)";
        g.drawText(name, 12, 0, width - 24, height, juce::Justification::centredLeft);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override {
        if (row >= 0 && row < files_.size() && onOpen)
            onOpen(files_[row]);
    }

    void openSelected() {
        const int row = list_.getSelectedRow();
        if (row >= 0 && row < files_.size() && onOpen)
            onOpen(files_[row]);
    }

    void deleteSelected() {
        const int row = list_.getSelectedRow();
        if (row < 0 || row >= files_.size())
            return;
        const auto f = files_[row];
        juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon, "Delete project?",
            "Delete \"" + f.getFileNameWithoutExtension() + "\"? This cannot be undone.",
            "Delete", "Cancel", this,
            juce::ModalCallbackFunction::create([this, f](int result) {
                if (result == 1 && onDelete)
                    onDelete(f);
            }));
    }

    juce::File folder_;
    juce::File current_;
    juce::Array<juce::File> files_;
    juce::ListBox list_;
    juce::TextButton newButton_ { "New" };
    juce::TextButton openButton_ { "Open" };
    juce::TextButton deleteButton_ { "Delete" };
    juce::TextButton closeButton_ { "Close" };
};
