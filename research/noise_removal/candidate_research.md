# Denoise Candidate Research

This is the short list for serious noise-removal experiments. The selection
criteria are biased toward a desktop plugin: low latency, fast load, CPU-only
operation, source availability, pretrained weights, and a path to C++.

## Current leading candidate: DeepFilterNet

DeepFilterNet is the first experiment because it has the strongest practical
fit for this product:

- designed for full-band 48 kHz voice;
- built around real-time noise suppression;
- has pretrained models;
- has Python and Rust paths;
- includes a command-line binary and LADSPA real-time plugin example;
- permissive licensing path: MIT or Apache-2.0 according to the repository.

Evidence:

- https://github.com/Rikorose/DeepFilterNet
- https://arxiv.org/abs/2110.05588
- https://arxiv.org/abs/2205.05474
- https://arxiv.org/abs/2305.08227

The repo documents `deep-filter` as a precompiled binary for WAV noise
suppression, currently targeting 48 kHz WAV files. It also documents Python
usage through `deepFilter` and model training/evaluation tooling.

## Comparison candidate: RNNoise

RNNoise is still relevant because it is tiny, proven, and specifically designed
for low-complexity neural noise suppression. It is probably a useful latency and
CPU floor, but it may not be the quality ceiling for polished voiceover work.

Evidence:

- https://github.com/xiph/rnnoise
- https://www.mozillafoundation.org/en/research/library/a-hybrid-dspdeep-learning-approach-to-real-time-full-band-speech-enhancement/

## Comparison candidate: PercepNet

PercepNet is relevant because the published target is explicitly high-quality
full-band real-time processing with very low CPU use. It is important as an
architecture reference, even if the implementation path is less immediate than
DeepFilterNet.

Evidence:

- https://arxiv.org/abs/2008.04259
- https://arxiv.org/abs/2102.05245

## First decision

Start with DeepFilterNet. Do not build a toy denoiser first. The first useful
question is whether DeepFilterNet-quality noise removal can be made to feel like
a responsive plugin in this app.

If the answer is yes, we decide between:

- embedding its runtime/model directly;
- exporting to ONNX and hosting with a C++ runtime;
- distilling/training a smaller model inspired by it.

If the answer is no, compare RNNoise and PercepNet-style approaches as lower
latency model families.
