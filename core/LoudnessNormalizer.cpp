#include "LoudnessNormalizer.h"

#include "Biquad.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace vc {
namespace {

// K-weighting biquad coefficients per BS.1770, designed at 48 kHz.
// The processing pipeline always feeds 48 kHz, so these are exact here; at
// other rates they are a close approximation (good enough for gain targeting).
void makeKWeighting(Biquad& shelf, Biquad& hpf) {
    shelf.setCoefficients(1.53512485958697, -2.69169618940638, 1.19839281085285,
                          -1.69065929318241, 0.73248077421585);
    hpf.setCoefficients(1.0, -2.0, 1.0,
                        -1.99004745483398, 0.99007225036621);
}

constexpr double kAbsoluteGate = -70.0; // LUFS
constexpr double kRelativeGate = -10.0; // LU below the ungated mean

} // namespace

void LoudnessNormalizer::prepare(int sampleRate, double targetLufs) {
    sampleRate_ = sampleRate;
    targetLufs_ = targetLufs;
}

double LoudnessNormalizer::measureIntegratedLufs(const AudioBuffer& buffer) const {
    const int channels = buffer.numChannels();
    const std::size_t frames = buffer.numFrames();
    if (channels == 0 || frames == 0)
        return -std::numeric_limits<double>::infinity();

    // K-weight a copy of every channel.
    std::vector<std::vector<float>> kw(channels);
    for (int ch = 0; ch < channels; ++ch) {
        Biquad shelf, hpf;
        makeKWeighting(shelf, hpf);
        const auto& src = buffer.channels[ch];
        auto& dst = kw[ch];
        dst.resize(frames);
        for (std::size_t i = 0; i < frames; ++i)
            dst[i] = shelf.process(hpf.process(src[i]));
    }

    // 400 ms blocks, 100 ms hop (75% overlap).
    const std::size_t blockLen = static_cast<std::size_t>(0.400 * sampleRate_);
    const std::size_t hop = static_cast<std::size_t>(0.100 * sampleRate_);
    if (blockLen == 0 || frames < blockLen)
        return -std::numeric_limits<double>::infinity();

    // z = mean-square (summed across channels with weight 1.0 for L/R/mono).
    std::vector<double> blockZ;
    for (std::size_t start = 0; start + blockLen <= frames; start += hop) {
        double z = 0.0;
        for (int ch = 0; ch < channels; ++ch) {
            const float* d = kw[ch].data() + start;
            double sumSq = 0.0;
            for (std::size_t i = 0; i < blockLen; ++i)
                sumSq += static_cast<double>(d[i]) * d[i];
            z += sumSq / static_cast<double>(blockLen);
        }
        blockZ.push_back(z);
    }
    if (blockZ.empty())
        return -std::numeric_limits<double>::infinity();

    auto loudnessOf = [](double z) { return -0.691 + 10.0 * std::log10(z + 1e-12); };

    // Absolute gate at -70 LUFS.
    double sumAbs = 0.0;
    std::size_t nAbs = 0;
    for (double z : blockZ) {
        if (loudnessOf(z) >= kAbsoluteGate) { sumAbs += z; ++nAbs; }
    }
    if (nAbs == 0)
        return -std::numeric_limits<double>::infinity();

    // Relative gate: -10 LU below the absolute-gated mean loudness.
    const double relThreshold = loudnessOf(sumAbs / nAbs) + kRelativeGate;
    double sumRel = 0.0;
    std::size_t nRel = 0;
    for (double z : blockZ) {
        if (loudnessOf(z) >= kAbsoluteGate && loudnessOf(z) >= relThreshold) {
            sumRel += z; ++nRel;
        }
    }
    if (nRel == 0)
        return -std::numeric_limits<double>::infinity();

    return loudnessOf(sumRel / nRel);
}

void LoudnessNormalizer::process(AudioBuffer& buffer) {
    const double measured = measureIntegratedLufs(buffer);
    lastInputLufs_ = measured;

    if (!std::isfinite(measured)) {
        lastAppliedGainDb_ = 0.0; // silence: leave untouched
        return;
    }

    // Corrective gain, clamped to avoid blowing up near-silent material.
    double gainDb = targetLufs_ - measured;
    gainDb = std::clamp(gainDb, -30.0, 30.0);
    lastAppliedGainDb_ = gainDb;

    const double g = std::pow(10.0, gainDb / 20.0);
    const int channels = buffer.numChannels();
    const std::size_t frames = buffer.numFrames();
    for (int ch = 0; ch < channels; ++ch)
        for (std::size_t i = 0; i < frames; ++i)
            buffer.channels[ch][i] = static_cast<float>(buffer.channels[ch][i] * g);
}

} // namespace vc
