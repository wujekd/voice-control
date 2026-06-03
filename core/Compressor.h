#pragma once

#include "AudioBuffer.h"

#include <atomic>

namespace vc {

// Feed-forward soft-knee compressor. Stereo-linked: the detector uses the
// peak across all channels and one gain is applied to every channel, so the
// stereo image is preserved. Controls short-term dynamics only — absolute
// level is handled separately by the loudness stage.
//
// Usable offline (process(AudioBuffer&)) and in a real-time block callback
// (process(float* const*, ...)). configure() changes parameters without
// touching the envelope state, so live parameter moves don't click.
class Compressor {
public:
    void prepare(int sampleRate, double thresholdDb, double ratio,
                 double attackMs, double releaseMs, double makeupDb, double kneeDb);
    void configure(int sampleRate, double thresholdDb, double ratio,
                   double attackMs, double releaseMs, double makeupDb, double kneeDb);
    void reset();

    void process(AudioBuffer& buffer);
    void process(float* const* channels, int numChannels, int numFrames);

    // Peak gain reduction (positive dB) observed in the last processed block.
    float currentReductionDb() const { return meterReductionDb_.load(std::memory_order_relaxed); }

private:
    double thresholdDb_ = -20.0;
    double ratio_ = 3.0;
    double kneeDb_ = 6.0;
    double makeupLin_ = 1.0;
    double attackCoeff_ = 0.0;
    double releaseCoeff_ = 0.0;
    double envDb_ = 0.0; // smoothed gain reduction in dB (<= 0)
    std::atomic<float> meterReductionDb_ { 0.0f };
};

} // namespace vc
