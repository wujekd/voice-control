#include "MainComponent.h"

#include <cmath>

void MainComponent::EncoderLookAndFeel::drawRotarySlider(
    juce::Graphics& g, int x, int y, int width, int height, float sliderPosProportional,
    float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) {
    const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height)
        .reduced(9.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 7.0f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const auto stroke = juce::PathStrokeType(6.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded);
    const auto green = juce::Colour(0xff6ee07a);

    juce::Path inactiveArc;
    inactiveArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                              rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(green.withAlpha(slider.isEnabled() ? 0.24f : 0.12f));
    g.strokePath(inactiveArc, stroke);

    juce::Path activeArc;
    activeArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                            rotaryStartAngle, angle, true);
    g.setColour(green.withAlpha(slider.isEnabled() ? 0.95f : 0.38f));
    g.strokePath(activeArc, stroke);

    g.setColour(juce::Colour(0xff15181d));
    g.fillEllipse(bounds.withSizeKeepingCentre(radius * 1.5f, radius * 1.5f));
    g.setColour(juce::Colours::white.withAlpha(slider.isEnabled() ? 0.14f : 0.07f));
    g.drawEllipse(bounds.withSizeKeepingCentre(radius * 1.5f, radius * 1.5f), 1.0f);

    const auto markerRadius = 4.5f;
    const auto markerDistance = radius;
    const auto marker = juce::Point<float>(
        centre.x + std::cos(angle - juce::MathConstants<float>::halfPi) * markerDistance,
        centre.y + std::sin(angle - juce::MathConstants<float>::halfPi) * markerDistance);
    g.setColour(juce::Colour(0xffeaffed));
    g.fillEllipse(marker.x - markerRadius, marker.y - markerRadius,
                  markerRadius * 2.0f, markerRadius * 2.0f);
    g.setColour(green);
    g.drawEllipse(marker.x - markerRadius, marker.y - markerRadius,
                  markerRadius * 2.0f, markerRadius * 2.0f, 1.4f);
}

MainComponent::MainComponent() {
    setWantsKeyboardFocus(true);

    titleLabel_.setText("Voice Control", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel_);

    settingsButton_.setWantsKeyboardFocus(false);
    settingsButton_.onClick = [this] {
        settingsPanelVisible_ = !settingsPanelVisible_;
        settingsPanel_.setVisible(settingsPanelVisible_);
        followSystemButton_.setVisible(settingsPanelVisible_);
        outputDeviceLabel_.setVisible(settingsPanelVisible_);
        outputDeviceBox_.setVisible(settingsPanelVisible_);
        if (settingsPanelVisible_) {
            if (auto* type = deviceManager_.getCurrentDeviceTypeObject())
                type->scanForDevices();
            refreshOutputDeviceList();
        }
        resized();
    };
    addAndMakeVisible(settingsButton_);

    settingsPanel_.setVisible(false);
    addChildComponent(settingsPanel_);

    followSystemButton_.setToggleState(followSystemDefault_, juce::dontSendNotification);
    followSystemButton_.setWantsKeyboardFocus(false);
    followSystemButton_.onClick = [this] {
        followSystemDefault_ = followSystemButton_.getToggleState();
        outputDeviceBox_.setEnabled(!followSystemDefault_);
        if (followSystemDefault_) {
            const auto def = systemDefaultOutputName();
            if (def.isNotEmpty())
                setOutputDevice(def);
        }
    };
    addChildComponent(followSystemButton_);

    outputDeviceLabel_.setText("Output", juce::dontSendNotification);
    addChildComponent(outputDeviceLabel_);

    outputDeviceBox_.setEnabled(!followSystemDefault_);
    outputDeviceBox_.onChange = [this] {
        if (followSystemDefault_)
            return;
        const auto name = outputDeviceBox_.getText();
        if (name.isNotEmpty())
            setOutputDevice(name);
    };
    addChildComponent(outputDeviceBox_);

    addAndMakeVisible(dropArea_);
    dropArea_.onFile = [this](const juce::File& f) { loadFile(f); };

    addAndMakeVisible(spectrumView_);

    toneCaption_.setText("Tone", juce::dontSendNotification);
    noiseReductionCaption_.setText("Noise Reduction", juce::dontSendNotification);
    strengthCaption_.setText("Intensity", juce::dontSendNotification);
    addAndMakeVisible(toneCaption_);
    addAndMakeVisible(noiseReductionCaption_);
    addAndMakeVisible(strengthCaption_);

    auto configureEncoder = [this](juce::Slider& slider) {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 82, 22);
        slider.setLookAndFeel(&encoderLookAndFeel_);
    };

    toneSlider_.setRange(-1.0, 1.0, 0.01);
    toneSlider_.setValue(0.0, juce::dontSendNotification);
    toneSlider_.textFromValueFunction = [](double value) {
        if (value < -0.08)
            return juce::String("Warm");
        if (value > 0.08)
            return juce::String("Crisp");
        return juce::String("Natural");
    };
    configureEncoder(toneSlider_);
    toneSlider_.onValueChange = [this] { applyParamsLive(); updateEqView(); };
    addAndMakeVisible(toneSlider_);

    noiseReductionSlider_.setRange(0.0, 100.0, 1.0);
    noiseReductionSlider_.setValue(75.0, juce::dontSendNotification);
    noiseReductionSlider_.setTextValueSuffix(" %");
    configureEncoder(noiseReductionSlider_);
    noiseReductionSlider_.onValueChange = [this] { applyParamsLive(); };
    addAndMakeVisible(noiseReductionSlider_);

    strengthSlider_.setRange(0.0, 100.0, 1.0);
    strengthSlider_.setValue(100.0, juce::dontSendNotification);
    strengthSlider_.setTextValueSuffix(" %");
    configureEncoder(strengthSlider_);
    strengthSlider_.onValueChange = [this] { applyParamsLive(); updateEqView(); };
    addAndMakeVisible(strengthSlider_);

    auto configureProSlider = [this, configureEncoder](juce::Slider& slider, double min, double max,
                                     double step, double value, const juce::String& suffix) {
        slider.setRange(min, max, step);
        slider.setValue(value, juce::dontSendNotification);
        slider.setTextValueSuffix(suffix);
        configureEncoder(slider);
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

    addMusicButton_.onClick = [this] { addMusicClip(nextMusicClipStartSeconds()); };
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

    auto configureMusicSlider = [this, configureEncoder](juce::Slider& slider, double min, double max,
                                       double step, double value, const juce::String& suffix) {
        slider.setRange(min, max, step);
        slider.setValue(value, juce::dontSendNotification);
        slider.setTextValueSuffix(suffix);
        configureEncoder(slider);
        slider.onValueChange = [this] { applySelectedMusicClipControls(); };
        addAndMakeVisible(slider);
    };
    configureMusicSlider(musicStartSlider_, 0.0, 600.0, 0.1, 0.0, " s");
    configureMusicSlider(musicVolumeSlider_, -60.0, 6.0, 0.5, -18.0, " dB");
    configureMusicSlider(musicFadeInSlider_, 0.0, 20.0, 0.1, 1.0, " s");
    configureMusicSlider(musicFadeOutSlider_, 0.0, 20.0, 0.1, 1.0, " s");
    musicVolumeSlider_.onValueChange = [this] { applySelectedMusicClipVolume(); };

    musicTimeline_.onAddAt = [this](double seconds) { addMusicClip(seconds); };
    musicTimeline_.onSeek = [this](double seconds) {
        player_.setPositionSeconds(seconds);
        engine_.setPlayheadFrame(player_.currentSourceFrame()); // prioritize denoise here
    };
    musicTimeline_.onSelectClip = [this](int index) {
        musicClipBox_.setSelectedId(index + 1, juce::sendNotification);
    };
    musicTimeline_.onClipDragStateChanged = [this](int index, bool dragging) {
        musicClipDragActive_ = dragging;
        if (dragging) {
            player_.setMutedMusicClipIndex(index);
        } else {
            player_.setMusicClips(engine_.musicClips());
            player_.setMutedMusicClipIndex(-1);
        }
    };
    musicTimeline_.onMoveOrResizeClip = [this](int index, double start, double sourceOffset, double length) {
        const auto& clips = engine_.musicClips();
        if (index < 0 || index >= static_cast<int>(clips.size()))
            return;
        const auto& clip = clips[static_cast<std::size_t>(index)];
        engine_.setMusicClipParams(index, start, sourceOffset, clip.gainDb,
                                   clip.fadeInSeconds, clip.fadeOutSeconds, length);
        if (!musicClipDragActive_)
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
    addMusicButton_.setEnabled(true);
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
    deviceManager_.addChangeListener(this);

    startTimerHz(30);
    setSize(920, 980);
    grabKeyboardFocus();
}

MainComponent::~MainComponent() {
    for (auto* s : { &toneSlider_, &noiseReductionSlider_, &strengthSlider_,
                     &fastThresholdSlider_, &fastRatioSlider_, &glueThresholdSlider_,
                     &glueRatioSlider_, &targetPreChainSlider_, &deEssFreqSlider_,
                     &deEssThresholdSlider_, &deEssPresenceSlider_, &deEssRatioSlider_,
                     &deEssRangeSlider_, &musicStartSlider_, &musicVolumeSlider_,
                     &musicFadeInSlider_, &musicFadeOutSlider_ })
        s->setLookAndFeel(nullptr);

    stopTimer();
    deviceManager_.removeChangeListener(this);
    deviceManager_.removeAudioCallback(&sourcePlayer_);
    sourcePlayer_.setSource(nullptr);
    player_.clearSources();
    if (worker_.joinable())
        worker_.join();
}

double MainComponent::currentToneAmount() const {
    return juce::jlimit(-0.95, 0.95, toneSlider_.getValue());
}

double MainComponent::currentIntensity() const {
    return juce::jlimit(0.0, 1.0, strengthSlider_.getValue() / 100.0);
}

vc::ChainParams MainComponent::buildParams() const {
    auto p = vc::fixedVoiceCleanupParams();
    p.tone = vc::Tone::Natural;
    p.toneAmount = currentToneAmount();
    p.noiseReductionAmount = vc::noiseReductionControlToBlend(noiseReductionSlider_.getValue() / 100.0);

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

    const double intensity = currentIntensity();
    vc::applyIntensity(p, intensity);
    p.inputCalibrationGainDb = vc::computeCalibrationGainDb(engine_.inputLufs(), p);

    p.autoEqEnabled = true;
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
                               index >= 0 && index < static_cast<int>(engine_.musicClips().size())
                                   ? engine_.musicClips()[static_cast<std::size_t>(index)].sourceOffsetSeconds
                                   : 0.0,
                               musicVolumeSlider_.getValue(),
                               musicFadeInSlider_.getValue(),
                               musicFadeOutSlider_.getValue(),
                               index >= 0 && index < static_cast<int>(engine_.musicClips().size())
                                   ? engine_.musicClips()[static_cast<std::size_t>(index)].durationSeconds()
                                   : 0.0);
    player_.setMusicClips(engine_.musicClips());
    updateMusicTimeline();
}

void MainComponent::applySelectedMusicClipVolume() {
    const int index = musicClipBox_.getSelectedId() - 1;
    const auto& clips = engine_.musicClips();
    if (index < 0 || index >= static_cast<int>(clips.size()))
        return;

    const auto& clip = clips[static_cast<std::size_t>(index)];
    const double gainDb = musicVolumeSlider_.getValue();
    engine_.setMusicClipParams(index, clip.startSeconds, clip.sourceOffsetSeconds, gainDb,
                               clip.fadeInSeconds, clip.fadeOutSeconds, clip.durationSeconds());
    player_.setMusicClipGainDb(index, gainDb);
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

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    if (updatingDevice_)
        return;

    if (settingsPanelVisible_)
        refreshOutputDeviceList();

    if (followSystemDefault_) {
        const auto def = systemDefaultOutputName();
        if (def.isNotEmpty() && def != deviceManager_.getAudioDeviceSetup().outputDeviceName)
            setOutputDevice(def);
    }
}

juce::String MainComponent::systemDefaultOutputName() const {
    if (auto* type = deviceManager_.getCurrentDeviceTypeObject()) {
        const auto names = type->getDeviceNames(false);
        const int idx = type->getDefaultDeviceIndex(false);
        if (juce::isPositiveAndBelow(idx, names.size()))
            return names[idx];
    }
    return {};
}

void MainComponent::setOutputDevice(const juce::String& name) {
    const juce::ScopedValueSetter<bool> guard(updatingDevice_, true);
    auto setup = deviceManager_.getAudioDeviceSetup();
    if (setup.outputDeviceName == name)
        return;
    setup.outputDeviceName = name;
    setup.useDefaultOutputChannels = true;
    deviceManager_.setAudioDeviceSetup(setup, true);
    refreshOutputDeviceList();
}

void MainComponent::refreshOutputDeviceList() {
    outputDeviceBox_.clear(juce::dontSendNotification);
    auto* type = deviceManager_.getCurrentDeviceTypeObject();
    if (type == nullptr)
        return;
    const auto names = type->getDeviceNames(false);
    for (int i = 0; i < names.size(); ++i)
        outputDeviceBox_.addItem(names[i], i + 1);
    const int idx = names.indexOf(deviceManager_.getAudioDeviceSetup().outputDeviceName);
    if (idx >= 0)
        outputDeviceBox_.setSelectedId(idx + 1, juce::dontSendNotification);
}

double MainComponent::currentDeviceRate() const {
    if (auto* dev = deviceManager_.getCurrentAudioDevice()) {
        const double r = dev->getCurrentSampleRate();
        if (r > 0.0) return r;
    }
    return 48000.0;
}

double MainComponent::nextMusicClipStartSeconds() const {
    const auto& clips = engine_.musicClips();
    if (clips.empty())
        return 0.0;

    const int selected = musicClipBox_.getSelectedId() - 1;
    if (selected >= 0 && selected < static_cast<int>(clips.size())) {
        const auto& clip = clips[static_cast<std::size_t>(selected)];
        return clip.startSeconds + clip.durationSeconds();
    }

    double end = 0.0;
    for (const auto& clip : clips)
        end = std::max(end, clip.startSeconds + clip.durationSeconds());
    return end;
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
    const double intensity = currentIntensity();
    double loudnessRef = intensityMinLoudnessRef_
        + (intensityMaxLoudnessRef_ - intensityMinLoudnessRef_) * intensity;
    if (!std::isfinite(loudnessRef))
        loudnessRef = intensityMaxLoudnessRef_;
    if (std::isfinite(loudnessRef))
        player_.setInputLoudness(loudnessRef);
    player_.setNoiseReductionAmount(params.noiseReductionAmount);
    player_.setParams(params);             // instant: sound changes now
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
                safe->analyzingMedia_ = false;
                safe->dropArea_.setStatus("Add voice audio or video\nDrag here, or click to browse");
                safe->setUiBusy(false, "Error: " + error);
            }
        });
    });
}

void MainComponent::loadFile(const juce::File& file) {
    player_.stop();
    analyzingMedia_ = true;
    dropArea_.setStatus("Analyzing " + file.getFileName() + "\nMeasuring level and voice profile");
    const auto params = buildParams();

    runOnWorker(
        "Loading + analysing audio...",
        [this, file, params]() -> juce::String {
            juce::String err;
            if (!engine_.loadMedia(file, err))
                return err.isEmpty() ? "could not read audio from file" : err;

            auto makeMeasuredParams = [&](double intensity) {
                auto measuredParams = params;
                vc::applyIntensity(measuredParams, intensity);
                measuredParams.inputCalibrationGainDb =
                    vc::computeCalibrationGainDb(engine_.inputLufs(), measuredParams);
                measuredParams.autoEqBands =
                    vc::computeAutoEqBands(engine_.spectrum(), measuredParams.baseAutoEqStrength);
                return measuredParams;
            };

            intensityMinLoudnessRef_ = engine_.measureChainLoudness(makeMeasuredParams(0.0));
            intensityMaxLoudnessRef_ = engine_.measureChainLoudness(makeMeasuredParams(1.0));
            return {};
        },
        [this, file]() {
            player_.setDrySource(&engine_.beforeBuffer(), engine_.sampleRate());
            player_.setDenoisedSource(engine_.denoisedPlanar(), engine_.denoisedValidHops(),
                                      engine_.denoisedNumHops(), engine_.denoisedHopSize());
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
            analyzingMedia_ = false;
            setUiBusy(false, juce::String::formatted(
                "Loaded \"%s\"  -  denoising in background, input %.1f LUFS, target %.0f LUFS.",
                file.getFileName().toRawUTF8(), engine_.inputLufs(), target));
        });
}

void MainComponent::addMusicClip(double startSeconds) {
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
                        double start = std::max(0.0, startSeconds);
                        double length = clip.sourceDurationSeconds();
                        if (engine_.hasAudio()) {
                            const double projectDuration = engine_.sampleRate() > 0.0
                                ? static_cast<double>(engine_.beforeBuffer().getNumSamples()) / engine_.sampleRate()
                                : clip.sourceDurationSeconds();
                            start = juce::jlimit(0.0, std::max(0.0, projectDuration - 0.1), start);
                            const double maxLength = std::max(0.1, projectDuration - start);
                            length = std::min(clip.sourceDurationSeconds(), maxLength);
                        }
                        engine_.setMusicClipParams(index, start, 0.0, clip.gainDb,
                                                   clip.fadeInSeconds, clip.fadeOutSeconds,
                                                   length);
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

    const bool haveAudio = engine_.hasAudio();
    dropArea_.setVisible(analyzingMedia_ || !haveAudio);
    dropArea_.setEnabled(!busy && !haveAudio);
    toneSlider_.setEnabled(!busy);
    noiseReductionSlider_.setEnabled(!busy && haveAudio);
    strengthSlider_.setEnabled(!busy);
    proButton_.setEnabled(!busy);
    playButton_.setEnabled(!busy && haveAudio);
    listenButton_.setEnabled(!busy && haveAudio);
    addMusicButton_.setEnabled(!busy);
    exportButton_.setEnabled(!busy && haveAudio);
    syncMusicControlsFromSelection();

    statusLabel_.setText(message, juce::dontSendNotification);
}

void MainComponent::timerCallback() {
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

    if (engine_.processMusicWaveformChunks(48))
        updateMusicTimeline();

    const double playheadSeconds = engine_.sampleRate() > 0.0
        ? player_.getPositionNormalised()
            * static_cast<double>(engine_.beforeBuffer().getNumSamples()) / engine_.sampleRate()
        : 0.0;
    musicTimeline_.setPlayheadSeconds(playheadSeconds);

    // Steer the background denoiser toward what's playing.
    if (engine_.hasAudio())
        engine_.setPlayheadFrame(player_.currentSourceFrame());
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff20232a));
}

void MainComponent::resized() {
    auto r = getLocalBounds().reduced(16);

    auto titleRow = r.removeFromTop(30);
    settingsButton_.setBounds(titleRow.removeFromRight(90).reduced(2));
    titleLabel_.setBounds(titleRow);
    r.removeFromTop(6);

    if (settingsPanelVisible_) {
        auto sArea = r.removeFromTop(78);
        settingsPanel_.setBounds(sArea);
        sArea.reduce(10, 18);
        followSystemButton_.setBounds(sArea.removeFromTop(24));
        sArea.removeFromTop(4);
        auto devRow = sArea.removeFromTop(24);
        outputDeviceLabel_.setBounds(devRow.removeFromLeft(100));
        outputDeviceBox_.setBounds(devRow);
        r.removeFromTop(8);
    }

    auto placeEncoder = [](juce::Rectangle<int> area, juce::Label& label, juce::Slider& slider) {
        label.setBounds(area.removeFromTop(20));
        slider.setBounds(area);
    };

    auto encoderRow = r.removeFromTop(138);
    constexpr int encoderWidth = 170;
    constexpr int encoderGap = 58;
    const int totalEncoderWidth = encoderWidth * 3 + encoderGap * 2;
    auto centeredEncoders = encoderRow.withSizeKeepingCentre(
        juce::jmin(totalEncoderWidth, encoderRow.getWidth()), encoderRow.getHeight());
    auto noiseArea = centeredEncoders.removeFromLeft(encoderWidth);
    centeredEncoders.removeFromLeft(encoderGap);
    auto strengthArea = centeredEncoders.removeFromLeft(encoderWidth);
    centeredEncoders.removeFromLeft(encoderGap);
    auto toneArea = centeredEncoders.removeFromLeft(encoderWidth);
    placeEncoder(noiseArea, noiseReductionCaption_, noiseReductionSlider_);
    placeEncoder(strengthArea, strengthCaption_, strengthSlider_);
    placeEncoder(toneArea, toneCaption_, toneSlider_);
    proButton_.setBounds(encoderRow.removeFromRight(58).removeFromTop(30).reduced(2));
    r.removeFromTop(8);

    spectrumView_.setBounds(r.removeFromTop(150));
    r.removeFromTop(6);

    auto timelineBounds = r.removeFromTop(150);
    musicTimeline_.setBounds(timelineBounds);
    auto voiceDropBounds = timelineBounds.reduced(10);
    voiceDropBounds.setHeight(58);
    dropArea_.setBounds(voiceDropBounds);
    dropArea_.setVisible(analyzingMedia_ || !engine_.hasAudio());
    dropArea_.toFront(false);
    r.removeFromTop(8);

    if (proPanelVisible_) {
        auto proArea = r.removeFromTop(286);
        proPanel_.setBounds(proArea);
        proArea.reduce(10, 18);

        auto placePro = [&proArea](juce::Label& leftLabel, juce::Slider& leftSlider,
                                   juce::Label& rightLabel, juce::Slider& rightSlider) {
            auto row = proArea.removeFromTop(50);
            auto left = row.removeFromLeft(row.getWidth() / 2).reduced(4, 0);
            auto right = row.reduced(4, 0);
            leftLabel.setBounds(left.removeFromLeft(118).reduced(0, 10));
            leftSlider.setBounds(left);
            rightLabel.setBounds(right.removeFromLeft(118).reduced(0, 10));
            rightSlider.setBounds(right);
            proArea.removeFromTop(4);
        };
        placePro(fastThresholdLabel_, fastThresholdSlider_, fastRatioLabel_, fastRatioSlider_);
        placePro(glueThresholdLabel_, glueThresholdSlider_, glueRatioLabel_, glueRatioSlider_);
        placePro(targetPreChainLabel_, targetPreChainSlider_, deEssFreqLabel_, deEssFreqSlider_);
        proArea.removeFromTop(4);
        placePro(deEssThresholdLabel_, deEssThresholdSlider_, deEssPresenceLabel_, deEssPresenceSlider_);
        placePro(deEssRatioLabel_, deEssRatioSlider_, deEssRangeLabel_, deEssRangeSlider_);
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
    auto musicControls = musicArea.removeFromTop(66);
    auto musicCellWidth = musicControls.getWidth() / 4;
    placeEncoder(musicControls.removeFromLeft(musicCellWidth).reduced(4, 0),
                 musicStartLabel_, musicStartSlider_);
    placeEncoder(musicControls.removeFromLeft(musicCellWidth).reduced(4, 0),
                 musicVolumeLabel_, musicVolumeSlider_);
    placeEncoder(musicControls.removeFromLeft(musicCellWidth).reduced(4, 0),
                 musicFadeInLabel_, musicFadeInSlider_);
    placeEncoder(musicControls.reduced(4, 0),
                 musicFadeOutLabel_, musicFadeOutSlider_);
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
