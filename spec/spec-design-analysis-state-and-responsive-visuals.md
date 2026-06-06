---
title: Analysis State and Responsive Visuals
version: 1.0
date_created: 2026-06-06
last_updated: 2026-06-06
owner: Voice Control
tags: design, audio, desktop, visualization, project-state
---

# Introduction

This specification defines how Voice Control shall represent background audio analysis, processed spectrum previews, waveform visualization, and future project-level caching. The goal is to make the interface feel stable and responsive while still allowing slow whole-file analysis tasks to complete in the background.

## 1. Purpose & Scope

This specification applies to the desktop JUCE application UI and the processing engine state exposed to that UI.

The scope includes:

- User-visible processing states after loading a file.
- Smooth visual updates for spectrum and processed voice preview lines.
- Waveform display sizing and normalization behavior.
- Future project caching requirements for precomputed analysis data.
- Project/session state persistence for quick-edit workflow continuity.

The scope excludes:

- New audio DSP processors such as limiters, de-essers, dynamic EQ, or multiband compression.
- Cloud sync or cross-device project storage.

## 2. Definitions

- **Analysis Profile**: Whole-file derived metadata used by the UI or DSP decisions, including average spectrum, auto-EQ source spectrum, loudness measurements, and waveform peaks.
- **Dry Spectrum**: Spectrum measured from the original decoded voice audio.
- **Denoised Voice Profile**: Spectrum measured from the fully denoised voice buffer, used so background rumble does not bias auto-EQ.
- **Processed Spectrum Preview**: Immediate UI model derived from the current noise-reduction blend spectrum plus EQ response. It is a stable visual preview, not a delayed full-chain offline render.
- **Fast Visual Prediction**: A UI-only approximation rendered immediately from current EQ and known spectrum data.
- **Authoritative Visual Result**: A slow but accurate visual result computed by the processing engine. It must not replace the visible static preview if doing so creates delayed state jumps.
- **Prepared State**: A state in which enough analysis data exists for the UI to avoid major visual jumps during ordinary control changes.
- **Project Cache**: Persisted analysis data associated with a source media file and a set of processing assumptions.
- **Project State**: Persisted user workflow state, including source media path, processing control values, relevant UI state, and lightweight metadata.
- **Autosaved Session**: The most recent Project State saved automatically by the app, used to restore the last working session on launch.

## 3. Requirements, Constraints & Guidelines

- **REQ-001**: The UI shall clearly indicate when a loaded file still has background preparation in progress.
- **REQ-002**: The UI shall distinguish blocking load work from non-blocking background analysis work.
- **REQ-003**: The UI shall not present a loaded file as fully prepared while denoising, voice-profile measurement, or required waveform analysis is still pending.
- **REQ-004**: The processed spectrum preview shall update immediately during Tone, Intensity, and Noise Reduction control changes using a Fast Visual Prediction.
- **REQ-005**: The static processed spectrum preview shall use one immediate visual model during parameter changes and after parameter release.
- **REQ-006**: The processed spectrum preview shall not snap abruptly after a delayed background computation unless the underlying source file changes.
- **REQ-007**: The EQ curve shall continue to move in real time with control changes.
- **REQ-008**: The static voice spectrum, processed spectrum, and waveform visuals shall have top and bottom visual headroom and shall not touch the component ceiling under normal normalized display conditions.
- **REQ-009**: The denoised voice profile update shall be visible as a preparation milestone to the user.
- **REQ-010**: The system shall avoid launching redundant processed-spectrum jobs for every intermediate slider event when a newer request supersedes older requests.
- **REQ-011**: The system shall persist reusable analysis data in a future Project Cache so reopening a project does not require full reanalysis.
- **REQ-012**: Cached analysis data shall be invalidated when the source media identity, source media modification metadata, analysis algorithm version, or relevant processing assumptions change.
- **REQ-013**: The unprocessed waveform may be displayed as soon as it is available.
- **REQ-014**: When processed or updated analysis data becomes available, the waveform and spectrum visuals shall crossfade or ease from the unprocessed visual state to the updated visual state.
- **REQ-015**: Auto-EQ analysis shall not rely exclusively on the denoised voice profile when generating boosts that could amplify noise, music bleed, hiss, harsh phone playback, or other background content if the user lowers Noise Reduction.
- **REQ-016**: Auto-EQ shall use a hybrid confidence model for corrective EQ decisions. Denoised voice analysis may drive vocal tone correction, but dry/original analysis must constrain boosts in frequency regions where background energy is present.
- **REQ-017**: Auto-EQ boosts in the presence, upper-mid, and air regions shall be conservative when the dry/original signal contains strong non-vocal energy that is reduced by denoising.
- **REQ-018**: Auto-EQ low-end and low-mid cuts shall distinguish between vocal boom/proximity and background rumble by comparing dry/original and denoised profiles.
- **REQ-019**: The app shall autosave the current session state so restarting the app restores the last loaded source and processing settings.
- **REQ-020**: The app shall support explicit project save and open operations using a lightweight project file.
- **REQ-021**: Starting a new project or opening another project shall prompt the user to save unsaved changes.
- **REQ-022**: Project State shall store source media path and processing/UI settings. It shall not store heavy processed or denoised audio buffers.

- **CON-001**: Audio playback responsiveness must take priority over offline visual analysis.
- **CON-002**: Background analysis must not block the audio callback or message thread.
- **CON-003**: Fast Visual Prediction may be approximate, but it must be visually consistent with the current control values.
- **CON-004**: Authoritative Visual Result must be derived from the actual processing chain used for preview/export, excluding backing music unless a future mixed-output spectrum mode is explicitly added. It must not automatically replace the static preview line after a delay.
- **CON-005**: Project Cache contents must be optional. The app must still function if no cache exists or if cache validation fails.
- **CON-006**: The Project Cache shall not store heavy denoised or processed audio buffers for the quick-edit workflow. It shall store derived analysis data only.
- **CON-007**: Auto-EQ must remain compatible with the user setting Noise Reduction below 100%. EQ decisions must not assume that denoised-only audio is always the final audible signal.
- **CON-008**: Project persistence must be workflow-oriented and lightweight. It must not force users into long-session project management before quick edits.

- **GUD-001**: The UI should use short status text for preparation state, for example: "Preparing voice profile..." or "Updating processed preview...".
- **GUD-002**: Status text should avoid implying that the user cannot interact when work is non-blocking.
- **GUD-003**: Visual easing should be fast enough to feel responsive and slow enough to prevent obvious jumps.
- **GUD-004**: Waveform amplitude scaling should favor readable dynamics over filling the entire lane height.

## 4. Interfaces & Data Contracts

### 4.1 Processing State

The processing engine shall expose a lightweight state contract to the UI.

```cpp
enum class AnalysisTaskState {
    NotStarted,
    Running,
    Ready,
    Failed
};

struct AnalysisPreparationState {
    AnalysisTaskState decode;
    AnalysisTaskState loudness;
    AnalysisTaskState drySpectrum;
    AnalysisTaskState denoise;
    AnalysisTaskState denoisedVoiceProfile;
    AnalysisTaskState voiceWaveform;
    AnalysisTaskState processedSpectrumPreview;
    double progress01; // -1.0 when indeterminate
    juce::String userStatus;
};
```

### 4.2 Spectrum View Inputs

The spectrum view shall support separate immediate and authoritative display inputs.

```cpp
void setBaseSpectrum(const vc::SpectrumResult& spectrum);
void setPredictedProcessedSpectrum(const vc::SpectrumResult& spectrum);
void setAuthoritativeProcessedSpectrum(const vc::SpectrumResult& spectrum);
void setProcessedSpectrumBusy(bool busy);
void animateTowardAuthoritativeResult();
```

### 4.3 Future Project Cache

The future project cache shall include, at minimum:

```json
{
  "schemaVersion": 1,
  "analysisVersion": "string",
  "source": {
    "path": "string",
    "sizeBytes": 0,
    "modifiedTimeUtc": "ISO-8601",
    "durationSeconds": 0,
    "sampleRate": 48000,
    "channels": 1
  },
  "analysis": {
    "inputLufs": -18.0,
    "inputPeakDb": -3.0,
    "drySpectrum": "serialized SpectrumResult",
    "denoisedSpectrum": "serialized SpectrumResult",
    "voiceWaveformPeaks": [0.0],
    "denoiseReady": true
  },
  "uiState": {
    "tone": 0.0,
    "intensity": 0.5,
    "noiseReduction": 0.75
  }
}
```

### 4.4 Project State

The project file shall include, at minimum:

```json
{
  "schemaVersion": 1,
  "sourcePath": "/path/to/source.mp4",
  "projectPath": "/path/to/project.vcproj",
  "controls": {
    "tone": 0.0,
    "noiseReduction": 75.0,
    "intensity": 100.0,
    "fastThreshold": -20.0,
    "fastRatio": 3.0,
    "glueThreshold": -24.0,
    "glueRatio": 2.5,
    "targetPreChain": -24.0,
    "deEssFreq": 6200.0,
    "deEssThreshold": -30.0,
    "deEssPresence": -18.0,
    "deEssRatio": 5.0,
    "deEssRange": 8.0,
    "musicMasterGain": 0.0
  },
  "view": {
    "settingsPanelVisible": false,
    "proPanelVisible": false
  }
}
```

## 5. Acceptance Criteria

- **AC-001**: Given a newly loaded file, when denoise/profile analysis is still running, then the UI shows a clear non-blocking preparation status.
- **AC-002**: Given a newly loaded file, when the denoised voice profile replaces the dry profile, then the user receives a visible preparation milestone instead of an unexplained visual jump.
- **AC-003**: Given the user drags Tone, Intensity, or Noise Reduction, then the processed preview line moves immediately using the same visual model that remains after the control is released.
- **AC-004**: Given an Authoritative Visual Result completes after a slider change, then the displayed static preview line does not switch to a different model and jump.
- **AC-005**: Given multiple quick slider changes, when old processed-spectrum jobs complete after newer requests, then old results are discarded.
- **AC-006**: Given a waveform lane with normalized peaks, when the waveform is drawn, then peaks retain visible top and bottom margin.
- **AC-007**: Given a project is reopened in the future with a valid Project Cache, when the source media metadata matches, then waveform and analysis visuals are restored without full reanalysis.
- **AC-008**: Given a project is reopened in the future with an invalid Project Cache, when validation fails, then the app reanalyzes and clearly reports preparation state.
- **AC-009**: Given the unprocessed waveform is visible, when processed or updated analysis data becomes available, then the waveform/spectrum transition uses a fade or easing animation instead of a snap.
- **AC-010**: Given a source file contains harsh background music or phone playback in the 2 kHz to 5 kHz range, when denoised analysis shows reduced upper-mid content, then auto-EQ shall not apply a large upper-mid boost that makes the dry background harsher when Noise Reduction is reduced.
- **AC-011**: Given dry/original analysis shows strong high-frequency noise that denoised analysis removes, when auto-EQ computes air-band correction, then any high-shelf boost shall be reduced or suppressed.
- **AC-012**: Given denoised analysis and dry/original analysis both show vocal low-end excess, when auto-EQ computes low-end correction, then low-end cuts may be applied confidently.
- **AC-013**: Given the app is closed with a loaded source and edited controls, when the app is reopened, then it restores the last source and control state.
- **AC-014**: Given unsaved edits exist, when the user starts a new project or opens another project, then the app prompts to save, discard, or cancel.
- **AC-015**: Given the user saves a project file, when that file is opened later, then the source path and processing settings are restored.

## 6. Test Automation Strategy

- **Test Levels**: Unit tests for analysis state transitions and cache validation; integration tests for stale processed-spectrum request rejection; visual/manual verification for easing and status behavior.
- **Frameworks**: Existing CMake/C++ test executable for core behavior; JUCE-level behavior may use targeted component tests if a UI test harness is added.
- **Test Data Management**: Use short generated voice-like buffers for deterministic analysis tests. Use noisy fixtures for denoised-profile behavior where available.
- **CI/CD Integration**: Build and run existing CMake tests. Add new non-GUI tests for cache validation once project caching exists.
- **Coverage Requirements**: At minimum, stale request rejection, state progression, and cache invalidation must be covered.
- **Performance Testing**: Measure that slider updates do not block the message thread and that background spectrum jobs are coalesced or superseded.

## 7. Rationale & Context

The current application performs useful whole-file work after a file appears loaded. This can cause visual changes that feel unexplained, especially when the denoised voice profile replaces the dry spectrum or when a processed spectrum result arrives after a control change.

The intended behavior is not to remove background processing. The intended behavior is to make it legible, responsive, and eventually cacheable.

Fast Visual Prediction solves the perceived lag by giving immediate feedback. The static display must not alternate between prediction and a delayed full-chain result, because that creates visible jumps after the user releases a control.

Project Cache support is required later because high-quality analysis and waveform data should not be recomputed every time a saved project is opened.

Auto-EQ must account for the fact that Noise Reduction is a user-controlled blend, not a fixed 100% preprocessing stage. A denoised-only spectrum can correctly identify a dull vocal, but boosting upper-mid or high-frequency regions based only on that denoised spectrum can amplify harsh background material when the user lowers Noise Reduction. A hybrid dry/denoised analysis model is required so boosts are gated by the full audible source context.

Project State is separate from Project Cache. Project State restores the user's workflow and selected settings. Project Cache restores derived analysis data. Neither should store heavy processed or denoised audio buffers.

## 8. Dependencies & External Integrations

### External Systems

- **EXT-001**: Local filesystem - Required for reading source media and future project/cache files.

### Technology Platform Dependencies

- **PLT-001**: JUCE desktop application framework - Required for UI rendering, timers, and component updates.
- **PLT-002**: Existing voice processing engine - Required for authoritative processed spectrum analysis and denoised profile generation.
- **PLT-003**: Existing DeepFilterNet integration - Required for denoised voice profile measurement.

### Data Dependencies

- **DAT-001**: Source media metadata - Required to validate cache freshness.
- **DAT-002**: Analysis algorithm version - Required to invalidate cache after analysis behavior changes.

## 9. Examples & Edge Cases

```text
Example: slider drag
1. User drags Intensity from 40% to 80%.
2. EQ curve updates immediately.
3. Processed spectrum preview updates immediately using Fast Visual Prediction.
4. The same preview model remains visible after the user releases the control.
5. Any later background analysis update may refresh cached analysis state, but must not replace the visible static preview with a different model.
```

```text
Edge case: denoise completes after load
1. File appears loaded and usable.
2. UI status says "Preparing voice profile..." while denoise continues.
3. Denoised profile completes.
4. Auto-EQ source profile updates.
5. UI status says "Voice profile ready."
6. Spectrum and waveform-related displays ease or crossfade into the updated profile.
```

```text
Edge case: project cache invalid
1. User opens a saved project.
2. Source file path matches but modified time differs.
3. Cache is rejected.
4. UI loads the source and shows preparation progress.
5. New cache data is written after analysis completes.
```

```text
Edge case: harsh background phone audio
1. Source contains vocal plus harsh phone music in the 2 kHz to 5 kHz range.
2. Denoised profile removes or reduces the phone music.
3. Denoised-only vocal analysis suggests an upper-mid boost.
4. Dry/original analysis shows strong upper-mid background energy.
5. Auto-EQ suppresses or reduces the boost so lowering Noise Reduction does not make the phone music harsher.
```

```text
Edge case: true dull vocal with clean background
1. Source contains a dull vocal and little background high-frequency energy.
2. Denoised profile and dry/original profile both indicate limited presence/air.
3. Auto-EQ may apply a conservative presence or air boost.
4. The boost remains bounded by normal max-boost limits.
```

## 10. Validation Criteria

- The UI shall show an explicit background-preparation indicator after load until all required analysis tasks are ready.
- The processed spectrum preview shall visibly respond within one frame interval of Tone, Intensity, or Noise Reduction changes.
- Delayed processed spectrum updates shall not visibly snap under normal slider interaction.
- Waveforms shall retain top and bottom headroom in the lane.
- Unprocessed waveform display shall be allowed before background processing completes.
- Heavy denoised or processed audio buffers shall not be required for future project-cache reuse.
- Existing audio preview and export behavior shall remain unchanged unless a separate requirement explicitly changes DSP output.
- Future project cache implementation shall validate source identity and analysis version before reuse.
- Auto-EQ boosts shall be constrained by dry/original spectrum checks so the app does not boost background content when Noise Reduction is reduced.
- Autosaved session restore shall reload the last source and restore user controls without requiring manual project opening.
- New Project and Open Project shall protect unsaved edits with a save prompt.

## 11. Related Specifications / Further Reading

- [README.md](/Users/dw/coding2/voice-control/README.md)
- [desktop-juce/SpectrumView.cpp](/Users/dw/coding2/voice-control/desktop-juce/SpectrumView.cpp)
- [desktop-juce/ProcessingEngine.cpp](/Users/dw/coding2/voice-control/desktop-juce/ProcessingEngine.cpp)

## 12. Decisions

- **DEC-001**: The UI may display the unprocessed waveform before background processing completes. When updated analysis becomes available, the visual transition shall be smooth.
- **DEC-002**: The future project cache shall not store heavy denoised or processed audio buffers. It shall store derived analysis data such as spectra, waveform peaks, levels, and UI state.
- **DEC-003**: Auto-EQ shall use hybrid dry/original plus denoised analysis. Denoised analysis may identify vocal correction needs, but dry/original analysis shall constrain boosts that could amplify background material.
- **DEC-004**: Project State persistence is workflow-based. The app shall autosave the last session and also allow explicit `.vcproj` save/open, without storing heavy audio.
