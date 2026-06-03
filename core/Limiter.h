#pragma once

#include "AudioBuffer.h"

namespace vc {

// Brick-wall peak limiter. Offline (look-ahead-free) design: it computes a
// per-sample gain that never exceeds what the ceiling requires, then slew-
// limits that gain both forward (release) and backward (attack). The backward
// pass lets the gain start dropping *before* a peak with zero added latency,
// so the ceiling is mathematically guaranteed without clicks.
class Limiter {
public:
    void prepare(int sampleRate, double ceilingDb, double attackMs, double releaseMs);
    void process(AudioBuffer& buffer);

private:
    double ceilingLin_ = 0.89125; // -1 dBFS
    double attackStep_ = 0.01;    // max gain rise per sample (backward pass)
    double releaseStep_ = 0.001;  // max gain rise per sample (forward pass)
};

} // namespace vc
