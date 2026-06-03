#pragma once

#include "AudioBuffer.h"
#include "Biquad.h"
#include "Eq.h"

#include <vector>

namespace vc {

// Applies a list of EqBands as cascaded biquads. Biquads are preallocated up to
// a fixed maximum, so configure() (which sets the active band count and their
// coefficients) never allocates and is safe to call from a live block callback.
class EqSection {
public:
    static constexpr int kMaxBands = 8;

    void prepare(int numChannels);                       // allocate + reset
    void configure(int sampleRate, const std::vector<EqBand>& bands); // no alloc
    void reset();

    void process(AudioBuffer& buffer);
    void process(float* const* channels, int numChannels, int numFrames);

private:
    int numChannels_ = 0;
    int activeBands_ = 0;
    // filters_[band][channel]
    std::vector<std::vector<Biquad>> filters_;
};

} // namespace vc
