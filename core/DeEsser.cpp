#include "DeEsser.h"

#include <algorithm>
#include <cmath>

namespace vc {

void DeEsser::configure(int sampleRate, double crossoverHz, double thresholdDb,
                        double ratio, double attackMs, double releaseMs, double rangeDb) {
    thresholdDb_ = thresholdDb;
    ratio_ = std::max(1.0, ratio);
    rangeDb_ = std::max(0.0, rangeDb);

    for (auto& bq : lowpass_)
        bq.setLowpass(sampleRate, crossoverHz);

    const double sr = static_cast<double>(sampleRate);
    attackCoeff_ = std::exp(-1.0 / (std::max(0.0001, attackMs / 1000.0) * sr));
    releaseCoeff_ = std::exp(-1.0 / (std::max(0.0001, releaseMs / 1000.0) * sr));
}

void DeEsser::reset() {
    envDb_ = 0.0;
    for (auto& bq : lowpass_)
        bq.reset();
    meterReductionDb_.store(0.0f, std::memory_order_relaxed);
}

void DeEsser::prepare(int sampleRate, int numChannels, double crossoverHz,
                      double thresholdDb, double ratio, double attackMs,
                      double releaseMs, double rangeDb) {
    lowpass_.assign(static_cast<std::size_t>(numChannels), Biquad{});
    highScratch_.assign(static_cast<std::size_t>(std::max(1, numChannels)), 0.0f);
    configure(sampleRate, crossoverHz, thresholdDb, ratio, attackMs, releaseMs, rangeDb);
    reset();
}

void DeEsser::process(float* const* channels, int numChannels, int numFrames) {
    if (numChannels <= 0) return;
    const int nch = std::min(numChannels, static_cast<int>(lowpass_.size()));
    if (nch <= 0) return;

    const double T = thresholdDb_;
    const double slope = (1.0 / ratio_) - 1.0; // <= 0
    double maxReduction = 0.0;

    for (int i = 0; i < numFrames; ++i) {
        // Band split per channel, and find the linked high-band peak.
        double peak = 0.0;
        for (int ch = 0; ch < nch; ++ch) {
            const float x = channels[ch][i];
            const float low = lowpass_[static_cast<std::size_t>(ch)].process(x);
            const float hi = x - low;
            highScratch_[static_cast<std::size_t>(ch)] = hi;
            peak = std::max(peak, static_cast<double>(std::fabs(hi)));
        }

        const double levelDb = 20.0 * std::log10(peak + 1e-12);

        double gainDb = (levelDb > T) ? slope * (levelDb - T) : 0.0;
        gainDb = std::max(gainDb, -rangeDb_);

        const double coeff = (gainDb < envDb_) ? attackCoeff_ : releaseCoeff_;
        envDb_ = coeff * envDb_ + (1.0 - coeff) * gainDb;
        maxReduction = std::max(maxReduction, -envDb_);

        const double g = std::pow(10.0, envDb_ / 20.0);
        for (int ch = 0; ch < nch; ++ch) {
            const float hi = highScratch_[static_cast<std::size_t>(ch)];
            const float low = channels[ch][i] - hi;
            channels[ch][i] = static_cast<float>(low + g * hi);
        }
    }

    meterReductionDb_.store(static_cast<float>(maxReduction), std::memory_order_relaxed);
}

void DeEsser::process(AudioBuffer& buffer) {
    const int channels = buffer.numChannels();
    if (channels <= 0) return;

    float* ptrs[32];
    const int n = std::min(channels, 32);
    for (int ch = 0; ch < n; ++ch)
        ptrs[ch] = buffer.channels[static_cast<std::size_t>(ch)].data();
    process(ptrs, n, static_cast<int>(buffer.numFrames()));
}

} // namespace vc
