#include "Compressor.h"

#include <algorithm>
#include <cmath>

namespace vc {

void Compressor::configure(int sampleRate, double thresholdDb, double ratio,
                           double attackMs, double releaseMs, double makeupDb,
                           double kneeDb) {
    thresholdDb_ = thresholdDb;
    ratio_ = std::max(1.0, ratio);
    kneeDb_ = std::max(0.0, kneeDb);
    makeupLin_ = std::pow(10.0, makeupDb / 20.0);

    const double sr = static_cast<double>(sampleRate);
    attackCoeff_ = std::exp(-1.0 / (std::max(0.0001, attackMs / 1000.0) * sr));
    releaseCoeff_ = std::exp(-1.0 / (std::max(0.0001, releaseMs / 1000.0) * sr));
}

void Compressor::reset() {
    envDb_ = 0.0;
    meterReductionDb_.store(0.0f, std::memory_order_relaxed);
}

void Compressor::prepare(int sampleRate, double thresholdDb, double ratio,
                         double attackMs, double releaseMs, double makeupDb,
                         double kneeDb) {
    configure(sampleRate, thresholdDb, ratio, attackMs, releaseMs, makeupDb, kneeDb);
    reset();
}

void Compressor::process(float* const* channels, int numChannels, int numFrames) {
    if (numChannels <= 0) return;

    const double T = thresholdDb_;
    const double W = kneeDb_;
    const double slope = (1.0 / ratio_) - 1.0; // <= 0
    double maxReduction = 0.0;

    for (int i = 0; i < numFrames; ++i) {
        // Linked detector: peak across all channels.
        double peak = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = std::max(peak, static_cast<double>(std::fabs(channels[ch][i])));

        const double levelDb = 20.0 * std::log10(peak + 1e-12);

        // Static curve -> gain reduction in dB (<= 0), with soft knee.
        double gainDb;
        if (2.0 * (levelDb - T) < -W) {
            gainDb = 0.0;
        } else if (2.0 * std::fabs(levelDb - T) <= W) {
            const double x = levelDb - T + W / 2.0;
            gainDb = slope * (x * x) / (2.0 * W);
        } else {
            gainDb = slope * (levelDb - T);
        }

        // Attack when reduction deepens, release when it recovers.
        const double coeff = (gainDb < envDb_) ? attackCoeff_ : releaseCoeff_;
        envDb_ = coeff * envDb_ + (1.0 - coeff) * gainDb;
        maxReduction = std::max(maxReduction, -envDb_);

        const double g = std::pow(10.0, envDb_ / 20.0) * makeupLin_;
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] = static_cast<float>(channels[ch][i] * g);
    }

    meterReductionDb_.store(static_cast<float>(maxReduction), std::memory_order_relaxed);
}

void Compressor::process(AudioBuffer& buffer) {
    const int channels = buffer.numChannels();
    if (channels <= 0) return;

    float* ptrs[32];
    const int n = std::min(channels, 32);
    for (int ch = 0; ch < n; ++ch)
        ptrs[ch] = buffer.channels[static_cast<std::size_t>(ch)].data();
    process(ptrs, n, static_cast<int>(buffer.numFrames()));
}

} // namespace vc
