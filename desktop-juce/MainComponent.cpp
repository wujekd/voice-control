#include "MainComponent.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kDefaultWindowWidth = 920;
constexpr int kOuterMargin = 16;
constexpr int kBottomBreathingRoom = 18;
constexpr int kDefaultWindowHeight = kOuterMargin * 2
    + (20 + 2 + 20 + 2 + 20 + 2 + 20 + 12 + 30 + 10) // meters
    + (138 + 8)                                      // main controls
    + (112 + 6)                                      // spectrum
    + (200 + 8)                                      // timeline
    + (116 + 8)                                      // music controls
    + (18 + 4 + 40)                                  // progress + status
    + kBottomBreathingRoom;
}

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

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
#endif

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

    playButton_.onClick = [this] { togglePlay(); };
    playButton_.setWantsKeyboardFocus(false);
    addAndMakeVisible(playButton_);

    listenButton_.setClickingTogglesState(true);
    listenButton_.setWantsKeyboardFocus(false);
    listenButton_.setToggleState(false, juce::dontSendNotification); // default: enhanced (not bypassed)
    listenButton_.onClick = [this] { updateListenButton(); };
    addAndMakeVisible(listenButton_);
    updateListenButton();

    musicCaption_.setText("Backing Music", juce::dontSendNotification);
    addAndMakeVisible(musicCaption_);

    // Add / remove / clip selection are now driven from the timeline. The
    // controls remain as hidden state-holders (the timeline still sets the
    // selected clip through musicClipBox_) but no longer appear in the GUI.
    addMusicButton_.onClick = [this] { addMusicClip(nextMusicClipStartSeconds()); };
    addMusicButton_.setWantsKeyboardFocus(false);
    addChildComponent(addMusicButton_);
    addMusicButton_.setVisible(false);

    removeMusicButton_.onClick = [this] { removeSelectedMusicClip(); };
    removeMusicButton_.setWantsKeyboardFocus(false);
    addChildComponent(removeMusicButton_);
    removeMusicButton_.setVisible(false);

    musicClipBox_.onChange = [this] {
        syncMusicControlsFromSelection();
        updateMusicTimeline();
    };
    addChildComponent(musicClipBox_);
    musicClipBox_.setVisible(false);

    musicStartLabel_.setText("Start", juce::dontSendNotification);
    musicMasterVolumeLabel_.setText("Music Volume", juce::dontSendNotification);
    musicVolumeLabel_.setText("Clip Volume", juce::dontSendNotification);
    musicFadeInLabel_.setText("Fade in", juce::dontSendNotification);
    musicFadeOutLabel_.setText("Fade out", juce::dontSendNotification);
    for (auto* label : { &musicStartLabel_, &musicMasterVolumeLabel_, &musicVolumeLabel_,
                         &musicFadeInLabel_, &musicFadeOutLabel_ })
        addAndMakeVisible(label);

    auto configureMusicSlider = [this, configureEncoder](juce::Slider& slider, double min, double max,
                                       double step, double value, const juce::String& suffix) {
        slider.setRange(min, max, step);
        slider.setValue(value, juce::dontSendNotification);
        slider.setTextValueSuffix(suffix);
        configureEncoder(slider);
        slider.onDragStart = [this] { beginMusicUndoGesture(); };
        slider.onDragEnd = [this] { endMusicUndoGesture(); };
        slider.onValueChange = [this] { applySelectedMusicClipControls(); };
        addAndMakeVisible(slider);
    };
    configureMusicSlider(musicStartSlider_, 0.0, 600.0, 0.1, 0.0, " s");
    configureMusicSlider(musicMasterVolumeSlider_, -60.0, 6.0, 0.5, 0.0, " dB");
    configureMusicSlider(musicVolumeSlider_, -60.0, 6.0, 0.5, -18.0, " dB");
    configureMusicSlider(musicFadeInSlider_, 0.0, 20.0, 0.1, 1.0, " s");
    configureMusicSlider(musicFadeOutSlider_, 0.0, 20.0, 0.1, 1.0, " s");
    musicMasterVolumeSlider_.onDragStart = {};
    musicMasterVolumeSlider_.onDragEnd = {};
    musicMasterVolumeSlider_.onValueChange = [this] { applyMusicMasterVolume(); };
    musicVolumeSlider_.onValueChange = [this] { applySelectedMusicClipVolume(); };

    // Background ducking (sketch): knobs only for now, not yet wired to DSP.
    // Look-ahead and reduction behave like a sidechain compressor; the filter
    // lets it act more like a dynamic EQ than a full-band compressor.
    duckCaption_.setText("Ducking", juce::dontSendNotification);
    addAndMakeVisible(duckCaption_);
    duckLookAheadLabel_.setText("Look-ahead", juce::dontSendNotification);
    duckReductionLabel_.setText("Reduction", juce::dontSendNotification);
    duckFilterLabel_.setText("Filter", juce::dontSendNotification);
    for (auto* label : { &duckLookAheadLabel_, &duckReductionLabel_, &duckFilterLabel_ })
        addAndMakeVisible(label);

    auto configureDuckSlider = [this, configureEncoder](juce::Slider& slider, double min, double max,
                                      double step, double value, const juce::String& suffix) {
        slider.setRange(min, max, step);
        slider.setValue(value, juce::dontSendNotification);
        slider.setTextValueSuffix(suffix);
        configureEncoder(slider);
        addAndMakeVisible(slider);
    };
    configureDuckSlider(duckLookAheadSlider_, 0.0, 50.0, 1.0, 5.0, " ms");
    configureDuckSlider(duckReductionSlider_, 0.0, 24.0, 0.5, 9.0, " dB");
    configureDuckSlider(duckFilterSlider_, 100.0, 8000.0, 10.0, 800.0, " Hz");

    // Start, fade in and fade out are now adjusted on the timeline; these
    // controls stay as hidden mirrors of the selected clip's values.
    musicStartLabel_.setVisible(false);
    musicStartSlider_.setVisible(false);
    musicFadeInLabel_.setVisible(false);
    musicFadeInSlider_.setVisible(false);
    musicFadeOutLabel_.setVisible(false);
    musicFadeOutSlider_.setVisible(false);

    musicTimeline_.onAddAt = [this](double seconds) { addMusicClip(seconds); };
    musicTimeline_.onSeek = [this](double seconds) {
        player_.setPositionSeconds(seconds);
        engine_.setPlayheadFrame(player_.currentSourceFrame()); // prioritize denoise here
    };
    musicTimeline_.onSelectClip = [this](int index) {
        musicClipBox_.setSelectedId(index + 1, juce::sendNotification);
    };
    musicTimeline_.onRemoveClip = [this](int index) {
        musicClipBox_.setSelectedId(index + 1, juce::dontSendNotification);
        removeSelectedMusicClip();
    };
    musicTimeline_.onClipEditStarted = [this](int) {
        beginMusicUndoGesture();
    };
    musicTimeline_.onClipDragStateChanged = [this](int index, bool dragging) {
        musicClipDragActive_ = dragging;
        if (dragging) {
            musicClipFadeSnapshot_.clear();
            for (const auto& c : engine_.musicClips())
                musicClipFadeSnapshot_.push_back({ c.fadeInSeconds, c.fadeOutSeconds });
            player_.setMutedMusicClipIndex(index);
        } else {
            endMusicUndoGesture();
            musicClipFadeSnapshot_.clear();
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
        applyMusicClipOverlapCrossfades(index);
        if (!musicClipDragActive_)
            player_.setMusicClips(engine_.musicClips());
        syncMusicControlsFromSelection();
        updateMusicTimeline();
    };
    musicTimeline_.onAdjustClipFades = [this](int index, double fadeIn, double fadeOut) {
        const auto& clips = engine_.musicClips();
        if (index < 0 || index >= static_cast<int>(clips.size()))
            return;
        const auto& clip = clips[static_cast<std::size_t>(index)];
        engine_.setMusicClipParams(index, clip.startSeconds, clip.sourceOffsetSeconds, clip.gainDb,
                                   fadeIn, fadeOut, clip.durationSeconds());
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
    for (auto* s : { &musicStartSlider_, &musicMasterVolumeSlider_, &musicVolumeSlider_,
                     &musicFadeInSlider_, &musicFadeOutSlider_ })
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
    setSize(kDefaultWindowWidth, kDefaultWindowHeight);
    grabKeyboardFocus();
}

MainComponent::~MainComponent() {
#if JUCE_MAC
    if (juce::MenuBarModel::getMacMainMenu() == this)
        juce::MenuBarModel::setMacMainMenu(nullptr);
#endif

    for (auto* s : { &toneSlider_, &noiseReductionSlider_, &strengthSlider_,
                     &fastThresholdSlider_, &fastRatioSlider_, &glueThresholdSlider_,
                     &glueRatioSlider_, &targetPreChainSlider_, &deEssFreqSlider_,
                     &deEssThresholdSlider_, &deEssPresenceSlider_, &deEssRatioSlider_,
                     &deEssRangeSlider_, &musicStartSlider_, &musicMasterVolumeSlider_, &musicVolumeSlider_,
                     &musicFadeInSlider_, &musicFadeOutSlider_,
                     &duckLookAheadSlider_, &duckReductionSlider_, &duckFilterSlider_ })
        s->setLookAndFeel(nullptr);

    stopTimer();
    deviceManager_.removeChangeListener(this);
    deviceManager_.removeAudioCallback(&sourcePlayer_);
    sourcePlayer_.setSource(nullptr);
    player_.clearSources();
    if (spectrumWorker_.joinable())
        spectrumWorker_.join();
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
    musicMasterVolumeSlider_.setEnabled(!busy_.load() && !clips.empty());
    for (auto* s : { &musicStartSlider_, &musicVolumeSlider_, &musicFadeInSlider_, &musicFadeOutSlider_ })
        s->setEnabled(!busy_.load() && valid);
    musicMasterVolumeSlider_.setValue(engine_.musicMasterGainDb(), juce::dontSendNotification);

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
    const auto& clips = engine_.musicClips();
    if (index < 0 || index >= static_cast<int>(clips.size()))
        return;

    if (!applyingMusicUndo_ && !musicUndoGestureActive_)
        pushMusicUndoState();
    engine_.setMusicClipParams(index,
                               musicStartSlider_.getValue(),
                               clips[static_cast<std::size_t>(index)].sourceOffsetSeconds,
                               musicVolumeSlider_.getValue(),
                               musicFadeInSlider_.getValue(),
                               musicFadeOutSlider_.getValue(),
                               clips[static_cast<std::size_t>(index)].durationSeconds());
    player_.setMusicClips(engine_.musicClips());
    updateMusicTimeline();
}

void MainComponent::applySelectedMusicClipVolume() {
    const int index = musicClipBox_.getSelectedId() - 1;
    const auto& clips = engine_.musicClips();
    if (index < 0 || index >= static_cast<int>(clips.size()))
        return;

    if (!applyingMusicUndo_ && !musicUndoGestureActive_)
        pushMusicUndoState();

    const auto& clip = clips[static_cast<std::size_t>(index)];
    const double gainDb = musicVolumeSlider_.getValue();
    engine_.setMusicClipParams(index, clip.startSeconds, clip.sourceOffsetSeconds, gainDb,
                               clip.fadeInSeconds, clip.fadeOutSeconds, clip.durationSeconds());
    player_.setMusicClipGainDb(index, gainDb);
}

void MainComponent::applyMusicMasterVolume() {
    const double gainDb = musicMasterVolumeSlider_.getValue();
    engine_.setMusicMasterGainDb(gainDb);
    player_.setMusicMasterGainDb(gainDb);
}

void MainComponent::pushMusicUndoState() {
    if (applyingMusicUndo_)
        return;

    static constexpr int maxUndoStates = 12;
    musicUndoStack_.push_back({ engine_.musicClips(), musicClipBox_.getSelectedId() - 1 });
    if (static_cast<int>(musicUndoStack_.size()) > maxUndoStates)
        musicUndoStack_.erase(musicUndoStack_.begin());
}

void MainComponent::beginMusicUndoGesture() {
    if (musicUndoGestureActive_ || applyingMusicUndo_)
        return;
    pushMusicUndoState();
    musicUndoGestureActive_ = true;
}

void MainComponent::endMusicUndoGesture() {
    if (musicUndoGestureActive_ && !musicUndoStack_.empty()
        && musicClipStateMatches(musicUndoStack_.back()))
        musicUndoStack_.pop_back();
    musicUndoGestureActive_ = false;
}

bool MainComponent::musicClipStateMatches(const MusicUndoState& state) const {
    const auto& clips = engine_.musicClips();
    if (clips.size() != state.clips.size())
        return false;

    constexpr double epsilon = 1.0e-6;
    for (std::size_t i = 0; i < clips.size(); ++i) {
        const auto& a = clips[i];
        const auto& b = state.clips[i];
        if (a.name != b.name
            || std::abs(a.sampleRate - b.sampleRate) > epsilon
            || std::abs(a.startSeconds - b.startSeconds) > epsilon
            || std::abs(a.sourceOffsetSeconds - b.sourceOffsetSeconds) > epsilon
            || std::abs(a.lengthSeconds - b.lengthSeconds) > epsilon
            || std::abs(a.gainDb - b.gainDb) > epsilon
            || std::abs(a.fadeInSeconds - b.fadeInSeconds) > epsilon
            || std::abs(a.fadeOutSeconds - b.fadeOutSeconds) > epsilon)
            return false;
    }
    return true;
}

void MainComponent::undoMusicTimelineEdit() {
    if (musicUndoStack_.empty() || busy_.load())
        return;

    applyingMusicUndo_ = true;
    auto state = std::move(musicUndoStack_.back());
    musicUndoStack_.pop_back();
    engine_.setMusicClips(std::move(state.clips));
    player_.setMutedMusicClipIndex(-1);
    player_.setMusicClips(engine_.musicClips());
    musicClipFadeSnapshot_.clear();
    musicClipDragActive_ = false;
    musicUndoGestureActive_ = false;

    refreshMusicClipList();
    const int count = static_cast<int>(engine_.musicClips().size());
    if (count > 0)
        musicClipBox_.setSelectedId(juce::jlimit(1, count, state.selectedIndex + 1),
                                    juce::sendNotification);
    else
        musicClipBox_.setSelectedId(0, juce::sendNotification);

    updateMusicTimeline();
    statusLabel_.setText("Undid music edit.", juce::dontSendNotification);
    applyingMusicUndo_ = false;
}

void MainComponent::applyMusicClipOverlapCrossfades(int draggedIndex) {
    const auto& clips = engine_.musicClips();
    const int count = static_cast<int>(clips.size());
    if (draggedIndex < 0 || draggedIndex >= count)
        return;
    if (static_cast<int>(musicClipFadeSnapshot_.size()) != count)
        return; // no valid drag-start snapshot

    struct ClipState {
        double start = 0.0;
        double end = 0.0;
        double length = 0.0;
        double fadeIn = 0.0;
        double fadeOut = 0.0;
    };

    std::vector<ClipState> state;
    state.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const auto& c = clips[static_cast<std::size_t>(i)];
        const double length = c.durationSeconds();
        const auto snapshot = musicClipFadeSnapshot_[static_cast<std::size_t>(i)];
        state.push_back({ c.startSeconds, c.startSeconds + length, length, snapshot.first, snapshot.second });
    }

    constexpr double epsilon = 1.0e-4;

    for (int i = 0; i < count; ++i) {
        if (i == draggedIndex)
            continue;

        const int leftIndex = state[static_cast<std::size_t>(draggedIndex)].start
                                  <= state[static_cast<std::size_t>(i)].start
            ? draggedIndex
            : i;
        const int rightIndex = leftIndex == draggedIndex ? i : draggedIndex;
        auto& left = state[static_cast<std::size_t>(leftIndex)];
        auto& right = state[static_cast<std::size_t>(rightIndex)];

        const bool partialEdgeOverlap = left.start + epsilon < right.start
            && right.start + epsilon < left.end
            && left.end + epsilon < right.end;
        if (!partialEdgeOverlap)
            continue;

        const double overlap = juce::jlimit(0.0, std::min(left.length, right.length), left.end - right.start);
        if (overlap <= epsilon)
            continue;

        left.fadeOut = overlap;
        right.fadeIn = overlap;
    }

    for (auto& s : state) {
        if (s.fadeIn + s.fadeOut > s.length && s.fadeIn + s.fadeOut > 0.0) {
            const double scale = s.length / (s.fadeIn + s.fadeOut);
            s.fadeIn *= scale;
            s.fadeOut *= scale;
        }
    }

    for (int i = 0; i < count; ++i) {
        const auto& c = clips[static_cast<std::size_t>(i)];
        const auto& s = state[static_cast<std::size_t>(i)];
        if (std::abs(c.fadeInSeconds - s.fadeIn) > epsilon
            || std::abs(c.fadeOutSeconds - s.fadeOut) > epsilon) {
            engine_.setMusicClipParams(i, c.startSeconds, c.sourceOffsetSeconds, c.gainDb,
                                       s.fadeIn, s.fadeOut, s.length);
        }
    }
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

void MainComponent::setSettingsPanelVisible(bool visible) {
    settingsPanelVisible_ = visible;
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
    updateMainMenu();
}

void MainComponent::setProPanelVisible(bool visible) {
    proPanelVisible_ = visible;
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
    updateMainMenu();
}

void MainComponent::updateMainMenu() {
#if JUCE_MAC
    menuItemsChanged();
#endif
}

juce::StringArray MainComponent::getMenuBarNames() {
    return { "View" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;
    if (topLevelMenuIndex == 0) {
        menu.addItem(toggleSettingsMenuId, "Audio Output Settings", true, settingsPanelVisible_);
        menu.addItem(toggleProMenuId, "Pro Controls", true, proPanelVisible_);
    }
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int) {
    switch (menuItemID) {
        case toggleSettingsMenuId:
            setSettingsPanelVisible(!settingsPanelVisible_);
            break;
        case toggleProMenuId:
            setProPanelVisible(!proPanelVisible_);
            break;
        default:
            break;
    }
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
    requestProcessedSpectrumUpdate();
}

void MainComponent::requestProcessedSpectrumUpdate() {
    if (!engine_.hasAudio())
        return;
    processedSpectrumRequest_.fetch_add(1, std::memory_order_relaxed);
}

void MainComponent::startProcessedSpectrumUpdateIfNeeded() {
    if (!engine_.hasAudio() || busy_.load() || player_.isPlaying())
        return;
    if (spectrumWorkerRunning_.load(std::memory_order_relaxed))
        return;

    const int request = processedSpectrumRequest_.load(std::memory_order_relaxed);
    if (request == processedSpectrumRendered_.load(std::memory_order_relaxed))
        return;

    if (spectrumWorker_.joinable())
        spectrumWorker_.join();

    const auto params = buildParams();
    spectrumWorkerRunning_.store(true, std::memory_order_relaxed);
    juce::Component::SafePointer<MainComponent> safe(this);
    spectrumWorker_ = std::thread([safe, params, request]() {
        if (safe == nullptr)
            return;
        auto resultSpectrum = safe->engine_.analyzeProcessedVoiceSpectrum(params);
        juce::MessageManager::callAsync([safe, readySpectrum = std::move(resultSpectrum), request]() mutable {
            if (safe == nullptr)
                return;
            if (request == safe->processedSpectrumRequest_.load(std::memory_order_relaxed)) {
                safe->spectrumView_.setProcessedSpectrum(readySpectrum);
                safe->processedSpectrumRendered_.store(request, std::memory_order_relaxed);
            }
            safe->spectrumWorkerRunning_.store(false, std::memory_order_relaxed);
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
                safe->analyzingMedia_ = false;
                safe->dropArea_.setStatus("Add voice audio or video\nDrag here, or click to browse");
                safe->setUiBusy(false, "Error: " + error);
            }
        });
    });
}

void MainComponent::loadFile(const juce::File& file) {
    player_.stop();
    if (spectrumWorker_.joinable())
        spectrumWorker_.join();
    spectrumWorkerRunning_.store(false, std::memory_order_relaxed);
    processedSpectrumRequest_.store(0, std::memory_order_relaxed);
    processedSpectrumRendered_.store(0, std::memory_order_relaxed);
    spectrumView_.setProcessedSpectrum({});
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
            listenButton_.setToggleState(false, juce::dontSendNotification);
            updateListenButton();
            exportButton_.setEnabled(true);
            addMusicButton_.setEnabled(true);
            refreshMusicClipList();
            exportButton_.setButtonText("Export");
            dropArea_.setStatus(file.getFileName());
            spectrumView_.setSpectrum(engine_.spectrum());
            updateEqView();
            requestProcessedSpectrumUpdate();

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
            const auto undoState = MusicUndoState { engine_.musicClips(), musicClipBox_.getSelectedId() - 1 };

            runOnWorker(
                "Adding backing music...",
                [this, file]() -> juce::String {
                    juce::String err;
                    if (!engine_.addMusicClip(file, err))
                        return err.isEmpty() ? "could not add music" : err;
                    return {};
                },
                [this, startSeconds, undoState]() {
                    musicUndoStack_.push_back(undoState);
                    if (static_cast<int>(musicUndoStack_.size()) > 12)
                        musicUndoStack_.erase(musicUndoStack_.begin());
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
    if (index >= 0 && index < static_cast<int>(engine_.musicClips().size()))
        pushMusicUndoState();
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
    const auto mods = key.getModifiers();
    if ((mods.isCtrlDown() || mods.isCommandDown())
        && (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z')) {
        undoMusicTimelineEdit();
        return true;
    }

    if (key == juce::KeyPress::spaceKey && playButton_.isEnabled()) {
        togglePlay();
        return true;
    }
    return false;
}

void MainComponent::updateListenButton() {
    const bool bypass = listenButton_.getToggleState();
    player_.setShowAfter(!bypass); // bypass -> hear the original, unprocessed voice
    listenButton_.setButtonText("Bypass");
    // Active bypass glows a muted, faded red (echoing the timeline's washed-out clips);
    // inactive falls back to the neutral button grey.
    const auto col = bypass ? juce::Colour(0xff7d2f2f) : juce::Colour(0xff3a3f49);
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
    // Gentle green backlight while playing (matching the encoders' green); neutral grey otherwise.
    const auto playCol = playing ? juce::Colour(0xff2f7d52) : juce::Colour(0xff3a3f49);
    playButton_.setColour(juce::TextButton::buttonColourId, playCol);
    playButton_.setColour(juce::TextButton::buttonOnColourId, playCol);

    // Meters reflect the live chain while playing; ease back to 0 when stopped.
    fastCompMeter_.setReduction(playing ? player_.fastCompReductionDb() : 0.0f);
    glueCompMeter_.setReduction(playing ? player_.glueCompReductionDb() : 0.0f);
    deEssMeter_.setReduction(playing ? player_.deEssReductionDb() : 0.0f);
    limiterMeter_.setReduction(playing ? player_.limiterReductionDb() : 0.0f);
    vuMeter_.setLevels(playing ? player_.rmsLevelDb() : -60.0f,
                       playing ? player_.peakLevelDb() : -60.0f);

    // Animate the spectrum to the playing audio; revert to the average when idle.
    if (playing)
        updateLiveSpectrum();
    else
        spectrumView_.setShowLive(false);

    if (engine_.processMusicWaveformChunks(256))
        updateMusicTimeline();

    startProcessedSpectrumUpdateIfNeeded();

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

    // Live gain-reduction meters.
    fastCompMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    glueCompMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    deEssMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    limiterMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(12);
    vuMeter_.setBounds(r.removeFromTop(30));
    r.removeFromTop(10);

    auto placeEncoder = [](juce::Rectangle<int> area, juce::Label& label, juce::Slider& slider) {
        label.setBounds(area.removeFromTop(20));
        slider.setBounds(area);
    };

    auto encoderRow = r.removeFromTop(138);

    // Five elements laid out left-to-right with one consistent gap so the space
    // between the buttons and the encoders matches the gaps between encoders:
    // play/bypass column, Intensity, Tone, Noise Reduction, Export.
    constexpr int buttonColWidth = 108;
    constexpr int encoderWidth = 156;
    constexpr int exportWidth = 104;
    constexpr int elementGap = 42;
    const int groupWidth = buttonColWidth + exportWidth + encoderWidth * 3 + elementGap * 4;
    auto group = encoderRow.withSizeKeepingCentre(
        juce::jmin(groupWidth, encoderRow.getWidth()), encoderRow.getHeight());

    auto mainTransport = group.removeFromLeft(buttonColWidth).reduced(2, 4);
    playButton_.setBounds(mainTransport.removeFromTop(82));
    mainTransport.removeFromTop(8);
    listenButton_.setBounds(mainTransport.removeFromTop(36));
    group.removeFromLeft(elementGap);

    // Export sits on the far right and spans the full combined height of the
    // play + bypass column.
    auto exportArea = group.removeFromRight(exportWidth).reduced(2, 4);
    exportButton_.setBounds(exportArea);
    group.removeFromRight(elementGap);

    auto strengthArea = group.removeFromLeft(encoderWidth);
    group.removeFromLeft(elementGap);
    auto toneArea = group.removeFromLeft(encoderWidth);
    group.removeFromLeft(elementGap);
    auto noiseArea = group.removeFromLeft(encoderWidth);
    placeEncoder(strengthArea, strengthCaption_, strengthSlider_);
    placeEncoder(toneArea, toneCaption_, toneSlider_);
    placeEncoder(noiseArea, noiseReductionCaption_, noiseReductionSlider_);
    r.removeFromTop(8);

    spectrumView_.setBounds(r.removeFromTop(112));
    r.removeFromTop(6);

    auto timelineBounds = r.removeFromTop(200);
    musicTimeline_.setBounds(timelineBounds);
    // The voice drop field covers only the voice lane (top), leaving the music
    // lane below clickable so its own "+" adds a music clip. Matches
    // MusicTimeline's internal lane layout: reduced(10), 88px voice lane.
    auto voiceLane = timelineBounds.reduced(10).removeFromTop(88);
    dropArea_.setBounds(voiceLane);
    dropArea_.setVisible(analyzingMedia_ || !engine_.hasAudio());
    dropArea_.toFront(false);
    r.removeFromTop(8);

    // Add / remove / clip selection moved to the timeline. The left of this row
    // holds the backing-music volume knobs; the right holds the background
    // ducking section.
    auto musicArea = r.removeFromTop(116);
    auto captionRow = musicArea.removeFromTop(20);
    auto musicRow = musicArea;
    constexpr int knobWidth = 96;

    auto volumeArea = musicRow.removeFromLeft(knobWidth * 2);
    musicCaption_.setBounds(captionRow.removeFromLeft(knobWidth * 2));
    placeEncoder(volumeArea.removeFromLeft(knobWidth).reduced(4, 0),
                 musicMasterVolumeLabel_, musicMasterVolumeSlider_);
    placeEncoder(volumeArea.reduced(4, 0),
                 musicVolumeLabel_, musicVolumeSlider_);

    auto duckArea = musicRow.removeFromRight(knobWidth * 3);
    duckCaption_.setBounds(captionRow.removeFromRight(knobWidth * 3));
    placeEncoder(duckArea.removeFromLeft(knobWidth).reduced(4, 0),
                 duckLookAheadLabel_, duckLookAheadSlider_);
    placeEncoder(duckArea.removeFromLeft(knobWidth).reduced(4, 0),
                 duckReductionLabel_, duckReductionSlider_);
    placeEncoder(duckArea.reduced(4, 0),
                 duckFilterLabel_, duckFilterSlider_);
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

    progressBar_.setBounds(r.removeFromTop(18));
    r.removeFromTop(4);
    statusLabel_.setBounds(r.removeFromTop(40));
}
