#include "EqSection.h"

#include <algorithm>

namespace vc {

void EqSection::prepare(int numChannels) {
    numChannels_ = std::max(1, numChannels);
    filters_.assign(kMaxBands,
                    std::vector<Biquad>(static_cast<std::size_t>(numChannels_), Biquad{}));
    activeBands_ = 0;
}

void EqSection::configure(int sampleRate, const std::vector<EqBand>& bands) {
    activeBands_ = std::min(static_cast<int>(bands.size()), kMaxBands);
    for (int b = 0; b < activeBands_; ++b)
        for (int ch = 0; ch < numChannels_; ++ch)
            configureBiquad(filters_[static_cast<std::size_t>(b)][static_cast<std::size_t>(ch)],
                            bands[static_cast<std::size_t>(b)], sampleRate);
}

void EqSection::reset() {
    for (auto& band : filters_)
        for (auto& bq : band)
            bq.reset();
}

void EqSection::process(float* const* channels, int numChannels, int numFrames) {
    const int nch = std::min(numChannels, numChannels_);
    for (int b = 0; b < activeBands_; ++b) {
        for (int ch = 0; ch < nch; ++ch) {
            Biquad& bq = filters_[static_cast<std::size_t>(b)][static_cast<std::size_t>(ch)];
            float* d = channels[ch];
            for (int i = 0; i < numFrames; ++i)
                d[i] = bq.process(d[i]);
        }
    }
}

void EqSection::process(AudioBuffer& buffer) {
    const int channels = buffer.numChannels();
    if (channels <= 0) return;
    float* ptrs[32];
    const int n = std::min(channels, 32);
    for (int ch = 0; ch < n; ++ch)
        ptrs[ch] = buffer.channels[static_cast<std::size_t>(ch)].data();
    process(ptrs, n, static_cast<int>(buffer.numFrames()));
}

} // namespace vc
