#include "MainComponent.h"

MainComponent::MainComponent() {
    titleLabel_.setText("Voice Control", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    addAndMakeVisible(dropArea_);
    dropArea_.onFile = [this](const juce::File& f) { loadFile(f); };

    addAndMakeVisible(spectrumView_);

    autoEqButton_.setToggleState(true, juce::dontSendNotification);
    autoEqButton_.onClick = [this] { applyParamsLive(); updateEqView(); };
    addAndMakeVisible(autoEqButton_);

    presetCaption_.setText("Cleanup", juce::dontSendNotification);
    toneCaption_.setText("Tone", juce::dontSendNotification);
    strengthCaption_.setText("Strength", juce::dontSendNotification);
    addAndMakeVisible(presetCaption_);
    addAndMakeVisible(toneCaption_);
    addAndMakeVisible(strengthCaption_);

    presetBox_.addItem("Light", 1);
    presetBox_.addItem("Balanced", 2);
    presetBox_.addItem("Strong", 3);
    presetBox_.setSelectedId(2, juce::dontSendNotification);
    presetBox_.onChange = [this] { applyParamsLive(); updateEqView(); };
    addAndMakeVisible(presetBox_);

    toneBox_.addItem("Natural", 1);
    toneBox_.addItem("Warm", 2);
    toneBox_.addItem("Crisp", 3);
    toneBox_.setSelectedId(1, juce::dontSendNotification);
    toneBox_.onChange = [this] { applyParamsLive(); updateEqView(); };
    addAndMakeVisible(toneBox_);

    strengthSlider_.setRange(0.0, 100.0, 1.0);
    strengthSlider_.setValue(100.0, juce::dontSendNotification);
    strengthSlider_.setTextValueSuffix(" %");
    strengthSlider_.onValueChange = [this] { applyParamsLive(); };
    addAndMakeVisible(strengthSlider_);

    playButton_.onClick = [this] { togglePlay(); };
    addAndMakeVisible(playButton_);

    listenButton_.setClickingTogglesState(true);
    listenButton_.setToggleState(true, juce::dontSendNotification); // default: enhanced
    listenButton_.onClick = [this] { updateListenButton(); };
    addAndMakeVisible(listenButton_);
    updateListenButton();

    exportButton_.onClick = [this] { doExport(); };
    addAndMakeVisible(exportButton_);

    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setText("Drop a video or audio file to begin.", juce::dontSendNotification);
    addAndMakeVisible(statusLabel_);

    addAndMakeVisible(compMeter_);
    addAndMakeVisible(deEssMeter_);
    addAndMakeVisible(limiterMeter_);

    progressBar_.setVisible(false);
    addAndMakeVisible(progressBar_);

    playButton_.setEnabled(false);
    listenButton_.setEnabled(false);
    exportButton_.setEnabled(false);

    // Live-spectrum FFT buffers.
    const int fftSize = 1 << kFftOrder;
    fftData_.assign(static_cast<std::size_t>(2 * fftSize), 0.0f);
    analysisScratch_.assign(static_cast<std::size_t>(fftSize), 0.0f);
    liveDb_.assign(static_cast<std::size_t>(fftSize / 2), -120.0f);
    fftWindow_.resize(static_cast<std::size_t>(fftSize));
    for (int i = 0; i < fftSize; ++i)
        fftWindow_[static_cast<std::size_t>(i)] =
            0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * i / (fftSize - 1));
    liveResult_.fftSize = fftSize;
    liveResult_.binDb.assign(static_cast<std::size_t>(fftSize / 2), -120.0f);

    deviceManager_.initialiseWithDefaultDevices(0, 2);
    sourcePlayer_.setSource(&player_);
    deviceManager_.addAudioCallback(&sourcePlayer_);

    startTimerHz(30);
    setSize(600, 700);
}

MainComponent::~MainComponent() {
    stopTimer();
    deviceManager_.removeAudioCallback(&sourcePlayer_);
    sourcePlayer_.setSource(nullptr);
    player_.clearSources();
    if (worker_.joinable())
        worker_.join();
    if (loudnessWorker_.joinable())
        loudnessWorker_.join();
}

vc::Preset MainComponent::currentPreset() const {
    switch (presetBox_.getSelectedId()) {
        case 1: return vc::Preset::Light;
        case 3: return vc::Preset::Strong;
        default: return vc::Preset::Balanced;
    }
}

vc::Tone MainComponent::currentTone() const {
    switch (toneBox_.getSelectedId()) {
        case 2: return vc::Tone::Warm;
        case 3: return vc::Tone::Crisp;
        default: return vc::Tone::Natural;
    }
}

vc::ChainParams MainComponent::buildParams() const {
    auto p = vc::paramsForPreset(currentPreset());
    p.tone = currentTone();

    p.autoEqEnabled = autoEqButton_.getToggleState();
    p.autoEqBands = engine_.autoEqBands(); // computed at load; empty before that

    // Strength scales the dynamics depth live: at 0% the compressor ratio
    // collapses to 1:1 and the de-esser range to 0 (just EQ + loudness); at
    // 100% it's the full preset. Loudness targeting stays on regardless.
    const double s = strengthSlider_.getValue() / 100.0;
    p.compRatio = 1.0 + (p.compRatio - 1.0) * s;
    p.deEssRangeDb = p.deEssRangeDb * s;
    return p;
}

void MainComponent::updateEqView() {
    const auto p = buildParams();
    spectrumView_.setEq(vc::fullEqBands(p), p.highpassHz, engine_.sampleRate());
}

double MainComponent::currentDeviceRate() const {
    if (auto* dev = deviceManager_.getCurrentAudioDevice()) {
        const double r = dev->getCurrentSampleRate();
        if (r > 0.0) return r;
    }
    return 48000.0;
}

void MainComponent::updateLiveSpectrum() {
    const int fftSize = 1 << kFftOrder;
    player_.readAnalysisBlock(analysisScratch_.data(), fftSize);

    for (int i = 0; i < fftSize; ++i)
        fftData_[static_cast<std::size_t>(i)] =
            analysisScratch_[static_cast<std::size_t>(i)] * fftWindow_[static_cast<std::size_t>(i)];
    std::fill(fftData_.begin() + fftSize, fftData_.end(), 0.0f);

    fft_.performFrequencyOnlyForwardTransform(fftData_.data());

    const int half = fftSize / 2;
    for (int k = 0; k < half; ++k) {
        const float mag = fftData_[static_cast<std::size_t>(k)];
        const float db = 20.0f * std::log10(mag + 1e-9f);
        float& s = liveDb_[static_cast<std::size_t>(k)];
        s += (db > s ? 0.5f : 0.2f) * (db - s); // fast rise, slower fall
        liveResult_.binDb[static_cast<std::size_t>(k)] = s;
    }
    liveResult_.sampleRate = currentDeviceRate();
    liveResult_.valid = true;
    spectrumView_.setLiveSpectrum(liveResult_);
}

void MainComponent::applyParamsLive() {
    if (!engine_.hasAudio()) return;
    player_.setParams(buildParams());      // instant: sound changes now
    loudnessCountdown_ = 4;                 // ~200ms debounce, then refine loudness
}

void MainComponent::recomputeLoudnessAsync() {
    if (busy_.load()) { loudnessCountdown_ = 4; return; }   // defer during load/export
    if (loudnessBusy_.exchange(true)) { loudnessCountdown_ = 4; return; }
    if (loudnessWorker_.joinable())
        loudnessWorker_.join();

    const auto params = buildParams();
    juce::Component::SafePointer<MainComponent> safe(this);
    loudnessWorker_ = std::thread([this, safe, params]() mutable {
        const double ref = engine_.measureChainLoudness(params);
        juce::MessageManager::callAsync([safe, ref]() mutable {
            if (safe == nullptr) return;
            safe->player_.setInputLoudness(ref);
            safe->loudnessBusy_.store(false);
        });
    });
}

void MainComponent::runOnWorker(const juce::String& busyMsg,
                                std::function<juce::String()> job,
                                std::function<void()> onSuccess) {
    if (busy_.exchange(true)) return;
    if (worker_.joinable())
        worker_.join();

    setUiBusy(true, busyMsg);

    juce::Component::SafePointer<MainComponent> safe(this);
    worker_ = std::thread([this, safe, job, onSuccess]() mutable {
        const juce::String error = job();
        juce::MessageManager::callAsync([safe, error, onSuccess]() mutable {
            if (safe == nullptr) return;
            safe->busy_.store(false);
            if (error.isEmpty()) {
                if (onSuccess) onSuccess();
            } else {
                safe->setUiBusy(false, "Error: " + error);
            }
        });
    });
}

void MainComponent::loadFile(const juce::File& file) {
    player_.stop();
    const auto params = buildParams();

    runOnWorker(
        "Loading + analysing audio...",
        [this, file, params]() -> juce::String {
            juce::String err;
            if (!engine_.loadMedia(file, err))
                return err.isEmpty() ? "could not read audio from file" : err;
            auto measuredParams = params;
            measuredParams.autoEqBands = engine_.autoEqBands();
            initialLoudnessRef_ = engine_.measureChainLoudness(measuredParams);
            return {};
        },
        [this, file]() {
            player_.setDrySource(&engine_.beforeBuffer(), engine_.sampleRate());
            player_.setInputLoudness(initialLoudnessRef_);
            player_.setParams(buildParams());

            playButton_.setEnabled(true);
            listenButton_.setEnabled(true);
            listenButton_.setToggleState(true, juce::dontSendNotification);
            updateListenButton();
            exportButton_.setEnabled(true);
            exportButton_.setButtonText(engine_.sourceHasVideo() ? "Export video..."
                                                                 : "Export audio...");
            dropArea_.setStatus(file.getFileName());
            spectrumView_.setSpectrum(engine_.spectrum());
            updateEqView();

            const double target = buildParams().targetLufs;
            setUiBusy(false, juce::String::formatted(
                "Loaded \"%s\"  -  input %.1f LUFS, target %.0f LUFS. Press Play, then use the Hearing button.",
                file.getFileName().toRawUTF8(), engine_.inputLufs(), target));
        });
}

void MainComponent::doExport() {
    if (!engine_.hasAudio()) return;

    auto src = engine_.sourceFile();
    const bool hasVideo = engine_.sourceHasVideo();
    const juce::String outExt = hasVideo ? ".mp4" : ".wav";
    const juce::String filter = hasVideo ? "*.mp4" : "*.wav;*.mp3;*.m4a;*.flac;*.aiff";
    auto suggested = src.getParentDirectory()
                         .getChildFile(src.getFileNameWithoutExtension() + "_enhanced" + outExt);

    exportChooser_ = std::make_unique<juce::FileChooser>(
        hasVideo ? "Export enhanced video" : "Export enhanced audio", suggested, filter);
    exportChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            auto out = fc.getResult();
            if (out == juce::File()) return;

            const auto params = buildParams();
            runOnWorker(
                "Exporting (exact offline render)...",
                [this, out, params]() -> juce::String {
                    juce::String err;
                    if (!engine_.exportTo(out, params, err))
                        return err.isEmpty() ? "export failed" : err;
                    return {};
                },
                [this, out]() { setUiBusy(false, "Exported: " + out.getFullPathName()); });
        });
}

void MainComponent::togglePlay() {
    if (player_.isPlaying()) {
        player_.stop();
        playButton_.setButtonText("Play");
    } else {
        player_.start();
        playButton_.setButtonText("Stop");
    }
}

void MainComponent::updateListenButton() {
    const bool enhanced = listenButton_.getToggleState();
    player_.setShowAfter(enhanced);
    listenButton_.setButtonText(enhanced ? "Hearing: ENHANCED  (click for Original)"
                                         : "Hearing: ORIGINAL  (click for Enhanced)");
    const auto col = enhanced ? juce::Colour(0xff2e7d32) : juce::Colour(0xff555b66);
    listenButton_.setColour(juce::TextButton::buttonColourId, col);
    listenButton_.setColour(juce::TextButton::buttonOnColourId, col);
}

void MainComponent::setUiBusy(bool busy, const juce::String& message) {
    progressBar_.setVisible(busy);
    progress_ = busy ? -1.0 : 0.0; // negative -> indeterminate animation

    dropArea_.setEnabled(!busy);
    const bool haveAudio = engine_.hasAudio();
    presetBox_.setEnabled(!busy);
    toneBox_.setEnabled(!busy);
    autoEqButton_.setEnabled(!busy);
    strengthSlider_.setEnabled(!busy);
    playButton_.setEnabled(!busy && haveAudio);
    listenButton_.setEnabled(!busy && haveAudio);
    exportButton_.setEnabled(!busy && haveAudio);

    statusLabel_.setText(message, juce::dontSendNotification);
}

void MainComponent::timerCallback() {
    // Debounced loudness refinement after a param change.
    if (loudnessCountdown_ > 0 && --loudnessCountdown_ == 0)
        recomputeLoudnessAsync();

    const bool playing = player_.isPlaying();
    playButton_.setButtonText(playing ? "Stop" : "Play");

    // Meters reflect the live chain while playing; ease back to 0 when stopped.
    compMeter_.setReduction(playing ? player_.compReductionDb() : 0.0f);
    deEssMeter_.setReduction(playing ? player_.deEssReductionDb() : 0.0f);
    limiterMeter_.setReduction(playing ? player_.limiterReductionDb() : 0.0f);

    // Animate the spectrum to the playing audio; revert to the average when idle.
    if (playing)
        updateLiveSpectrum();
    else
        spectrumView_.setShowLive(false);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff20232a));
}

void MainComponent::resized() {
    auto r = getLocalBounds().reduced(16);

    titleLabel_.setBounds(r.removeFromTop(30));
    r.removeFromTop(6);

    dropArea_.setBounds(r.removeFromTop(78));
    r.removeFromTop(10);

    spectrumView_.setBounds(r.removeFromTop(150));
    r.removeFromTop(6);

    autoEqButton_.setBounds(r.removeFromTop(24));
    r.removeFromTop(6);

    auto controls = r.removeFromTop(54);
    auto left = controls.removeFromLeft(controls.getWidth() / 2).reduced(4);
    presetCaption_.setBounds(left.removeFromTop(18));
    presetBox_.setBounds(left);
    auto right = controls.reduced(4);
    toneCaption_.setBounds(right.removeFromTop(18));
    toneBox_.setBounds(right);
    r.removeFromTop(8);

    auto strengthRow = r.removeFromTop(44);
    strengthCaption_.setBounds(strengthRow.removeFromLeft(70));
    strengthSlider_.setBounds(strengthRow);
    r.removeFromTop(8);

    auto transport = r.removeFromTop(34);
    playButton_.setBounds(transport.removeFromLeft(90).reduced(2));
    listenButton_.setBounds(transport.reduced(2));
    r.removeFromTop(10);

    // Live gain-reduction meters.
    compMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    deEssMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    limiterMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(12);

    exportButton_.setBounds(r.removeFromTop(38).reduced(2));
    r.removeFromTop(8);

    progressBar_.setBounds(r.removeFromTop(18));
    r.removeFromTop(4);
    statusLabel_.setBounds(r.removeFromTop(40));
}
