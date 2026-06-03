#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "FileDropComponent.h"
#include "GrMeter.h"
#include "PreviewPlayer.h"
#include "ProcessingEngine.h"
#include "Presets.h" // vc::Preset, vc::Tone, vc::ChainParams
#include "SpectrumView.h"

#include <atomic>
#include <functional>
#include <thread>

// Top-level UI. Preview is live: preset / tone / strength changes are pushed
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

private:
    void loadFile(const juce::File& file);
    void applyParamsLive();
    void recomputeLoudnessAsync();
    void doExport();
    void togglePlay();
    void updateListenButton();
    void updateEqView();
    void updateLiveSpectrum();
    double currentDeviceRate() const;

    void runOnWorker(const juce::String& busyMsg,
                     std::function<juce::String()> job,
                     std::function<void()> onSuccess);
    void setUiBusy(bool busy, const juce::String& message);
    vc::Preset currentPreset() const;
    vc::Tone currentTone() const;
    vc::ChainParams buildParams() const;

    void timerCallback() override;

    juce::AudioDeviceManager deviceManager_;
    juce::AudioSourcePlayer sourcePlayer_;
    PreviewPlayer player_;
    ProcessingEngine engine_;

    FileDropComponent dropArea_;
    SpectrumView spectrumView_;
    juce::Label titleLabel_;
    juce::Label presetCaption_, toneCaption_, strengthCaption_;
    juce::ComboBox presetBox_, toneBox_;
    juce::ToggleButton autoEqButton_ { "Auto-EQ (spectral)" };
    juce::Slider strengthSlider_;
    juce::TextButton playButton_ { "Play" };
    juce::TextButton listenButton_; // toggles Original vs Enhanced output
    juce::TextButton exportButton_ { "Export video..." };
    juce::Label statusLabel_;

    GrMeter compMeter_ { "Comp", 18.0f, juce::Colour(0xff4da6ff) };
    GrMeter deEssMeter_ { "De-ess", 18.0f, juce::Colour(0xffffc14d) };
    GrMeter limiterMeter_ { "Limiter", 6.0f, juce::Colour(0xffff5d5d) };

    double progress_ = 0.0;
    juce::ProgressBar progressBar_ { progress_ };

    std::thread worker_;
    std::atomic<bool> busy_ { false };
    std::unique_ptr<juce::FileChooser> exportChooser_;

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
