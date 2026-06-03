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

// Top-level UI. Preview is live: tone / intensity changes are pushed
// straight to the running chain (no re-render), the A/B toggle switches dry vs.
// processed instantly, and gain-reduction meters read the live chain. Only the
// slow jobs (load/extract, export) run on a worker thread. Export still uses
// the exact offline path in ProcessingEngine.
class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void loadFile(const juce::File& file);
    void applyParamsLive();
    void recomputeLoudnessAsync();
    void doExport();
    void addMusicClip(double startSeconds = 0.0);
    void removeSelectedMusicClip();
    void refreshMusicClipList();
    void syncMusicControlsFromSelection();
    void applySelectedMusicClipControls();
    void updateMusicTimeline();
    void togglePlay();
    void updateListenButton();
    void updateEqView();
    void updateLiveSpectrum();
    double currentDeviceRate() const;

    void runOnWorker(const juce::String& busyMsg,
                     std::function<juce::String()> job,
                     std::function<void()> onSuccess);
    void setUiBusy(bool busy, const juce::String& message);
    vc::Tone currentTone() const;
    vc::ChainParams buildParams() const;
    void resetProDefaults();

    void timerCallback() override;

    juce::AudioDeviceManager deviceManager_;
    juce::AudioSourcePlayer sourcePlayer_;
    PreviewPlayer player_;
    ProcessingEngine engine_;

    FileDropComponent dropArea_;
    SpectrumView spectrumView_;
    juce::Label titleLabel_;
    juce::Label toneCaption_, noiseReductionCaption_, strengthCaption_;
    juce::ComboBox toneBox_;
    juce::ToggleButton autoEqButton_ { "Auto-EQ (spectral)" };
    juce::Slider noiseReductionSlider_;
    juce::Slider strengthSlider_;
    juce::TextButton proButton_ { "Pro" };
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
    juce::TextButton exportButton_ { "Export video..." };
    juce::Label statusLabel_;

    bool proPanelVisible_ = false;

    GrMeter fastCompMeter_ { "Peak Comp", 18.0f, juce::Colour(0xff66d9ef) };
    GrMeter glueCompMeter_ { "Glue Comp", 18.0f, juce::Colour(0xffa6e22e) };
    GrMeter deEssMeter_ { "De-ess", 18.0f, juce::Colour(0xffffc14d) };
    GrMeter limiterMeter_ { "Limiter", 6.0f, juce::Colour(0xffff5d5d) };
    VuMeter vuMeter_ { "VU" };

    double progress_ = 0.0;
    juce::ProgressBar progressBar_ { progress_ };

    std::thread worker_;
    std::atomic<bool> busy_ { false };
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

    // Debounced, off-thread recompute of the live loudness reference.
    std::thread loudnessWorker_;
    std::atomic<bool> loudnessBusy_ { false };
    int loudnessCountdown_ = 0; // timer ticks remaining before recompute
    double initialLoudnessRef_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
