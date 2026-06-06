#pragma once

#include "Biquad.h"

#include <vector>

namespace vc {

// Sidechain ducker for backing music. The voice is the key: when the voice is
// present the music is pulled down. A look-ahead delay on the music lets the
// duck start slightly before the voice transient (the detector reads the
// undelayed key while the music it controls is delayed).
//
// The "blend" morphs from full-band ducking (0) to mid-band-only ducking (1):
// at blend 1 only the mid band (kCrossLoHz..kCrossHiHz) ducks, so the music's
// low end and highs pass through — a dynamic-EQ-style duck. One band-pass of
// the music covers both modes:
//
//     out = m - (1 - g) * ( m*(1 - blend) + mid*blend )
//
// where m is the (delayed) music sample, mid its band-passed component, and g
// the live duck gain (<= 1). Allocation-free in process(); call from the audio
// thread after prepare().
class Ducker {
public:
    static constexpr double kCrossLoHz = 250.0;  // mid band lower edge
    static constexpr double kCrossHiHz = 4000.0; // mid band upper edge

    void prepare(double sampleRate, int numChannels);
    void reset();

    void setLookAheadMs(double ms);
    void setMaxReductionDb(double db) { maxReductionDb_ = db < 0.0 ? 0.0 : db; }
    void setBlend(double blend01) { blend_ = blend01 < 0.0 ? 0.0 : (blend01 > 1.0 ? 1.0 : blend01); }

    // Ducks `music` in place (numChannels planar pointers, numSamples each)
    // using `keyMono` (numSamples) as the sidechain key.
    void process(float* const* music, int numChannels, int numSamples, const float* keyMono);

    // Smoothed reduction (positive dB) from the last processed sample, for the
    // UI meter / display.
    float lastReductionDb() const { return lastReductionDb_; }

private:
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;

    // Detector
    double env_ = 0.0;
    double attackCoeff_ = 0.0;
    double releaseCoeff_ = 0.0;
    double maxReductionDb_ = 9.0;
    double blend_ = 0.0;
    float lastReductionDb_ = 0.0f;

    // Band-pass per channel: HPF (kCrossLoHz) then LPF (kCrossHiHz).
    std::vector<Biquad> hp_, lp_;

    // Look-ahead delay line: one ring per channel.
    std::vector<std::vector<float>> delayLine_;
    int delayWrite_ = 0;
    int delaySamples_ = 0;
    int maxDelaySamples_ = 0;
};

} // namespace vc
