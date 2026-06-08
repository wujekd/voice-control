#include "PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace vc {
namespace {

constexpr double kAbsoluteThreshold = 0.12; // YIN voiced-dip threshold
constexpr double kVoicedAperiodicity = 0.20; // accept frame only below this d'(tau)
constexpr double kFrameSeconds = 0.046;      // ~2 periods at 70 Hz
constexpr double kHopSeconds = 0.015;
constexpr int    kMaxAnalyzedFrames = 800;   // cap cost on long files

// Mixes all channels of one frame to mono.
void fillMonoFrame(const AudioBuffer& buffer, std::size_t start, std::size_t len,
                   std::vector<double>& out) {
    const int channels = buffer.numChannels();
    out.assign(len, 0.0);
    for (std::size_t i = 0; i < len; ++i) {
        double mono = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            mono += buffer.channels[static_cast<std::size_t>(ch)][start + i];
        out[i] = mono / std::max(1, channels);
    }
}

// Estimates F0 for one frame; returns <= 0 if the frame is unvoiced.
double estimateFrameF0(const std::vector<double>& x, double sampleRate,
                       int tauMin, int tauMax) {
    const int W = static_cast<int>(x.size()) - tauMax;
    if (W <= 0) return 0.0;

    // Difference function d(tau).
    std::vector<double> d(static_cast<std::size_t>(tauMax) + 1, 0.0);
    for (int tau = tauMin; tau <= tauMax; ++tau) {
        double sum = 0.0;
        for (int i = 0; i < W; ++i) {
            const double diff = x[static_cast<std::size_t>(i)]
                              - x[static_cast<std::size_t>(i + tau)];
            sum += diff * diff;
        }
        d[static_cast<std::size_t>(tau)] = sum;
    }

    // Cumulative mean normalized difference d'(tau).
    std::vector<double> dp(static_cast<std::size_t>(tauMax) + 1, 1.0);
    double running = 0.0;
    for (int tau = tauMin; tau <= tauMax; ++tau) {
        running += d[static_cast<std::size_t>(tau)];
        dp[static_cast<std::size_t>(tau)] = running > 0.0
            ? d[static_cast<std::size_t>(tau)] * static_cast<double>(tau - tauMin + 1) / running
            : 1.0;
    }

    // Absolute threshold: first local minimum that dips below the threshold;
    // otherwise the global minimum of d'.
    int bestTau = -1;
    for (int tau = tauMin + 1; tau < tauMax; ++tau) {
        if (dp[static_cast<std::size_t>(tau)] < kAbsoluteThreshold) {
            while (tau + 1 < tauMax
                   && dp[static_cast<std::size_t>(tau + 1)] < dp[static_cast<std::size_t>(tau)])
                ++tau;
            bestTau = tau;
            break;
        }
    }
    if (bestTau < 0) {
        double minVal = dp[static_cast<std::size_t>(tauMin)];
        bestTau = tauMin;
        for (int tau = tauMin + 1; tau <= tauMax; ++tau) {
            if (dp[static_cast<std::size_t>(tau)] < minVal) {
                minVal = dp[static_cast<std::size_t>(tau)];
                bestTau = tau;
            }
        }
    }

    if (dp[static_cast<std::size_t>(bestTau)] > kVoicedAperiodicity)
        return 0.0; // unvoiced / too aperiodic

    // Parabolic interpolation around bestTau for sub-sample precision.
    double tauEst = bestTau;
    if (bestTau > tauMin && bestTau < tauMax) {
        const double a = dp[static_cast<std::size_t>(bestTau - 1)];
        const double b = dp[static_cast<std::size_t>(bestTau)];
        const double c = dp[static_cast<std::size_t>(bestTau + 1)];
        const double denom = a - 2.0 * b + c;
        if (std::fabs(denom) > 1e-12)
            tauEst = bestTau + 0.5 * (a - c) / denom;
    }

    return tauEst > 0.0 ? sampleRate / tauEst : 0.0;
}

} // namespace

PitchResult PitchDetector::detectFundamental(const AudioBuffer& buffer,
                                             double fMin, double fMax) {
    PitchResult result;
    const double sampleRate = static_cast<double>(buffer.sampleRate);
    const std::size_t frames = buffer.numFrames();
    if (buffer.numChannels() == 0 || sampleRate <= 0.0 || fMax <= fMin)
        return result;

    const int tauMin = std::max(2, static_cast<int>(std::floor(sampleRate / fMax)));
    const int tauMax = static_cast<int>(std::ceil(sampleRate / fMin));
    const std::size_t window = static_cast<std::size_t>(
        std::max<double>(kFrameSeconds * sampleRate, 2.0 * tauMax));
    const std::size_t hop = std::max<std::size_t>(1,
        static_cast<std::size_t>(kHopSeconds * sampleRate));
    if (frames < window)
        return result;

    // Evenly stride if the file is long, so cost stays bounded on long takes.
    const std::size_t totalFrames = (frames - window) / hop + 1;
    const std::size_t stride = std::max<std::size_t>(1, totalFrames / kMaxAnalyzedFrames);

    std::vector<double> f0s;
    std::vector<double> frame;
    int analyzed = 0;
    for (std::size_t i = 0; i < totalFrames; i += stride) {
        const std::size_t start = i * hop;
        fillMonoFrame(buffer, start, window, frame);
        ++analyzed;

        const double f0 = estimateFrameF0(frame, sampleRate, tauMin, tauMax);
        if (f0 >= fMin && f0 <= fMax)
            f0s.push_back(f0);
    }

    if (analyzed == 0)
        return result;

    result.voicedFrames = static_cast<int>(f0s.size());
    result.confidence = static_cast<double>(f0s.size()) / static_cast<double>(analyzed);

    const std::size_t minVoiced = std::max<std::size_t>(8,
        static_cast<std::size_t>(0.1 * analyzed));
    if (f0s.size() < minVoiced)
        return result;

    std::sort(f0s.begin(), f0s.end());
    const std::size_t mid = f0s.size() / 2;
    result.f0Hz = (f0s.size() % 2 == 0)
        ? 0.5 * (f0s[mid - 1] + f0s[mid])
        : f0s[mid];
    result.valid = true;
    return result;
}

} // namespace vc
