#include "AudioBuffer.h"
#include "LoudnessNormalizer.h"
#include "Presets.h"
#include "VoiceChain.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

vc::AudioBuffer makeVoiceLike(double gain, int sampleRate = 48000, double seconds = 2.0) {
    vc::AudioBuffer b;
    b.sampleRate = sampleRate;
    const std::size_t frames = static_cast<std::size_t>(seconds * sampleRate);
    b.channels.assign(1, std::vector<float>(frames, 0.0f));

    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double body = std::sin(2.0 * 3.14159265358979323846 * 180.0 * t);
        const double presence = 0.35 * std::sin(2.0 * 3.14159265358979323846 * 900.0 * t);
        const double envelope = 0.55 + 0.45 * std::sin(2.0 * 3.14159265358979323846 * 2.3 * t);
        b.channels[0][i] = static_cast<float>((body + presence) * envelope * gain);
    }

    return b;
}

vc::AudioBuffer makeSibilantVoice(double gain, int sampleRate = 48000, double seconds = 2.0) {
    auto b = makeVoiceLike(gain, sampleRate, seconds);
    const std::size_t frames = b.numFrames();

    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double phase = std::fmod(t, 0.5);
        if (phase < 0.08) {
            const double burst = std::sin(2.0 * 3.14159265358979323846 * 7200.0 * t);
            const double env = std::sin(3.14159265358979323846 * phase / 0.08);
            b.channels[0][i] += static_cast<float>(burst * env * gain * 1.2);
        }
    }

    return b;
}

double measureLufs(const vc::AudioBuffer& b) {
    vc::LoudnessNormalizer meter;
    meter.prepare(b.sampleRate, 0.0);
    return meter.measureIntegratedLufs(b);
}

double peakAbs(const vc::AudioBuffer& b) {
    double peak = 0.0;
    for (const auto& ch : b.channels)
        for (float s : ch)
            peak = std::max(peak, static_cast<double>(std::fabs(s)));
    return peak;
}

bool expect(bool condition, const std::string& message) {
    if (!condition)
        std::cerr << "FAIL: " << message << "\n";
    return condition;
}

} // namespace

int main() {
    bool ok = true;

    auto quiet = makeVoiceLike(0.03);
    auto hot = makeVoiceLike(0.6);

    auto quietParams = vc::fixedVoiceCleanupParams();
    auto hotParams = vc::fixedVoiceCleanupParams();

    quietParams.inputCalibrationGainDb = vc::computeCalibrationGainDb(measureLufs(quiet), quietParams);
    hotParams.inputCalibrationGainDb = vc::computeCalibrationGainDb(measureLufs(hot), hotParams);

    ok &= expect(quietParams.inputCalibrationGainDb > 0.0,
                 "quiet source should receive positive calibration gain");
    ok &= expect(hotParams.inputCalibrationGainDb < 0.0,
                 "hot source should receive negative calibration gain");

    auto lowIntensity = quiet;
    auto highIntensity = quiet;
    auto lowParams = quietParams;
    auto highParams = quietParams;
    vc::applyIntensity(lowParams, 0.0);
    vc::applyIntensity(highParams, 1.0);

    ok &= expect(std::fabs(lowParams.deEssRangeDb - highParams.deEssRangeDb) < 1e-9,
                 "intensity should not change de-esser range");
    ok &= expect(lowParams.baseAutoEqStrength > 0.0,
                 "minimum intensity should still apply corrective EQ");
    ok &= expect(std::fabs(vc::noiseReductionControlToBlend(0.5) - 0.6) < 0.02,
                 "noise reduction control should reach the useful range earlier");
    ok &= expect(vc::noiseReductionControlToBlend(0.0) == 0.0
                 && vc::noiseReductionControlToBlend(1.0) == 1.0,
                 "noise reduction control should preserve its endpoints");

    vc::VoiceChain lowChain;
    lowChain.prepare(lowIntensity.sampleRate, lowIntensity.numChannels(), lowParams);
    lowChain.process(lowIntensity);

    vc::VoiceChain highChain;
    highChain.prepare(highIntensity.sampleRate, highIntensity.numChannels(), highParams);
    highChain.process(highIntensity);

    ok &= expect(highChain.fastCompReductionDb() >= lowChain.fastCompReductionDb(),
                 "higher intensity should not reduce fast compressor activity");
    ok &= expect(highChain.glueCompReductionDb() >= lowChain.glueCompReductionDb(),
                 "higher intensity should not reduce glue compressor activity");
    ok &= expect(peakAbs(highIntensity) <= 0.92,
                 "limiter should keep peaks below about -1 dBFS");

    const double lowLufs = measureLufs(lowIntensity);
    const double highLufs = measureLufs(highIntensity);
    ok &= expect(std::fabs(lowLufs - highLufs) < 2.0,
                 "low and high intensity should remain roughly loudness matched");

    auto sibilant = makeSibilantVoice(0.08);
    auto deEssParams = vc::fixedVoiceCleanupParams();
    deEssParams.inputCalibrationGainDb = vc::computeCalibrationGainDb(measureLufs(sibilant), deEssParams);
    vc::applyIntensity(deEssParams, 1.0);

    vc::VoiceChain deEssChain;
    deEssChain.prepare(sibilant.sampleRate, sibilant.numChannels(), deEssParams);
    deEssChain.process(sibilant);

    ok &= expect(deEssChain.deEssReductionDb() > 2.0f,
                 "sibilant source should visibly trigger the de-esser");

    if (!ok)
        return 1;

    std::cout << "DSP behavior tests passed\n";
    return 0;
}
