# Listening Review

Small local A/B review tool for noise-removal experiments.

It serves a browser page with native audio players for original, processed, and
optional reference WAV files. Notes are saved as JSON next to the reviewed
experiment.

## Run

From the repository root:

```sh
python3 research/noise_removal/tools/listening_review/server.py \
  --experiment research/noise_removal/experiments/denoise_deepfilternet
```

Then open:

```text
http://127.0.0.1:8765
```

Expected experiment layout:

```text
data/input/*.wav
data/output/*.wav
data/reference/*.wav
```
