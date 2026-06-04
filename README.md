# Voice Control

Voice Control is a desktop tool for cleaning up voice audio in videos and
recordings, then exporting a polished result.

It is aimed at creators who want a fast workflow for talking-head videos,
voiceovers, courses, podcasts, reels, and similar content without opening a full
DAW or video editor.

## What It Does

- Loads video or audio files.
- Extracts and previews the voice track.
- Removes background noise with a native, in-process DeepFilterNet pass that runs
  in the background during playback (faster than real time) — no waiting at load.
- Lets you smoothly blend between the original and denoised audio.
- Applies a voice cleanup chain:
  - high-pass filtering;
  - auto-EQ;
  - tone shaping;
  - compression;
  - de-essing;
  - loudness normalization;
  - limiting.
- Shows a live spectrum and gain-reduction meters.
- Adds backing music on a simple timeline.
- Supports multiple music clips with:
  - start time;
  - drag positioning;
  - edge resizing;
  - volume;
  - fade in;
  - fade out.
- Exports enhanced audio, or muxes enhanced audio back into the original video.

## Current Status

This is an early desktop prototype. The core audio chain is implemented in
portable C++, while the GUI is built with JUCE.

The denoiser runs DeepFilterNet3 natively in-process via the project's Rust
`deep_filter` C API (no Python at runtime). A background worker denoises the
loaded audio progressively, prioritizing wherever the playhead is, so playback
starts immediately on the dry signal and the denoised version is blended in live
per the noise-reduction amount. Export waits for the full file to finish.

## Project Layout

```text
core/          Portable voice DSP engine
desktop-juce/  JUCE desktop app
video/         FFmpeg wrapper for extract/export
app/           CLI entry point
tests/         DSP behavior tests
research/      Noise-removal experiments and listening tools
```

## Requirements

- macOS, currently the tested platform.
- CMake 3.16 or newer.
- A C++17 compiler.
- FFmpeg available on `PATH`.
- JUCE checked out at `third_party/JUCE`.
- DeepFilterNet checked out at `third_party/DeepFilterNet` (git submodule).
- A Rust toolchain plus `cargo-c` to build the native denoiser:
  ```sh
  brew install rust cargo-c        # or: rustup + `cargo install cargo-c`
  ```
- Python is only needed for the noise-removal *research* tools (below), not at
  runtime.

## Build

Fetch submodules (JUCE + DeepFilterNet), then build:

```sh
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --target voice-control-app
```

The CMake build compiles the DeepFilterNet C API static lib (`libdeepfilter.a`)
via `cargo cbuild` and links it in; the DeepFilterNet3 model ships in the
submodule at `third_party/DeepFilterNet/models/DeepFilterNet3_onnx.tar.gz` and is
resolved at runtime.

> Two one-time edits inside the `third_party/DeepFilterNet` submodule are needed
> (they are local working-tree changes, not committed upstream):
>
> 1. On a clean checkout the vendored `time` crate (0.3.28) may fail to compile
>    on recent rustc:
>    `cargo update -p time --manifest-path third_party/DeepFilterNet/libDF/Cargo.toml`
> 2. `libDF/src/capi.rs` `DFState::new` must set the model SNR thresholds to match
>    the reference `deep-filter` CLI — append
>    `.with_thresholds(-15., 35., 35.)` to the `RuntimeParams` builder. Without it
>    the C API's stricter defaults over-attenuate and add musical/granular noise.

The app bundle is produced at:

```text
build/voice-control-app_artefacts/Release/Voice Control.app
```

## Run Tests

```sh
cmake --build build --target vc-dsp-tests
./build/vc-dsp-tests
```

## CLI Usage

The CLI can process a video with the portable voice chain:

```sh
cmake --build build --target voice-control
./build/voice-control input.mp4 output.mp4 balanced natural
```

Presets:

```text
light | balanced | strong
```

Tone:

```text
natural | warm | crisp
```

## Denoise Research

Noise-removal experiments live in:

```text
research/noise_removal
```

The first serious candidate is DeepFilterNet3. A small browser-based listening
review tool is included:

```sh
python3 research/noise_removal/tools/listening_review/server.py \
  --experiment research/noise_removal/experiments/denoise_deepfilternet
```

Then open:

```text
http://127.0.0.1:8765
```

## Notes

- FFmpeg is used for decoding audio from video and remuxing exports.
- DeepFilterNet3 runs natively in-process (Rust `deep_filter` C API), streamed in
  the background during playback rather than as an external preprocessing tool.
- Backing music is intentionally simple: one music lane with multiple clips, not
  a full DAW.
- Music ducking is not implemented yet.

## License

No project license has been selected yet.
