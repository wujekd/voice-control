#include "Denoiser.h"
#include "WavIo.h"

// Diagnostic tool for comparing DeepFilterNet C API runtime defaults against
// the official Rust CLI render path. Not linked into the app.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

vc::AudioBuffer renderNative(const vc::AudioBuffer& source,
                             const std::string& modelPath,
                             float attenLimitDb,
                             float postFilterBeta) {
    constexpr int kDelayHops = 3;
    constexpr int kWarmupHops = 20;

    vc::Denoiser denoiser(modelPath, attenLimitDb, postFilterBeta);
    if (!denoiser.valid())
        throw std::runtime_error("DeepFilterNet model failed to load");

    const int hop = denoiser.hop();
    const int channels = std::max(1, source.numChannels());
    const auto frames = static_cast<std::int64_t>(source.numFrames());
    const int hops = frames > 0 ? static_cast<int>((frames + hop - 1) / hop) : 0;

    vc::AudioBuffer out;
    out.sampleRate = source.sampleRate;
    out.channels.assign(static_cast<std::size_t>(channels),
                        std::vector<float>(static_cast<std::size_t>(frames), 0.0f));

    std::vector<float> monoIn(static_cast<std::size_t>(hop), 0.0f);
    std::vector<float> monoOut(static_cast<std::size_t>(hop), 0.0f);

    auto processHop = [&](int g, bool discard) {
        const int inHop = g + kDelayHops;
        const std::int64_t inBase = static_cast<std::int64_t>(inHop) * hop;

        for (int j = 0; j < hop; ++j) {
            const std::int64_t f = inBase + j;
            float mono = 0.0f;
            if (f >= 0 && f < frames) {
                for (int ch = 0; ch < channels; ++ch)
                    mono += source.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(f)];
                mono /= static_cast<float>(channels);
            }
            monoIn[static_cast<std::size_t>(j)] = mono;
        }

        denoiser.processHop(monoIn.data(), monoOut.data());
        if (discard)
            return;

        const std::int64_t outBase = static_cast<std::int64_t>(g) * hop;
        for (int j = 0; j < hop; ++j) {
            const std::int64_t f = outBase + j;
            if (f >= frames)
                break;
            const float v = monoOut[static_cast<std::size_t>(j)];
            for (int ch = 0; ch < channels; ++ch)
                out.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(f)] = v;
        }
    };

    int lastInputHop = -1;
    for (int g = 0; g < hops; ++g) {
        const int inHop = g + kDelayHops;
        if (inHop != lastInputHop + 1) {
            for (int k = std::max(0, inHop - kWarmupHops); k < inHop; ++k)
                processHop(k - kDelayHops, true);
        }
        processHop(g, false);
        lastInputHop = inHop;
    }

    return out;
}

vc::AudioBuffer renderNativeCliStyle(const vc::AudioBuffer& source,
                                     const std::string& modelPath,
                                     float attenLimitDb,
                                     float postFilterBeta) {
    vc::Denoiser denoiser(modelPath, attenLimitDb, postFilterBeta);
    if (!denoiser.valid())
        throw std::runtime_error("DeepFilterNet model failed to load");

    const int hop = denoiser.hop();
    const int channels = std::max(1, source.numChannels());
    const auto frames = static_cast<std::int64_t>(source.numFrames());
    const int fullHops = static_cast<int>(frames / hop);
    const int delayFrames = 3 * hop;
    const auto outFrames = std::max<std::int64_t>(0, frames - delayFrames);

    vc::AudioBuffer raw;
    raw.sampleRate = source.sampleRate;
    raw.channels.assign(static_cast<std::size_t>(channels),
                        std::vector<float>(static_cast<std::size_t>(frames), 0.0f));

    std::vector<float> monoIn(static_cast<std::size_t>(hop), 0.0f);
    std::vector<float> monoOut(static_cast<std::size_t>(hop), 0.0f);

    for (int g = 0; g < fullHops; ++g) {
        const std::int64_t base = static_cast<std::int64_t>(g) * hop;
        for (int j = 0; j < hop; ++j) {
            float mono = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                mono += source.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(base + j)];
            monoIn[static_cast<std::size_t>(j)] = mono / static_cast<float>(channels);
        }

        denoiser.processHop(monoIn.data(), monoOut.data());
        for (int j = 0; j < hop; ++j) {
            const std::int64_t f = base + j;
            const float v = monoOut[static_cast<std::size_t>(j)];
            for (int ch = 0; ch < channels; ++ch)
                raw.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(f)] = v;
        }
    }

    vc::AudioBuffer out;
    out.sampleRate = source.sampleRate;
    out.channels.assign(static_cast<std::size_t>(channels),
                        std::vector<float>(static_cast<std::size_t>(outFrames), 0.0f));
    for (int ch = 0; ch < channels; ++ch) {
        const auto& src = raw.channels[static_cast<std::size_t>(ch)];
        auto& dst = out.channels[static_cast<std::size_t>(ch)];
        for (std::int64_t i = 0; i < outFrames; ++i)
            dst[static_cast<std::size_t>(i)] = src[static_cast<std::size_t>(i + delayFrames)];
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 5 && argc != 6 && argc != 7) {
        std::fprintf(stderr, "usage: %s <input.wav> <output.wav> <atten_db> <pf_beta> [model.tar.gz] [cli-style]\n", argv[0]);
        return 2;
    }

    try {
        const auto input = vc::readWav(argv[1]);
        const auto model = argc >= 6 ? std::string(argv[5]) : vc::Denoiser::findDefaultModel();
        const float atten = std::atof(argv[3]);
        const float pf = std::atof(argv[4]);
        const bool cliStyle = argc == 7 && std::string(argv[6]) == "cli-style";
        auto output = cliStyle
            ? renderNativeCliStyle(input, model, atten, pf)
            : renderNative(input, model, atten, pf);
        vc::writeWavFloat32(argv[2], output);
        std::printf("wrote %s atten=%.1f pf=%.3f\n", argv[2], atten, pf);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
