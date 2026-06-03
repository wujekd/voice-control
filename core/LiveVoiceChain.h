#pragma once

#include "Biquad.h"
#include "Compressor.h"
#include "DeEsser.h"
#include "EqSection.h"
#include "LiveLimiter.h"
#include "Presets.h"

#include <atomic>
#include <mutex>
#include <vector>

namespace vc {

// Real-time streaming version of the voice chain, for live preview:
//   pre-gain -> high-pass -> EQ -> peak comp -> glue comp -> de-esser
//   -> loudness gain -> limiter
//
// Differences from the offline VoiceChain:
//   * processes block-by-block, allocation-free (process() runs on the audio
//     thread);
//   * the limiter is the causal LiveLimiter (adds look-ahead latency);
//   * loudness is a single cached gain derived from the input's integrated
//     loudness (measured once, off-thread) and the target — not the exact
//     two-pass measurement (export still uses the offline chain for that).
//
// Parameters are pushed from the message thread via setParams()/
// setInputLoudness() and picked up by the audio thread without blocking it.
class LiveVoiceChain {
public:
    void prepare(int sampleRate, int numChannels); // allocate + reset (once)

    void setParams(const ChainParams& params);     // message thread
    void setInputLoudness(double integratedLufs);   // message thread

    void process(float* const* channels, int numChannels, int numFrames); // audio thread

    int latencySamples() const { return limiter_.latencySamples(); }
    float fastCompReductionDb() const { return fastComp_.currentReductionDb(); }
    float glueCompReductionDb() const { return glueComp_.currentReductionDb(); }
    float deEssReductionDb() const { return deEsser_.currentReductionDb(); }
    float limiterReductionDb() const { return limiter_.currentReductionDb(); }
    double loudnessGainDb() const { return loudnessGainDb_.load(std::memory_order_relaxed); }

private:
    void applyParams(const ChainParams& p);  // audio thread; no allocation
    void recomputeLoudnessGain();

    int sampleRate_ = 48000;
    int numChannels_ = 2;

    std::vector<Biquad> highpass_;
    EqSection eq_;
    Compressor fastComp_;
    Compressor glueComp_;
    DeEsser deEsser_;
    LiveLimiter limiter_;

    ChainParams active_;
    ChainParams pending_;
    bool paramsDirty_ = false;
    bool loudnessDirty_ = false;
    std::mutex paramMutex_;

    std::atomic<double> inputLufs_ { 0.0 };
    std::atomic<bool> inputLufsValid_ { false };
    double loudnessGainLin_ = 1.0;
    std::atomic<double> loudnessGainDb_ { 0.0 };
};

} // namespace vc
