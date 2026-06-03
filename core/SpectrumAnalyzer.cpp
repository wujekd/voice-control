#include "SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace vc {
namespace {

// In-place iterative radix-2 FFT (N must be a power of two).
void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace

double SpectrumResult::bandDb(double f1, double f2) const {
    if (!valid || binPower.empty()) return -120.0;
    const double binHz = sampleRate / fftSize;
    const int lo = std::max(1, static_cast<int>(std::floor(f1 / binHz)));
    const int hi = std::min(static_cast<int>(binPower.size()) - 1,
                            static_cast<int>(std::ceil(f2 / binHz)));
    if (hi < lo) return -120.0;
    double sum = 0.0;
    for (int k = lo; k <= hi; ++k) sum += binPower[static_cast<std::size_t>(k)];
    const double mean = sum / static_cast<double>(hi - lo + 1);
    return 10.0 * std::log10(mean + 1e-12);
}

float SpectrumResult::dbAt(double freqHz) const {
    if (!valid || binDb.empty()) return -120.0f;
    const double binHz = sampleRate / fftSize;
    const double pos = freqHz / binHz;
    const int k = std::clamp(static_cast<int>(pos), 0, static_cast<int>(binDb.size()) - 2);
    const float frac = static_cast<float>(pos - k);
    return binDb[static_cast<std::size_t>(k)] * (1.0f - frac)
         + binDb[static_cast<std::size_t>(k + 1)] * frac;
}

SpectrumResult SpectrumAnalyzer::analyze(const AudioBuffer& buffer, int fftOrder) {
    SpectrumResult result;
    result.sampleRate = static_cast<double>(buffer.sampleRate);

    const int channels = buffer.numChannels();
    const std::size_t frames = buffer.numFrames();
    const std::size_t fftSize = static_cast<std::size_t>(1) << fftOrder;
    result.fftSize = static_cast<int>(fftSize);
    if (channels == 0 || frames < fftSize) return result;

    // Hann window.
    std::vector<double> window(fftSize);
    double winPow = 0.0;
    for (std::size_t i = 0; i < fftSize; ++i) {
        window[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (fftSize - 1));
        winPow += window[i] * window[i];
    }

    const std::size_t halfBins = fftSize / 2;
    std::vector<double> accum(halfBins, 0.0);
    const std::size_t hop = fftSize / 2;
    std::size_t windows = 0;

    std::vector<std::complex<double>> buf(fftSize);
    for (std::size_t start = 0; start + fftSize <= frames; start += hop) {
        for (std::size_t i = 0; i < fftSize; ++i) {
            double mono = 0.0;
            for (int ch = 0; ch < channels; ++ch)
                mono += buffer.channels[static_cast<std::size_t>(ch)][start + i];
            mono /= channels;
            buf[i] = std::complex<double>(mono * window[i], 0.0);
        }
        fft(buf);
        for (std::size_t k = 0; k < halfBins; ++k)
            accum[k] += std::norm(buf[k]); // |X|^2
        ++windows;
    }
    if (windows == 0) return result;

    const double norm = 1.0 / (static_cast<double>(windows) * winPow);
    result.binPower.resize(halfBins);
    result.binDb.resize(halfBins);
    for (std::size_t k = 0; k < halfBins; ++k) {
        const double p = accum[k] * norm;
        result.binPower[k] = p;
        result.binDb[k] = static_cast<float>(10.0 * std::log10(p + 1e-12));
    }
    result.valid = true;
    return result;
}

} // namespace vc
