#include "Limiter.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vc {

void Limiter::prepare(int sampleRate, double ceilingDb, double attackMs, double releaseMs) {
    ceilingLin_ = std::pow(10.0, ceilingDb / 20.0);
    const double sr = static_cast<double>(sampleRate);
    attackStep_ = 1.0 / std::max(1.0, (attackMs / 1000.0) * sr);
    releaseStep_ = 1.0 / std::max(1.0, (releaseMs / 1000.0) * sr);
}

void Limiter::process(AudioBuffer& buffer) {
    const int channels = buffer.numChannels();
    const std::size_t frames = buffer.numFrames();
    if (channels == 0 || frames == 0) return;

    // Desired gain per sample: <= 1, just enough to keep the linked peak
    // under the ceiling.
    std::vector<double> g(frames, 1.0);
    for (std::size_t i = 0; i < frames; ++i) {
        double peak = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            peak = std::max(peak, static_cast<double>(std::fabs(buffer.channels[ch][i])));
        if (peak > ceilingLin_)
            g[i] = ceilingLin_ / peak;
    }

    // Forward pass: limit how fast gain *rises* -> gradual release.
    for (std::size_t i = 1; i < frames; ++i)
        g[i] = std::min(g[i], g[i - 1] + releaseStep_);

    // Backward pass: limit how fast gain rises going backward -> gain drops
    // ahead of a peak (attack), still never above the desired gain.
    for (std::size_t i = frames - 1; i-- > 0;)
        g[i] = std::min(g[i], g[i + 1] + attackStep_);

    for (std::size_t i = 0; i < frames; ++i) {
        const double gain = g[i];
        for (int ch = 0; ch < channels; ++ch)
            buffer.channels[ch][i] = static_cast<float>(buffer.channels[ch][i] * gain);
    }
}

} // namespace vc
