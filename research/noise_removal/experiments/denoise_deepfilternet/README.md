# Denoise Experiment: DeepFilterNet

This is the first serious denoising experiment.

The goal is to evaluate DeepFilterNet as a production candidate for removing
noise from voiceovers and recordings, not to build a toy baseline.

## What this experiment answers

- Does it remove realistic room, fan, street, keyboard, handling, and camera
  noise without shredding consonants?
- Does it preserve the speaker identity and natural tone?
- Does it create musical noise, watery tails, pumping, or dullness?
- How much latency does the real-time path imply?
- How fast does it load?
- Can its runtime be embedded or ported cleanly into the C++ plugin?

## Evidence Behind This Pick

DeepFilterNet is selected first because the project and papers target real-time
full-band 48 kHz noise suppression, include pretrained models, and provide both
Python and Rust paths.

See:

- `../../candidate_research.md`
- https://github.com/Rikorose/DeepFilterNet

## Layout

- `configs/evaluation.json`: files, model choice, and scoring checklist.
- `scripts/run_deepfilternet.py`: wrapper around installed DeepFilterNet tools.
- `data/input`: noisy WAV files to evaluate.
- `data/output`: processed WAV files.
- `data/reference`: optional clean references.
- `notes`: listening notes and model/runtime findings.

## Setup

Use either the prebuilt DeepFilterNet binary or the Python package.

Binary path:

```sh
deep-filter --help
```

Python path:

```sh
python3 -m venv research/noise_removal/.venv
research/noise_removal/.venv/bin/python -m pip install -r research/noise_removal/requirements.txt
deepFilter --help
```

The binary currently expects 48 kHz WAV files. Keep source files at 48 kHz for
this experiment so we are evaluating the model in its intended operating mode.

## Run

From the repository root:

```sh
python3 research/noise_removal/experiments/denoise_deepfilternet/scripts/run_deepfilternet.py \
  --input research/noise_removal/experiments/denoise_deepfilternet/data/input \
  --output research/noise_removal/experiments/denoise_deepfilternet/data/output
```

The wrapper prefers `deep-filter` if installed, then falls back to `deepFilter`.

## Pass Criteria

DeepFilterNet stays the leading candidate if it:

- gives clearly better results than the unprocessed input on real voiceover
  samples;
- keeps artifacts acceptable at moderate and strong noise levels;
- runs with plugin-feasible latency and CPU;
- has a credible integration path into the C++ core.
