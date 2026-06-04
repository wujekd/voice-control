# Fixed Chain Intensity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace overlapping cleanup presets with one fixed voice-cleanup chain controlled by a single intensity slider, with an optional pro panel for development/advanced tuning.

**Architecture:** File load measures the source and computes an internal calibration gain so every recording enters the chain near a fixed working level. Intensity adds relative drive from that calibrated point, fixed processors do the cleanup, loudness normalization restores consistent output level, and the limiter remains the final safety stage. The current preset dropdown is removed or hidden; a pro panel edits internal defaults without changing the simple main UI.

**Tech Stack:** C++17, JUCE desktop UI, existing `vc_core` DSP classes, CMake.

---

## File Structure

- Modify `core/Presets.h`: replace Light/Balanced/Strong as user-facing intensity presets with one fixed `ChainParams` baseline and add fields for calibration, intensity drive, second compressor, and scalable depths.
- Modify `core/VoiceChain.h` and `core/VoiceChain.cpp`: add pre-chain gain, two compressor stages, and update offline order.
- Modify `core/LiveVoiceChain.h` and `core/LiveVoiceChain.cpp`: mirror offline chain in the live preview, keep allocation out of the audio thread, expose both compressor meters.
- Modify `desktop-juce/ProcessingEngine.h` and `desktop-juce/ProcessingEngine.cpp`: compute source loudness/peak stats on load and expose calibration gain helpers.
- Modify `desktop-juce/MainComponent.h` and `desktop-juce/MainComponent.cpp`: remove/hide cleanup dropdown, make intensity the primary control, apply calibration + intensity drive, and add hidden pro panel.
- Modify `desktop-juce/PreviewPlayer.h` only if two compressor meter readouts need separate names.
- Modify `CMakeLists.txt`: add a lightweight test executable.
- Create `tests/DspBehaviorTests.cpp`: generated-signal tests for calibration, intensity, two-compressor behavior, and output loudness consistency.

## Chain Model

Target signal path:

```text
raw input
-> calibration gain from file analysis
-> intensity drive
-> high-pass
-> auto-EQ + tone EQ
-> fast peak compressor
-> slow glue compressor
-> de-esser
-> loudness normalization
-> limiter
```

Initial recommended defaults:

```text
targetPreChainLufs: -24 LUFS
calibrationGainClamp: -18 dB to +18 dB
intensityDrive: -6 dB at 0%, 0 dB at 50%, +8 dB at 100%
outputTarget: -16 LUFS
limiterCeiling: -1 dB

fast compressor:
  threshold: -18 dBFS
  ratio: 10:1
  attack: 1.5 ms
  release: 45 ms
  knee: 4 dB
  makeup: 0 dB

glue compressor:
  threshold: -24 dBFS
  ratio: 2.5:1
  attack: 20 ms
  release: 180 ms
  knee: 8 dB
  makeup: 0 dB

de-esser:
  frequency: 6000 Hz
  threshold: -30 dBFS
  ratio: 4:1
  range: 0 dB at 0%, 12 dB at 100%

auto-EQ:
  0.0 at 0%, 0.6 at 100%
```

## Task 1: Add DSP Behavior Tests

**Files:**
- Create: `tests/DspBehaviorTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add test target to CMake**

Add this after the CLI target in `CMakeLists.txt`:

```cmake
add_executable(vc-dsp-tests tests/DspBehaviorTests.cpp)
target_link_libraries(vc-dsp-tests PRIVATE vc_core)
```

- [ ] **Step 2: Create initial failing tests**

Create `tests/DspBehaviorTests.cpp`:

```cpp
#include "AudioBuffer.h"
#include "LoudnessNormalizer.h"
#include "Presets.h"
#include "VoiceChain.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

vc::AudioBuffer makeSine(double lufsishGain, int sampleRate = 48000, double seconds = 2.0) {
    vc::AudioBuffer b;
    b.sampleRate = sampleRate;
    const std::size_t frames = static_cast<std::size_t>(seconds * sampleRate);
    b.channels.assign(1, std::vector<float>(frames, 0.0f));
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        b.channels[0][i] = static_cast<float>(std::sin(2.0 * 3.141592653589793 * 180.0 * t) * lufsishGain);
    }
    return b;
}

double measureLufs(const vc::AudioBuffer& b) {
    vc::LoudnessNormalizer meter;
    meter.prepare(b.sampleRate, 0.0);
    return meter.measureIntegratedLufs(b);
}

double peakAbs(const vc::AudioBuffer& b) {
    double peak = 0.0;
    for (const auto& ch : b.channels)
        for (float s : ch)
            peak = std::max(peak, static_cast<double>(std::fabs(s)));
    return peak;
}

bool expect(bool condition, const std::string& message) {
    if (!condition)
        std::cerr << "FAIL: " << message << "\n";
    return condition;
}

} // namespace

int main() {
    bool ok = true;

    auto quiet = makeSine(0.03);
    auto hot = makeSine(0.6);

    auto quietParams = vc::fixedVoiceCleanupParams();
    auto hotParams = vc::fixedVoiceCleanupParams();

    quietParams.inputCalibrationGainDb = vc::computeCalibrationGainDb(measureLufs(quiet), quietParams);
    hotParams.inputCalibrationGainDb = vc::computeCalibrationGainDb(measureLufs(hot), hotParams);

    ok &= expect(quietParams.inputCalibrationGainDb > 0.0, "quiet source should receive positive calibration gain");
    ok &= expect(hotParams.inputCalibrationGainDb < 0.0, "hot source should receive negative calibration gain");

    auto lowIntensity = quiet;
    auto highIntensity = quiet;
    auto lowParams = quietParams;
    auto highParams = quietParams;
    vc::applyIntensity(lowParams, 0.0);
    vc::applyIntensity(highParams, 1.0);

    vc::VoiceChain lowChain;
    lowChain.prepare(lowIntensity.sampleRate, lowIntensity.numChannels(), lowParams);
    lowChain.process(lowIntensity);

    vc::VoiceChain highChain;
    highChain.prepare(highIntensity.sampleRate, highIntensity.numChannels(), highParams);
    highChain.process(highIntensity);

    ok &= expect(highChain.fastCompReductionDb() >= lowChain.fastCompReductionDb(),
                 "higher intensity should not reduce fast compressor activity");
    ok &= expect(highChain.glueCompReductionDb() >= lowChain.glueCompReductionDb(),
                 "higher intensity should not reduce glue compressor activity");
    ok &= expect(peakAbs(highIntensity) <= 0.92, "limiter should keep peaks below about -1 dBFS");

    const double lowLufs = measureLufs(lowIntensity);
    const double highLufs = measureLufs(highIntensity);
    ok &= expect(std::fabs(lowLufs - highLufs) < 2.0,
                 "low and high intensity should remain roughly loudness matched");

    if (!ok)
        return 1;

    std::cout << "DSP behavior tests passed\n";
    return 0;
}
```

- [ ] **Step 3: Run test and confirm it fails**

Run: `cmake --build build --target vc-dsp-tests`

Expected: compile fails because `fixedVoiceCleanupParams`, `computeCalibrationGainDb`, `applyIntensity`, `fastCompReductionDb`, and `glueCompReductionDb` do not exist yet.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt tests/DspBehaviorTests.cpp
git commit -m "test: add DSP behavior coverage"
```

## Task 2: Define Fixed Cleanup Parameters And Intensity Mapping

**Files:**
- Modify: `core/Presets.h`

- [ ] **Step 1: Add fields to `ChainParams`**

In `core/Presets.h`, extend `ChainParams` with:

```cpp
double inputCalibrationGainDb = 0.0;
double intensityDriveDb = 0.0;
double targetPreChainLufs = -24.0;
double minCalibrationGainDb = -18.0;
double maxCalibrationGainDb = 18.0;

bool fastCompEnabled = true;
double fastCompThresholdDb = -18.0;
double fastCompRatio = 10.0;
double fastCompAttackMs = 1.5;
double fastCompReleaseMs = 45.0;
double fastCompMakeupDb = 0.0;
double fastCompKneeDb = 4.0;

bool glueCompEnabled = true;
double glueCompThresholdDb = -24.0;
double glueCompRatio = 2.5;
double glueCompAttackMs = 20.0;
double glueCompReleaseMs = 180.0;
double glueCompMakeupDb = 0.0;
double glueCompKneeDb = 8.0;

double baseAutoEqStrength = 0.6;
```

Keep existing `comp*` fields temporarily for compatibility until Task 3 removes or stops using them.

- [ ] **Step 2: Add helper functions**

Add below `paramsForPreset`:

```cpp
inline ChainParams fixedVoiceCleanupParams() {
    ChainParams params;
    params.highpassHz = 85.0;
    params.targetLufs = -16.0;
    params.limiterEnabled = true;
    params.limiterCeilingDb = -1.0;
    params.deEssFreqHz = 6000.0;
    params.deEssThresholdDb = -30.0;
    params.deEssRatio = 4.0;
    params.deEssRangeDb = 12.0;
    return params;
}

inline double computeCalibrationGainDb(double measuredInputLufs, const ChainParams& params) {
    if (!std::isfinite(measuredInputLufs))
        return 0.0;
    return std::clamp(params.targetPreChainLufs - measuredInputLufs,
                      params.minCalibrationGainDb,
                      params.maxCalibrationGainDb);
}

inline double intensityToDriveDb(double intensity01) {
    const double s = std::clamp(intensity01, 0.0, 1.0);
    return -6.0 + (14.0 * s);
}

inline void applyIntensity(ChainParams& params, double intensity01) {
    const double s = std::clamp(intensity01, 0.0, 1.0);
    params.intensityDriveDb = intensityToDriveDb(s);
    params.deEssRangeDb = 12.0 * s;
    params.baseAutoEqStrength = 0.6 * s;
}
```

Add `#include <algorithm>` and `#include <cmath>` at the top of `core/Presets.h`.

- [ ] **Step 3: Build and confirm expected remaining failures**

Run: `cmake --build build --target vc-dsp-tests`

Expected: compile now reaches missing `VoiceChain` meter methods.

- [ ] **Step 4: Commit**

```bash
git add core/Presets.h
git commit -m "feat: define fixed cleanup intensity parameters"
```

## Task 3: Add Two Compressors And Pre-Chain Gain Offline

**Files:**
- Modify: `core/VoiceChain.h`
- Modify: `core/VoiceChain.cpp`

- [ ] **Step 1: Update `VoiceChain.h`**

Add two compressor members and meters:

```cpp
Compressor fastComp_;
Compressor glueComp_;

float fastCompReductionDb() const { return fastComp_.currentReductionDb(); }
float glueCompReductionDb() const { return glueComp_.currentReductionDb(); }
```

Remove or stop using the old single `comp_` member.

- [ ] **Step 2: Configure both compressors**

In `VoiceChain::prepare`, replace the single compressor prepare call with:

```cpp
fastComp_.prepare(sampleRate, params_.fastCompThresholdDb, params_.fastCompRatio,
                  params_.fastCompAttackMs, params_.fastCompReleaseMs,
                  params_.fastCompMakeupDb, params_.fastCompKneeDb);

glueComp_.prepare(sampleRate, params_.glueCompThresholdDb, params_.glueCompRatio,
                  params_.glueCompAttackMs, params_.glueCompReleaseMs,
                  params_.glueCompMakeupDb, params_.glueCompKneeDb);
```

- [ ] **Step 3: Apply calibration and intensity drive before filtering**

At the start of `VoiceChain::process`, after `channels` and `frames`:

```cpp
const double preGainDb = params_.inputCalibrationGainDb + params_.intensityDriveDb;
const float preGain = static_cast<float>(std::pow(10.0, preGainDb / 20.0));
if (preGain != 1.0f) {
    for (int ch = 0; ch < channels; ++ch) {
        float* data = buffer.channels[static_cast<std::size_t>(ch)].data();
        for (std::size_t i = 0; i < frames; ++i)
            data[i] *= preGain;
    }
}
```

Add `#include <cmath>` to `core/VoiceChain.cpp`.

- [ ] **Step 4: Run both compressors in order**

Replace the old compressor processing block with:

```cpp
if (params_.fastCompEnabled)
    fastComp_.process(buffer);

if (params_.glueCompEnabled)
    glueComp_.process(buffer);
```

- [ ] **Step 5: Run tests**

Run: `cmake --build build --target vc-dsp-tests && ./build/vc-dsp-tests`

Expected: tests compile and pass or fail only because live/UI code is not updated yet. If tests fail due to overly strict thresholds, adjust the test tolerance before changing DSP defaults.

- [ ] **Step 6: Commit**

```bash
git add core/VoiceChain.h core/VoiceChain.cpp
git commit -m "feat: add fixed two-stage offline compression"
```

## Task 4: Mirror The Chain In Live Preview

**Files:**
- Modify: `core/LiveVoiceChain.h`
- Modify: `core/LiveVoiceChain.cpp`
- Modify: `desktop-juce/PreviewPlayer.h`
- Modify: `desktop-juce/MainComponent.h`
- Modify: `desktop-juce/MainComponent.cpp`

- [ ] **Step 1: Update live chain members and meter methods**

In `core/LiveVoiceChain.h`, replace `Compressor comp_;` with:

```cpp
Compressor fastComp_;
Compressor glueComp_;
```

Replace `compReductionDb()` with:

```cpp
float fastCompReductionDb() const { return fastComp_.currentReductionDb(); }
float glueCompReductionDb() const { return glueComp_.currentReductionDb(); }
```

- [ ] **Step 2: Configure both live compressors**

In `LiveVoiceChain::prepare` and `LiveVoiceChain::applyParams`, configure `fastComp_` and `glueComp_` using the same fields as the offline chain.

- [ ] **Step 3: Apply pre-chain gain in live processing**

At the start of `LiveVoiceChain::process`, after `nch` validation and before high-pass:

```cpp
const double preGainDb = active_.inputCalibrationGainDb + active_.intensityDriveDb;
const float preGain = static_cast<float>(std::pow(10.0, preGainDb / 20.0));
if (preGain != 1.0f) {
    for (int ch = 0; ch < nch; ++ch) {
        float* d = channels[ch];
        for (int i = 0; i < numFrames; ++i)
            d[i] *= preGain;
    }
}
```

- [ ] **Step 4: Process both live compressors**

Replace the old live compressor block with:

```cpp
if (active_.fastCompEnabled)
    fastComp_.process(channels, numChannels, numFrames);

if (active_.glueCompEnabled)
    glueComp_.process(channels, numChannels, numFrames);
```

- [ ] **Step 5: Update UI meter access**

In `PreviewPlayer.h`, expose both reductions:

```cpp
float fastCompReductionDb() const { return chain_.fastCompReductionDb(); }
float glueCompReductionDb() const { return chain_.glueCompReductionDb(); }
```

In `MainComponent.h`, either add a second compressor meter or combine display. Recommended development choice:

```cpp
GrMeter fastCompMeter_ { "Peak Comp", 12.0f, juce::Colour(0xff66d9ef) };
GrMeter glueCompMeter_ { "Glue Comp", 12.0f, juce::Colour(0xffa6e22e) };
```

In `MainComponent.cpp`, add both meters, update `timerCallback`, and lay them out where the single compressor meter was.

- [ ] **Step 6: Build app**

Run: `cmake --build build --target voice-control-app`

Expected: app builds. Existing warnings unrelated to this task may remain.

- [ ] **Step 7: Commit**

```bash
git add core/LiveVoiceChain.h core/LiveVoiceChain.cpp desktop-juce/PreviewPlayer.h desktop-juce/MainComponent.h desktop-juce/MainComponent.cpp
git commit -m "feat: mirror two-stage compression in preview"
```

## Task 5: Use File Analysis For Calibration

**Files:**
- Modify: `desktop-juce/ProcessingEngine.h`
- Modify: `desktop-juce/ProcessingEngine.cpp`
- Modify: `desktop-juce/MainComponent.cpp`

- [ ] **Step 1: Store input peak stats**

Add to `ProcessingEngine` private fields:

```cpp
double inputPeakDb_ = -120.0;
```

Add public getter:

```cpp
double inputPeakDb() const { return inputPeakDb_; }
```

- [ ] **Step 2: Compute input peak on load**

In `ProcessingEngine::loadMedia`, after `inputLufs_`:

```cpp
double peak = 0.0;
for (const auto& ch : original_.channels)
    for (float s : ch)
        peak = std::max(peak, static_cast<double>(std::fabs(s)));
inputPeakDb_ = 20.0 * std::log10(peak + 1e-12);
```

Add `#include <algorithm>` and `#include <cmath>` if missing.

- [ ] **Step 3: Apply calibration in `buildParams`**

In `MainComponent::buildParams`, start from:

```cpp
auto p = vc::fixedVoiceCleanupParams();
```

Then set:

```cpp
p.tone = currentTone();
p.autoEqEnabled = autoEqButton_.getToggleState();
p.autoEqBands = engine_.autoEqBands();
p.inputCalibrationGainDb = vc::computeCalibrationGainDb(engine_.inputLufs(), p);
vc::applyIntensity(p, strengthSlider_.getValue() / 100.0);
```

Remove the old cleanup preset switch from the active path.

- [ ] **Step 4: Recompute auto-EQ with intensity**

Long-term clean option: store the loaded spectrum and compute auto-EQ inside `buildParams` using `p.baseAutoEqStrength`.

Implement in `MainComponent::buildParams`:

```cpp
if (engine_.hasAudio())
    p.autoEqBands = vc::computeAutoEqBands(engine_.spectrum(), p.baseAutoEqStrength);
else
    p.autoEqBands = {};
```

This replaces using `engine_.autoEqBands()` for active params.

- [ ] **Step 5: Build and test**

Run: `cmake --build build --target vc-dsp-tests && ./build/vc-dsp-tests`

Run: `cmake --build build --target voice-control-app`

Expected: tests pass and app builds.

- [ ] **Step 6: Commit**

```bash
git add desktop-juce/ProcessingEngine.h desktop-juce/ProcessingEngine.cpp desktop-juce/MainComponent.cpp
git commit -m "feat: calibrate cleanup chain from source analysis"
```

## Task 6: Simplify Main UI

**Files:**
- Modify: `desktop-juce/MainComponent.h`
- Modify: `desktop-juce/MainComponent.cpp`

- [ ] **Step 1: Remove cleanup dropdown from the visible UI**

Remove or hide:

```cpp
presetCaption_
presetBox_
currentPreset()
```

Recommended implementation: keep `currentPreset()` removed and keep no visible cleanup dropdown. The main controls become:

```text
Tone
Auto EQ
Intensity
Play / Hearing
Meters
Export
```

- [ ] **Step 2: Rename strength label**

Change:

```cpp
strengthCaption_.setText("Strength", juce::dontSendNotification);
```

to:

```cpp
strengthCaption_.setText("Intensity", juce::dontSendNotification);
```

- [ ] **Step 3: Update status text**

After load, show calibration info without being too technical:

```cpp
"Loaded \"%s\"  -  input %.1f LUFS, calibrated for cleanup, target %.0f LUFS."
```

- [ ] **Step 4: Update layout**

Remove the left/right cleanup/tone split and give tone the full width, or put Tone and Auto-EQ compactly on one row. Keep Intensity visually primary.

- [ ] **Step 5: Build app**

Run: `cmake --build build --target voice-control-app`

Expected: app builds and the visible UI has no cleanup intensity dropdown.

- [ ] **Step 6: Commit**

```bash
git add desktop-juce/MainComponent.h desktop-juce/MainComponent.cpp
git commit -m "feat: simplify voice cleanup controls"
```

## Task 7: Add Hidden Pro Panel

**Files:**
- Modify: `desktop-juce/MainComponent.h`
- Modify: `desktop-juce/MainComponent.cpp`

- [ ] **Step 1: Add pro panel state**

In `MainComponent.h`:

```cpp
bool proPanelVisible_ = false;
juce::TextButton proButton_ { "Pro" };
juce::GroupComponent proPanel_ { "proPanel", "Pro" };
juce::Slider fastThresholdSlider_;
juce::Slider fastRatioSlider_;
juce::Slider glueThresholdSlider_;
juce::Slider glueRatioSlider_;
juce::Slider targetPreChainSlider_;
```

- [ ] **Step 2: Initialize sliders**

In constructor:

```cpp
proButton_.onClick = [this] {
    proPanelVisible_ = !proPanelVisible_;
    proPanel_.setVisible(proPanelVisible_);
    fastThresholdSlider_.setVisible(proPanelVisible_);
    fastRatioSlider_.setVisible(proPanelVisible_);
    glueThresholdSlider_.setVisible(proPanelVisible_);
    glueRatioSlider_.setVisible(proPanelVisible_);
    targetPreChainSlider_.setVisible(proPanelVisible_);
    resized();
};
addAndMakeVisible(proButton_);

fastThresholdSlider_.setRange(-36.0, -6.0, 0.5);
fastThresholdSlider_.setValue(-18.0, juce::dontSendNotification);
fastThresholdSlider_.onValueChange = [this] { applyParamsLive(); };
addChildComponent(fastThresholdSlider_);

fastRatioSlider_.setRange(2.0, 20.0, 0.5);
fastRatioSlider_.setValue(10.0, juce::dontSendNotification);
fastRatioSlider_.onValueChange = [this] { applyParamsLive(); };
addChildComponent(fastRatioSlider_);

glueThresholdSlider_.setRange(-36.0, -6.0, 0.5);
glueThresholdSlider_.setValue(-24.0, juce::dontSendNotification);
glueThresholdSlider_.onValueChange = [this] { applyParamsLive(); };
addChildComponent(glueThresholdSlider_);

glueRatioSlider_.setRange(1.0, 8.0, 0.1);
glueRatioSlider_.setValue(2.5, juce::dontSendNotification);
glueRatioSlider_.onValueChange = [this] { applyParamsLive(); };
addChildComponent(glueRatioSlider_);

targetPreChainSlider_.setRange(-32.0, -18.0, 0.5);
targetPreChainSlider_.setValue(-24.0, juce::dontSendNotification);
targetPreChainSlider_.onValueChange = [this] { applyParamsLive(); };
addChildComponent(targetPreChainSlider_);
```

- [ ] **Step 3: Apply pro overrides in `buildParams`**

After `vc::fixedVoiceCleanupParams()`:

```cpp
if (proPanelVisible_) {
    p.fastCompThresholdDb = fastThresholdSlider_.getValue();
    p.fastCompRatio = fastRatioSlider_.getValue();
    p.glueCompThresholdDb = glueThresholdSlider_.getValue();
    p.glueCompRatio = glueRatioSlider_.getValue();
    p.targetPreChainLufs = targetPreChainSlider_.getValue();
}
```

- [ ] **Step 4: Hide pro controls by default**

Call:

```cpp
proPanel_.setVisible(false);
fastThresholdSlider_.setVisible(false);
fastRatioSlider_.setVisible(false);
glueThresholdSlider_.setVisible(false);
glueRatioSlider_.setVisible(false);
targetPreChainSlider_.setVisible(false);
```

- [ ] **Step 5: Layout pro panel at bottom**

In `resized`, place `proButton_` near the title or bottom. If `proPanelVisible_` is true, allocate a compact 150 px section near the bottom for sliders.

- [ ] **Step 6: Build app**

Run: `cmake --build build --target voice-control-app`

Expected: app builds; Pro panel is hidden until toggled.

- [ ] **Step 7: Commit**

```bash
git add desktop-juce/MainComponent.h desktop-juce/MainComponent.cpp
git commit -m "feat: add hidden pro tuning panel"
```

## Task 8: Manual Audio Verification

**Files:**
- No code changes expected unless listening reveals tuning issues.

- [ ] **Step 1: Test with three file types**

Use:

```text
quiet voice recording
hot/loud voice recording
uneven voice recording with peaks
```

- [ ] **Step 2: Verify behavior**

Expected:

```text
0% intensity: close to original, lighter processing, output still level-matched
50% intensity: clear cleanup, moderate compressor/de-esser meters
100% intensity: more controlled but not obviously crushed
hot file: calibration reduces drive before intensity
quiet file: calibration increases drive before intensity
limiter: mostly safety, not constantly pinned
```

- [ ] **Step 3: Tune defaults if needed**

Only tune these first:

```text
targetPreChainLufs
intensity drive range
fast compressor threshold
glue compressor threshold
de-esser range
```

Avoid changing many parameters at once.

- [ ] **Step 4: Commit final tuning**

```bash
git add core/Presets.h desktop-juce/MainComponent.cpp
git commit -m "tune: refine fixed voice cleanup defaults"
```

## Self-Review

Spec coverage:
- Single fixed setup: covered by Tasks 2, 5, and 6.
- Intensity from calibrated operating point: covered by Tasks 2 and 5.
- Two compressors: covered by Tasks 3 and 4.
- Loudness consistency: covered by Tasks 1, 3, and 5.
- Hidden pro panel: covered by Task 7.
- Development verification: covered by Tasks 1 and 8.

Placeholder scan:
- No TBD/TODO placeholders remain.
- All tasks include exact files and concrete commands.

Type consistency:
- `fixedVoiceCleanupParams`, `computeCalibrationGainDb`, and `applyIntensity` are defined in Task 2 before use in later tasks.
- `fastCompReductionDb` and `glueCompReductionDb` are defined offline in Task 3 and live in Task 4.
