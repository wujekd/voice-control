#pragma once

#include "AudioBuffer.h"

#include <vector>

namespace vc {

// Long-term average magnitude spectrum of a whole signal (Welch's method:
// averaged overlapping windowed FFTs). Portable — uses a small built-in FFT,
// no JUCE. Computed once at load (offline); feeds the auto-EQ and the GUI.
struct SpectrumResult {
    double sampleRate = 48000.0;
    int fftSize = 4096;
    bool valid = false;
    std::vector<double> binPower; // mean power per bin, size fftSize/2
    std::vector<float> binDb;     // 10*log10(binPower), for display

    // Mean power across [f1,f2], expressed in dB. Used for band balancing.
    double bandDb(double f1, double f2) const;

    // Interpolated display level (dB) at an arbitrary frequency.
    float dbAt(double freqHz) const;
};

class SpectrumAnalyzer {
public:
    // fftOrder 12 -> 4096-point FFT. Mixes channels to mono before analysis.
    static SpectrumResult analyze(const AudioBuffer& buffer, int fftOrder = 12);
};

} // namespace vc
