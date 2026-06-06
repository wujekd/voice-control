#include "Ducker.h"

#include <algorithm>
#include <cmath>

namespace vc {

namespace {
// Detector envelope (linear amplitude, in dBFS) is mapped to reduction with a
// smoothstep between these thresholds: below thLo the music is untouched, above
// thHi the full knob reduction applies.
constexpr double kThLoDb = -45.0;
constexpr double kThHiDb = -15.0;
constexpr double kAttackSeconds = 0.010;
constexpr double kReleaseSeconds = 0.250;
constexpr double kMaxLookAheadSeconds = 0.050;

double onePoleCoeff(double tauSeconds, double sampleRate) {
    return 1.0 - std::exp(-1.0 / (sampleRate * tauSeconds));
}
} // namespace

void Ducker::prepare(double sampleRate, int numChannels) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels_ = numChannels > 0 ? numChannels : 1;

    attackCoeff_ = onePoleCoeff(kAttackSeconds, sampleRate_);
    releaseCoeff_ = onePoleCoeff(kReleaseSeconds, sampleRate_);

    hp_.assign(static_cast<std::size_t>(numChannels_), Biquad{});
    lp_.assign(static_cast<std::size_t>(numChannels_), Biquad{});
    for (int c = 0; c < numChannels_; ++c) {
        hp_[static_cast<std::size_t>(c)].setHighpass(sampleRate_, kCrossLoHz);
        lp_[static_cast<std::size_t>(c)].setLowpass(sampleRate_, kCrossHiHz);
    }

    maxDelaySamples_ = static_cast<int>(std::ceil(kMaxLookAheadSeconds * sampleRate_)) + 2;
    delayLine_.assign(static_cast<std::size_t>(numChannels_),
                      std::vector<float>(static_cast<std::size_t>(maxDelaySamples_), 0.0f));
    reset();
}

void Ducker::reset() {
    env_ = 0.0;
    lastReductionDb_ = 0.0f;
    delayWrite_ = 0;
    for (auto& bq : hp_) bq.reset();
    for (auto& bq : lp_) bq.reset();
    for (auto& line : delayLine_)
        std::fill(line.begin(), line.end(), 0.0f);
}

void Ducker::setLookAheadMs(double ms) {
    if (maxDelaySamples_ <= 0) { delaySamples_ = 0; return; }
    int s = static_cast<int>(std::lround(ms * 0.001 * sampleRate_));
    if (s < 0) s = 0;
    if (s > maxDelaySamples_ - 1) s = maxDelaySamples_ - 1;
    delaySamples_ = s;
}

void Ducker::process(float* const* music, int numChannels, int numSamples, const float* keyMono) {
    if (numChannels <= 0 || numSamples <= 0)
        return;
    const int ch = numChannels < numChannels_ ? numChannels : numChannels_;
    const bool delayed = maxDelaySamples_ > 0 && delaySamples_ > 0;

    for (int i = 0; i < numSamples; ++i) {
        // Detector on the undelayed key.
        const double rect = std::fabs(static_cast<double>(keyMono[i]));
        env_ += (rect - env_) * (rect > env_ ? attackCoeff_ : releaseCoeff_);
        const double envDb = 20.0 * std::log10(env_ + 1e-9);
        double t = (envDb - kThLoDb) / (kThHiDb - kThLoDb);
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        const double s = t * t * (3.0 - 2.0 * t); // smoothstep
        const double reductionDb = maxReductionDb_ * s;
        const double g = std::pow(10.0, -reductionDb / 20.0);
        lastReductionDb_ = static_cast<float>(reductionDb);

        for (int c = 0; c < ch; ++c) {
            float m = music[c][i];
            if (delayed) {
                std::vector<float>& line = delayLine_[static_cast<std::size_t>(c)];
                line[static_cast<std::size_t>(delayWrite_)] = m;
                int rd = delayWrite_ - delaySamples_;
                if (rd < 0) rd += maxDelaySamples_;
                m = line[static_cast<std::size_t>(rd)];
            }
            const float mid = lp_[static_cast<std::size_t>(c)].process(
                hp_[static_cast<std::size_t>(c)].process(m));
            const double weighted = m * (1.0 - blend_) + mid * blend_;
            music[c][i] = static_cast<float>(m - (1.0 - g) * weighted);
        }

        if (delayed && ++delayWrite_ >= maxDelaySamples_)
            delayWrite_ = 0;
    }
}

} // namespace vc
