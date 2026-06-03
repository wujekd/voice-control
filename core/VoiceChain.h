#pragma once

#include "AudioBuffer.h"
#include "Biquad.h"
#include "Compressor.h"
#include "DeEsser.h"
#include "EqSection.h"
#include "Limiter.h"
#include "LoudnessNormalizer.h"
#include "Presets.h"

#include <vector>

namespace vc {

// The portable voice-enhancement chain:
//   pre-gain -> high-pass -> EQ -> peak comp -> glue comp -> de-esser
//   -> loudness -> limiter
// No JUCE / platform dependencies — reusable from CLI, JUCE, or a backend.
class VoiceChain {
public:
    void prepare(int sampleRate, int numChannels, const ChainParams& params);

    // Processes the buffer in place. `prepare` must have been called with a
    // matching channel count and sample rate.
    void process(AudioBuffer& buffer);

    // Loudness diagnostics from the last process() call.
    double measuredInputLufs() const { return loudness_.lastInputLufs(); }
    double appliedLoudnessGainDb() const { return loudness_.lastAppliedGainDb(); }
    float fastCompReductionDb() const { return fastComp_.currentReductionDb(); }
    float glueCompReductionDb() const { return glueComp_.currentReductionDb(); }
    float deEssReductionDb() const { return deEsser_.currentReductionDb(); }

private:
    ChainParams params_;
    std::vector<Biquad> highpass_; // one per channel
    EqSection eq_;
    Compressor fastComp_;
    Compressor glueComp_;
    DeEsser deEsser_;
    LoudnessNormalizer loudness_;
    Limiter limiter_;
};

} // namespace vc
