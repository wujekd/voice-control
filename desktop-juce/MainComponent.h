#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "FileDropComponent.h"
#include "GrMeter.h"
#include "MusicTimeline.h"
#include "PreviewPlayer.h"
#include "ProcessingEngine.h"
#include "Presets.h" // vc::Preset, vc::Tone, vc::ChainParams
#include "SpectrumView.h"

#include <atomic>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

// Top-level UI. Preview is live: tone / intensity changes are pushed
// straight to the running chain (no re-render), the A/B toggle switches dry vs.
// processed instantly, and gain-reduction meters read the live chain. Only the
// slow jobs (load/extract, export) run on a worker thread. Export still uses
// the exact offline path in ProcessingEngine.
class MainComponent : public juce::Component,
                      private juce::MenuBarModel,
                      private juce::Timer,
                      private juce::ChangeListener {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    enum MenuItemIds {
        toggleSettingsMenuId = 1,
        toggleProMenuId = 2,
    };

    struct MusicUndoState;

    class EncoderLookAndFeel : public juce::LookAndFeel_V4 {
    public:
        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPosProportional, float rotaryStartAngle,
                              float rotaryEndAngle, juce::Slider& slider) override;
    };

    void loadFile(const juce::File& file);
    void applyParamsLive();
    void doExport();
    void addMusicClip(double startSeconds = 0.0);
    double nextMusicClipStartSeconds() const;
    void removeSelectedMusicClip();
    void refreshMusicClipList();
    void syncMusicControlsFromSelection();
    void applySelectedMusicClipControls();
    void applySelectedMusicClipVolume();
    void pushMusicUndoState();
    void beginMusicUndoGesture();
    void endMusicUndoGesture();
    bool musicClipStateMatches(const MusicUndoState& state) const;
    void undoMusicTimelineEdit();
    void updateMusicTimeline();
    void togglePlay();
    void updateListenButton();
    void updateEqView();
    void updateLiveSpectrum();
    void requestProcessedSpectrumUpdate();
    void startProcessedSpectrumUpdateIfNeeded();
    void setSettingsPanelVisible(bool visible);
    void setProPanelVisible(bool visible);
    void updateMainMenu();
    double currentDeviceRate() const;
    double currentIntensity() const;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refreshOutputDeviceList();
    juce::String systemDefaultOutputName() const;
    void setOutputDevice(const juce::String& name);

    void runOnWorker(const juce::String& busyMsg,
                     std::function<juce::String()> job,
                     std::function<void()> onSuccess);
    void setUiBusy(bool busy, const juce::String& message);
    double currentToneAmount() const;
    vc::ChainParams buildParams() const;
    void resetProDefaults();

    void timerCallback() override;

    juce::AudioDeviceManager deviceManager_;
    juce::AudioSourcePlayer sourcePlayer_;
    PreviewPlayer player_;
    ProcessingEngine engine_;

    FileDropComponent dropArea_;
    SpectrumView spectrumView_;
    juce::GroupComponent settingsPanel_ { "settingsPanel", "Audio Output" };
    juce::ToggleButton followSystemButton_ { "Follow system output" };
    juce::Label outputDeviceLabel_;
    juce::ComboBox outputDeviceBox_;
    bool settingsPanelVisible_ = false;
    bool followSystemDefault_ = true;
    bool updatingDevice_ = false; // re-entrancy guard for device changes
    EncoderLookAndFeel encoderLookAndFeel_;
    juce::Label toneCaption_, noiseReductionCaption_, strengthCaption_;
    juce::Slider toneSlider_;
    juce::Slider noiseReductionSlider_;
    juce::Slider strengthSlider_;
    juce::GroupComponent proPanel_ { "proPanel", "Pro" };
    juce::Label fastThresholdLabel_, fastRatioLabel_, glueThresholdLabel_, glueRatioLabel_, targetPreChainLabel_;
    juce::Slider fastThresholdSlider_, fastRatioSlider_, glueThresholdSlider_, glueRatioSlider_, targetPreChainSlider_;
    juce::Label deEssFreqLabel_, deEssThresholdLabel_, deEssPresenceLabel_, deEssRatioLabel_, deEssRangeLabel_;
    juce::Slider deEssFreqSlider_, deEssThresholdSlider_, deEssPresenceSlider_, deEssRatioSlider_, deEssRangeSlider_;
    juce::TextButton resetProButton_ { "Reset" };
    juce::TextButton playButton_ { "Play" };
    juce::TextButton listenButton_; // toggles Original vs Enhanced output
    juce::TextButton addMusicButton_ { "Add music..." };
    juce::TextButton removeMusicButton_ { "Remove" };
    juce::ComboBox musicClipBox_;
    juce::Label musicCaption_, musicStartLabel_, musicVolumeLabel_, musicFadeInLabel_, musicFadeOutLabel_;
    juce::Slider musicStartSlider_, musicVolumeSlider_, musicFadeInSlider_, musicFadeOutSlider_;
    MusicTimeline musicTimeline_;
    bool musicClipDragActive_ = false;
    bool musicUndoGestureActive_ = false;
    bool applyingMusicUndo_ = false;
    struct MusicUndoState {
        std::vector<MusicClip> clips;
        int selectedIndex = -1;
    };
    std::vector<MusicUndoState> musicUndoStack_;
    // Per-clip (fadeIn, fadeOut) captured at drag start, so overlap crossfades
    // can be reset when clips are pulled apart again.
    std::vector<std::pair<double, double>> musicClipFadeSnapshot_;
    void applyMusicClipOverlapCrossfades(int draggedIndex);
    bool analyzingMedia_ = false;
    juce::TextButton exportButton_ { "Export video..." };
    juce::Label statusLabel_;

    bool proPanelVisible_ = false;

    GrMeter fastCompMeter_ { "Peak Comp", 10.0f, juce::Colour(0xff66d9ef) };
    GrMeter glueCompMeter_ { "Glue Comp", 10.0f, juce::Colour(0xffa6e22e) };
    GrMeter deEssMeter_ { "De-ess", 10.0f, juce::Colour(0xffffc14d) };
    GrMeter limiterMeter_ { "Limiter", 10.0f, juce::Colour(0xffff5d5d) };
    VuMeter vuMeter_ { "VU" };

    double progress_ = 0.0;
    juce::ProgressBar progressBar_ { progress_ };

    std::thread worker_;
    std::thread spectrumWorker_;
    std::atomic<bool> busy_ { false };
    std::atomic<bool> spectrumWorkerRunning_ { false };
    std::atomic<int> processedSpectrumRequest_ { 0 };
    std::atomic<int> processedSpectrumRendered_ { 0 };
    std::unique_ptr<juce::FileChooser> exportChooser_;
    std::unique_ptr<juce::FileChooser> musicChooser_;

    // Live spectrum FFT (message-thread side; fed by PreviewPlayer's ring).
    static constexpr int kFftOrder = 11; // 2048-point
    juce::dsp::FFT fft_ { kFftOrder };
    std::vector<float> fftData_;     // 2 * fftSize
    std::vector<float> fftWindow_;   // fftSize Hann
    std::vector<float> analysisScratch_; // fftSize
    std::vector<float> liveDb_;      // fftSize/2, smoothed
    vc::SpectrumResult liveResult_;

    // Measured once during file analysis and interpolated for live preview.
    double intensityMinLoudnessRef_ = 0.0;
    double intensityMaxLoudnessRef_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
