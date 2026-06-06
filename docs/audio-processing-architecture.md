---
title: Audio Processing Architecture
date_created: 2026-06-06
owner: Voice Control
tags: audio, dsp, engine, architecture
---

# Audio Processing Architecture

This document describes how Voice Control processes audio today: what the engine
does, the order of the signal chain, and — most importantly — **how processing
decisions are made** (which EQ moves, how much noise reduction, how loud, etc.).
It is written as the reference for adding the planned **automatic noise-reduction
dial-in** feature (see the final section).

The DSP itself is platform-agnostic and lives in [`core/`](../core) (namespace
`vc`). The JUCE desktop app in [`desktop-juce/`](../desktop-juce) is glue: it
owns decoded audio, drives FFmpeg, builds parameters from the UI, and calls into
`vc_core`. The core never depends on JUCE; the UI never reimplements DSP.

---

## 1. The two halves: a *blend* and a *fixed chain*

Conceptually every processed result is:

```
result = VoiceChain( blend(original, denoised, amount) ) + music
```

There are two independent decisions baked into this:

1. **How much denoise to mix in** — the `noiseReductionAmount` blend (0 = raw
   original, 1 = fully denoised). This is the part the new feature will set
   automatically.
2. **What the voice chain does** — high-pass, EQ, compression, de-essing,
   loudness, limiting. Most of this is *fixed* per preset, except the **auto-EQ**
   curve, which is *derived from the measured spectrum*.

Key architectural point: **noise reduction is not a stage inside `VoiceChain`.**
The denoised signal is produced separately and out-of-band (neural model, runs
in the background), cached, and then blended with the dry original *before* the
chain runs. The chain only ever sees the already-blended signal.

---

## 2. The signal chain (`vc::VoiceChain`)

Defined in [`core/VoiceChain.cpp`](../core/VoiceChain.cpp). Fixed stage order,
processed in place on an `AudioBuffer`:

```
0. pre-gain        (inputCalibrationGainDb + intensityDriveDb)
1. high-pass       (rumble removal, biquad per channel)
2. EQ              (auto-EQ bands + tone bands)
3. fast compressor (fast peak control)
4. glue compressor (slower, denser)
5. de-esser        (sibilance, after compression which accentuates it)
6. loudness        (normalize toward target LUFS)
7. limiter         (guard ceiling after loudness gain)
```

Each stage reads its own fields from a single `ChainParams` struct
([`core/Presets.h`](../core/Presets.h)). Adding a stage means adding fields, not
changing the chain interface. Every stage except EQ uses **fixed numeric
parameters** (chosen by preset / intensity); EQ is the one data-driven stage.

The blend step (`blendNoiseReduction`) lives in
[`ProcessingEngine.cpp`](../desktop-juce/ProcessingEngine.cpp), *not* in the
chain — it runs first, then `VoiceChain::process` runs on the result.

---

## 3. How each decision is currently made

### 3.1 Noise-reduction amount (the human dial today)

- The UI has a "Noise Reduction" encoder, 0–100 % (default 75 %).
- The raw percentage is bent through
  `noiseReductionControlToBlend(amount01) = pow(amount01, 0.74)`
  ([`Presets.h`](../core/Presets.h)) so the control feels more linear
  perceptually, then stored as `ChainParams::noiseReductionAmount`.
- At render time, `blendNoiseReduction(original, denoised, amount)` does a simple
  linear crossfade per sample: `dry*(1-wet) + den*wet`.
- **This number is currently chosen entirely by the user.** Nothing measures the
  signal to suggest it. That is exactly the gap the new feature fills.

The denoised signal comes from `DenoiseStreamer` wrapping DeepFilterNet3
(`core/Denoiser.*`, `core/DenoiseStreamer.*`): a neural model at 48 kHz mono, run
hop-by-hop on a background thread, faster than real time, playhead-prioritized so
the region you're about to hear denoises first. The model is allowed
near-unlimited attenuation (`attenLimDb = 100`).

### 3.2 EQ — the only *measured* decision in the chain

This is the heart of "how decisions are made," and the closest existing analogue
to what the noise-reduction auto-dial needs to do.

**Inputs.** A whole-file average spectrum (Welch's method, 4096-pt FFTs averaged
across the file — `core/SpectrumAnalyzer.cpp`), computed once at load and cached.
Two spectra are tracked:

- **Dry spectrum** (`drySpectrum_`) — measured from the raw original. Available
  immediately at load.
- **Denoised voice profile** (`spectrum_`) — measured from the *fully denoised*
  buffer once the background pass finishes (`refreshVoiceProfileFromDenoised`).
  Using the denoised voice means room noise / rumble doesn't bias the curve.

**How the curve is derived** (`computeAutoEqBands` / `computeNoiseAwareAutoEqBands`
in [`core/Eq.cpp`](../core/Eq.cpp)):

1. Measure four band levels *relative to a speech-core anchor* (300–3000 Hz):
   - low (60–200), mud (200–450), presence (3000–6000), air (8000–14000).
2. Compare each against a fixed **target balance** for clean speech
   (`lowTarget=-2, mudTarget=-1, presTarget=-3, airTarget=-10` dB rel. anchor).
3. `gain = (target - measured) * strength`, then clamp:
   - cuts up to 6 dB (controlling excess is safe), boosts up to 4 dB
     (conservative);
   - never boost a near-empty band (`< -18 dB` rel) — that just amplifies hiss;
   - deadzone of 1 dB so tiny moves are dropped.
4. Only shelves and broad bells — no surgical notches.

**Noise-aware variant.** When both spectra exist,
`computeNoiseAwareAutoEqBands` uses the *denoised* profile to decide intent but
constrains boosts using the *dry* profile, because the user may dial the blend
back down and re-expose background energy:

- If the dry signal already has enough energy in a band, scale a boost there to
  25 % (you'd mostly be brightening noise that comes back).
- If denoise removed a lot from a band (`dryRel - voiceRel > 2 dB`), assume the
  missing energy was background and get progressively more conservative.
- Never add low-end boost when the dry signal is already low-heavy (rumble).

This pattern — **measure dry vs. denoised, compare band balances, decide an
amount** — is the template the noise-reduction auto-dial should follow.

**Strength.** `strength` (0..1) scales the whole correction. It's set from the
"intensity" control via `applyIntensity`: `baseAutoEqStrength = 0.25 + 0.35*s`.
The default load-time strength is `0.6`.

### 3.3 Calibration gain & intensity drive

- The file's integrated loudness is measured once at load (`inputLufs_`).
- `computeCalibrationGainDb` trims the file to a working level
  (`targetPreChainLufs = -24 LUFS`, clamped ±18 dB) so the fixed-threshold
  compressors behave consistently regardless of how hot the source was.
- The "intensity" control adds `intensityDriveDb = -6 + 14*s` dB of extra drive
  on top, and also raises auto-EQ strength (above).

### 3.4 Compression, de-essing, loudness, limiter

All **fixed** per preset (`paramsForPreset` / `fixedVoiceCleanupParams`):

- Two compressors in series: a fast one (ratio 10, ~1.5 ms attack) for peaks,
  then a glue one (ratio 2.5, 20 ms attack) for density.
- De-esser at ~6.2 kHz with a presence-threshold gate so it only ducks true
  sibilance.
- Loudness normalizer targets `-16 LUFS` (Strong preset: `-14`).
- Limiter ceiling `-1 dBFS`.

None of these adapt to content beyond the load-time calibration gain. They are
not part of the auto-dial scope.

---

## 4. Engine orchestration (`ProcessingEngine`)

[`desktop-juce/ProcessingEngine.cpp`](../desktop-juce/ProcessingEngine.cpp) is
the conductor. Relevant responsibilities for the new feature:

- **`loadMedia`**: FFmpeg-decodes to a WAV, measures input LUFS + peak, analyzes
  the dry spectrum, computes initial auto-EQ at strength 0.6, kicks off the
  background denoise, and caches all of it (schema-versioned JSON keyed by path +
  size + mtime).
- **`refreshVoiceProfileFromDenoised`**: once the *whole-file* denoise completes,
  re-measure the spectrum from the denoised buffer, swap it in as the active
  voice profile, recompute auto-EQ, re-cache. After this call,
  `voiceProfileUsesDenoised()` is true and both `drySpectrum()` and `spectrum()`
  (denoised) are available — **this is the moment both signals needed for an
  automatic noise estimate are present.**
- **`previewSpectrum(amount)`**: power-blends dry and denoised spectra for the
  live UI preview at a given noise-reduction amount. Useful for *showing* the
  effect of an auto-chosen amount.
- **`process(params)`**: blend → run chain → mix music → store result. This is
  the authoritative offline render (also used by export).

### Threading / state model

- Denoise runs on a background worker; playback uses the dry signal until hops
  fill in. `hasDenoised()` / `streamer_.isComplete()` gate when the full denoised
  buffer is usable.
- Spectra and the denoised waveform peaks are persisted in the analysis cache so
  a re-opened file is "prepared" instantly without re-running the model.

---

## 5. Designing the automatic noise-reduction dial-in

Goal: instead of the user picking a noise-reduction percentage, **measure the
separation between voice and background and choose the amount automatically** —
more reduction when the background is loud, less (e.g. 40–50 %) when the voice is
already clearly intelligible and only needs cleanup.

### 5.1 What we already have to work with

Everything needed to *estimate* background level is already computed and cached:

| Signal | Source | Meaning |
|---|---|---|
| `drySpectrum()` | original audio | voice **+** background |
| `spectrum()` (denoised) | DeepFilterNet output | voice with background largely removed |
| `inputLufs()`, `inputPeakDb()` | load-time measurement | overall level of the dry signal |
| `previewSpectrum(amount)` | power blend | what a chosen amount would look like |

The **difference between the dry and denoised spectra is, by construction, an
estimate of what the denoiser considers background/noise.** A large dry-minus-
denoised gap (especially outside the speech core) means a loud, separable
background → push the amount up. A small gap means the model found little to
remove → the voice is already clean → a gentler 40–50 % blend just tidies it.

This is the same dry-vs-denoised comparison the noise-aware auto-EQ already does
in `computeNoiseAwareAutoEqBands`; the auto-dial is a sibling of that function.

### 5.2 Suggested approach (broad-stroke, for discussion)

1. **Compute a "noise floor / separation" metric** from `drySpectrum()` vs.
   `spectrum()`. Candidates:
   - Broadband energy removed: `bandDb(dry) - bandDb(denoised)` over the full
     range, or weighted toward non-speech bands (below ~200 Hz and above
     ~6 kHz, where noise lives and speech doesn't).
   - A crude SNR proxy: denoised energy in the speech core vs. removed energy
     elsewhere.
2. **Map the metric to an amount** with a saturating curve (loud background →
   toward ~90–100 %; clean voice → floor around ~40–50 %, never 0 unless the
   removed energy is negligible). Keep the mapping in `core/` next to
   `noiseReductionControlToBlend` so CLI and UI share it and it's testable.
3. **Plumb it through** the way auto-EQ already flows: compute it when both
   profiles exist (after `refreshVoiceProfileFromDenoised`), expose a
   `suggestedNoiseReductionAmount()` on `ProcessingEngine`, and let the UI either
   apply it automatically or seed the encoder with it.
4. **Validate visually** with `previewSpectrum(amount)` and the existing spectrum
   view before committing.

### 5.3 Architectural fit / constraints

- **Keep it in `vc_core`, pure and testable.** The metric→amount mapping should
  be a free function over two `SpectrumResult`s (plus maybe LUFS), mirroring
  `computeNoiseAwareAutoEqBands`. Add a behavior test in
  [`tests/DspBehaviorTests.cpp`](../tests/DspBehaviorTests.cpp).
- **Timing.** A reliable estimate needs the *full* denoised profile, i.e. after
  the background pass finishes. At load you can offer a provisional value from
  the dry spectrum alone (high background → high default), then refine once
  denoised. Mirror how auto-EQ starts at dry and upgrades to noise-aware.
- **Don't fight the user.** Decide whether the auto value seeds the control
  (user can override) or continuously drives it. The current code treats the
  encoder as the source of truth (`buildParams` reads the slider), so the
  cleanest first version *sets the slider once* when analysis completes.
- **Caching.** If the chosen amount is derived purely from cached spectra it
  doesn't need separate persistence, but storing the computed suggestion in the
  analysis-cache JSON (bump `kAnalysisCacheVersion`) avoids recomputing and keeps
  the value stable across reopens.

### 5.4 Open questions to settle before implementing

- Should the metric be broadband, or band-weighted toward known noise regions?
- What are the floor/ceiling amounts (e.g. 40 % min, 95 % max) and the shape of
  the mapping between them?
- Auto-apply vs. suggest-and-seed?
- Does music presence affect the estimate? (Music is mixed *after* the chain, so
  it shouldn't bias the voice spectra — but worth confirming.)
</content>
</invoke>
