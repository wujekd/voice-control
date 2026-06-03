#include <juce_gui_extra/juce_gui_extra.h>

#include "MainComponent.h"

// Application entry point and main window. Standard JUCE boilerplate; all the
// product logic lives in MainComponent and the engine.
class VoiceControlApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Voice Control"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow_ = nullptr; }

    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Colours::black,
                             DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(VoiceControlApplication)
