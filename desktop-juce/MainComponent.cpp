#include "MainComponent.h"

MainComponent::MainComponent() {
    setWantsKeyboardFocus(true);

    titleLabel_.setText("Voice Control", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    addAndMakeVisible(dropArea_);
    dropArea_.onFile = [this](const juce::File& f) { loadFile(f); };

    addAndMakeVisible(spectrumView_);

    autoEqButton_.setToggleState(true, juce::dontSendNotification);
    autoEqButton_.onClick = [this] { applyParamsLive(); updateEqView(); };
    addAndMakeVisible(autoEqButton_);

    toneCaption_.setText("Tone", juce::dontSendNotification);
    noiseReductionCaption_.setText("Noise Reduction", juce::dontSendNotification);
    strengthCaption_.setText("Intensity", juce::dontSendNotification);
    addAndMakeVisible(toneCaption_);
    addAndMakeVisible(noiseReductionCaption_);
    addAndMakeVisible(strengthCaption_);

    toneBox_.addItem("Natural", 1);
    toneBox_.addItem("Warm", 2);
    toneBox_.addItem("Crisp", 3);
    toneBox_.setSelectedId(1, juce::dontSendNotification);
    toneBox_.onChange = [this] { applyParamsLive(); updateEqView(); };
    addAndMakeVisible(toneBox_);

    noiseReductionSlider_.setRange(0.0, 100.0, 1.0);
    noiseReductionSlider_.setValue(100.0, juce::dontSendNotification);
    noiseReductionSlider_.setTextValueSuffix(" %");
    noiseReductionSlider_.onValueChange = [this] { applyParamsLive(); };
    addAndMakeVisible(noiseReductionSlider_);

    strengthSlider_.setRange(0.0, 100.0, 1.0);
    strengthSlider_.setValue(100.0, juce::dontSendNotification);
    strengthSlider_.setTextValueSuffix(" %");
    strengthSlider_.onValueChange = [this] { applyParamsLive(); };
    addAndMakeVisible(strengthSlider_);

    auto configureProSlider = [this](juce::Slider& slider, double min, double max,
                                     double step, double value, const juce::String& suffix) {
        slider.setRange(min, max, step);
        slider.setValue(value, juce::dontSendNotification);
        slider.setTextValueSuffix(suffix);
        slider.onValueChange = [this] { applyParamsLive(); };
        addChildComponent(slider);
    };

    fastThresholdLabel_.setText("Peak Threshold", juce::dontSendNotification);
    fastRatioLabel_.setText("Peak Ratio", juce::dontSendNotification);
    glueThresholdLabel_.setText("Glue Threshold", juce::dontSendNotification);
    glueRatioLabel_.setText("Glue Ratio", juce::dontSendNotification);
    targetPreChainLabel_.setText("Work Level", juce::dontSendNotification);
    deEssFreqLabel_.setText("De-ess Freq", juce::dontSendNotification);
    deEssThresholdLabel_.setText("De-ess Threshold", juce::dontSendNotification);
    deEssPresenceLabel_.setText("De-ess Presence", juce::dontSendNotification);
    deEssRatioLabel_.setText("De-ess Ratio", juce::dontSendNotification);
    deEssRangeLabel_.setText("De-ess Range", juce::dontSendNotification);
    for (auto* label : { &fastThresholdLabel_, &fastRatioLabel_, &glueThresholdLabel_,
                         &glueRatioLabel_, &targetPreChainLabel_, &deEssFreqLabel_,
                         &deEssThresholdLabel_, &deEssPresenceLabel_, &deEssRatioLabel_,
                         &deEssRangeLabel_ })
        addChildComponent(label);

    const auto defaults = vc::fixedVoiceCleanupParams();
    configureProSlider(fastThresholdSlider_, -36.0, -6.0, 0.5, defaults.fastCompThresholdDb, " dB");
    configureProSlider(fastRatioSlider_, 2.0, 20.0, 0.5, defaults.fastCompRatio, ":1");
    configureProSlider(glueThresholdSlider_, -36.0, -6.0, 0.5, defaults.glueCompThresholdDb, " dB");
    configureProSlider(glueRatioSlider_, 1.0, 8.0, 0.1, defaults.glueCompRatio, ":1");
    configureProSlider(targetPreChainSlider_, -32.0, -18.0, 0.5, defaults.targetPreChainLufs, " LUFS");
    configureProSlider(deEssFreqSlider_, 3500.0, 9000.0, 100.0, defaults.deEssFreqHz, " Hz");
    configureProSlider(deEssThresholdSlider_, -60.0, -24.0, 0.5, defaults.deEssThresholdDb, " dB");
    configureProSlider(deEssPresenceSlider_, -36.0, -6.0, 0.5, defaults.deEssPresenceThresholdDb, " dB");
    configureProSlider(deEssRatioSlider_, 1.0, 12.0, 0.1, defaults.deEssRatio, ":1");
    configureProSlider(deEssRangeSlider_, 0.0, 18.0, 0.5, defaults.deEssRangeDb, " dB");

    resetProButton_.onClick = [this] { resetProDefaults(); };
    resetProButton_.setWantsKeyboardFocus(false);
    addChildComponent(resetProButton_);

    proPanel_.setVisible(false);
    addChildComponent(proPanel_);
    proButton_.onClick = [this] {
        proPanelVisible_ = !proPanelVisible_;
        proPanel_.setVisible(proPanelVisible_);
        for (auto* c : { static_cast<juce::Component*>(&fastThresholdLabel_),
                         static_cast<juce::Component*>(&fastThresholdSlider_),
                         static_cast<juce::Component*>(&fastRatioLabel_),
                         static_cast<juce::Component*>(&fastRatioSlider_),
                         static_cast<juce::Component*>(&glueThresholdLabel_),
                         static_cast<juce::Component*>(&glueThresholdSlider_),
                         static_cast<juce::Component*>(&glueRatioLabel_),
                         static_cast<juce::Component*>(&glueRatioSlider_),
                         static_cast<juce::Component*>(&targetPreChainLabel_),
                         static_cast<juce::Component*>(&targetPreChainSlider_),
                         static_cast<juce::Component*>(&deEssFreqLabel_),
                         static_cast<juce::Component*>(&deEssFreqSlider_),
                         static_cast<juce::Component*>(&deEssThresholdLabel_),
                         static_cast<juce::Component*>(&deEssThresholdSlider_),
                         static_cast<juce::Component*>(&deEssPresenceLabel_),
                         static_cast<juce::Component*>(&deEssPresenceSlider_),
                         static_cast<juce::Component*>(&deEssRatioLabel_),
                         static_cast<juce::Component*>(&deEssRatioSlider_),
                         static_cast<juce::Component*>(&deEssRangeLabel_),
                         static_cast<juce::Component*>(&deEssRangeSlider_),
                         static_cast<juce::Component*>(&resetProButton_) })
            c->setVisible(proPanelVisible_);
        resized();
    };
    proButton_.setWantsKeyboardFocus(false);
    addAndMakeVisible(proButton_);

    playButton_.onClick = [this] { togglePlay(); };
    playButton_.setWantsKeyboardFocus(false);
    addAndMakeVisible(playButton_);

    listenButton_.setClickingTogglesState(true);
    listenButton_.setWantsKeyboardFocus(false);
    listenButton_.setToggleState(true, juce::dontSendNotification); // default: enhanced
    listenButton_.onClick = [this] { updateListenButton(); };
    addAndMakeVisible(listenButton_);
    updateListenButton();

    musicCaption_.setText("Backing Music", juce::dontSendNotification);
    addAndMakeVisible(musicCaption_);

    addMusicButton_.onClick = [this] { addMusicClip(0.0); };
    addMusicButton_.setWantsKeyboardFocus(false);
    addAndMakeVisible(addMusicButton_);

    removeMusicButton_.onClick = [this] { removeSelectedMusicClip(); };
    removeMusicButton_.setWantsKeyboardFocus(false);
    addAndMakeVisible(removeMusicButton_);

    musicClipBox_.onChange = [this] { syncMusicControlsFromSelection(); };
    addAndMakeVisible(musicClipBox_);

    musicStartLabel_.setText("Start", juce::dontSendNotification);
    musicVolumeLabel_.setText("Volume", juce::dontSendNotification);
    musicFadeInLabel_.setText("Fade in", juce::dontSendNotification);
    musicFadeOutLabel_.setText("Fade out", juce::dontSendNotification);
    for (auto* label : { &musicStartLabel_, &musicVolumeLabel_, &musicFadeInLabel_, &musicFadeOutLabel_ })
        addAndMakeVisible(label);

    auto configureMusicSlider = [this](juce::Slider& slider, double min, double max,
                                       double step, double value, const juce::String& suffix) {
        slider.setRange(min, max, step);
        slider.setValue(value, juce::dontSendNotification);
        slider.setTextValueSuffix(suffix);
        slider.onValueChange = [this] { applySelectedMusicClipControls(); };
        addAndMakeVisible(slider);
    };
    configureMusicSlider(musicStartSlider_, 0.0, 600.0, 0.1, 0.0, " s");
    configureMusicSlider(musicVolumeSlider_, -60.0, 6.0, 0.5, -18.0, " dB");
    configureMusicSlider(musicFadeInSlider_, 0.0, 20.0, 0.1, 1.0, " s");
    configureMusicSlider(musicFadeOutSlider_, 0.0, 20.0, 0.1, 1.0, " s");

    musicTimeline_.onAddAt = [this](double seconds) { addMusicClip(seconds); };
    musicTimeline_.onSelectClip = [this](int index) {
        musicClipBox_.setSelectedId(index + 1, juce::sendNotification);
    };
    musicTimeline_.onMoveOrResizeClip = [this](int index, double start, double length) {
        const auto& clips = engine_.musicClips();
        if (index < 0 || index >= static_cast<int>(clips.size()))
            return;
        const auto& clip = clips[static_cast<std::size_t>(index)];
        engine_.setMusicClipParams(index, start, clip.gainDb,
                                   clip.fadeInSeconds, clip.fadeOutSeconds, length);
        player_.setMusicClips(engine_.musicClips());
        syncMusicControlsFromSelection();
        updateMusicTimeline();
    };
    addAndMakeVisible(musicTimeline_);

    exportButton_.onClick = [this] { doExport(); };
    exportButton_.setWantsKeyboardFocus(false);
    addAndMakeVisible(exportButton_);

    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setText("Drop a video or audio file to begin.", juce::dontSendNotification);
    addAndMakeVisible(statusLabel_);

    addAndMakeVisible(fastCompMeter_);
    addAndMakeVisible(glueCompMeter_);
    addAndMakeVisible(deEssMeter_);
    addAndMakeVisible(limiterMeter_);
    addAndMakeVisible(vuMeter_);

    progressBar_.setVisible(false);
    addAndMakeVisible(progressBar_);

    playButton_.setEnabled(false);
    listenButton_.setEnabled(false);
    addMusicButton_.setEnabled(false);
    removeMusicButton_.setEnabled(false);
    musicClipBox_.setEnabled(false);
    for (auto* s : { &musicStartSlider_, &musicVolumeSlider_, &musicFadeInSlider_, &musicFadeOutSlider_ })
        s->setEnabled(false);
    exportButton_.setEnabled(false);
    noiseReductionSlider_.setEnabled(false);

    // Live-spectrum FFT buffers.
    const int fftSize = 1 << kFftOrder;
    fftData_.assign(static_cast<std::size_t>(2 * fftSize), 0.0f);
    analysisScratch_.assign(static_cast<std::size_t>(fftSize), 0.0f);
    liveDb_.assign(static_cast<std::size_t>(fftSize / 2), -120.0f);
    fftWindow_.resize(static_cast<std::size_t>(fftSize));
    for (int i = 0; i < fftSize; ++i)
        fftWindow_[static_cast<std::size_t>(i)] =
            0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi
                                    * static_cast<float>(i) / static_cast<float>(fftSize - 1));
    liveResult_.fftSize = fftSize;
    liveResult_.binDb.assign(static_cast<std::size_t>(fftSize / 2), -120.0f);

    deviceManager_.initialiseWithDefaultDevices(0, 2);
    sourcePlayer_.setSource(&player_);
    deviceManager_.addAudioCallback(&sourcePlayer_);

    startTimerHz(30);
    setSize(920, 980);
    grabKeyboardFocus();
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

vc::Tone MainComponent::currentTone() const {
    switch (toneBox_.getSelectedId()) {
        case 2: return vc::Tone::Warm;
        case 3: return vc::Tone::Crisp;
        default: return vc::Tone::Natural;
    }
}

vc::ChainParams MainComponent::buildParams() const {
    auto p = vc::fixedVoiceCleanupParams();
    p.tone = currentTone();
    p.noiseReductionAmount = noiseReductionSlider_.getValue() / 100.0;

    p.fastCompThresholdDb = fastThresholdSlider_.getValue();
    p.fastCompRatio = fastRatioSlider_.getValue();
    p.glueCompThresholdDb = glueThresholdSlider_.getValue();
    p.glueCompRatio = glueRatioSlider_.getValue();
    p.targetPreChainLufs = targetPreChainSlider_.getValue();
    p.deEssFreqHz = deEssFreqSlider_.getValue();
    p.deEssThresholdDb = deEssThresholdSlider_.getValue();
    p.deEssPresenceThresholdDb = deEssPresenceSlider_.getValue();
    p.deEssRatio = deEssRatioSlider_.getValue();
    p.deEssRangeDb = deEssRangeSlider_.getValue();

    vc::applyIntensity(p, strengthSlider_.getValue() / 100.0);
    p.deEssRangeDb = deEssRangeSlider_.getValue() * (strengthSlider_.getValue() / 100.0);
    p.inputCalibrationGainDb = vc::computeCalibrationGainDb(engine_.inputLufs(), p);

    p.autoEqEnabled = autoEqButton_.getToggleState();
    if (engine_.hasAudio())
        p.autoEqBands = vc::computeAutoEqBands(engine_.spectrum(), p.baseAutoEqStrength);

    return p;
}

void MainComponent::refreshMusicClipList() {
    const int previous = musicClipBox_.getSelectedId();
    musicClipBox_.clear(juce::dontSendNotification);
    const auto& clips = engine_.musicClips();
    for (int i = 0; i < static_cast<int>(clips.size()); ++i)
        musicClipBox_.addItem(juce::String(i + 1) + ". " + clips[static_cast<std::size_t>(i)].name, i + 1);

    if (!clips.empty())
        musicClipBox_.setSelectedId(juce::jlimit(1, static_cast<int>(clips.size()), previous > 0 ? previous : 1),
                                    juce::dontSendNotification);
    syncMusicControlsFromSelection();
    player_.setMusicClips(clips);
    updateMusicTimeline();
}

void MainComponent::syncMusicControlsFromSelection() {
    const int index = musicClipBox_.getSelectedId() - 1;
    const auto& clips = engine_.musicClips();
    const bool valid = index >= 0 && index < static_cast<int>(clips.size());
    removeMusicButton_.setEnabled(!busy_.load() && valid);
    musicClipBox_.setEnabled(!busy_.load() && !clips.empty());
    for (auto* s : { &musicStartSlider_, &musicVolumeSlider_, &musicFadeInSlider_, &musicFadeOutSlider_ })
        s->setEnabled(!busy_.load() && valid);

    if (!valid)
        return;

    const auto& clip = clips[static_cast<std::size_t>(index)];
    musicStartSlider_.setValue(clip.startSeconds, juce::dontSendNotification);
    musicVolumeSlider_.setValue(clip.gainDb, juce::dontSendNotification);
    musicFadeInSlider_.setValue(clip.fadeInSeconds, juce::dontSendNotification);
    musicFadeOutSlider_.setValue(clip.fadeOutSeconds, juce::dontSendNotification);
}

void MainComponent::applySelectedMusicClipControls() {
    const int index = musicClipBox_.getSelectedId() - 1;
    engine_.setMusicClipParams(index,
                               musicStartSlider_.getValue(),
                               musicVolumeSlider_.getValue(),
                               musicFadeInSlider_.getValue(),
                               musicFadeOutSlider_.getValue(),
                               index >= 0 && index < static_cast<int>(engine_.musicClips().size())
                                   ? engine_.musicClips()[static_cast<std::size_t>(index)].durationSeconds()
                                   : 0.0);
    player_.setMusicClips(engine_.musicClips());
    updateMusicTimeline();
}

void MainComponent::updateMusicTimeline() {
    musicTimeline_.setVoice(engine_.hasAudio() ? &engine_.beforeBuffer() : nullptr, engine_.sampleRate());
    musicTimeline_.setClips(&engine_.musicClips(), musicClipBox_.getSelectedId() - 1);
}

void MainComponent::resetProDefaults() {
    const auto p = vc::fixedVoiceCleanupParams();
    fastThresholdSlider_.setValue(p.fastCompThresholdDb, juce::dontSendNotification);
    fastRatioSlider_.setValue(p.fastCompRatio, juce::dontSendNotification);
    glueThresholdSlider_.setValue(p.glueCompThresholdDb, juce::dontSendNotification);
    glueRatioSlider_.setValue(p.glueCompRatio, juce::dontSendNotification);
    targetPreChainSlider_.setValue(p.targetPreChainLufs, juce::dontSendNotification);
    deEssFreqSlider_.setValue(p.deEssFreqHz, juce::dontSendNotification);
    deEssThresholdSlider_.setValue(p.deEssThresholdDb, juce::dontSendNotification);
    deEssPresenceSlider_.setValue(p.deEssPresenceThresholdDb, juce::dontSendNotification);
    deEssRatioSlider_.setValue(p.deEssRatio, juce::dontSendNotification);
    deEssRangeSlider_.setValue(p.deEssRangeDb, juce::dontSendNotification);
    applyParamsLive();
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
    const auto params = buildParams();
    player_.setNoiseReductionAmount(params.noiseReductionAmount);
    player_.setParams(params);             // instant: sound changes now
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
    worker_ = std::thread([safe, job, onSuccess]() mutable {
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
            measuredParams.inputCalibrationGainDb =
                vc::computeCalibrationGainDb(engine_.inputLufs(), measuredParams);
            measuredParams.autoEqBands =
                vc::computeAutoEqBands(engine_.spectrum(), measuredParams.baseAutoEqStrength);
            initialLoudnessRef_ = engine_.measureChainLoudness(measuredParams);
            return {};
        },
        [this, file]() {
            player_.setDrySource(&engine_.beforeBuffer(), engine_.sampleRate());
            player_.setDenoisedSource(engine_.hasDenoised() ? &engine_.denoisedBuffer() : nullptr);
            player_.setInputLoudness(initialLoudnessRef_);
            applyParamsLive();

            playButton_.setEnabled(true);
            listenButton_.setEnabled(true);
            listenButton_.setToggleState(true, juce::dontSendNotification);
            updateListenButton();
            exportButton_.setEnabled(true);
            addMusicButton_.setEnabled(true);
            refreshMusicClipList();
            exportButton_.setButtonText(engine_.sourceHasVideo() ? "Export video..."
                                                                 : "Export audio...");
            dropArea_.setStatus(file.getFileName());
            spectrumView_.setSpectrum(engine_.spectrum());
            updateEqView();

            const double target = buildParams().targetLufs;
            const auto denoise = engine_.hasDenoised() ? "noise cache ready" : "noise cache unavailable";
            setUiBusy(false, juce::String::formatted(
                "Loaded \"%s\"  -  %s, input %.1f LUFS, target %.0f LUFS.",
                file.getFileName().toRawUTF8(), denoise, engine_.inputLufs(), target));
        });
}

void MainComponent::addMusicClip(double startSeconds) {
    if (!engine_.hasAudio()) return;

    musicChooser_ = std::make_unique<juce::FileChooser>(
        "Add backing music", juce::File(), "*.wav;*.mp3;*.m4a;*.flac;*.aiff");
    musicChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, startSeconds](const juce::FileChooser& fc) {
            const auto file = fc.getResult();
            if (file == juce::File()) return;

            runOnWorker(
                "Adding backing music...",
                [this, file]() -> juce::String {
                    juce::String err;
                    if (!engine_.addMusicClip(file, err))
                        return err.isEmpty() ? "could not add music" : err;
                    return {};
                },
                [this, startSeconds]() {
                    const int index = static_cast<int>(engine_.musicClips().size()) - 1;
                    if (index >= 0) {
                        const auto& clip = engine_.musicClips()[static_cast<std::size_t>(index)];
                        const double projectDuration = engine_.sampleRate() > 0.0
                            ? static_cast<double>(engine_.beforeBuffer().getNumSamples()) / engine_.sampleRate()
                            : clip.sourceDurationSeconds();
                        const double maxLength = std::max(0.1, projectDuration - startSeconds);
                        engine_.setMusicClipParams(index, startSeconds, clip.gainDb,
                                                   clip.fadeInSeconds, clip.fadeOutSeconds,
                                                   std::min(clip.sourceDurationSeconds(), maxLength));
                    }
                    refreshMusicClipList();
                    if (index >= 0)
                        musicClipBox_.setSelectedId(index + 1, juce::sendNotification);
                    setUiBusy(false, "Backing music added.");
                });
        });
}

void MainComponent::removeSelectedMusicClip() {
    const int index = musicClipBox_.getSelectedId() - 1;
    engine_.removeMusicClip(index);
    refreshMusicClipList();
    updateMusicTimeline();
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

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::spaceKey && playButton_.isEnabled()) {
        togglePlay();
        return true;
    }
    return false;
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
    toneBox_.setEnabled(!busy);
    autoEqButton_.setEnabled(!busy);
    noiseReductionSlider_.setEnabled(!busy && haveAudio && engine_.hasDenoised());
    strengthSlider_.setEnabled(!busy);
    proButton_.setEnabled(!busy);
    playButton_.setEnabled(!busy && haveAudio);
    listenButton_.setEnabled(!busy && haveAudio);
    addMusicButton_.setEnabled(!busy && haveAudio);
    exportButton_.setEnabled(!busy && haveAudio);
    syncMusicControlsFromSelection();

    statusLabel_.setText(message, juce::dontSendNotification);
}

void MainComponent::timerCallback() {
    // Debounced loudness refinement after a param change.
    if (loudnessCountdown_ > 0 && --loudnessCountdown_ == 0)
        recomputeLoudnessAsync();

    const bool playing = player_.isPlaying();
    playButton_.setButtonText(playing ? "Stop" : "Play");

    // Meters reflect the live chain while playing; ease back to 0 when stopped.
    fastCompMeter_.setReduction(playing ? player_.fastCompReductionDb() : 0.0f);
    glueCompMeter_.setReduction(playing ? player_.glueCompReductionDb() : 0.0f);
    deEssMeter_.setReduction(playing ? player_.deEssReductionDb() : 0.0f);
    limiterMeter_.setReduction(playing ? player_.limiterReductionDb() : 0.0f);
    vuMeter_.setLevelDb(playing ? player_.rmsLevelDb() : -60.0f);

    // Animate the spectrum to the playing audio; revert to the average when idle.
    if (playing)
        updateLiveSpectrum();
    else
        spectrumView_.setShowLive(false);

    const double playheadSeconds = engine_.sampleRate() > 0.0
        ? player_.getPositionNormalised()
            * static_cast<double>(engine_.beforeBuffer().getNumSamples()) / engine_.sampleRate()
        : 0.0;
    musicTimeline_.setPlayheadSeconds(playheadSeconds);
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

    musicTimeline_.setBounds(r.removeFromTop(150));
    r.removeFromTop(8);

    autoEqButton_.setBounds(r.removeFromTop(24));
    r.removeFromTop(6);

    auto toneRow = r.removeFromTop(54).reduced(4);
    toneCaption_.setBounds(toneRow.removeFromTop(18));
    toneBox_.setBounds(toneRow);
    r.removeFromTop(8);

    auto noiseRow = r.removeFromTop(44);
    noiseReductionCaption_.setBounds(noiseRow.removeFromLeft(118));
    noiseReductionSlider_.setBounds(noiseRow);
    r.removeFromTop(8);

    auto strengthRow = r.removeFromTop(44);
    strengthCaption_.setBounds(strengthRow.removeFromLeft(70));
    proButton_.setBounds(strengthRow.removeFromRight(58).reduced(4, 2));
    strengthSlider_.setBounds(strengthRow);
    r.removeFromTop(8);

    if (proPanelVisible_) {
        auto proArea = r.removeFromTop(292);
        proPanel_.setBounds(proArea);
        proArea.reduce(10, 18);

        auto placePro = [&proArea](juce::Label& label, juce::Slider& slider) {
            auto row = proArea.removeFromTop(24);
            label.setBounds(row.removeFromLeft(112));
            slider.setBounds(row);
            proArea.removeFromTop(3);
        };
        placePro(fastThresholdLabel_, fastThresholdSlider_);
        placePro(fastRatioLabel_, fastRatioSlider_);
        placePro(glueThresholdLabel_, glueThresholdSlider_);
        placePro(glueRatioLabel_, glueRatioSlider_);
        placePro(targetPreChainLabel_, targetPreChainSlider_);
        proArea.removeFromTop(4);
        placePro(deEssFreqLabel_, deEssFreqSlider_);
        placePro(deEssThresholdLabel_, deEssThresholdSlider_);
        placePro(deEssPresenceLabel_, deEssPresenceSlider_);
        placePro(deEssRatioLabel_, deEssRatioSlider_);
        placePro(deEssRangeLabel_, deEssRangeSlider_);
        resetProButton_.setBounds(proArea.removeFromTop(28).removeFromRight(90));
        r.removeFromTop(8);
    }

    auto transport = r.removeFromTop(34);
    playButton_.setBounds(transport.removeFromLeft(90).reduced(2));
    listenButton_.setBounds(transport.reduced(2));
    r.removeFromTop(10);

    auto musicArea = r.removeFromTop(126);
    musicCaption_.setBounds(musicArea.removeFromTop(20));
    auto musicTop = musicArea.removeFromTop(30);
    addMusicButton_.setBounds(musicTop.removeFromLeft(116).reduced(2));
    removeMusicButton_.setBounds(musicTop.removeFromRight(86).reduced(2));
    musicClipBox_.setBounds(musicTop.reduced(2));
    musicArea.removeFromTop(4);
    auto musicRow1 = musicArea.removeFromTop(30);
    musicStartLabel_.setBounds(musicRow1.removeFromLeft(54));
    musicStartSlider_.setBounds(musicRow1.removeFromLeft(musicRow1.getWidth() / 2));
    musicVolumeLabel_.setBounds(musicRow1.removeFromLeft(62));
    musicVolumeSlider_.setBounds(musicRow1);
    auto musicRow2 = musicArea.removeFromTop(30);
    musicFadeInLabel_.setBounds(musicRow2.removeFromLeft(54));
    musicFadeInSlider_.setBounds(musicRow2.removeFromLeft(musicRow2.getWidth() / 2));
    musicFadeOutLabel_.setBounds(musicRow2.removeFromLeft(62));
    musicFadeOutSlider_.setBounds(musicRow2);
    r.removeFromTop(8);

    // Live gain-reduction meters.
    fastCompMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    glueCompMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    deEssMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    limiterMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(12);
    vuMeter_.setBounds(r.removeFromTop(22));
    r.removeFromTop(10);

    exportButton_.setBounds(r.removeFromTop(38).reduced(2));
    r.removeFromTop(8);

    progressBar_.setBounds(r.removeFromTop(18));
    r.removeFromTop(4);
    statusLabel_.setBounds(r.removeFromTop(40));
}
