#pragma once

#include "AudioBuffer.h"

namespace vc {

// Loudness normalisation to a target integrated loudness (LUFS), following
// ITU-R BS.1770 / EBU R128: K-weighting filters, 400 ms blocks with 75%
// overlap, absolute (-70 LUFS) and relative (-10 LU) gating. Two-pass:
// measure the whole buffer, then apply one corrective gain. Offline only.
class LoudnessNormalizer {
public:
    void prepare(int sampleRate, double targetLufs);

    // Measures the buffer and applies the gain needed to reach the target.
    void process(AudioBuffer& buffer);

    // Pure measurement (no modification). Returns integrated LUFS, or
    // -INFINITY if the signal is effectively silent / ungated.
    double measureIntegratedLufs(const AudioBuffer& buffer) const;

    // Diagnostics from the last process() call.
    double lastInputLufs() const { return lastInputLufs_; }
    double lastAppliedGainDb() const { return lastAppliedGainDb_; }

private:
    int sampleRate_ = 48000;
    double targetLufs_ = -16.0;
    double lastInputLufs_ = 0.0;
    double lastAppliedGainDb_ = 0.0;
};

} // namespace vc
