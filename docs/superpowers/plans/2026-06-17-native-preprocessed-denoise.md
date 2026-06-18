# Native Preprocessed Denoise Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make high-quality native/Rust DeepFilterNet preprocessing the default denoise path, cache the fully denoised result and visual layers, and keep live playback visuals tied to the actual output signal.

**Architecture:** Denoise once at load using the native DeepFilterNet C API, but with runtime defaults matched to the official `deep-filter --pf` CLI that passed listening tests. The app should use complete dry and denoised buffers for preview/export, cache denoised audio and analysis layers, and render paused visuals from cached dry/clean layers while live playback analysis continues to use the real heard output.

**Tech Stack:** C++17, JUCE, DeepFilterNet Rust `libDF` C API, CMake, existing `vc::AudioBuffer`, `vc::DenoiseStreamer`, `ProcessingEngine`, `PreviewPlayer`, `SpectrumAnalyzer`, `PitchDetector`, and WAV cache helpers.

---

## Context and Decisions

Listening tests established the actual cause of the artifact:

- `noise48_cli_deepFilter_pf.wav` sounded clean.
- `noise48_rustcli_onnx_pf_compdelay.wav` sounded clean.
- `noise48_cpp_capi_clistyle_onnx_pf_compdelay.wav` had artifacts.
- `noise48_rustcli_onnx_pf_capi_defaults.wav` had artifacts.
- `noise48_cpp_capi_clistyle_cli_defaults_pf_compdelay.wav` sounded clean after patching the C API defaults.

Therefore the defect is not real-time denoise itself and not the Rust/ONNX model. The app’s C API path used different DeepFilterNet runtime defaults than the official Rust CLI.

Good defaults to preserve:

```text
post_filter_beta = 0.02
atten_lim_db = 100
min_db_thresh = -15
max_db_erb_thresh = 35
max_db_df_thresh = 35
reduce_mask = MAX
```

Product direction:

- Default to preprocessing before playback.
- Keep native/Rust/ONNX as the engine.
- Cache the denoised buffer.
- Use cached dry and denoised layers for paused/editing visuals.
- Use actual playback/output analysis while playing.
- Keep real-time streaming logic only if it remains useful internally, but do not let partial denoise readiness affect default playback.

## File Structure

- Modify `third_party/DeepFilterNet/libDF/src/capi.rs`
  - Make C API runtime defaults match the official Rust CLI.
- Modify `core/DenoiseStreamer.cpp`
  - Ensure post-filter is enabled with beta `0.02`.
  - Ensure the full-file render path is deterministic and complete before playback.
- Modify `desktop-juce/ProcessingEngine.cpp`
  - Load or compute denoised audio before playback.
  - Build dry and denoised analysis layers.
  - Persist denoised cache after successful native render.
  - Invalidate old artifact-prone denoise caches via schema/version bump or cache key change.
- Modify `desktop-juce/ProcessingEngine.h`
  - Expose complete denoised buffer and cached visual layers.
  - Keep public API stable where possible for `MainComponent` and `PreviewPlayer`.
- Modify `desktop-juce/PreviewPlayer.cpp`
  - Blend dry and complete denoised buffers only; no per-hop fallback in the default path.
  - Keep live analysis ring based on actual heard output.
- Modify `desktop-juce/PreviewPlayer.h`
  - Remove or de-emphasize per-hop validity inputs from default preview path.
- Modify `desktop-juce/MainComponent.cpp`
  - Wire complete denoised source after load.
  - Remove mid-playback profile swaps from the default path.
  - Keep waveform/spectrum updates based on cached layers while paused and live output while playing.
- Modify `research/noise_removal/tools/listening_review/server.py`
  - Keep the single-column newest-first review UI, or split it into a small committed improvement if desired.
- Keep or remove `tests/RenderDenoiseVariants.cpp`
  - If retained, make it a proper diagnostic tool.
  - If removed, document the listening evidence in this plan and avoid shipping test harness clutter.

## Task 1: Preserve Current Exploratory State

**Files:**
- Inspect only: repository working tree

- [ ] **Step 1: Capture status**

Run:

```bash
git status --short
git diff --stat
git -C third_party/DeepFilterNet diff --stat
```

Expected:

```text
Modified files include DeepFilterNet C API/default work, ProcessingEngine/PreviewPlayer experiments, listening review page, and RenderDenoiseVariants if present.
```

- [ ] **Step 2: Save a patch backup outside Git**

Run:

```bash
mkdir -p /private/tmp/voice-control-denoise-backup
git diff > /private/tmp/voice-control-denoise-backup/app-working-tree.patch
git -C third_party/DeepFilterNet diff > /private/tmp/voice-control-denoise-backup/deepfilternet-working-tree.patch
```

Expected:

```text
/private/tmp/voice-control-denoise-backup/app-working-tree.patch exists
/private/tmp/voice-control-denoise-backup/deepfilternet-working-tree.patch exists
```

- [ ] **Step 3: Decide branch strategy**

If branch creation is allowed by the local filesystem, run:

```bash
git switch -c denoise-preprocess-quality
```

Expected:

```text
Switched to a new branch 'denoise-preprocess-quality'
```

If `.git` branch creation is still blocked, continue on the current branch after confirming the patch backup exists.

## Task 2: Make DeepFilterNet C API Match Good CLI Defaults

**Files:**
- Modify: `third_party/DeepFilterNet/libDF/src/capi.rs`

- [ ] **Step 1: Patch C API runtime defaults**

In `third_party/DeepFilterNet/libDF/src/capi.rs`, update `DFState::new` to:

```rust
impl DFState {
    fn new(model_path: &str, channels: usize, atten_lim: f32) -> Self {
        let r_params = RuntimeParams::default_with_ch(channels)
            .with_atten_lim(atten_lim)
            .with_thresholds(-15., 35., 35.)
            .with_mask_reduce(ReduceMask::MAX);
        let df_params =
            DfParams::new(PathBuf::from(model_path)).expect("Could not load model from path");
        let m =
            DfTract::new(df_params, &r_params).expect("Could not initialize DeepFilter runtime.");
        DFState(m)
    }
    fn boxed(self) -> Box<DFState> {
        Box::new(self)
    }
}
```

- [ ] **Step 2: Rebuild native library**

Run:

```bash
cargo cbuild --release --no-default-features --features capi --manifest-path libDF/Cargo.toml
```

From:

```text
third_party/DeepFilterNet
```

Expected:

```text
Finished `release` profile
Building pkg-config files
Building header file using cbindgen
```

- [ ] **Step 3: Render C API listening proof**

Compile the diagnostic helper if retained:

```bash
c++ -std=c++17 -O2 -Icore tests/RenderDenoiseVariants.cpp core/Denoiser.cpp build/libvc_core.a third_party/DeepFilterNet/target/aarch64-apple-darwin/release/libdeepfilter.a -framework Foundation -framework Security -framework CoreFoundation -o /tmp/render_denoise_variants
```

Render:

```bash
/tmp/render_denoise_variants /tmp/noise48.wav research/noise_removal/experiments/denoise_regression_listen/data/output/noise48_cpp_capi_clistyle_cli_defaults_pf_compdelay.wav 100 0.02 third_party/DeepFilterNet/models/DeepFilterNet3_onnx.tar.gz cli-style
```

Expected:

```text
wrote ...noise48_cpp_capi_clistyle_cli_defaults_pf_compdelay.wav atten=100.0 pf=0.020
```

Listening expected: clear, matching `noise48_rustcli_onnx_pf_compdelay.wav`.

## Task 3: Make App Denoise Preprocess Complete Before Playback

**Files:**
- Modify: `desktop-juce/ProcessingEngine.cpp`
- Modify: `desktop-juce/ProcessingEngine.h`
- Modify: `desktop-juce/MainComponent.cpp`

- [ ] **Step 1: Keep load-time full denoise**

In `ProcessingEngine::loadMedia`, preserve this flow:

```cpp
if (!denoisedRestored) {
    streamer_.start(original_, vc::Denoiser::findDefaultModel());
    streamer_.waitUntilComplete();
}

denoisedAudioSaved_ = denoisedRestored;
refreshVoiceProfileFromDenoised();
persistDenoisedAudioIfReady();
```

Expected behavior:

```text
The UI load spinner waits for denoise completion before playback is enabled.
```

- [ ] **Step 2: Ensure failed model does not enable denoise**

In `ProcessingEngine.h`, keep:

```cpp
bool hasDenoised() const { return streamer_.modelReady(); }
bool voiceProfileUsesDenoised() const { return voiceProfileUsesDenoised_; }
const vc::AudioBuffer* denoisedPlanar() const { return &streamer_.denoised(); }
```

In `MainComponent::loadFile`, pass the denoised source only after the profile uses denoised audio:

```cpp
player_.setDenoisedSource(engine_.voiceProfileUsesDenoised()
                              ? engine_.denoisedPlanar()
                              : nullptr);
```

Expected behavior:

```text
If denoise fails, playback remains dry rather than blending a zero/partial buffer.
```

- [ ] **Step 3: Remove default mid-playback denoise profile swap**

In `MainComponent::timerCallback`, remove the branch that calls:

```cpp
engine_.refreshVoiceProfileFromDenoised()
engine_.persistDenoisedAudioIfReady()
```

Expected behavior:

```text
No auto-EQ/profile swap happens mid-playback because preprocessing already completed during load.
```

## Task 4: Update PreviewPlayer to Blend Complete Dry/Denoised Buffers

**Files:**
- Modify: `desktop-juce/PreviewPlayer.cpp`
- Modify: `desktop-juce/PreviewPlayer.h`

- [ ] **Step 1: Simplify denoised source API**

In `PreviewPlayer.h`, use:

```cpp
void setDenoisedSource(const vc::AudioBuffer* denoised);
```

Remove default-path storage for:

```cpp
const std::atomic<std::uint8_t>* denoisedValidHops_
int denoisedNumHops_
int denoisedHopSize_
```

Expected compile errors:

```text
Call sites still passing validHops/numHops/hopSize fail until updated.
```

- [ ] **Step 2: Implement complete-buffer setter**

In `PreviewPlayer.cpp`:

```cpp
void PreviewPlayer::setDenoisedSource(const vc::AudioBuffer* denoised) {
    const juce::ScopedLock sl(lock_);
    denoised_ = denoised;
}
```

- [ ] **Step 3: Blend without per-hop fallback**

In `PreviewPlayer::getNextAudioBlock`, use this shape:

```cpp
const auto* den = denoised_;
const int denLen = (den != nullptr) ? static_cast<int>(den->numFrames()) : 0;
const float amount = noiseReductionAmount_.load(std::memory_order_relaxed);
const bool canBlend = den != nullptr && denLen > 1 && amount > 0.0f;

if (!canBlend) {
    for (int ch = 0; ch < procCh; ++ch)
        scratch_.copyFrom(ch, 0, *info.buffer, ch, info.startSample, produced);
} else {
    const float wet = amount;
    const float dry = 1.0f - wet;
    const int denChannels = den->numChannels();
    double denPos = readPos_ - produced * ratio;
    for (int i = 0; i < produced; ++i) {
        const int i0 = static_cast<int>(juce::jlimit(0.0, static_cast<double>(denLen - 2), denPos));
        const float frac = static_cast<float>(denPos - i0);
        for (int ch = 0; ch < procCh; ++ch) {
            const float origSample = info.buffer->getSample(ch, info.startSample + i);
            const int dc = juce::jmin(ch, denChannels - 1);
            const float* d = den->channels[static_cast<std::size_t>(dc)].data();
            const float denSample = d[i0] + frac * (d[i0 + 1] - d[i0]);
            scratch_.setSample(ch, i, dry * origSample + wet * denSample);
        }
        denPos += ratio;
    }
}
```

Expected behavior:

```text
Noise reduction knob crossfades between dry and fully denoised buffers only.
```

- [ ] **Step 4: Keep live analysis based on actual output**

Do not change the existing analysis ring logic that reads from `info.buffer` after chain processing and music mixing:

```cpp
float mono = 0.0f;
for (int ch = 0; ch < outChannels; ++ch)
    mono += info.buffer->getSample(ch, info.startSample + i);
analysisRing_[static_cast<std::size_t>(w & (kAnalysisSize - 1))] = mono;
```

Expected behavior:

```text
While playing, visual analysis reflects the real heard output.
```

## Task 5: Cache and Invalidate Denoised Audio Correctly

**Files:**
- Modify: `desktop-juce/ProcessingEngine.cpp`

- [ ] **Step 1: Bump analysis cache version**

In `ProcessingEngine.cpp`, change:

```cpp
constexpr int kAnalysisCacheVersion = 3;
```

to:

```cpp
constexpr int kAnalysisCacheVersion = 4;
```

Reason:

```text
Old cache files may contain artifact-prone C API/default denoise output and denoised profile metadata.
```

- [ ] **Step 2: Version denoised cache filenames**

Change `denoisedCacheFileFor` to include a denoise engine suffix:

```cpp
return dir.getChildFile(juce::String::toHexString(source.getFullPathName().hashCode64())
                        + ".dfn-cli-defaults-v1.denoised.wav");
```

Expected behavior:

```text
Old .denoised.wav files are ignored without deleting user files.
```

- [ ] **Step 3: Persist clean native denoise output**

Keep:

```cpp
vc::writeWavPcm16(denoisedCacheFileFor(sourceFile_).getFullPathName().toStdString(),
                  denoised);
```

Expected:

```text
Second load of the same media restores the clean denoised buffer from cache.
```

## Task 6: Paused Visuals Use Cached Dry/Clean Layers

**Files:**
- Modify: `desktop-juce/ProcessingEngine.cpp`
- Modify: `desktop-juce/ProcessingEngine.h`
- Modify: `desktop-juce/MainComponent.cpp`

- [ ] **Step 1: Preserve dry waveform and denoised waveform layers**

In `ProcessingEngine::loadMedia`, dry layer:

```cpp
voiceWaveformPeaks_ = computeWaveformPeaks(original_, 2048);
```

In `refreshVoiceProfileFromDenoised`, denoised layer:

```cpp
processedVoiceWaveformPeaks_ = computeWaveformPeaks(denoised, 2048);
```

Expected behavior:

```text
The UI can fade between dry and denoised waveform peaks while paused.
```

- [ ] **Step 2: Preserve dry and denoised spectrum layers**

In `loadMedia`, dry layer:

```cpp
drySpectrum_ = vc::SpectrumAnalyzer::analyze(original_, 12);
spectrum_ = drySpectrum_;
```

In `refreshVoiceProfileFromDenoised`, clean layer:

```cpp
spectrum_ = vc::SpectrumAnalyzer::analyze(denoised, 12);
```

Expected behavior:

```text
Noise reduction knob can fade paused spectrum display between drySpectrum_ and spectrum_.
```

- [ ] **Step 3: Use existing previewSpectrum interpolation**

Keep `ProcessingEngine::previewSpectrum` interpolation:

```cpp
const double p = dry * dry * drySpectrum_.binPower[i] + wet * wet * spectrum_.binPower[i];
out.binPower[i] = p;
out.binDb[i] = static_cast<float>(10.0 * std::log10(p + 1e-12));
```

Expected behavior:

```text
Paused spectrum updates immediately as the noise reduction knob moves.
```

- [ ] **Step 4: Apply intensity/EQ visualization through existing auto-EQ path**

Keep:

```cpp
if (voiceProfileUsesDenoised_)
    return vc::computeNoiseAwareAutoEqBands(spectrum_, drySpectrum_, strength, voiceFundamentalHz_);
return vc::computeAutoEqBands(spectrum_, strength, voiceFundamentalHz_);
```

Expected behavior:

```text
Intensity changes continue to update the predicted EQ curve from cached dry/clean profiles.
```

## Task 7: Build and Listen-Test the App

**Files:**
- Verify: app binary

- [ ] **Step 1: Build app**

Run:

```bash
cmake --build /Users/dw/coding2/voice-control/build --target voice-control-app -- -j8
```

Expected:

```text
[100%] Built target voice-control-app
```

- [ ] **Step 2: Clear old artifact-prone analysis cache**

Only if required for local verification:

```bash
mkdir -p /private/tmp/voice-control-cache-backup
ditto "$HOME/Library/Voice Control/AnalysisCache" /private/tmp/voice-control-cache-backup/AnalysisCache
```

Then remove only old denoise/audio analysis cache files if the versioned cache key does not already bypass them:

```bash
find "$HOME/Library/Voice Control/AnalysisCache" -name '*.denoised.wav' -delete
```

Expected:

```text
The next app load recomputes denoise using patched native defaults.
```

- [ ] **Step 3: Launch app**

Run:

```bash
open "/Users/dw/coding2/voice-control/build/voice-control-app_artefacts/Release/Voice Control.app"
```

Expected:

```text
App launches and accepts a media file.
```

- [ ] **Step 4: User listening check**

Load the same noisy test source or a representative voice recording. Set noise reduction to 100%.

Expected by ear:

```text
No granular/musical-noise/warbly artifact beyond the clean Rust CLI reference.
```

- [ ] **Step 5: Visual behavior check**

With playback paused:

```text
Move Noise Reduction: waveform/spectrum visibly fade between dry and denoised layers.
Move Intensity: EQ/predicted tone visualization updates immediately.
```

During playback:

```text
Spectrum/analyzer follows actual heard output.
```

## Task 8: Clean Up Diagnostic Scaffolding

**Files:**
- Modify or delete: `tests/RenderDenoiseVariants.cpp`
- Modify: `desktop-juce/PreviewPlayer.cpp`
- Modify: `desktop-juce/PreviewPlayer.h`
- Modify: `desktop-juce/MainComponent.cpp`
- Modify: `research/noise_removal/tools/listening_review/server.py`

- [ ] **Step 1: Remove debug capture taps unless still needed**

If `VC_CAPTURE` code exists in `PreviewPlayer`, remove:

```cpp
#include "WavIo.h"
#include <cstdlib>
capBlend_
capOut_
capReady_
capturing_
writeCaptureIfReady()
```

Also remove:

```cpp
player_.writeCaptureIfReady();
```

from `MainComponent::timerCallback`.

Expected behavior:

```text
No debug WAVs are written during normal app use.
```

- [ ] **Step 2: Decide whether to keep listening review UI change**

If keeping it, ensure `server.py` retains:

```python
def find_matches(output_dir, stem):
    return sorted(output_dir.glob(f"{stem}*.wav"), key=lambda p: p.stat().st_mtime, reverse=True)
```

and:

```css
.grid { display: grid; grid-template-columns: minmax(260px, 720px); gap: 14px; }
```

Expected behavior:

```text
Listening review page remains single-column, newest-first.
```

- [ ] **Step 3: Remove or formalize RenderDenoiseVariants**

If deleting:

```bash
git rm tests/RenderDenoiseVariants.cpp
```

If keeping, add a short header comment:

```cpp
// Diagnostic tool for comparing DeepFilterNet C API runtime defaults against
// the official Rust CLI render path. Not shipped in the app.
```

Expected:

```text
No accidental shipping dependency on the diagnostic helper.
```

## Task 9: Final Verification

**Files:**
- Verify: all modified files

- [ ] **Step 1: Run status**

Run:

```bash
git status --short
git diff --stat
```

Expected:

```text
Only intentional source, DeepFilterNet C API, cache/version, and optional listening review changes remain.
```

- [ ] **Step 2: Run build**

Run:

```bash
cmake --build /Users/dw/coding2/voice-control/build --target voice-control-app -- -j8
```

Expected:

```text
Build succeeds.
```

- [ ] **Step 3: Run available unit tests**

Run:

```bash
cmake --build /Users/dw/coding2/voice-control/build --target vc-dsp-tests -- -j8
/Users/dw/coding2/voice-control/build/vc-dsp-tests
```

Expected:

```text
DSP behavior tests pass.
```

- [ ] **Step 4: Produce final listening render if needed**

Render a final app/native denoise file or capture app output for the user to judge.

Expected:

```text
User confirms full noise reduction is clear and comparable to the Rust CLI reference.
```

## Risks and Notes

- The patch is inside a vendored submodule. Long-term, either keep the submodule patch documented or upstream/add a cleaner C API constructor that accepts thresholds and mask reduction explicitly.
- Existing bad `.denoised.wav` cache files must be bypassed by versioned cache filename or deleted during testing.
- Python/PyTorch `deepFilter` is no longer needed for the default path if the patched native/Rust C API stays clean.
- Real-time denoise can be revisited later, but default product behavior should be high-quality preload.

