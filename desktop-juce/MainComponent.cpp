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
constexpr int kWaveformSectionGap = 4;               // EQ↔voice and voice↔music
constexpr int kVoiceLaneHeight = 96;
constexpr int kMusicLaneHeight = 88;
constexpr int kTimelineHeight = kVoiceLaneHeight + kWaveformSectionGap + kMusicLaneHeight;
constexpr int kGrMeterRowHeight = 18;
constexpr int kOutputMeterRowHeight = 20;
constexpr int kMeterRowGap = 2;
constexpr int kMetersBottomGap = 6;
constexpr int kMetersSectionHeight = kGrMeterRowHeight + kMeterRowGap
    + kGrMeterRowHeight + kMeterRowGap
    + kGrMeterRowHeight + kMeterRowGap
    + kOutputMeterRowHeight + kMetersBottomGap;
constexpr int kMusicToggleBarHeight = 26;            // "Add background music" bar
constexpr int kMusicSlotWidth = 80;
constexpr int kMusicSideColWidth = kMusicSlotWidth * 3; // three pot slots per side
constexpr int kMusicControlsHeight = 116;            // background-music controls row

// The background-music section folds away by default. Everything above it is
// common to both states; only the bottom block differs.
constexpr int kCommonWindowHeight = kOuterMargin * 2
    + kMetersSectionHeight                           // meters (matches resized())
    + (138 + 8)                                      // main controls
    + kSpectrumHeight + kWaveformSectionGap          // spectrum + gap to voice lane
    + (18)                                           // progress
    + kBottomBreathingRoom;
// Voice only: timeline shows the voice lane and nothing music-related at all
// (the "Always off" preference).
constexpr int kVoiceOnlyWindowHeight = kCommonWindowHeight
    + (kVoiceLaneHeight + 8);
// Collapsed: voice-lane-only timeline plus the slim disclosure bar.
constexpr int kCollapsedWindowHeight = kVoiceOnlyWindowHeight
    + (kMusicToggleBarHeight + 8);
// Expanded: full timeline (voice + music lanes) and the controls row. The
// disclosure bar disappears entirely — a small "Hide" button in the section
// header takes over, so no vertical space is wasted on a full-width bar.
constexpr int kExpandedWindowHeight = kCommonWindowHeight
    + (kTimelineHeight + 8)
    + (kMusicControlsHeight + 8);

juce::File appDataDir() {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Voice Control");
    dir.createDirectory();
    return dir;
}

// App-wide, cross-session preferences (distinct from per-project state).
juce::PropertiesFile& globalPrefs() {
    static juce::PropertiesFile prefs(appDataDir().getChildFile("Preferences.settings"),
                                      juce::PropertiesFile::Options{});
    return prefs;
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

std::vector<float> floatVectorProperty(const juce::DynamicObject* obj, const juce::Identifier& id) {
    std::vector<float> out;
    if (obj == nullptr || !obj->hasProperty(id))
        return out;
    const auto value = obj->getProperty(id);
    if (!value.isArray())
        return out;
    const auto* arr = value.getArray();
    out.reserve(static_cast<std::size_t>(arr->size()));
    for (const auto& v : *arr)
        out.push_back(static_cast<float>(static_cast<double>(v)));
    return out;
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

// Scrollable body for the User Guide window; keeps the editor inset on resize.
class HelpContent : public juce::Component {
public:
    void setBody(std::unique_ptr<juce::TextEditor> body) {
        body_ = std::move(body);
        addAndMakeVisible(*body_);
        resized();
    }
    void resized() override {
        if (body_ != nullptr)
            body_->setBounds(getLocalBounds().reduced(12));
    }
private:
    std::unique_ptr<juce::TextEditor> body_;
};

const juce::Colour kSectionOffText { 0xff5c636d };
const juce::Colour kSectionOffTextReadable { 0xff9aa3ad };
constexpr float kSectionDisabledLabelAlpha = 0.45f;
constexpr float kSectionDisabledLabelAlphaSoft = 0.72f;

void applyEncoderSectionStyle(bool active,
                              juce::Label* caption,
                              std::initializer_list<juce::Label*> labels,
                              juce::Colour activeLabelCol,
                              float inactiveAlpha = kSectionDisabledLabelAlpha) {
    if (caption != nullptr) {
        caption->setEnabled(true);
        caption->setColour(juce::Label::textColourId, active ? activeLabelCol : kSectionOffTextReadable);
        caption->setAlpha(active ? 1.0f : inactiveAlpha);
    }
    for (auto* label : labels) {
        label->setEnabled(true);
        label->setColour(juce::Label::textColourId, active ? activeLabelCol : kSectionOffTextReadable);
        label->setAlpha(active ? 1.0f : inactiveAlpha);
    }
}
}

void MainComponent::EncoderLookAndFeel::drawRotarySlider(
    juce::Graphics& g, int x, int y, int width, int height, float sliderPosProportional,
    float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) {
    const bool compact = compactSliders_.count(&slider) > 0;
    const bool softOff = !slider.isEnabled() && softDisabledSliders_.count(&slider) > 0;
    const float inset = compact ? 4.0f : 9.0f;
    const float strokeWidth = compact ? 3.5f : 6.0f;
    const float radiusTrim = compact ? 3.0f : 7.0f;
    const float markerRadius = compact ? 3.0f : 4.5f;

    const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height)
        .reduced(inset);
    const auto radius = juce::jmax(4.0f, juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - radiusTrim);
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const auto stroke = juce::PathStrokeType(strokeWidth, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded);
    const auto green = juce::Colour(0xff6ee07a);

    juce::Path inactiveArc;
    inactiveArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                              rotaryStartAngle, rotaryEndAngle, true);
    const bool enabled = slider.isEnabled();
    const float inactiveArcAlpha = enabled ? 0.24f : (softOff ? 0.14f : 0.08f);
    const float activeArcAlpha = enabled ? 0.95f : (softOff ? 0.32f : 0.18f);
    const float hubRingAlpha = enabled ? 0.14f : (softOff ? 0.09f : 0.05f);
    const float markerFillAlpha = enabled ? 1.0f : (softOff ? 0.42f : 0.22f);
    const float markerStrokeAlpha = enabled ? 1.0f : (softOff ? 0.42f : 0.22f);
    g.setColour(green.withAlpha(inactiveArcAlpha));
    g.strokePath(inactiveArc, stroke);

    juce::Path activeArc;
    activeArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                            rotaryStartAngle, angle, true);
    g.setColour(green.withAlpha(activeArcAlpha));
    g.strokePath(activeArc, stroke);

    g.setColour(juce::Colour(0xff15181d));
    g.fillEllipse(bounds.withSizeKeepingCentre(radius * 1.5f, radius * 1.5f));
    g.setColour(juce::Colours::white.withAlpha(hubRingAlpha));
    g.drawEllipse(bounds.withSizeKeepingCentre(radius * 1.5f, radius * 1.5f), 1.0f);

    const auto markerDistance = radius;
    const auto marker = juce::Point<float>(
        centre.x + std::cos(angle - juce::MathConstants<float>::halfPi) * markerDistance,
        centre.y + std::sin(angle - juce::MathConstants<float>::halfPi) * markerDistance);
    g.setColour(juce::Colour(0xffeaffed).withAlpha(markerFillAlpha));
    g.fillEllipse(marker.x - markerRadius, marker.y - markerRadius,
                  markerRadius * 2.0f, markerRadius * 2.0f);
    g.setColour(green.withAlpha(markerStrokeAlpha));
    g.drawEllipse(marker.x - markerRadius, marker.y - markerRadius,
                  markerRadius * 2.0f, markerRadius * 2.0f, compact ? 1.0f : 1.4f);
}

juce::Font MainComponent::EncoderLookAndFeel::getSliderPopupFont(juce::Slider& slider) {
    if (compactSliders_.count(&slider) > 0)
        return juce::Font(juce::FontOptions(10.0f));
    return juce::LookAndFeel_V4::getSliderPopupFont(slider);
}

juce::Font MainComponent::EncoderLookAndFeel::getLabelFont(juce::Label& label) {
    for (auto* parent = label.getParentComponent(); parent != nullptr;
         parent = parent->getParentComponent()) {
        if (auto* slider = dynamic_cast<juce::Slider*>(parent)) {
            if (compactSliders_.count(slider) > 0)
                return juce::Font(juce::FontOptions(10.0f));
            break;
        }
    }
    return juce::LookAndFeel_V4::getLabelFont(label);
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
        maybeSaveBeforeReplacingSession([this, f](bool proceed) {
            if (proceed)
                loadFile(f);
        });
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

    fastThresholdLabel_.setText("Peak Thr", juce::dontSendNotification);
    fastRatioLabel_.setText("Peak Ratio", juce::dontSendNotification);
    glueThresholdLabel_.setText("Glue Thr", juce::dontSendNotification);
    glueRatioLabel_.setText("Glue Ratio", juce::dontSendNotification);
    targetPreChainLabel_.setText("Target", juce::dontSendNotification);
    deEssFreqLabel_.setText("Freq", juce::dontSendNotification);
    deEssThresholdLabel_.setText("Threshold", juce::dontSendNotification);
    deEssPresenceLabel_.setText("Presence", juce::dontSendNotification);
    deEssRatioLabel_.setText("Ratio", juce::dontSendNotification);
    deEssRangeLabel_.setText("Range", juce::dontSendNotification);

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

    for (auto* label : { &fastThresholdLabel_, &fastRatioLabel_, &glueThresholdLabel_, &glueRatioLabel_,
                         &targetPreChainLabel_, &deEssFreqLabel_, &deEssThresholdLabel_,
                         &deEssPresenceLabel_, &deEssRatioLabel_, &deEssRangeLabel_ }) {
        label->setFont(juce::Font(juce::FontOptions(10.0f)));
        label->setJustificationType(juce::Justification::centred);
    }
    for (auto* slider : { &fastThresholdSlider_, &fastRatioSlider_, &glueThresholdSlider_,
                          &glueRatioSlider_, &targetPreChainSlider_, &deEssFreqSlider_,
                          &deEssThresholdSlider_, &deEssPresenceSlider_, &deEssRatioSlider_,
                          &deEssRangeSlider_ }) {
        encoderLookAndFeel_.markCompact(*slider);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 16);
    }

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

    musicCaption_.setText("Background", juce::dontSendNotification);
    musicCaption_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(musicCaption_);

    // Small mute toggle at the start of the Background Music section: silences the
    // music channel so the voice can be auditioned alone.
    musicMuteButton_.setClickingTogglesState(true);
    musicMuteButton_.setTooltip("Mute background music (voice only)");
    musicMuteButton_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb24a4a));
    musicMuteButton_.onClick = [this] {
        player_.setMusicMuted(musicMuteButton_.getToggleState());
        updateMusicMuteUi();
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
    musicMasterVolumeLabel_.setText("Volume", juce::dontSendNotification);
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
    duckCaption_.setText("Auto volume", juce::dontSendNotification);
    duckCaption_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(duckCaption_);

    // Ducking on/off: off by default; when off controls gray out and the duck
    // curve hides, but the monitor and knobs stay in the layout.
    duckOnOffButton_.setClickingTogglesState(true);
    duckOnOffButton_.setToggleState(false, juce::dontSendNotification);
    duckOnOffButton_.setTooltip("Enable auto volume (duck music under the voice)");
    duckOnOffButton_.onClick = [this] {
        updateDuckingUi();
        markProjectDirty();
    };
    addAndMakeVisible(duckOnOffButton_);

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
    // the main ones: compact caption fonts and tighter value boxes.
    for (auto* label : { &musicMasterVolumeLabel_, &musicVolumeLabel_,
                         &duckLookAheadLabel_, &duckReductionLabel_, &duckFilterLabel_ }) {
        label->setFont(juce::Font(juce::FontOptions(10.0f)));
        label->setJustificationType(juce::Justification::centred);
    }
    for (auto* slider : { &musicMasterVolumeSlider_, &musicVolumeSlider_,
                          &duckLookAheadSlider_, &duckReductionSlider_, &duckFilterSlider_ }) {
        encoderLookAndFeel_.markCompact(*slider);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, 18);
    }
    for (auto* slider : { &duckLookAheadSlider_, &duckReductionSlider_, &duckFilterSlider_ })
        encoderLookAndFeel_.markSoftDisabled(*slider);

    addAndMakeVisible(duckView_);

    // The background-music section folds away by default so the simple "clean up
    // a voice file" flow stays uncluttered. Collapsed, a slim full-width bar
    // reveals it; expanded, the bar is gone and a small "Hide" button in the
    // section header folds it back so no vertical space is wasted.
    musicSectionToggle_.setWantsKeyboardFocus(false);
    musicSectionToggle_.setTooltip("Show the background-music timeline and controls");
    musicSectionToggle_.onClick = [this] { setMusicSectionExpanded(true); };
    addAndMakeVisible(musicSectionToggle_);

    musicHideButton_.setWantsKeyboardFocus(false);
    musicHideButton_.setTooltip("Hide the background-music section");
    musicHideButton_.onClick = [this] { setMusicSectionExpanded(false); };
    addChildComponent(musicHideButton_);

    // Background-music behaviour preference (lives in the settings General tab;
    // configured here, re-parented when the settings window opens).
    musicSectionModeLabel_.setText("Background music", juce::dontSendNotification);
    musicSectionModeBox_.addItem("Foldable (show a button)", 1);
    musicSectionModeBox_.addItem("Always shown", 2);
    musicSectionModeBox_.addItem("Always hidden", 3);
    musicSectionModeBox_.setTooltip("Choose whether the background-music section is "
                                    "foldable, always shown, or always hidden");
    musicSectionMode_ = static_cast<MusicSectionMode>(juce::jlimit(
        0, 2, globalPrefs().getIntValue("musicSectionMode",
                                        static_cast<int>(MusicSectionMode::Foldable))));
    musicSectionModeBox_.setSelectedId(static_cast<int>(musicSectionMode_) + 1,
                                       juce::dontSendNotification);
    musicSectionModeBox_.onChange = [this] {
        setMusicSectionMode(static_cast<MusicSectionMode>(musicSectionModeBox_.getSelectedId() - 1));
    };

    // Help: the "?" button mirrors the Help menu's "User Guide".
    helpButton_.setWantsKeyboardFocus(false);
    helpButton_.setTooltip("Open the user guide");
    helpButton_.onClick = [this] { openHelp(); };
    addAndMakeVisible(helpButton_);

    captureSectionChromeBaselines();
    updateMusicMuteUi();
    updateDuckingUi();
    applyMusicSectionMode();

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
    const int initialHeight = musicSectionExpanded_ ? kExpandedWindowHeight
        : (musicSectionMode_ == MusicSectionMode::AlwaysOff ? kVoiceOnlyWindowHeight
                                                            : kCollapsedWindowHeight);
    setSize(kDefaultWindowWidth, initialHeight);
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

    if (!clips.empty()) {
        musicClipBox_.setSelectedId(juce::jlimit(1, static_cast<int>(clips.size()), previous > 0 ? previous : 1),
                                    juce::dontSendNotification);
        // A project that actually uses background music should open with the
        // section revealed (only relevant in Foldable mode — the other modes are
        // already forced open or closed).
        if (musicSectionMode_ == MusicSectionMode::Foldable && !musicSectionExpanded_)
            setMusicSectionExpanded(true);
    }
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
    musicMasterVolumeSlider_.setValue(engine_.musicMasterGainDb(), juce::dontSendNotification);

    if (!valid) {
        updateMusicMuteUi();
        return;
    }

    const auto& clip = clips[static_cast<std::size_t>(index)];
    musicStartSlider_.setValue(clip.startSeconds, juce::dontSendNotification);
    musicVolumeSlider_.setValue(clip.gainDb, juce::dontSendNotification);
    musicFadeInSlider_.setValue(clip.fadeInSeconds, juce::dontSendNotification);
    musicFadeOutSlider_.setValue(clip.fadeOutSeconds, juce::dontSendNotification);
    updateMusicMuteUi();
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

    // The main window has a fixed content height that depends on the
    // background-music section's state (audio + pro controls live in the separate
    // settings window).
    int contentHeight = kCollapsedWindowHeight;
    if (musicSectionExpanded_)
        contentHeight = kExpandedWindowHeight;
    else if (musicSectionMode_ == MusicSectionMode::AlwaysOff)
        contentHeight = kVoiceOnlyWindowHeight; // no disclosure bar either
    else
        contentHeight = kCollapsedWindowHeight;

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

juce::String MainComponent::buildProjectInfoText() const {
    if (!engine_.hasAudio())
        return "No media loaded.\n\nDrop a video or audio file to see how it is "
               "analysed and processed.";

    const auto fmtDb = [](double db) {
        return (db >= 0.0 ? "+" : "") + juce::String(db, 1) + " dB";
    };

    const auto p = buildParams();
    const auto& buf = engine_.beforeBuffer();
    const double sr = engine_.sampleRate();
    const double seconds = sr > 0.0 ? buf.getNumSamples() / sr : 0.0;
    const int minutes = static_cast<int>(seconds) / 60;
    const int secs = static_cast<int>(seconds) % 60;

    juce::StringArray lines;

    lines.add("SOURCE");
    lines.add("  File: " + engine_.sourceFile().getFileName());
    lines.add("  Type: " + juce::String(engine_.sourceHasVideo() ? "video" : "audio"));
    lines.add("  Length: " + juce::String::formatted("%d:%02d", minutes, secs)
              + "   Sample rate: " + juce::String(static_cast<int>(sr)) + " Hz"
              + "   Channels: " + juce::String(buf.getNumChannels()));
    lines.add("");

    lines.add("ANALYSIS");
    lines.add("  Input loudness: " + juce::String(engine_.inputLufs(), 1) + " LUFS");
    lines.add("  Input peak: " + juce::String(engine_.inputPeakDb(), 1) + " dBFS");

    const double f0 = engine_.voiceFundamentalHz();
    if (f0 > 0.0)
        lines.add("  Voice fundamental (F0): " + juce::String(f0, 1) + " Hz");
    else
        lines.add("  Voice fundamental (F0): not detected (using default EQ layout)");

    lines.add(juce::String("  Voice profile: ")
              + (engine_.voiceProfileUsesDenoised()
                     ? "measured from denoised (speech-only) signal"
                     : (engine_.hasDenoised() ? "dry signal (denoise ready)"
                                              : "dry signal (denoising in background...)")));
    lines.add("");

    lines.add("PROCESSING DECISIONS");
    lines.add("  Volume boost (calibration): " + fmtDb(p.inputCalibrationGainDb));
    lines.add("  Intensity: " + juce::String(juce::roundToInt(currentIntensity() * 100.0)) + " %");
    lines.add("  Noise reduction: "
              + juce::String(juce::roundToInt(noiseReductionSlider_.getValue())) + " %");
    lines.add("  High-pass (rumble): " + juce::String(static_cast<int>(p.highpassHz)) + " Hz");
    lines.add("  Target loudness: " + juce::String(p.targetLufs, 1) + " LUFS");

    const double tone = currentToneAmount();
    juce::String toneDesc = tone > 0.05 ? "crisp" : (tone < -0.05 ? "warm" : "natural");
    lines.add("  Tone: " + toneDesc + " (" + juce::String(juce::roundToInt(tone * 100.0)) + " %)");
    lines.add("");

    lines.add("AUTO-EQ (derived from the voice spectrum)");
    if (p.autoEqBands.empty()) {
        lines.add("  (no corrective bands — spectrum already balanced)");
    } else {
        for (const auto& b : p.autoEqBands) {
            const char* type = b.type == vc::EqBand::Type::LowShelf  ? "Low shelf "
                             : b.type == vc::EqBand::Type::HighShelf ? "High shelf"
                                                                     : "Peak      ";
            lines.add("  " + juce::String(type) + " @ "
                      + juce::String(static_cast<int>(b.freq)) + " Hz: " + fmtDb(b.gainDb));
        }
    }

    return lines.joinIntoString("\n");
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
    content->setProjectInfo(buildProjectInfoText());
    content->setGeneralControls(musicSectionModeLabel_, musicSectionModeBox_,
                                followSystemButton_, outputDeviceLabel_, outputDeviceBox_);
    content->setProControls({
        { "LEVEL",
          { { &targetPreChainLabel_, &targetPreChainSlider_ } },
          1 },
        { "COMPRESSORS",
          { { &fastThresholdLabel_, &fastThresholdSlider_ },
            { &fastRatioLabel_, &fastRatioSlider_ },
            { &glueThresholdLabel_, &glueThresholdSlider_ },
            { &glueRatioLabel_, &glueRatioSlider_ } },
          4 },
        { "DE-ESS",
          { { &deEssFreqLabel_, &deEssFreqSlider_ },
            { &deEssThresholdLabel_, &deEssThresholdSlider_ },
            { &deEssPresenceLabel_, &deEssPresenceSlider_ },
            { &deEssRatioLabel_, &deEssRatioSlider_ },
            { &deEssRangeLabel_, &deEssRangeSlider_ } },
          5 },
    }, resetProButton_);

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
    return { "File" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;
    if (topLevelMenuIndex == 0) {
        menu.addItem(newProjectMenuId, "New Project");
        menu.addItem(openProjectManagerMenuId, "Projects...");
        menu.addSeparator();
        menu.addItem(saveProjectMenuId, "Save Project");
        menu.addSeparator();
        menu.addItem(openSettingsMenuId, "Settings...", true, settingsWindow_ != nullptr);
        menu.addSeparator();
        menu.addItem(userGuideMenuId, "User Guide");
        menu.addItem(keyboardShortcutsMenuId, "Keyboard Shortcuts");
        menu.addSeparator();
        menu.addItem(aboutMenuId, "About Voice Control");
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
        case userGuideMenuId:
            openHelp();
            break;
        case keyboardShortcutsMenuId:
            showKeyboardShortcuts();
            break;
        case aboutMenuId:
            showAbout();
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
    const auto analysis = engine_.makeAnalysisCacheState();
    if (analysis.getDynamicObject() != nullptr)
        root->setProperty("analysisCache", analysis);

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
    addNumber(*controls, "duckEnabled", duckOnOffButton_.getToggleState() ? 1.0 : 0.0);
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
        if (!clip.waveformPeaks.empty()) {
            juce::Array<juce::var> waveform;
            waveform.ensureStorageAllocated(static_cast<int>(clip.waveformPeaks.size()));
            for (float peak : clip.waveformPeaks)
                waveform.add(static_cast<double>(peak));
            clipObj->setProperty("waveformPeaks", waveform);
            addNumber(*clipObj, "waveformProcessedColumns", clip.waveformProcessedColumns);
        }
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
    pendingProjectAnalysisCache_ = root->getProperty("analysisCache");

    player_.stop();
    player_.clearSources();
    engine_.clear();
    musicUndoStack_.clear();
    musicClipBox_.clear(juce::dontSendNotification);
    applyMusicSectionMode(); // re-expanded later if the project has clips (Foldable)
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
    if (controls != nullptr && controls->hasProperty("duckEnabled"))
        duckOnOffButton_.setToggleState(numberProperty(controls, "duckEnabled", 0.0) > 0.5,
                                        juce::dontSendNotification);
    else
        duckOnOffButton_.setToggleState(false, juce::dontSendNotification);
    updateDuckingUi();

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
            clip.waveformPeaks = floatVectorProperty(clipObj, "waveformPeaks");
            clip.waveformProcessedColumns = static_cast<int>(numberProperty(clipObj, "waveformProcessedColumns",
                                                                            static_cast<double>(clip.waveformPeaks.size())));
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
            pendingProjectAnalysisCache_ = juce::var();
        }
    } else {
        restoringProject_ = false;
        pendingMusicClipRestore_.clear();
        pendingSelectedMusicClipRestore_ = -1;
        pendingProjectAnalysisCache_ = juce::var();
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

void MainComponent::maybeSaveBeforeReplacingSession(std::function<void(bool proceed)> onDone) {
    // Prompt whenever there is real work that isn't safely stored in a named
    // project file: either it changed since the last save (dirty), or it has
    // never been saved to a .vcproj at all (only sitting in the autosave).
    const bool unsaved = projectDirty_ || currentProjectFile_ == juce::File();
    if (!unsaved || !hasProjectContent()) {
        if (onDone)
            onDone(true);
        return;
    }
    juce::AlertWindow::showYesNoCancelBox(
        juce::AlertWindow::QuestionIcon,
        "Save current project?",
        "Do you want to save the current session before starting a new one?",
        "Save", "Don't Save", "Cancel", this,
        juce::ModalCallbackFunction::create([this, done = std::move(onDone)](int result) {
            if (result == 0) {
                if (done)
                    done(false);
                return;
            }
            if (result == 1) {
                if (currentProjectFile_ == juce::File()) {
                    saveAsNewProject([done](bool saved) {
                        if (done)
                            done(saved);
                    });
                    return;
                }
                saveAutosaveSession();
                saveAnalysisFileForCurrentMedia();
                projectDirty_ = false;
                if (done)
                    done(true);
                return;
            }
            if (currentProjectFile_ == juce::File() && engine_.hasAudio()) {
                juce::AlertWindow::showYesNoCancelBox(
                    juce::AlertWindow::QuestionIcon,
                    "Save analysis file?",
                    "Save the analysis data for this media? It will open faster next time.",
                    "Save Analysis", "Skip", "Cancel", this,
                    juce::ModalCallbackFunction::create([this, done](int analysisResult) {
                        if (analysisResult == 0) {
                            if (done)
                                done(false);
                            return;
                        }
                        if (analysisResult == 1)
                            saveAnalysisFileForCurrentMedia();
                        if (done)
                            done(true);
                    }));
                return;
            }
            if (done)
                done(true);
        }));
}

void MainComponent::clearProject() {
    player_.stop();
    player_.clearSources();
    engine_.clear();
    pendingMusicClipRestore_.clear();
    pendingSelectedMusicClipRestore_ = -1;
    pendingProjectAnalysisCache_ = juce::var();
    currentProjectFile_ = juce::File();
    projectDirty_ = false;
    autosaveCountdown_ = 0;
    intensityMinLoudnessRef_ = 0.0;
    intensityMaxLoudnessRef_ = 0.0;
    spectrumView_.setSpectrum({});
    spectrumView_.setProcessedSpectrum({});
    musicUndoStack_.clear();
    musicClipBox_.clear(juce::dontSendNotification);
    applyMusicSectionMode();
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
    maybeSaveBeforeReplacingSession([this](bool proceed) {
        if (!proceed)
            return;
        promptForProjectName("New Project", "Untitled", [this](juce::String name) {
            clearProject();
            currentProjectFile_ = uniqueProjectFile(projectsFolder(), name);
            saveAutosaveSession(); // writes the new (empty) project file
            updateMainMenu();
            statusLabel_.setText("Created project: " + currentProjectFile_.getFileNameWithoutExtension(),
                                 juce::dontSendNotification);
        });
    });
}

void MainComponent::promptForProjectName(const juce::String& title, const juce::String& defaultName,
                                         std::function<void(juce::String)> onName,
                                         std::function<void()> onCancel) {
    auto aw = std::make_shared<juce::AlertWindow>(title, "Project name:",
                                                  juce::AlertWindow::NoIcon, this);
    aw->addTextEditor("name", defaultName);
    aw->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    aw->enterModalState(true,
        juce::ModalCallbackFunction::create([aw, cb = std::move(onName),
                                             cancel = std::move(onCancel)](int result) {
            const auto entered = aw->getTextEditorContents("name").trim();
            aw->exitModalState(result);
            aw->setVisible(false);
            if (result == 1 && cb)
                cb(entered.isNotEmpty() ? entered : "Untitled");
            else if (result != 1 && cancel)
                cancel();
        }), false);
}

void MainComponent::openProjectManager() {
    auto content = std::make_unique<ProjectManagerComponent>(projectsFolder(), currentProjectFile_);
    auto* pm = content.get();
    pm->onNew = [this] {
        closeProjectManager();
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
            if (safe != nullptr)
                safe->newProject();
        });
    };
    pm->onOpen = [this](juce::File f) {
        closeProjectManager();
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this), f] {
            if (safe != nullptr)
                safe->openProjectFile(f);
        });
    };
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
    maybeSaveBeforeReplacingSession([this, file](bool proceed) {
        if (!proceed)
            return;
        juce::var parsed;
        juce::JSON::parse(file.loadFileAsString(), parsed);
        currentProjectFile_ = file;
        if (!applyProjectState(parsed, false))
            statusLabel_.setText("Could not open project.", juce::dontSendNotification);
    });
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
    saveAnalysisFileForCurrentMedia();
    projectDirty_ = false;
    autosaveCountdown_ = 0;
    statusLabel_.setText("Saved: " + currentProjectFile_.getFileNameWithoutExtension(),
                         juce::dontSendNotification);
}

void MainComponent::saveAsNewProject(std::function<void(bool saved)> onComplete) {
    // Save the current editor content (not cleared) into a freshly named project.
    promptForProjectName("Save Project", "Untitled",
        [this, onComplete](juce::String name) {
            currentProjectFile_ = uniqueProjectFile(projectsFolder(), name);
            saveAutosaveSession();
            saveAnalysisFileForCurrentMedia();
            projectDirty_ = false;
            autosaveCountdown_ = 0;
            updateMainMenu();
            statusLabel_.setText("Saved: " + currentProjectFile_.getFileNameWithoutExtension(),
                                 juce::dontSendNotification);
            if (onComplete)
                onComplete(true);
        },
        [onComplete] {
            if (onComplete)
                onComplete(false);
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

void MainComponent::saveAnalysisFileForCurrentMedia() {
    engine_.saveAnalysisCacheNow();
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
        "Restoring background music...",
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
                engine_.setMusicClipWaveformPeaks(index, saved.waveformPeaks,
                                                  saved.waveformProcessedColumns);
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
            setUiBusy(false, loadedMessage + " Background music restored.");
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
    engine_.setProjectAnalysisCache(pendingProjectAnalysisCache_);
    pendingProjectAnalysisCache_ = juce::var();

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

            // The intensity loudness references mean two full offline chain
            // renders of the whole file. Reuse the cached values when the
            // analysis cache supplied them; only measure on a cache miss.
            if (engine_.hasIntensityLoudnessRefs()) {
                intensityMinLoudnessRef_ = engine_.intensityMinLoudnessRef();
                intensityMaxLoudnessRef_ = engine_.intensityMaxLoudnessRef();
            } else {
                intensityMinLoudnessRef_ = engine_.measureChainLoudness(makeMeasuredParams(0.0));
                intensityMaxLoudnessRef_ = engine_.measureChainLoudness(makeMeasuredParams(1.0));
                engine_.setIntensityLoudnessRefs(intensityMinLoudnessRef_, intensityMaxLoudnessRef_);
            }
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
        "Add background music", juce::File(), "*.wav;*.mp3;*.m4a;*.flac;*.aiff");
    musicChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, startSeconds](const juce::FileChooser& fc) {
            const auto file = fc.getResult();
            if (file == juce::File()) return;
            const auto undoState = MusicUndoState { engine_.musicClips(), musicClipBox_.getSelectedId() - 1 };

            runOnWorker(
                "Adding background music...",
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
                    setUiBusy(false, "Background music added.");
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

void MainComponent::setMusicSectionExpanded(bool expanded) {
    musicSectionExpanded_ = expanded;

    // The disclosure bar and the "Hide" header button only exist in Foldable
    // mode. "Always shown" keeps the section open with no way to fold it; "Always
    // hidden" never shows the section or its entry point.
    const bool foldable = musicSectionMode_ == MusicSectionMode::Foldable;
    musicSectionToggle_.setButtonText(juce::String::fromUTF8("▸  Add background music"));
    musicSectionToggle_.setVisible(!expanded && foldable);
    musicHideButton_.setVisible(expanded && foldable);

    for (auto* c : { static_cast<juce::Component*>(&musicCaption_),
                     static_cast<juce::Component*>(&musicMuteButton_),
                     static_cast<juce::Component*>(&musicMasterVolumeLabel_),
                     static_cast<juce::Component*>(&musicMasterVolumeSlider_),
                     static_cast<juce::Component*>(&musicVolumeLabel_),
                     static_cast<juce::Component*>(&musicVolumeSlider_),
                     static_cast<juce::Component*>(&duckCaption_),
                     static_cast<juce::Component*>(&duckOnOffButton_),
                     static_cast<juce::Component*>(&duckLookAheadLabel_),
                     static_cast<juce::Component*>(&duckLookAheadSlider_),
                     static_cast<juce::Component*>(&duckReductionLabel_),
                     static_cast<juce::Component*>(&duckReductionSlider_),
                     static_cast<juce::Component*>(&duckFilterLabel_),
                     static_cast<juce::Component*>(&duckFilterSlider_),
                     static_cast<juce::Component*>(&duckView_) })
        c->setVisible(expanded);

    musicTimeline_.setMusicLaneVisible(expanded);

    applyWindowConstraints(); // relock the window to the new fixed height
    resized();
    repaint();
}

void MainComponent::applyMusicSectionMode() {
    bool expanded = false;
    switch (musicSectionMode_) {
        case MusicSectionMode::AlwaysOn:  expanded = true; break;
        case MusicSectionMode::AlwaysOff: expanded = false; break;
        case MusicSectionMode::Foldable:  expanded = !engine_.musicClips().empty(); break;
    }
    setMusicSectionExpanded(expanded);
}

void MainComponent::setMusicSectionMode(MusicSectionMode mode) {
    musicSectionMode_ = mode;
    globalPrefs().setValue("musicSectionMode", static_cast<int>(mode));
    globalPrefs().saveIfNeeded();
    if (musicSectionModeBox_.getSelectedId() != static_cast<int>(mode) + 1)
        musicSectionModeBox_.setSelectedId(static_cast<int>(mode) + 1, juce::dontSendNotification);
    applyMusicSectionMode();
}

void MainComponent::openHelp() {
    if (helpWindow_ != nullptr) {
        helpWindow_->toFront(true);
        return;
    }

    auto body = std::make_unique<juce::TextEditor>();
    body->setMultiLine(true);
    body->setReadOnly(true);
    body->setCaretVisible(false);
    body->setScrollbarsShown(true);
    body->setPopupMenuEnabled(false);
    body->setFont(juce::Font(juce::FontOptions(14.0f)));
    body->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff14161b));
    body->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    body->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    body->setColour(juce::TextEditor::textColourId, juce::Colours::white.withAlpha(0.88f));
    body->setText(juce::String::fromUTF8(
        "Voice Control — User Guide\n"
        "================================\n\n"
        "1. Load a voice recording\n"
        "   Drag an audio or video file onto the Voice lane (or click it to "
        "browse). Voice Control analyses the file, then plays it back cleaned up "
        "in real time.\n\n"
        "2. Shape the voice\n"
        "   • Strength — how hard the cleanup chain works overall.\n"
        "   • Tone — darker to brighter tonal balance.\n"
        "   • Noise reduction — how much background noise is removed.\n"
        "   Press Play (or the spacebar) to audition. Use Bypass to compare with "
        "the untouched original. The meters show compression, de-essing, limiting "
        "and output level.\n\n"
        "3. Add background music (optional)\n"
        "   Click “Add background music” to reveal the music lane and its "
        "controls, then drop a music file onto the Background lane. Drag clips to "
        "move them, drag the edges to trim, and drag the top corners to set "
        "fades.\n"
        "   • Volume / Clip volume — overall and per-clip levels.\n"
        "   • Mute — silence the music to check the voice alone.\n"
        "   • Auto volume — automatically dips the music under the voice. "
        "Tune look-ahead, reduction and mid-focus.\n"
        "   Use Hide to fold the section away again. In Settings › General you "
        "can make the section always shown, always hidden, or foldable.\n\n"
        "4. Export\n"
        "   Click Export to render the result. If the source was a video, the "
        "cleaned audio is muxed back into it.\n\n"
        "5. Projects\n"
        "   Work is autosaved. Use File › Projects to manage saved sessions; "
        "File › Save Project to store the current one.\n\n"
        "Keyboard shortcuts\n"
        "   Space — Play / pause\n"
        "   Cmd/Ctrl+N — New project\n"
        "   Cmd/Ctrl+O — Open projects\n"
        "   Cmd/Ctrl+S — Save project\n"),
        juce::dontSendNotification);

    auto content = std::make_unique<HelpContent>();
    content->setBody(std::move(body));
    content->setSize(560, 480);

    auto window = std::make_unique<SettingsDialogWindow>("User Guide", juce::Colour(0xff2b2f36));
    window->onClose = [this] { closeHelp(); };
    window->setUsingNativeTitleBar(true);
    window->setContentOwned(content.release(), true);
    window->centreAroundComponent(this, 560, 480);
    window->setResizable(true, false);
    window->setVisible(true);
    helpWindow_.reset(window.release());
}

void MainComponent::closeHelp() {
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
        if (safe != nullptr)
            safe->helpWindow_.reset();
    });
}

void MainComponent::showKeyboardShortcuts() {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        "Keyboard Shortcuts",
        "Space\t\tPlay / pause\n"
        "Cmd/Ctrl+N\tNew project\n"
        "Cmd/Ctrl+O\tOpen projects\n"
        "Cmd/Ctrl+S\tSave project",
        "OK", this);
}

void MainComponent::showAbout() {
    juce::String version = "0.1.0";
    if (auto* app = juce::JUCEApplication::getInstance())
        version = app->getApplicationVersion();
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        "About Voice Control",
        "Voice Control " + version + "\n\n"
        "Clean up voice recordings and add background music, "
        "then export back to audio or video.",
        "OK", this);
}

void MainComponent::captureSectionChromeBaselines() {
    if (sectionChromeBaselinesCaptured_)
        return;
    sectionActiveLabelColour_ = musicCaption_.findColour(juce::Label::textColourId);
    sectionActiveSliderTextColour_ = musicMasterVolumeSlider_.findColour(juce::Slider::textBoxTextColourId);
    sectionActiveSliderBgColour_ = musicMasterVolumeSlider_.findColour(juce::Slider::textBoxBackgroundColourId);
    sectionActiveSliderOutlineColour_ =
        musicMasterVolumeSlider_.findColour(juce::Slider::textBoxOutlineColourId);
    sectionChromeBaselinesCaptured_ = true;
}

void MainComponent::restoreSectionSliderChrome(juce::Slider& slider) const {
    slider.setAlpha(1.0f);
    slider.setColour(juce::Slider::textBoxTextColourId, sectionActiveSliderTextColour_);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, sectionActiveSliderBgColour_);
    slider.setColour(juce::Slider::textBoxOutlineColourId, sectionActiveSliderOutlineColour_);
}

void MainComponent::updateDuckingUi() {
    const bool on = duckOnOffButton_.getToggleState();
    duckOnOffButton_.setButtonText(on ? "On" : "Off");
    player_.setDuckBypassed(!on);

    const auto offGrey = juce::Colour(0xff3a3f48);
    const auto onGreen = juce::Colour(0xff6ee07a);
    duckOnOffButton_.setColour(juce::TextButton::buttonColourId, offGrey);
    duckOnOffButton_.setColour(juce::TextButton::buttonOnColourId, on ? onGreen : offGrey);
    duckOnOffButton_.setColour(juce::TextButton::textColourOffId,
                               on ? juce::Colours::white : kSectionOffTextReadable);
    duckOnOffButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff101217));

    for (auto* label : { &duckLookAheadLabel_, &duckReductionLabel_, &duckFilterLabel_ })
        label->setEnabled(true);
    for (auto* slider : { &duckLookAheadSlider_, &duckReductionSlider_, &duckFilterSlider_ }) {
        restoreSectionSliderChrome(*slider);
        slider->setEnabled(on);
        slider->repaint();
    }

    applyEncoderSectionStyle(on,
                             &duckCaption_,
                             { &duckLookAheadLabel_, &duckReductionLabel_, &duckFilterLabel_ },
                             sectionActiveLabelColour_,
                             kSectionDisabledLabelAlphaSoft);

    duckView_.setDuckingEnabled(on);
}

void MainComponent::updateMusicMuteUi() {
    const bool muted = musicMuteButton_.getToggleState();
    const int index = musicClipBox_.getSelectedId() - 1;
    const auto& clips = engine_.musicClips();
    const bool validClip = index >= 0 && index < static_cast<int>(clips.size());
    const bool busy = busy_.load();
    const bool hasClips = !clips.empty();

    const bool masterActive = !busy && hasClips && !muted;
    const bool clipActive = !busy && validClip && !muted;

    musicMasterVolumeSlider_.setEnabled(masterActive);
    musicVolumeSlider_.setEnabled(clipActive);
    restoreSectionSliderChrome(musicMasterVolumeSlider_);
    restoreSectionSliderChrome(musicVolumeSlider_);
    musicMasterVolumeSlider_.repaint();
    musicVolumeSlider_.repaint();
    for (auto* s : { &musicStartSlider_, &musicFadeInSlider_, &musicFadeOutSlider_ })
        s->setEnabled(!busy && validClip);

    applyEncoderSectionStyle(!muted && hasClips,
                             &musicCaption_,
                             { &musicMasterVolumeLabel_ },
                             sectionActiveLabelColour_);
    applyEncoderSectionStyle(clipActive,
                             nullptr,
                             { &musicVolumeLabel_ },
                             sectionActiveLabelColour_);
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

    // Music-output monitor always runs; the duck curve only draws when enabled.
    const bool duckingOn = duckOnOffButton_.getToggleState();
    std::array<float, 512> musicWave;
    if (playing)
        player_.readMusicAnalysisBlock(musicWave.data(), static_cast<int>(musicWave.size()));
    else
        musicWave.fill(0.0f);
    duckView_.setMusicWaveform(musicWave.data(), static_cast<int>(musicWave.size()));
    duckView_.setDuckingEnabled(duckingOn);
    if (duckingOn)
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

    // Once the background denoise finishes, persist the denoised audio so the
    // next load skips the model entirely. Covers both the fresh-analysis and the
    // restored-profile-without-audio-cache cases.
    if (engine_.hasAudio() && !busy_.load(std::memory_order_relaxed))
        engine_.persistDenoisedAudioIfReady();
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff20232a));
}

void MainComponent::resized() {
    auto r = getLocalBounds().reduced(16);

    // Live gain-reduction meters.
    compMeter_.setBounds(r.removeFromTop(kGrMeterRowHeight));
    r.removeFromTop(kMeterRowGap);
    deEssMeter_.setBounds(r.removeFromTop(kGrMeterRowHeight));
    r.removeFromTop(kMeterRowGap);
    limiterMeter_.setBounds(r.removeFromTop(kGrMeterRowHeight));
    r.removeFromTop(kMeterRowGap);
    vuMeter_.setBounds(r.removeFromTop(kOutputMeterRowHeight));
    r.removeFromTop(kMetersBottomGap);

    auto placeEncoder = [](juce::Rectangle<int> area, juce::Label& label, juce::Slider& slider,
                           int labelHeight = 20) {
        label.setBounds(area.removeFromTop(labelHeight));
        slider.setBounds(area);
    };

    // Bottom row: a small "?" help button pinned right, progress bar filling the
    // rest. Used by every exit path below.
    auto layoutBottomRow = [this](juce::Rectangle<int> row) {
        helpButton_.setBounds(row.removeFromRight(22));
        row.removeFromRight(6);
        progressBar_.setBounds(row);
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
    r.removeFromTop(kWaveformSectionGap);

    // The timeline shrinks to the voice lane alone when the backing-music
    // section is folded away.
    const int timelineHeight = musicSectionExpanded_ ? kTimelineHeight : kVoiceLaneHeight;
    auto timelineBounds = r.removeFromTop(timelineHeight);
    musicTimeline_.setBounds(timelineBounds);
    // The voice drop field covers only the voice lane (top), leaving the music
    // lane below clickable so its own "+" adds a music clip.
    auto voiceLane = timelineBounds.removeFromTop(kVoiceLaneHeight);
    dropArea_.setBounds(voiceLane);
    dropArea_.setVisible(analyzingMedia_ || !engine_.hasAudio());
    dropArea_.toFront(false);
    r.removeFromTop(8);

    if (!musicSectionExpanded_) {
        // "Always hidden": no section and no entry point at all.
        if (musicSectionMode_ != MusicSectionMode::AlwaysOff) {
            // Foldable + collapsed: a small left-aligned button, sitting under the
            // "Voice" label at the left of the timeline, is the only entry point.
            auto toggleRow = r.removeFromTop(kMusicToggleBarHeight);
            musicSectionToggle_.setBounds(toggleRow.removeFromLeft(168).withSizeKeepingCentre(168, 22));
            r.removeFromTop(8);
        }
        layoutBottomRow(r.removeFromTop(18));
        return;
    }

    // Background-music section: equal-width left/right columns (three 80px pot
    // slots each) with the duck monitor centered in the space between them.
    auto musicArea = r.removeFromTop(kMusicControlsHeight);
    constexpr int labelHeight = 16; // compact captions above section encoders
    constexpr int captionH = 20;

    auto leftCol = musicArea.removeFromLeft(kMusicSideColWidth);
    auto rightCol = musicArea.removeFromRight(kMusicSideColWidth);
    auto screenCol = musicArea;

    // Left header: "Background" name on the left, Mute + Hide buttons on the
    // right -- mirrors the duck header (name then buttons) on the other side.
    auto leftCaption = leftCol.removeFromTop(captionH);
    musicMuteButton_.setBounds(leftCaption.removeFromRight(46).withSizeKeepingCentre(44, 18));
    leftCaption.removeFromRight(6);
    if (musicHideButton_.isVisible()) { // absent in "Always shown" mode
        musicHideButton_.setBounds(leftCaption.removeFromRight(46).withSizeKeepingCentre(44, 18));
        leftCaption.removeFromRight(6);
    }
    musicCaption_.setBounds(leftCaption);

    // Left controls: two volume encoders centred as two elements, evenly spaced
    // across the full column width (same span the three duck knobs occupy).
    auto leftHalf = leftCol.removeFromLeft(leftCol.getWidth() / 2);
    placeEncoder(leftHalf.withSizeKeepingCentre(kMusicSlotWidth, leftHalf.getHeight()).reduced(4, 0),
                 musicMasterVolumeLabel_, musicMasterVolumeSlider_, labelHeight);
    placeEncoder(leftCol.withSizeKeepingCentre(kMusicSlotWidth, leftCol.getHeight()).reduced(4, 0),
                 musicVolumeLabel_, musicVolumeSlider_, labelHeight);

    // Right column: On/Off in the header; "Auto volume" + three knobs below.
    auto rightCaption = rightCol.removeFromTop(captionH);
    duckOnOffButton_.setBounds(rightCaption.removeFromRight(58).withSizeKeepingCentre(56, 18));
    rightCaption.removeFromRight(6);
    duckCaption_.setBounds(rightCaption);
    placeEncoder(rightCol.removeFromLeft(kMusicSlotWidth).reduced(4, 0),
                 duckLookAheadLabel_, duckLookAheadSlider_, labelHeight);
    placeEncoder(rightCol.removeFromLeft(kMusicSlotWidth).reduced(4, 0),
                 duckReductionLabel_, duckReductionSlider_, labelHeight);
    placeEncoder(rightCol.reduced(4, 0),
                 duckFilterLabel_, duckFilterSlider_, labelHeight);

    duckView_.setBounds(screenCol.reduced(8, 4));
    r.removeFromTop(8);

    // The Pro controls now live in the settings window (see SettingsComponent),
    // so resized() no longer lays them out here.
    layoutBottomRow(r.removeFromTop(18));
}
