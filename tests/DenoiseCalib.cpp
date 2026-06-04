// Dev calibration tool (not shipped): runs the native DeepFilterNet wrapper over
// a 48 kHz mono WAV and measures the model's input->output net delay via
// cross-correlation, plus a coarse energy-reduction sanity check. Used to set
// DenoiseStreamer::delayHops_.
//
//   ./build/denoise-calib <in_48k_mono.wav>

#include "Denoiser.h"
#include "DenoiseStreamer.h"
#include "WavIo.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <in_48k_mono.wav> [model.tar.gz]\n", argv[0]);
        return 2;
    }
    const std::string model = vc::Denoiser::findDefaultModel();
    if (model.empty()) {
        std::fprintf(stderr, "model not found\n");
        return 1;
    }
    const float attenLim = argc >= 3 ? std::atof(argv[2]) : 100.0f;
    const float pfBeta = argc >= 4 ? std::atof(argv[3]) : 0.02f;
    std::printf("model: %s  attenLim=%.1f pfBeta=%.3f\n", model.c_str(), attenLim, pfBeta);

    vc::AudioBuffer in = vc::readWav(argv[1]);
    std::printf("input: %d Hz, %d ch, %zu frames\n", in.sampleRate, in.numChannels(), in.numFrames());
    if (in.numChannels() < 1) return 1;
    const auto& x = in.channels[0];
    const int n = static_cast<int>(x.size());

    vc::Denoiser df(model, attenLim, pfBeta);
    if (!df.valid()) { std::fprintf(stderr, "denoiser init failed\n"); return 1; }
    const int hop = df.hop();
    std::printf("hop: %d\n", hop);

    std::vector<float> y(static_cast<std::size_t>(n), 0.0f);
    std::vector<float> ibuf(static_cast<std::size_t>(hop), 0.0f);
    std::vector<float> obuf(static_cast<std::size_t>(hop), 0.0f);
    for (int base = 0; base < n; base += hop) {
        for (int j = 0; j < hop; ++j)
            ibuf[static_cast<std::size_t>(j)] = (base + j < n) ? x[static_cast<std::size_t>(base + j)] : 0.0f;
        df.processHop(ibuf.data(), obuf.data());
        for (int j = 0; j < hop && base + j < n; ++j)
            y[static_cast<std::size_t>(base + j)] = obuf[static_cast<std::size_t>(j)];
    }

    // Energy sanity: denoised should not be louder than input.
    double ex = 0.0, ey = 0.0;
    for (int i = 0; i < n; ++i) { ex += double(x[i]) * x[i]; ey += double(y[i]) * y[i]; }
    std::printf("rms in=%.5f out=%.5f (ratio %.3f)\n",
                std::sqrt(ex / n), std::sqrt(ey / n), std::sqrt(ey / std::max(1e-12, ex)));

    // Cross-correlate y against x over candidate lags; peak lag = net delay.
    const int maxLag = 2000; // ~42 ms
    int bestLag = 0; double best = -1e18;
    for (int lag = 0; lag <= maxLag; ++lag) {
        double dot = 0.0; int cnt = 0;
        for (int i = 0; i + lag < n; i += 7) { dot += double(y[i + lag]) * x[i]; ++cnt; }
        if (cnt > 0) dot /= cnt;
        if (dot > best) { best = dot; bestLag = lag; }
    }
    std::printf("best lag = %d samples (%.2f ms, %.2f hops), corr=%.6f\n",
                bestLag, 1000.0 * bestLag / in.sampleRate,
                double(bestLag) / hop, best);

    vc::AudioBuffer out;
    out.sampleRate = in.sampleRate;
    out.channels.push_back(y);
    vc::writeWavFloat32("/tmp/df_calib_out.wav", out);
    std::printf("wrote /tmp/df_calib_out.wav\n");

    // --- Verify the full DenoiseStreamer (delay-compensated, progressive) ---
    vc::DenoiseStreamer streamer;
    streamer.start(in, model);
    streamer.setPlayheadFrame(0);
    const auto t0 = std::chrono::steady_clock::now();
    streamer.waitUntilComplete();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const auto& sden = streamer.denoised().channels[0];

    int bestLag2 = -maxLag; double best2 = -1e18;
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        double dot = 0.0; int cnt = 0;
        for (int i = std::max(0, -lag); i + lag < n && i < n; i += 7) { dot += double(sden[i + lag]) * x[i]; ++cnt; }
        if (cnt > 0) dot /= cnt;
        if (dot > best2) { best2 = dot; bestLag2 = lag; }
    }
    const double audioSecs = double(n) / in.sampleRate;
    std::printf("streamer: denoised %d hops in %.2fs (%.1fx realtime), aligned lag = %d samples (%.1f ms)\n",
                streamer.numHops(), secs, audioSecs / std::max(1e-6, secs),
                bestLag2, 1000.0 * bestLag2 / in.sampleRate);
    streamer.stop();
    return 0;
}
