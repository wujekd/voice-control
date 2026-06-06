---
title: Background Music Ducking (Sidechain + Mid-Band Blend + Display)
version: 0.2
date_created: 2026-06-06
last_updated: 2026-06-06
owner: Voice Control
status: implemented (live preview; export parity deferred — OQ-4)
tags: design, audio, dsp, desktop, ducking, sidechain, visualization
---

# Background Music Ducking

## 1. Summary

Wire the three existing (currently dead) ducking knobs into the live preview
DSP and add a compact display between the music-volume knobs and the ducking
knobs. Voice is the sidechain key; the backing music ducks when the voice is
present. The "Filter" knob is repurposed from a Hz cutoff into a **full-band ↔
mid-band blend**, so the user can go from classic full-band ducking to a
dynamic-EQ-style duck that only pulls down the mid band where music clashes with
the voice, leaving the low end (and highs) intact.

### Locked design decisions

- **Band split — duck mid only.** A true band-pass duck: low AND high pass
  through unducked; only the mid band ducks. Two internal crossovers.
- **Filter knob — full↔band blend %.** 0% = whole music ducks evenly; 100% =
  only the mid band ducks. Crossover frequencies are fixed internal constants.
- **Display — both stacked.** A small scrolling music waveform on top, an
  EQ/spectrum-style line below whose middle dips as the duck engages.
- **Scope — preview only for now.** Ducking applies in live playback
  (`PreviewPlayer`). Offline export (`ProcessingEngine::mixMusicInto`) is left
  unchanged this pass; export parity is a tracked follow-up.

## 2. Current state (what exists)

- Three encoders exist but drive nothing: `duckLookAheadSlider_` (0–50 ms),
  `duckReductionSlider_` (0–24 dB), `duckFilterSlider_` (100–8000 Hz). Declared
  `desktop-juce/MainComponent.h:159`, configured `desktop-juce/MainComponent.cpp:329`,
  laid out `desktop-juce/MainComponent.cpp:1786` (right of the two volume knobs).
  Comment at `MainComponent.cpp:310`: "knobs only for now, not yet wired to DSP."
- Live mix happens in `PreviewPlayer::getNextAudioBlock`
  (`desktop-juce/PreviewPlayer.cpp:174`). At line 258 the processed voice is in
  `info.buffer`; line 278 calls `mixMusicInto`, which adds each music clip
  sample-by-sample with smoothed per-clip + master gain. **This is the seam:**
  the voice key is in `info.buffer` immediately before music is added.
- Reusable DSP: `vc::Biquad` + `vc::configureBiquad` (`core/Eq.h`) for the
  crossover band-pass; `core/LiveLimiter.h` is the existing look-ahead reference.
- Reusable visual patterns: `SpectrumView` (log-freq axis line drawing,
  `desktop-juce/SpectrumView.h`) and `GrMeter`/`VuMeter` (eased bars,
  `desktop-juce/GrMeter.h`).
- The analysis ring already captures the **voice-only** mono output for the
  spectrum view (`PreviewPlayer.cpp:266`). The new display needs a separate
  **music-output** ring.

## 3. DSP design

**The whole duck (detector + band-pass + look-ahead + blend) lives in a
portable, JUCE-free `vc::Ducker` (`core/Ducker.h/.cpp`)** so it links into
`vc_core` and is unit-testable headlessly. `PreviewPlayer` only renders music
into a scratch buffer, builds the voice key, and calls `ducker_.process(...)`.

### 3.1 Sidechain detector (voice key → duck gain)

Run inside `Ducker::process`, called after `chain_.process` and before music is
summed into the output.

1. **Key signal** = mono sum of the heard voice in `info.buffer`, per sample,
   built by `PreviewPlayer` and passed in.
2. **Envelope follower** on the rectified key: one-pole, fast attack / slower
   release (fixed internal constants, ~10 ms attack / ~250 ms release). Not
   exposed as knobs this pass.
3. **Gain map**: `envDb → smoothstep(−45 dB, −15 dB) → reductionDb`, scaled by
   the **Reduction** knob's max (0–24 dB). Silent voice ⇒ 0 dB (music full);
   loud voice ⇒ knob max. `g = 10^(−reductionDb/20)`.
4. **Look-ahead**: a preallocated per-channel ring delays the **music** by the
   knob's ms (≤ 50 ms at device rate). The detector reads the *undelayed* key,
   so the reduction lands ahead of the delayed music. **Trade-off:** this shifts
   the whole music track late by the look-ahead amount; negligible at the 5 ms
   default, noticeable only near 50 ms. The band-pass is computed on the
   *delayed* music so `music` and `mid` stay sample-aligned.

The detector runs every block whenever music is audible; it costs ~nothing when
no clips overlap the block.

### 3.2 Mid-band blend (the math that unifies both modes)

Let `music` = the fully-mixed music for this block (what `mixMusicInto`
produces today, pre-duck), `mid` = its band-pass component, `g` = duck gain
(≤1), `blend` = Filter knob (0..1).

- Full-band duck (`blend=0`):  `music · g`
- Mid-only duck (`blend=1`):   `low + mid·g + high` = `music − mid·(1−g)`

Crossfading the two:

```
output = music − (1−g) · ( music·(1−blend) + mid·blend )
```

So we only ever need **one extra signal: the band-passed `mid`.** No separate
low/high filters required. `mid` is produced by a band-pass = HPF at `f_lo`
followed by LPF at `f_hi`, per channel, using `vc::Biquad`. Crossovers are fixed
constants (initial targets `f_lo ≈ 250 Hz`, `f_hi ≈ 4 kHz`; tune by ear, leave
as named constants for easy adjustment, possibly Linkwitz-Riley 2nd order for
flat recombination).

### 3.3 Integration in `PreviewPlayer::getNextAudioBlock`

Today `mixMusicInto` adds music straight into `info.buffer`. Change it to render
into a cleared **`musicScratch_`** buffer (same clip/gain/fade logic, just a
different destination), then:

1. Build `keyMono_[]` = mono of the heard voice in `info.buffer` (pre-music).
2. Push the three knob atomics into `ducker_`; `ducker_.process(musicScratch, keyMono)`
   ducks the scratch in place (delay → band-pass → `out = m − (1−g)·(m·(1−blend) + mid·blend)`).
3. `addFrom` the ducked scratch into `info.buffer`.
4. Capture the post-duck music mono into a new analysis ring for the display.

The existing voice-only analysis ring, VU metering, and smoothed master/clip
gains are untouched.

### 3.4 Parameters plumbed to the audio thread

Add atomics on `PreviewPlayer` (set from the UI thread, read lock-free in the
callback), mirroring the existing `musicMasterGainDb_` pattern:

```cpp
std::atomic<float> duckLookAheadMs_   { 5.0f };
std::atomic<float> duckMaxReductionDb_{ 9.0f };
std::atomic<float> duckBlend_         { 0.0f }; // 0=full-band, 1=mid-only
```

Plus a setter trio (`setDuckLookAheadMs`, `setDuckReductionDb`, `setDuckBlend`).

## 4. UI changes

### 4.1 Filter knob repurpose

- Change `configureDuckSlider(duckFilterSlider_, ...)` from `100..8000 Hz` to
  `0..100`, step 1, default 0, suffix `" %"` (`MainComponent.cpp:331`).
- Rename label "Filter" → "Mid focus" (or keep "Filter"; see open question OQ-1).
- Add `onValueChange` handlers on all three duck sliders → push to
  `PreviewPlayer` setters. Currently they have none.

### 4.2 New `DuckView` component (both stacked)

A new `desktop-juce/DuckView.h` (header-only, like `GrMeter`), a
`juce::Component`, placed in the gap between the volume knobs (left) and the
duck knobs (right) in the music row layout (`MainComponent.cpp:1779`–`1793`).
Today `volumeArea` takes `knobWidth*2` from the left and `duckArea` takes
`knobWidth*3` from the right; the middle is currently empty — the display fills
it.

Two stacked lanes:

- **Top — scrolling music waveform.** A small mono waveform of the **heard
  music output** (post-duck), scrolling right-to-left. Fed from a new
  music-output analysis ring on `PreviewPlayer`.
- **Bottom — EQ/spectrum duck line.** A log-frequency axis line (reuse
  `SpectrumView`'s `freqToX`). A baseline represents the music at full level;
  the live duck pushes the line **down**: full width when `blend≈0`, only the
  mid section (between `f_lo` and `f_hi`) when `blend≈1`. Computed directly from
  the current reduction dB + blend — no FFT required for the dip shape; an
  optional faint real music spectrum can sit behind it later.

Driven by the existing UI timer (same one updating the meters,
`MainComponent.cpp` UI tick) calling `duckView_.setState(reductionDb, blend,
musicWaveSamples...)`. Eased like `GrMeter` so it reads smoothly.

### 4.3 Data the UI reads from `PreviewPlayer`

- `float musicDuckReductionDb()` — current smoothed reduction (atomic).
- `float duckBlend()` — echo of the knob for the dip shape.
- `readMusicAnalysisBlock(float* dest, int n)` — new ring capturing post-duck
  music mono, parallel to the existing `readAnalysisBlock`.

## 5. Files touched

- `core/Ducker.h/.cpp` (new) — portable detector + band-pass + look-ahead + mid
  blend. Added to `vc_core` in `CMakeLists.txt`.
- `desktop-juce/PreviewPlayer.h/.cpp` — `vc::Ducker` member, `musicScratch_`,
  `keyMono_`, duck atomics + setters, music analysis ring + getter.
- `desktop-juce/DuckView.h` — new component (waveform lane + EQ duck-line lane).
- `desktop-juce/MainComponent.h/.cpp` — Filter knob range/label, three
  `onValueChange` handlers, instantiate + lay out `DuckView`, timer push.
- `CMakeLists.txt` — add `DuckView.h` if listed explicitly (header-only may not
  need it; verify).
- `tests/DspBehaviorTests.cpp` — see §6.
- Persistence: extend Project State `controls` (the JSON in
  `spec-design-analysis-state-and-responsive-visuals.md` §4.4) with
  `duckLookAhead`, `duckReduction`, `duckBlend`; wire save/restore where the
  other music controls are persisted.

## 6. Testing

The duck math is portable and testable headlessly:

- **Full-band path** (`blend=0`, `g<1`): output ≈ `music·g`.
- **Mid-only path** (`blend=1`): low/high bands unchanged within tolerance; mid
  band attenuated by `g`. Verify with low-tone, mid-tone, high-tone inputs.
- **Blend monotonicity**: low-band attenuation decreases as `blend` goes 0→1.
- **Detector**: loud voice key drives reduction to the knob max; silence returns
  to 0 dB; release time within expected bounds.
- **Look-ahead**: reduction onset precedes the music transient by ~the
  configured delay.

Factor the duck/blend/detector into a small free function or struct in `core/`
so `tests/DspBehaviorTests.cpp` can exercise it without JUCE audio I/O (mirrors
how existing DSP is tested).

## 7. Open questions / follow-ups

- **OQ-1**: Filter knob label — keep "Filter", or rename to "Mid focus" /
  "Band"? (Default: rename for clarity.)
- **OQ-2**: Crossover frequencies `f_lo`/`f_hi` — fixed constants now; expose
  later? (Default: fixed, tune by ear.)
- **OQ-3**: Detector threshold/knee, attack, release are internal constants this
  pass; surface any of them later if the duck feels wrong. (Default: internal.)
- **OQ-4**: Export parity — bake the same duck into
  `ProcessingEngine::mixMusicInto` so exports match preview. Deferred this pass.
- **OQ-5**: Bottom display — start with the computed dip shape only; add a real
  faint music spectrum behind it later if useful.
