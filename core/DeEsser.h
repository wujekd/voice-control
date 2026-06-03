#pragma once

#include "AudioBuffer.h"
#include "Biquad.h"

#include <atomic>
#include <vector>

namespace vc {

// Split-band de-esser. The signal is split into low and high bands at a
// crossover (~6 kHz): high = x - lowpass(x), so low + high == x exactly when
// no reduction is applied. The high (sibilance) band's level drives a
// compressor that ducks only that band, so de-essing never dulls the body of
// the voice. Stereo-linked detector preserves the image.
//
// prepare() allocates (call once); configure() updates parameters allocation-
// free and without resetting state, so it is safe to call from a live block
// callback when a slider moves.
class DeEsser {
public:
    void prepare(int sampleRate, int numChannels, double crossoverHz,
                 double thresholdDb, double ratio, double attackMs,
                 double releaseMs, double rangeDb);
    void configure(int sampleRate, double crossoverHz, double thresholdDb,
                   double ratio, double attackMs, double releaseMs, double rangeDb);
    void reset();

    void process(AudioBuffer& buffer);
    void process(float* const* channels, int numChannels, int numFrames);

    float currentReductionDb() const { return meterReductionDb_.load(std::memory_order_relaxed); }

private:
    std::vector<Biquad> lowpass_;     // one per channel, defines the band split
    std::vector<float> highScratch_;  // per-channel high band, preallocated
    double thresholdDb_ = -30.0;
    double ratio_ = 4.0;
    double rangeDb_ = 12.0; // max attenuation of the high band
    double attackCoeff_ = 0.0;
    double releaseCoeff_ = 0.0;
    double envDb_ = 0.0; // smoothed high-band gain reduction in dB (<= 0)
    std::atomic<float> meterReductionDb_ { 0.0f };
};

} // namespace vc
