#pragma once

#include "AudioBuffer.h"

namespace vc {

// Result of whole-file fundamental-frequency (F0) estimation.
struct PitchResult {
    bool   valid = false;     // true when enough voiced frames were found
    double f0Hz = 0.0;        // median F0 over voiced frames
    double confidence = 0.0;  // voicedFrames / analyzedFrames, 0..1
    int    voicedFrames = 0;
};

// Estimates the speaking fundamental from a (preferably denoised) voice buffer.
//
// Uses the YIN algorithm (de Cheveigné & Kawahara, 2002): per-frame difference
// function -> cumulative mean normalized difference -> absolute threshold, with
// parabolic interpolation. The CMND step plus a median across voiced frames make
// the estimate resistant to the octave jumps that plague raw autocorrelation.
//
// fMin/fMax bound the search; defaults span deep male to high female/child.
class PitchDetector {
public:
    static PitchResult detectFundamental(const AudioBuffer& buffer,
                                         double fMin = 70.0,
                                         double fMax = 500.0);
};

} // namespace vc
