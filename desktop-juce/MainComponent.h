#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "DuckView.h"
#include "ExportComponent.h"
#include "FileDropComponent.h"
#include "GrMeter.h"
#include "MusicTimeline.h"
#include "PreviewPlayer.h"
#include "ProcessingEngine.h"
#include "ProjectManagerComponent.h"
#include "Presets.h" // vc::Preset, vc::Tone, vc::ChainParams
#include "SettingsComponent.h"
#include "SpectrumView.h"

#include <array>
#include <atomic>
#include <functional>
#include <thread>
#include <unordered_set>
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
    void parentHierarchyChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    // Lock the host window's height (non-resizable vertically) and floor its
    // width so controls never vanish.
    void applyWindowConstraints();
    enum MenuItemIds {
        newProjectMenuId = 1,
        openProjectManagerMenuId = 2,
        saveProjectMenuId = 3,
        openSettingsMenuId = 20,
    };

    struct MusicUndoState;
    struct PendingMusicClipRestore;

    class EncoderLookAndFeel : public juce::LookAndFeel_V4 {
    public:
        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPosProportional, float rotaryStartAngle,
                              float rotaryEndAngle, juce::Slider& slider) override;
        void markCompact(juce::Slider& slider) { compactSliders_.insert(&slider); }
        juce::Font getSliderPopupFont(juce::Slider& slider) override;

    private:
        std::unordered_set<juce::Slider*> compactSliders_;
    };

    void loadFile(const juce::File& file);
    void applyParamsLive();
    void doExport();
    void performExport(juce::File out, bool muxVideo);
    void addMusicClip(double startSeconds = 0.0);
    double nextMusicClipStartSeconds() const;
    void removeSelectedMusicClip();
    void refreshMusicClipList();
    void syncMusicControlsFromSelection();
    void applySelectedMusicClipControls();
    void applySelectedMusicClipVolume();
    void applyMusicMasterVolume();
    void pushMusicUndoState();
    void beginMusicUndoGesture();
    void endMusicUndoGesture();
    bool musicClipStateMatches(const MusicUndoState& state) const;
    void undoMusicTimelineEdit();
    void updateMusicTimeline();
    void togglePlay();
    void updateListenButton();
    void updateDuckingUi();
    void updateMusicMuteUi();
    void updateEqView();
    void updateLiveSpectrum();
    void updateMusicSpectrum();
    void requestProcessedSpectrumUpdate();
    void startProcessedSpectrumUpdateIfNeeded();
    void newProject();
    void openProjectManager();
    void closeProjectManager();
    void openProjectFile(const juce::File& file);
    void deleteProjectFile(const juce::File& file);
    void saveCurrentProject();
    void saveAsNewProject(std::function<void(bool saved)> onComplete = {});
    void promptForProjectName(const juce::String& title, const juce::String& defaultName,
                              std::function<void(juce::String)> onName,
                              std::function<void()> onCancel = {});
    void maybeSaveBeforeReplacingSession(std::function<void(bool proceed)> onDone);
    void clearProject();
    void markProjectDirty();
    bool hasProjectContent() const;
    void saveAutosaveSession();
    void saveAnalysisFileForCurrentMedia();
    void restoreAutosaveSession();
    juce::var makeProjectState() const;
    bool applyProjectState(const juce::var& state, bool fromAutosave);
    void restorePendingMusicClipsAfterVoiceLoad(const juce::File& voiceFile, const juce::String& loadedMessage);
    void openSettings();
    void closeSettings();
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
    // Audio-output controls. They live on MainComponent (so device handling and
    // project state stay here) but are re-parented into the settings window's
    // "Audio" tab while it is open.
    juce::ToggleButton followSystemButton_ { "Follow system output" };
    juce::Label outputDeviceLabel_;
    juce::ComboBox outputDeviceBox_;
    std::unique_ptr<juce::DialogWindow> projectManagerWindow_;
    std::unique_ptr<juce::DialogWindow> settingsWindow_;
    bool followSystemDefault_ = true;
    bool updatingDevice_ = false; // re-entrancy guard for device changes
    EncoderLookAndFeel encoderLookAndFeel_;
    juce::Label toneCaption_, noiseReductionCaption_, strengthCaption_;
    juce::Slider toneSlider_;
    juce::Slider noiseReductionSlider_;
    juce::Slider strengthSlider_;
    // Pro controls: re-parented into the settings window's "Pro" tab while open.
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
    juce::TextButton musicMuteButton_ { "Mute" }; // mute the backing-music channel
    juce::Label musicCaption_, musicStartLabel_, musicMasterVolumeLabel_, musicVolumeLabel_, musicFadeInLabel_, musicFadeOutLabel_;
    juce::Slider musicStartSlider_, musicMasterVolumeSlider_, musicVolumeSlider_, musicFadeInSlider_, musicFadeOutSlider_;
    // Background ducking: voice sidechains the backing music. Look-ahead +
    // reduction behave like a sidechain compressor; "Mid focus" blends from
    // full-band ducking toward mid-band-only (dynamic-EQ-style) ducking.
    juce::TextButton duckOnOffButton_ { "Off" };
    juce::Label duckCaption_, duckLookAheadLabel_, duckReductionLabel_, duckFilterLabel_;
    juce::Slider duckLookAheadSlider_, duckReductionSlider_, duckFilterSlider_;
    DuckView duckView_;
    MusicTimeline musicTimeline_;
    bool musicClipDragActive_ = false;
    bool musicUndoGestureActive_ = false;
    bool applyingMusicUndo_ = false;
    struct PendingMusicClipRestore {
        juce::File file;
        double startSeconds = 0.0;
        double sourceOffsetSeconds = 0.0;
        double lengthSeconds = 0.0;
        double gainDb = -18.0;
        double fadeInSeconds = 1.0;
        double fadeOutSeconds = 1.0;
        std::vector<float> waveformPeaks;
        int waveformProcessedColumns = 0;
    };
    struct MusicUndoState {
        std::vector<MusicClip> clips;
        int selectedIndex = -1;
    };
    std::vector<MusicUndoState> musicUndoStack_;
    std::vector<PendingMusicClipRestore> pendingMusicClipRestore_;
    int pendingSelectedMusicClipRestore_ = -1;
    juce::var pendingProjectAnalysisCache_;
    // Per-clip (fadeIn, fadeOut) captured at drag start, so overlap crossfades
    // can be reset when clips are pulled apart again.
    std::vector<std::pair<double, double>> musicClipFadeSnapshot_;
    void applyMusicClipOverlapCrossfades(int draggedIndex);
    bool analyzingMedia_ = false;
    juce::TextButton exportButton_ { "Export" };
    juce::Label statusLabel_;

    CompMeter compMeter_ { "Comp", 12.0f, juce::Colour(0xff5aa0ff), juce::Colour(0xff5aa0ff) };
    GrMeter deEssMeter_ { "De-ess", 10.0f, juce::Colour(0xffffc14d) };
    GrMeter limiterMeter_ { "Limiter", 5.0f, juce::Colour(0xffff5d5d) };
    VuMeter vuMeter_ { "Output" };

    double progress_ = 0.0;
    juce::ProgressBar progressBar_ { progress_ };

    std::thread worker_;
    std::thread spectrumWorker_;
    std::atomic<bool> busy_ { false };
    std::atomic<bool> spectrumWorkerRunning_ { false };
    std::atomic<int> processedSpectrumRequest_ { 0 };
    std::atomic<int> processedSpectrumRendered_ { 0 };
    std::unique_ptr<juce::FileChooser> musicChooser_;
    juce::File currentProjectFile_;
    bool projectDirty_ = false;
    bool restoringProject_ = false;
    int autosaveCountdown_ = 0;

    // Live spectrum FFT (message-thread side; fed by PreviewPlayer's ring).
    static constexpr int kFftOrder = 11; // 2048-point
    juce::dsp::FFT fft_ { kFftOrder };
    std::vector<float> fftData_;     // 2 * fftSize
    std::vector<float> fftWindow_;   // fftSize Hann
    std::vector<float> analysisScratch_; // fftSize
    std::vector<float> liveDb_;      // fftSize/2, smoothed
    vc::SpectrumResult liveResult_;
    std::vector<float> musicDb_;     // fftSize/2, smoothed (backing music)
    vc::SpectrumResult musicResult_;

    // Measured once during file analysis and interpolated for live preview.
    double intensityMinLoudnessRef_ = 0.0;
    double intensityMaxLoudnessRef_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
