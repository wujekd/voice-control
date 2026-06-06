#include "MainComponent.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
constexpr int kDefaultWindowWidth = 920;
constexpr int kMinWindowWidth = kDefaultWindowWidth - 70; // floor before things vanish
constexpr int kOuterMargin = 16;
constexpr int kBottomBreathingRoom = 0;
constexpr int kSpectrumHeight = 102;
constexpr int kDefaultWindowHeight = kOuterMargin * 2
    + (20 + 2 + 20 + 2 + 20 + 2 + 24 + 6)            // meters (matches resized())
    + (138 + 8)                                      // main controls
    + (kSpectrumHeight + 6)                          // spectrum
    + (216 + 8)                                      // timeline
    + (116 + 8)                                      // music controls
    + (18)                                           // progress
    + kBottomBreathingRoom;

juce::File appDataDir() {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Voice Control");
    dir.createDirectory();
    return dir;
}

juce::File autosaveProjectFile() {
    return appDataDir().getChildFile("LastSession.vcproj");
}

// Managed project library: a single, fixed, user-visible folder. Projects are
// saved here automatically so the user never picks a location.
juce::File projectsFolder() {
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Voice Control Projects");
    dir.createDirectory();
    return dir;
}

// A non-colliding "<name>.vcproj" in the given folder ("Untitled", "Untitled 2"...).
juce::File uniqueProjectFile(const juce::File& folder, const juce::String& baseName) {
    auto safe = juce::File::createLegalFileName(baseName).trim();
    if (safe.isEmpty())
        safe = "Untitled";
    auto file = folder.getChildFile(safe + ".vcproj");
    for (int n = 2; file.existsAsFile(); ++n)
        file = folder.getChildFile(safe + " " + juce::String(n) + ".vcproj");
    return file;
}

double numberProperty(const juce::DynamicObject* obj, const juce::Identifier& id, double fallback) {
    if (obj == nullptr || !obj->hasProperty(id))
        return fallback;
    return static_cast<double>(obj->getProperty(id));
}

juce::String stringProperty(const juce::DynamicObject* obj, const juce::Identifier& id,
                            const juce::String& fallback = {}) {
    if (obj == nullptr || !obj->hasProperty(id))
        return fallback;
    return obj->getProperty(id).toString();
}

void setSliderIfPresent(juce::Slider& slider, const juce::DynamicObject* obj,
                        const juce::Identifier& id) {
    if (obj != nullptr && obj->hasProperty(id))
        slider.setValue(static_cast<double>(obj->getProperty(id)), juce::dontSendNotification);
}

void addNumber(juce::DynamicObject& obj, const juce::Identifier& id, double value) {
    obj.setProperty(id, value);
}

// DialogWindow that reports its own close (native close button / escape key)
// back to the owner so the unique_ptr holding it can be released.
class SettingsDialogWindow : public juce::DialogWindow {
public:
    SettingsDialogWindow(const juce::String& title, juce::Colour background)
        : juce::DialogWindow(title, background, /*escapeKeyTriggersClose*/ true,
                             /*addToDesktop*/ true) {}

    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }
};
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

    // Audio-output and Pro controls below are configured here but not added as
    // children of MainComponent; the settings window re-parents them into its
    // tabs while open (see openSettings()).
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

    outputDeviceLabel_.setText("Output", juce::dontSendNotification);

    outputDeviceBox_.setEnabled(!followSystemDefault_);
    outputDeviceBox_.onChange = [this] {
        if (followSystemDefault_)
            return;
        const auto name = outputDeviceBox_.getText();
        if (name.isNotEmpty())
            setOutputDevice(name);
    };

    addAndMakeVisible(dropArea_);
    dropArea_.onFile = [this](const juce::File& f) {
        if (maybeSaveBeforeReplacingSession())
            loadFile(f);
    };

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

    // Small mute toggle at the start of the Backing Music section: silences the
    // music channel so the voice can be auditioned alone.
    musicMuteButton_.setClickingTogglesState(true);
    musicMuteButton_.setTooltip("Mute backing music (voice only)");
    musicMuteButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb24a4a));
    musicMuteButton_.onClick = [this] {
        player_.setMusicMuted(musicMuteButton_.getToggleState());
    };
    addAndMakeVisible(musicMuteButton_);

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
        markProjectDirty();
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

    // Background ducking: the voice sidechains the backing music. Look-ahead and
    // reduction behave like a sidechain compressor; "Mid focus" blends from
    // full-band ducking (0%) toward mid-band-only ducking (100%), so the music's
    // low end and highs survive while the clashing mids duck.
    duckCaption_.setText("Ducking", juce::dontSendNotification);
    addAndMakeVisible(duckCaption_);

    // Mirror of the Mute button on the ducking side: bypasses the sidechain
    // ducker so the music keeps its full level.
    duckBypassButton_.setClickingTogglesState(true);
    duckBypassButton_.setTooltip("Bypass ducking (music at full level)");
    duckBypassButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb24a4a));
    duckBypassButton_.onClick = [this] {
        player_.setDuckBypassed(duckBypassButton_.getToggleState());
    };
    addAndMakeVisible(duckBypassButton_);

    duckLookAheadLabel_.setText("Look-ahead", juce::dontSendNotification);
    duckReductionLabel_.setText("Reduction", juce::dontSendNotification);
    duckFilterLabel_.setText("Mid focus", juce::dontSendNotification);
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
    configureDuckSlider(duckFilterSlider_, 0.0, 100.0, 1.0, 0.0, " %");
    duckLookAheadSlider_.onValueChange = [this] { player_.setDuckLookAheadMs(duckLookAheadSlider_.getValue()); };
    duckReductionSlider_.onValueChange = [this] { player_.setDuckReductionDb(duckReductionSlider_.getValue()); };
    duckFilterSlider_.onValueChange = [this] { player_.setDuckBlend(duckFilterSlider_.getValue() / 100.0); };
    player_.setDuckLookAheadMs(duckLookAheadSlider_.getValue());
    player_.setDuckReductionDb(duckReductionSlider_.getValue());
    player_.setDuckBlend(duckFilterSlider_.getValue() / 100.0);

    // The backing-music section is busy, so its encoders are made narrower than
    // the main ones: smaller labels and tighter value boxes to claw back width.
    for (auto* label : { &musicMasterVolumeLabel_, &musicVolumeLabel_,
                         &duckLookAheadLabel_, &duckReductionLabel_, &duckFilterLabel_ }) {
        label->setFont(juce::Font(juce::FontOptions(12.0f)));
        label->setJustificationType(juce::Justification::centred);
    }
    for (auto* slider : { &musicMasterVolumeSlider_, &musicVolumeSlider_,
                          &duckLookAheadSlider_, &duckReductionSlider_, &duckFilterSlider_ })
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, 20);

    addAndMakeVisible(duckView_);

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
        markProjectDirty();
    };
    addAndMakeVisible(musicTimeline_);

    exportButton_.onClick = [this] { doExport(); };
    exportButton_.setWantsKeyboardFocus(false);
    addAndMakeVisible(exportButton_);

    // The bottom status line was redundant with the progress bar / busy state,
    // so statusLabel_ is no longer shown (setText calls below are harmless).
    addAndMakeVisible(compMeter_);
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
    musicDb_.assign(static_cast<std::size_t>(fftSize / 2), -120.0f);
    musicResult_.fftSize = fftSize;
    musicResult_.binDb.assign(static_cast<std::size_t>(fftSize / 2), -120.0f);

    deviceManager_.initialiseWithDefaultDevices(0, 2);
    sourcePlayer_.setSource(&player_);
    deviceManager_.addAudioCallback(&sourcePlayer_);
    deviceManager_.addChangeListener(this);

    startTimerHz(30);
    setSize(kDefaultWindowWidth, kDefaultWindowHeight);
    grabKeyboardFocus();
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
        if (safe != nullptr)
            safe->restoreAutosaveSession();
    });
}

MainComponent::~MainComponent() {
#if JUCE_MAC
    if (juce::MenuBarModel::getMacMainMenu() == this)
        juce::MenuBarModel::setMacMainMenu(nullptr);
#endif

    // Tear down the settings window first: it re-parents controls that are
    // members of this component, so it must release them before they are
    // destroyed below.
    settingsWindow_.reset();

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
    saveAutosaveSession();
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
        p.autoEqBands = engine_.autoEqBands(p.baseAutoEqStrength);

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
    markProjectDirty();
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
    markProjectDirty();
}

void MainComponent::applyMusicMasterVolume() {
    const double gainDb = musicMasterVolumeSlider_.getValue();
    engine_.setMusicMasterGainDb(gainDb);
    player_.setMusicMasterGainDb(gainDb);
    markProjectDirty();
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
            || a.sourcePath != b.sourcePath
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
    markProjectDirty();
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
    musicTimeline_.setVoiceWaveformPeaks(
        engine_.hasAudio() ? &engine_.voiceWaveformPeaks() : nullptr,
        engine_.hasAudio() && !engine_.processedVoiceWaveformPeaks().empty()
            ? &engine_.processedVoiceWaveformPeaks()
            : nullptr,
        engine_.waveformDisplayGain());
    musicTimeline_.setVoiceNoiseReduction(
        static_cast<float>(vc::noiseReductionControlToBlend(noiseReductionSlider_.getValue() / 100.0)));
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

void MainComponent::parentHierarchyChanged() {
    if (findParentComponentOfClass<juce::ResizableWindow>() == nullptr)
        return;
    // Defer: during construction the window's constrainer isn't installed and
    // its final size isn't known until setContentOwned/setResizable finish.
    juce::Component::SafePointer<MainComponent> self(this);
    juce::MessageManager::callAsync([self] {
        if (self != nullptr)
            self->applyWindowConstraints();
    });
}

void MainComponent::applyWindowConstraints() {
    auto* window = findParentComponentOfClass<juce::ResizableWindow>();
    if (window == nullptr)
        return;
    auto* constrainer = window->getConstrainer();
    if (constrainer == nullptr)
        return;

    // The main window now has a fixed content height (audio + pro controls live
    // in the separate settings window).
    const int contentHeight = kDefaultWindowHeight;

    // Translate content size into window size (native title bar adds chrome).
    const int chromeH = window->getHeight() - getHeight();
    const int chromeW = window->getWidth() - getWidth();
    const int winH = contentHeight + chromeH;
    const int minWinW = kMinWindowWidth + chromeW;

    // Equal min/max height => the user cannot drag the height; width stays free
    // down to the floor.
    constrainer->setSizeLimits(minWinW, winH, 4096, winH);
    window->setSize(window->getWidth(), winH);
}

void MainComponent::openSettings() {
    if (settingsWindow_ != nullptr) {
        settingsWindow_->toFront(true);
        return;
    }

    // Refresh the device list before the Audio tab shows it.
    if (auto* type = deviceManager_.getCurrentDeviceTypeObject())
        type->scanForDevices();
    refreshOutputDeviceList();

    auto content = std::make_unique<SettingsComponent>();
    content->setAudioControls(followSystemButton_, outputDeviceLabel_, outputDeviceBox_);
    content->setProControls({ { &fastThresholdLabel_, &fastThresholdSlider_ },
                              { &fastRatioLabel_, &fastRatioSlider_ },
                              { &glueThresholdLabel_, &glueThresholdSlider_ },
                              { &glueRatioLabel_, &glueRatioSlider_ },
                              { &targetPreChainLabel_, &targetPreChainSlider_ },
                              { &deEssFreqLabel_, &deEssFreqSlider_ },
                              { &deEssThresholdLabel_, &deEssThresholdSlider_ },
                              { &deEssPresenceLabel_, &deEssPresenceSlider_ },
                              { &deEssRatioLabel_, &deEssRatioSlider_ },
                              { &deEssRangeLabel_, &deEssRangeSlider_ } },
                            resetProButton_);

    auto window = std::make_unique<SettingsDialogWindow>("Settings", juce::Colour(0xff2b2f36));
    window->onClose = [this] { closeSettings(); };
    window->setUsingNativeTitleBar(true);
    window->setContentOwned(content.release(), true);
    window->centreAroundComponent(this, SettingsComponent::kWidth, SettingsComponent::kHeight);
    window->setVisible(true);
    settingsWindow_.reset(window.release());
    updateMainMenu();
}

void MainComponent::closeSettings() {
    // Defer destruction: this fires from inside the dialog (its own close button
    // or the escape key), so the window must outlive the current call stack.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
        if (safe != nullptr) {
            safe->settingsWindow_.reset();
            safe->updateMainMenu();
        }
    });
}

void MainComponent::updateMainMenu() {
#if JUCE_MAC
    menuItemsChanged();
#endif
}

juce::StringArray MainComponent::getMenuBarNames() {
    return { "File", "View" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;
    if (topLevelMenuIndex == 0) {
        menu.addItem(newProjectMenuId, "New Project");
        menu.addItem(openProjectManagerMenuId, "Projects...");
        menu.addSeparator();
        menu.addItem(saveProjectMenuId, "Save Project");
    } else if (topLevelMenuIndex == 1) {
        menu.addItem(openSettingsMenuId, "Settings...", true, settingsWindow_ != nullptr);
    }
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int) {
    switch (menuItemID) {
        case newProjectMenuId:
            newProject();
            break;
        case openProjectManagerMenuId:
            openProjectManager();
            break;
        case saveProjectMenuId:
            saveCurrentProject();
            break;
        case openSettingsMenuId:
            openSettings();
            break;
        default:
            break;
    }
}

juce::var MainComponent::makeProjectState() const {
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("schemaVersion", 1);
    root->setProperty("sourcePath", engine_.sourceFile().getFullPathName());
    root->setProperty("projectPath", currentProjectFile_.getFullPathName());

    auto controls = std::make_unique<juce::DynamicObject>();
    addNumber(*controls, "tone", toneSlider_.getValue());
    addNumber(*controls, "noiseReduction", noiseReductionSlider_.getValue());
    addNumber(*controls, "intensity", strengthSlider_.getValue());
    addNumber(*controls, "fastThreshold", fastThresholdSlider_.getValue());
    addNumber(*controls, "fastRatio", fastRatioSlider_.getValue());
    addNumber(*controls, "glueThreshold", glueThresholdSlider_.getValue());
    addNumber(*controls, "glueRatio", glueRatioSlider_.getValue());
    addNumber(*controls, "targetPreChain", targetPreChainSlider_.getValue());
    addNumber(*controls, "deEssFreq", deEssFreqSlider_.getValue());
    addNumber(*controls, "deEssThreshold", deEssThresholdSlider_.getValue());
    addNumber(*controls, "deEssPresence", deEssPresenceSlider_.getValue());
    addNumber(*controls, "deEssRatio", deEssRatioSlider_.getValue());
    addNumber(*controls, "deEssRange", deEssRangeSlider_.getValue());
    addNumber(*controls, "musicMasterGain", musicMasterVolumeSlider_.getValue());
    addNumber(*controls, "duckLookAhead", duckLookAheadSlider_.getValue());
    addNumber(*controls, "duckReduction", duckReductionSlider_.getValue());
    addNumber(*controls, "duckBlend", duckFilterSlider_.getValue());
    root->setProperty("controls", juce::var(controls.release()));

    juce::Array<juce::var> musicClips;
    const auto& clips = engine_.musicClips();
    musicClips.ensureStorageAllocated(static_cast<int>(clips.size()));
    for (const auto& clip : clips) {
        auto clipObj = std::make_unique<juce::DynamicObject>();
        clipObj->setProperty("sourcePath", clip.sourcePath);
        clipObj->setProperty("name", clip.name);
        addNumber(*clipObj, "startSeconds", clip.startSeconds);
        addNumber(*clipObj, "sourceOffsetSeconds", clip.sourceOffsetSeconds);
        addNumber(*clipObj, "lengthSeconds", clip.durationSeconds());
        addNumber(*clipObj, "gainDb", clip.gainDb);
        addNumber(*clipObj, "fadeInSeconds", clip.fadeInSeconds);
        addNumber(*clipObj, "fadeOutSeconds", clip.fadeOutSeconds);
        musicClips.add(juce::var(clipObj.release()));
    }
    root->setProperty("musicClips", musicClips);
    root->setProperty("selectedMusicClip", musicClipBox_.getSelectedId() - 1);

    return juce::var(root.release());
}

bool MainComponent::applyProjectState(const juce::var& state, bool fromAutosave) {
    (void) fromAutosave;
    auto* root = state.getDynamicObject();
    if (root == nullptr || static_cast<int>(root->getProperty("schemaVersion")) != 1)
        return false;

    auto* controls = root->getProperty("controls").getDynamicObject();

    player_.stop();
    player_.clearSources();
    engine_.clear();
    musicUndoStack_.clear();
    musicClipBox_.clear(juce::dontSendNotification);
    spectrumView_.setSpectrum({});
    spectrumView_.setProcessedSpectrum({});
    updateMusicTimeline();
    playButton_.setEnabled(false);
    listenButton_.setEnabled(false);
    exportButton_.setEnabled(false);
    noiseReductionSlider_.setEnabled(false);

    restoringProject_ = true;
    setSliderIfPresent(toneSlider_, controls, "tone");
    setSliderIfPresent(noiseReductionSlider_, controls, "noiseReduction");
    setSliderIfPresent(strengthSlider_, controls, "intensity");
    setSliderIfPresent(fastThresholdSlider_, controls, "fastThreshold");
    setSliderIfPresent(fastRatioSlider_, controls, "fastRatio");
    setSliderIfPresent(glueThresholdSlider_, controls, "glueThreshold");
    setSliderIfPresent(glueRatioSlider_, controls, "glueRatio");
    setSliderIfPresent(targetPreChainSlider_, controls, "targetPreChain");
    setSliderIfPresent(deEssFreqSlider_, controls, "deEssFreq");
    setSliderIfPresent(deEssThresholdSlider_, controls, "deEssThreshold");
    setSliderIfPresent(deEssPresenceSlider_, controls, "deEssPresence");
    setSliderIfPresent(deEssRatioSlider_, controls, "deEssRatio");
    setSliderIfPresent(deEssRangeSlider_, controls, "deEssRange");
    setSliderIfPresent(musicMasterVolumeSlider_, controls, "musicMasterGain");
    engine_.setMusicMasterGainDb(musicMasterVolumeSlider_.getValue());
    player_.setMusicMasterGainDb(musicMasterVolumeSlider_.getValue());
    setSliderIfPresent(duckLookAheadSlider_, controls, "duckLookAhead");
    setSliderIfPresent(duckReductionSlider_, controls, "duckReduction");
    setSliderIfPresent(duckFilterSlider_, controls, "duckBlend");
    player_.setDuckLookAheadMs(duckLookAheadSlider_.getValue());
    player_.setDuckReductionDb(duckReductionSlider_.getValue());
    player_.setDuckBlend(duckFilterSlider_.getValue() / 100.0);

    pendingMusicClipRestore_.clear();
    pendingSelectedMusicClipRestore_ = static_cast<int>(numberProperty(root, "selectedMusicClip", -1.0));
    const auto musicClipState = root->getProperty("musicClips");
    if (musicClipState.isArray()) {
        for (const auto& item : *musicClipState.getArray()) {
            auto* clipObj = item.getDynamicObject();
            const auto clipPath = stringProperty(clipObj, "sourcePath");
            if (clipPath.isEmpty())
                continue;

            PendingMusicClipRestore clip;
            clip.file = juce::File(clipPath);
            clip.startSeconds = numberProperty(clipObj, "startSeconds", clip.startSeconds);
            clip.sourceOffsetSeconds = numberProperty(clipObj, "sourceOffsetSeconds", clip.sourceOffsetSeconds);
            clip.lengthSeconds = numberProperty(clipObj, "lengthSeconds", clip.lengthSeconds);
            clip.gainDb = numberProperty(clipObj, "gainDb", clip.gainDb);
            clip.fadeInSeconds = numberProperty(clipObj, "fadeInSeconds", clip.fadeInSeconds);
            clip.fadeOutSeconds = numberProperty(clipObj, "fadeOutSeconds", clip.fadeOutSeconds);
            pendingMusicClipRestore_.push_back(std::move(clip));
        }
    }

    const auto sourcePath = stringProperty(root, "sourcePath");
    if (sourcePath.isNotEmpty()) {
        const juce::File source(sourcePath);
        if (source.existsAsFile()) {
            loadFile(source);
        } else {
            setUiBusy(false, "Project source is missing: " + sourcePath);
            restoringProject_ = false;
            pendingMusicClipRestore_.clear();
            pendingSelectedMusicClipRestore_ = -1;
        }
    } else {
        restoringProject_ = false;
        pendingMusicClipRestore_.clear();
        pendingSelectedMusicClipRestore_ = -1;
        setUiBusy(false, "Project opened.");
    }

    const auto projectPath = stringProperty(root, "projectPath");
    if (projectPath.isNotEmpty())
        currentProjectFile_ = juce::File(projectPath);

    projectDirty_ = false;
    autosaveCountdown_ = 0;
    updateMainMenu();
    return true;
}

bool MainComponent::hasProjectContent() const {
    return engine_.hasAudio() || !engine_.musicClips().empty();
}

bool MainComponent::maybeSaveBeforeReplacingSession() {
    // Prompt whenever there is real work that isn't safely stored in a named
    // project file: either it changed since the last save (dirty), or it has
    // never been saved to a .vcproj at all (only sitting in the autosave).
    const bool unsaved = projectDirty_ || currentProjectFile_ == juce::File();
    if (!unsaved || !hasProjectContent())
        return true;
    const int result = juce::AlertWindow::showYesNoCancelBox(
        juce::AlertWindow::QuestionIcon,
        "Save current project?",
        "Do you want to save the current session before starting a new one?",
        "Save", "Don't Save", "Cancel", this, nullptr);
    if (result == 0)
        return false;
    if (result == 1) {
        if (currentProjectFile_ == juce::File()) {
            saveAsNewProject();
            return false;
        }
        saveAutosaveSession();
        projectDirty_ = false;
        return true;
    }
    return true;
}

void MainComponent::clearProject() {
    player_.stop();
    player_.clearSources();
    engine_.clear();
    pendingMusicClipRestore_.clear();
    pendingSelectedMusicClipRestore_ = -1;
    currentProjectFile_ = juce::File();
    projectDirty_ = false;
    autosaveCountdown_ = 0;
    intensityMinLoudnessRef_ = 0.0;
    intensityMaxLoudnessRef_ = 0.0;
    spectrumView_.setSpectrum({});
    spectrumView_.setProcessedSpectrum({});
    musicUndoStack_.clear();
    musicClipBox_.clear(juce::dontSendNotification);
    updateMusicTimeline();
    resetProDefaults();
    toneSlider_.setValue(0.0, juce::dontSendNotification);
    noiseReductionSlider_.setValue(75.0, juce::dontSendNotification);
    strengthSlider_.setValue(100.0, juce::dontSendNotification);
    playButton_.setEnabled(false);
    listenButton_.setEnabled(false);
    exportButton_.setEnabled(false);
    noiseReductionSlider_.setEnabled(false);
    dropArea_.setStatus("Add voice audio or video\nDrag here, or click to browse");
    setUiBusy(false, "New project.");
    saveAutosaveSession();
    updateMainMenu();
}

void MainComponent::newProject() {
    if (!maybeSaveBeforeReplacingSession())
        return;
    promptForProjectName("New Project", "Untitled", [this](juce::String name) {
        clearProject();
        currentProjectFile_ = uniqueProjectFile(projectsFolder(), name);
        saveAutosaveSession(); // writes the new (empty) project file
        updateMainMenu();
        statusLabel_.setText("Created project: " + currentProjectFile_.getFileNameWithoutExtension(),
                             juce::dontSendNotification);
    });
}

void MainComponent::promptForProjectName(const juce::String& title, const juce::String& defaultName,
                                         std::function<void(juce::String)> onName) {
    auto aw = std::make_shared<juce::AlertWindow>(title, "Project name:",
                                                  juce::AlertWindow::NoIcon, this);
    aw->addTextEditor("name", defaultName);
    aw->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    aw->enterModalState(true,
        juce::ModalCallbackFunction::create([aw, cb = std::move(onName)](int result) {
            const auto entered = aw->getTextEditorContents("name").trim();
            aw->exitModalState(result);
            aw->setVisible(false);
            if (result == 1 && cb)
                cb(entered.isNotEmpty() ? entered : "Untitled");
        }), false);
}

void MainComponent::openProjectManager() {
    auto content = std::make_unique<ProjectManagerComponent>(projectsFolder(), currentProjectFile_);
    auto* pm = content.get();
    pm->onNew = [this] { closeProjectManager(); newProject(); };
    pm->onOpen = [this](juce::File f) { closeProjectManager(); openProjectFile(f); };
    pm->onDelete = [this, pm](juce::File f) {
        deleteProjectFile(f);
        pm->setCurrent(currentProjectFile_);
    };
    pm->onClose = [this] { closeProjectManager(); };

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(content.release());
    o.dialogTitle = "Projects";
    o.dialogBackgroundColour = juce::Colour(0xff20232a);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    projectManagerWindow_.reset(o.create());
    projectManagerWindow_->centreWithSize(440, 360);
    projectManagerWindow_->setVisible(true);
}

void MainComponent::closeProjectManager() {
    // Defer destruction: callbacks fire from inside the dialog's own components.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
        if (safe != nullptr)
            safe->projectManagerWindow_.reset();
    });
}

void MainComponent::openProjectFile(const juce::File& file) {
    if (!file.existsAsFile())
        return;
    if (!maybeSaveBeforeReplacingSession())
        return;
    juce::var parsed;
    juce::JSON::parse(file.loadFileAsString(), parsed);
    currentProjectFile_ = file;
    if (!applyProjectState(parsed, false))
        statusLabel_.setText("Could not open project.", juce::dontSendNotification);
}

void MainComponent::deleteProjectFile(const juce::File& file) {
    const bool wasCurrent = (file == currentProjectFile_);
    file.deleteFile();
    if (wasCurrent)
        clearProject();
    statusLabel_.setText("Deleted project: " + file.getFileNameWithoutExtension(),
                         juce::dontSendNotification);
}

void MainComponent::saveCurrentProject() {
    if (currentProjectFile_ == juce::File()) {
        saveAsNewProject();
        return;
    }
    saveAutosaveSession();
    projectDirty_ = false;
    autosaveCountdown_ = 0;
    statusLabel_.setText("Saved: " + currentProjectFile_.getFileNameWithoutExtension(),
                         juce::dontSendNotification);
}

void MainComponent::saveAsNewProject() {
    // Save the current editor content (not cleared) into a freshly named project.
    promptForProjectName("Save Project", "Untitled", [this](juce::String name) {
        currentProjectFile_ = uniqueProjectFile(projectsFolder(), name);
        saveAutosaveSession();
        projectDirty_ = false;
        autosaveCountdown_ = 0;
        updateMainMenu();
        statusLabel_.setText("Saved: " + currentProjectFile_.getFileNameWithoutExtension(),
                             juce::dontSendNotification);
    });
}

void MainComponent::markProjectDirty() {
    if (restoringProject_)
        return;
    projectDirty_ = true;
    autosaveCountdown_ = 15;
}

void MainComponent::saveAutosaveSession() {
    const auto state = juce::JSON::toString(makeProjectState());
    // Crash/relaunch restore of the latest editor state (named or not)...
    autosaveProjectFile().replaceWithText(state);
    // ...and continuously persist named projects to their own file so the user
    // never has to think about saving.
    if (currentProjectFile_ != juce::File())
        currentProjectFile_.replaceWithText(state);
}

void MainComponent::restoreAutosaveSession() {
    const auto file = autosaveProjectFile();
    if (!file.existsAsFile())
        return;
    juce::var parsed;
    juce::JSON::parse(file.loadFileAsString(), parsed);
    applyProjectState(parsed, true);
}

void MainComponent::updateEqView() {
    const auto p = buildParams();
    if (engine_.hasAudio())
        spectrumView_.setSpectrum(engine_.previewSpectrum(p.noiseReductionAmount));
    spectrumView_.setEq(vc::fullEqBands(p), p.highpassHz, engine_.sampleRate());
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    if (updatingDevice_)
        return;

    if (settingsWindow_ != nullptr)
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

void MainComponent::updateMusicSpectrum() {
    const int fftSize = 1 << kFftOrder;
    player_.readMusicAnalysisBlock(analysisScratch_.data(), fftSize);

    for (int i = 0; i < fftSize; ++i)
        fftData_[static_cast<std::size_t>(i)] =
            analysisScratch_[static_cast<std::size_t>(i)] * fftWindow_[static_cast<std::size_t>(i)];
    std::fill(fftData_.begin() + fftSize, fftData_.end(), 0.0f);

    fft_.performFrequencyOnlyForwardTransform(fftData_.data());

    const int half = fftSize / 2;
    for (int k = 0; k < half; ++k) {
        const float mag = fftData_[static_cast<std::size_t>(k)];
        const float db = 20.0f * std::log10(mag + 1e-9f);
        float& s = musicDb_[static_cast<std::size_t>(k)];
        s += (db > s ? 0.5f : 0.2f) * (db - s); // fast rise, slower fall
        musicResult_.binDb[static_cast<std::size_t>(k)] = s;
    }
    musicResult_.sampleRate = currentDeviceRate();
    musicResult_.valid = true;
    duckView_.setMusicSpectrum(musicResult_);
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
    musicTimeline_.setVoiceNoiseReduction(static_cast<float>(params.noiseReductionAmount));
    updateEqView();
    markProjectDirty();
}

void MainComponent::requestProcessedSpectrumUpdate() {
    if (!engine_.hasAudio())
        return;
    spectrumView_.invalidateProcessedSpectrumPreview();
    processedSpectrumRequest_.fetch_add(1, std::memory_order_relaxed);
}

void MainComponent::startProcessedSpectrumUpdateIfNeeded() {
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

void MainComponent::restorePendingMusicClipsAfterVoiceLoad(const juce::File& voiceFile,
                                                           const juce::String& loadedMessage) {
    if (pendingMusicClipRestore_.empty()) {
        restoringProject_ = false;
        projectDirty_ = false;
        saveAutosaveSession();
        setUiBusy(false, loadedMessage);
        return;
    }

    auto clipsToRestore = pendingMusicClipRestore_;
    const int selectedClip = pendingSelectedMusicClipRestore_;
    pendingMusicClipRestore_.clear();
    pendingSelectedMusicClipRestore_ = -1;

    runOnWorker(
        "Restoring backing music...",
        [this, clipsToRestore]() -> juce::String {
            for (const auto& saved : clipsToRestore) {
                if (!saved.file.existsAsFile())
                    continue;

                juce::String err;
                if (!engine_.addMusicClip(saved.file, err))
                    continue;

                const int index = static_cast<int>(engine_.musicClips().size()) - 1;
                engine_.setMusicClipParams(index, saved.startSeconds, saved.sourceOffsetSeconds,
                                           saved.gainDb, saved.fadeInSeconds, saved.fadeOutSeconds,
                                           saved.lengthSeconds);
            }

            return {};
        },
        [this, voiceFile, loadedMessage, selectedClip]() {
            player_.setMusicMasterGainDb(engine_.musicMasterGainDb());
            refreshMusicClipList();
            const int count = static_cast<int>(engine_.musicClips().size());
            if (count > 0)
                musicClipBox_.setSelectedId(juce::jlimit(1, count, selectedClip + 1),
                                            juce::sendNotification);
            updateMusicTimeline();

            restoringProject_ = false;
            projectDirty_ = false;
            saveAutosaveSession();
            setUiBusy(false, loadedMessage + " Backing music restored.");
            dropArea_.setStatus(voiceFile.getFileName());
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
    engine_.setMusicClips({});
    player_.setMusicClips({});
    musicUndoStack_.clear();
    musicClipBox_.clear(juce::dontSendNotification);
    updateMusicTimeline();
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
                measuredParams.autoEqBands = engine_.autoEqBands(measuredParams.baseAutoEqStrength);
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
            const auto loadedMessage = juce::String::formatted(
                "Loaded \"%s\"  -  denoising in background, input %.1f LUFS, target %.0f LUFS.",
                file.getFileName().toRawUTF8(), engine_.inputLufs(), target);
            if (restoringProject_) {
                restorePendingMusicClipsAfterVoiceLoad(file, loadedMessage);
                return;
            } else {
                markProjectDirty();
            }
            saveAutosaveSession();
            setUiBusy(false, loadedMessage);
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
                    markProjectDirty();
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
    markProjectDirty();
}

void MainComponent::doExport() {
    if (!engine_.hasAudio()) return;

    auto* content = new ExportComponent(engine_.sourceFile(), engine_.sourceHasVideo());
    content->onExport = [this](juce::File out, bool muxVideo) { performExport(out, muxVideo); };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content);
    options.dialogTitle = "Export";
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour(0xff2b2f36);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::performExport(juce::File out, bool muxVideo) {
    if (out == juce::File()) return;

    const auto params = buildParams();
    runOnWorker(
        "Exporting (exact offline render)...",
        [this, out, params, muxVideo]() -> juce::String {
            juce::String err;
            if (!engine_.exportTo(out, params, muxVideo, err))
                return err.isEmpty() ? "export failed" : err;
            return {};
        },
        [this, out]() { setUiBusy(false, "Exported: " + out.getFullPathName()); });
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
    if (mods.isCtrlDown() || mods.isCommandDown()) {
        if (key.getKeyCode() == 'n' || key.getKeyCode() == 'N') {
            newProject();
            return true;
        }
        if (key.getKeyCode() == 'o' || key.getKeyCode() == 'O') {
            openProjectManager();
            return true;
        }
        if (key.getKeyCode() == 's' || key.getKeyCode() == 'S') {
            saveCurrentProject();
            return true;
        }
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
    compMeter_.setReductions(playing ? player_.glueCompReductionDb() : 0.0f,
                             playing ? player_.fastCompReductionDb() : 0.0f);
    deEssMeter_.setReduction(playing ? player_.deEssReductionDb() : 0.0f);
    limiterMeter_.setReduction(playing ? player_.limiterReductionDb() : 0.0f);
    vuMeter_.setLevels(playing ? player_.rmsLevelDb() : -60.0f,
                       playing ? player_.peakLevelDb() : -60.0f);

    // Music-output / duck display.
    std::array<float, 512> musicWave;
    if (playing)
        player_.readMusicAnalysisBlock(musicWave.data(), static_cast<int>(musicWave.size()));
    else
        musicWave.fill(0.0f);
    duckView_.setMusicWaveform(musicWave.data(), static_cast<int>(musicWave.size()));
    duckView_.setDuckState(playing ? player_.musicDuckReductionDb() : 0.0f,
                           static_cast<float>(duckFilterSlider_.getValue() / 100.0));
    if (playing)
        updateMusicSpectrum();
    else
        duckView_.setMusicSpectrum({});

    // Animate the spectrum to the playing audio; revert to the average when idle.
    if (playing)
        updateLiveSpectrum();
    else
        spectrumView_.setShowLive(false);

    if (engine_.processMusicWaveformChunks(256))
        updateMusicTimeline();

    spectrumView_.tickAnimation();
    musicTimeline_.tickAnimation();
    startProcessedSpectrumUpdateIfNeeded();

    const double playheadSeconds = engine_.sampleRate() > 0.0
        ? player_.getPositionNormalised()
            * static_cast<double>(engine_.beforeBuffer().getNumSamples()) / engine_.sampleRate()
        : 0.0;
    musicTimeline_.setPlayheadSeconds(playheadSeconds);

    // Steer the background denoiser toward what's playing.
    if (engine_.hasAudio())
        engine_.setPlayheadFrame(player_.currentSourceFrame());

    if (autosaveCountdown_ > 0 && --autosaveCountdown_ == 0)
        saveAutosaveSession();

    if (engine_.hasAudio() && !busy_.load(std::memory_order_relaxed)
        && engine_.refreshVoiceProfileFromDenoised()) {
        spectrumView_.setSpectrum(engine_.spectrum());
        updateEqView();
        updateMusicTimeline();
        applyParamsLive();
        statusLabel_.setText("Voice balance updated from denoised vocal profile.",
                             juce::dontSendNotification);
    } else if (engine_.hasAudio() && !busy_.load(std::memory_order_relaxed)
               && !engine_.voiceProfileUsesDenoised()) {
        statusLabel_.setText("Preparing voice profile in background...",
                             juce::dontSendNotification);
    }
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff20232a));
}

void MainComponent::resized() {
    auto r = getLocalBounds().reduced(16);

    // Live gain-reduction meters.
    compMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    deEssMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    limiterMeter_.setBounds(r.removeFromTop(20));
    r.removeFromTop(2);
    vuMeter_.setBounds(r.removeFromTop(24));
    r.removeFromTop(6);

    auto placeEncoder = [](juce::Rectangle<int> area, juce::Label& label, juce::Slider& slider,
                           int labelHeight = 20) {
        label.setBounds(area.removeFromTop(labelHeight));
        slider.setBounds(area);
    };

    auto encoderRow = r.removeFromTop(138);

    // Play/bypass hug the left edge and Export the right edge (respecting only
    // the shared outer margin); the three encoders are then spread evenly across
    // the space that remains in between.
    constexpr int buttonColWidth = 108;
    constexpr int encoderWidth = 156;
    constexpr int exportWidth = 104;

    auto mainTransport = encoderRow.removeFromLeft(buttonColWidth).reduced(2, 4);
    playButton_.setBounds(mainTransport.removeFromTop(82));
    mainTransport.removeFromTop(8);
    listenButton_.setBounds(mainTransport.removeFromTop(36));

    // Export sits flush right and spans the full combined height of the
    // play + bypass column.
    auto exportArea = encoderRow.removeFromRight(exportWidth).reduced(2, 4);
    exportButton_.setBounds(exportArea);

    // Distribute the three encoders evenly within the remaining middle band:
    // four equal gaps (before, between, between, after).
    const int encoderGap = juce::jmax(0, (encoderRow.getWidth() - encoderWidth * 3) / 4);
    encoderRow.removeFromLeft(encoderGap);
    auto strengthArea = encoderRow.removeFromLeft(encoderWidth);
    encoderRow.removeFromLeft(encoderGap);
    auto toneArea = encoderRow.removeFromLeft(encoderWidth);
    encoderRow.removeFromLeft(encoderGap);
    auto noiseArea = encoderRow.removeFromLeft(encoderWidth);
    placeEncoder(strengthArea, strengthCaption_, strengthSlider_);
    placeEncoder(toneArea, toneCaption_, toneSlider_);
    placeEncoder(noiseArea, noiseReductionCaption_, noiseReductionSlider_);
    r.removeFromTop(8);

    spectrumView_.setBounds(r.removeFromTop(kSpectrumHeight));
    r.removeFromTop(6);

    auto timelineBounds = r.removeFromTop(216);
    musicTimeline_.setBounds(timelineBounds);
    // The voice drop field covers only the voice lane (top), leaving the music
    // lane below clickable so its own "+" adds a music clip. Matches
    // MusicTimeline's internal lane layout: reduced(10), 96px voice lane.
    auto voiceLane = timelineBounds.reduced(10).removeFromTop(96);
    dropArea_.setBounds(voiceLane);
    dropArea_.setVisible(analyzingMedia_ || !engine_.hasAudio());
    dropArea_.toFront(false);
    r.removeFromTop(8);

    // Backing-music section, laid out columns-first: a left column of music
    // volume controls, a right column of ducking controls, and a middle column
    // that gives the duck/oscilloscope screen the section's full height (caption
    // band included) instead of leaving dead space above it.
    auto musicArea = r.removeFromTop(116);
    constexpr int knobWidth = 80;
    constexpr int labelHeight = 16; // compact labels for this section
    constexpr int captionH = 20;

    auto leftCol = musicArea.removeFromLeft(knobWidth * 2);
    auto rightCol = musicArea.removeFromRight(knobWidth * 3);
    auto screenCol = musicArea; // full-height middle column for the screen

    // Left column: "Backing Music" + Mute on top, two volume knobs below.
    auto leftCaption = leftCol.removeFromTop(captionH);
    musicMuteButton_.setBounds(leftCaption.removeFromLeft(50).withSizeKeepingCentre(48, 18));
    leftCaption.removeFromLeft(6);
    musicCaption_.setBounds(leftCaption);
    placeEncoder(leftCol.removeFromLeft(knobWidth).reduced(4, 0),
                 musicMasterVolumeLabel_, musicMasterVolumeSlider_, labelHeight);
    placeEncoder(leftCol.reduced(4, 0),
                 musicVolumeLabel_, musicVolumeSlider_, labelHeight);

    // Right column: Bypass mirrors Mute on the far side; "Ducking" + 3 knobs.
    auto rightCaption = rightCol.removeFromTop(captionH);
    duckBypassButton_.setBounds(rightCaption.removeFromRight(58).withSizeKeepingCentre(56, 18));
    rightCaption.removeFromRight(6);
    duckCaption_.setBounds(rightCaption);
    placeEncoder(rightCol.removeFromLeft(knobWidth).reduced(4, 0),
                 duckLookAheadLabel_, duckLookAheadSlider_, labelHeight);
    placeEncoder(rightCol.removeFromLeft(knobWidth).reduced(4, 0),
                 duckReductionLabel_, duckReductionSlider_, labelHeight);
    placeEncoder(rightCol.reduced(4, 0),
                 duckFilterLabel_, duckFilterSlider_, labelHeight);

    // Middle column: the screen spans the full section height.
    duckView_.setBounds(screenCol.reduced(8, 4));
    r.removeFromTop(8);

    // The Pro controls now live in the settings window (see SettingsComponent),
    // so resized() no longer lays them out here.
    progressBar_.setBounds(r.removeFromTop(18));
}
