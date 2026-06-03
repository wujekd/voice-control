# Noise Removal Research

This area is for denoise and dereverb experiments before they graduate into the
C++ processing core.

The aim is not to make speech more flattering here. The aim is to remove
unwanted noise and room sound while preserving the voice.

## Current experiments

- `experiments/denoise_deepfilternet`: first serious denoise evaluation track.

## Direction

1. Select candidates from published real-time neural noise-removal work.
2. Evaluate the strongest pretrained candidate on real voiceover files.
3. Measure quality, artifacts, latency, CPU use, and model/runtime fit.
4. Decide whether to embed, port, distill, or train our own variant.
5. Host the chosen model/runtime in `core/` behind a small real-time wrapper.

Keep experiments separate from the plugin until they meet three bars:

- low enough latency for preview;
- stable enough CPU use for real-time playback;
- audible improvement without obvious speech damage.
