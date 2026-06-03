#include "LiveLimiter.h"

#include <algorithm>
#include <cmath>

namespace vc {

void LiveLimiter::prepare(int sampleRate, int numChannels, double lookaheadMs) {
    lookahead_ = std::max(1, static_cast<int>(lookaheadMs / 1000.0 * sampleRate));
    delay_.assign(static_cast<std::size_t>(std::max(1, numChannels)),
                  std::vector<float>(static_cast<std::size_t>(lookahead_), 0.0f));

    dqCap_ = lookahead_ + 2;
    dqVal_.assign(static_cast<std::size_t>(dqCap_), 0.0f);
    dqIdx_.assign(static_cast<std::size_t>(dqCap_), 0);
    reset();
}

void LiveLimiter::configure(int sampleRate, double ceilingDb, double releaseMs) {
    ceilingLin_ = std::pow(10.0, ceilingDb / 20.0);
    const double sr = static_cast<double>(sampleRate);
    releaseStep_ = 1.0 / std::max(1.0, (releaseMs / 1000.0) * sr);
}

void LiveLimiter::reset() {
    pos_ = 0;
    sampleIndex_ = 0;
    gain_ = 1.0;
    dqHead_ = 0;
    dqSize_ = 0;
    for (auto& d : delay_)
        std::fill(d.begin(), d.end(), 0.0f);
    meterReductionDb_.store(0.0f, std::memory_order_relaxed);
}

void LiveLimiter::process(float* const* channels, int numChannels, int numFrames) {
    if (numChannels <= 0) return;
    const int nch = std::min(numChannels, static_cast<int>(delay_.size()));
    if (nch <= 0) return;

    double maxReduction = 0.0;

    for (int i = 0; i < numFrames; ++i) {
        const long k = sampleIndex_++;

        // Linked peak of the incoming sample.
        double peak = 0.0;
        for (int ch = 0; ch < nch; ++ch)
            peak = std::max(peak, static_cast<double>(std::fabs(channels[ch][i])));
        const float pk = static_cast<float>(peak);

        // Monotonic (decreasing) deque: drop smaller values at the back...
        while (dqSize_ > 0) {
            const int back = (dqHead_ + dqSize_ - 1) % dqCap_;
            if (dqVal_[static_cast<std::size_t>(back)] <= pk) --dqSize_;
            else break;
        }
        const int slot = (dqHead_ + dqSize_) % dqCap_;
        dqVal_[static_cast<std::size_t>(slot)] = pk;
        dqIdx_[static_cast<std::size_t>(slot)] = k;
        ++dqSize_;

        // ...and expire entries older than the window [k - lookahead, k].
        while (dqSize_ > 0 && dqIdx_[static_cast<std::size_t>(dqHead_)] < k - lookahead_) {
            dqHead_ = (dqHead_ + 1) % dqCap_;
            --dqSize_;
        }

        const double windowMax = dqVal_[static_cast<std::size_t>(dqHead_)];
        const double target = (windowMax > ceilingLin_) ? (ceilingLin_ / windowMax) : 1.0;

        // Instant attack (guarantees the ceiling), linear release.
        gain_ = std::min(target, gain_ + releaseStep_);
        maxReduction = std::max(maxReduction, -20.0 * std::log10(std::max(gain_, 1e-6)));

        for (int ch = 0; ch < nch; ++ch) {
            float& s = delay_[static_cast<std::size_t>(ch)][pos_];
            const float delayed = s;
            s = channels[ch][i];
            channels[ch][i] = static_cast<float>(delayed * gain_);
        }
        pos_ = (pos_ + 1) % static_cast<std::size_t>(lookahead_);
    }

    meterReductionDb_.store(static_cast<float>(maxReduction), std::memory_order_relaxed);
}

} // namespace vc
