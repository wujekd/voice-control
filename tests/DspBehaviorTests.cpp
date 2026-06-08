#include "AudioBuffer.h"
#include "Ducker.h"
#include "Eq.h"
#include "LoudnessNormalizer.h"
#include "PitchDetector.h"
#include "Presets.h"
#include "SpectrumAnalyzer.h"
#include "VoiceChain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

vc::SpectrumResult makeRelativeSpectrum(double lowRel, double mudRel, double presRel, double airRel) {
    vc::SpectrumResult s;
    s.sampleRate = 48000.0;
    s.fftSize = 4096;
    s.valid = true;
    const int bins = s.fftSize / 2;
    s.binPower.assign(static_cast<std::size_t>(bins), 1.0);
    s.binDb.assign(static_cast<std::size_t>(bins), 0.0f);

    auto setBand = [&](double f1, double f2, double relDb) {
        const double binHz = s.sampleRate / s.fftSize;
        const int lo = std::max(1, static_cast<int>(std::floor(f1 / binHz)));
        const int hi = std::min(bins - 1, static_cast<int>(std::ceil(f2 / binHz)));
        const double power = std::pow(10.0, relDb / 10.0);
        for (int k = lo; k <= hi; ++k) {
            s.binPower[static_cast<std::size_t>(k)] = power;
            s.binDb[static_cast<std::size_t>(k)] = static_cast<float>(relDb);
        }
    };

    setBand(60.0, 200.0, lowRel);
    setBand(200.0, 450.0, mudRel);
    setBand(3000.0, 6000.0, presRel);
    setBand(8000.0, 14000.0, airRel);
    return s;
}

bool expect(bool condition, const std::string& message) {
    if (!condition)
        std::cerr << "FAIL: " << message << "\n";
    return condition;
}

// A harmonic-rich periodic tone (decaying harmonics) at the given fundamental —
// a stand-in for a voiced speech segment with a clear pitch.
vc::AudioBuffer makeHarmonicTone(double f0Hz, double gain = 0.3,
                                 int harmonics = 8, int sampleRate = 48000,
                                 double seconds = 1.5) {
    vc::AudioBuffer b;
    b.sampleRate = sampleRate;
    const std::size_t frames = static_cast<std::size_t>(seconds * sampleRate);
    b.channels.assign(1, std::vector<float>(frames, 0.0f));
    const double twoPi = 2.0 * 3.14159265358979323846;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        double s = 0.0;
        for (int h = 1; h <= harmonics; ++h)
            s += (1.0 / h) * std::sin(twoPi * f0Hz * h * t);
        b.channels[0][i] = static_cast<float>(s * gain);
    }
    return b;
}

// A tone whose 2nd harmonic dominates the fundamental — the classic octave trap
// that fools naive autocorrelation/FFT-peak pitch detectors.
vc::AudioBuffer makeOctaveTrap(double f0Hz, double gain = 0.3,
                               int sampleRate = 48000, double seconds = 1.5) {
    vc::AudioBuffer b;
    b.sampleRate = sampleRate;
    const std::size_t frames = static_cast<std::size_t>(seconds * sampleRate);
    b.channels.assign(1, std::vector<float>(frames, 0.0f));
    const double twoPi = 2.0 * 3.14159265358979323846;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double fund = 0.4 * std::sin(twoPi * f0Hz * t);
        const double second = 1.0 * std::sin(twoPi * 2.0 * f0Hz * t);
        const double third = 0.6 * std::sin(twoPi * 3.0 * f0Hz * t);
        b.channels[0][i] = static_cast<float>((fund + second + third) * gain);
    }
    return b;
}

vc::AudioBuffer makeWhiteNoise(double gain = 0.2, int sampleRate = 48000,
                               double seconds = 1.5) {
    vc::AudioBuffer b;
    b.sampleRate = sampleRate;
    const std::size_t frames = static_cast<std::size_t>(seconds * sampleRate);
    b.channels.assign(1, std::vector<float>(frames, 0.0f));
    std::uint32_t state = 0x12345678u;
    for (std::size_t i = 0; i < frames; ++i) {
        state = state * 1664525u + 1013904223u; // LCG
        const double r = (state >> 8) / static_cast<double>(1u << 24) - 0.5;
        b.channels[0][i] = static_cast<float>(r * 2.0 * gain);
    }
    return b;
}

// Frequency of the first band of the given type in a band list (0 if absent).
double bandFreq(const std::vector<vc::EqBand>& bands, vc::EqBand::Type type) {
    for (const auto& b : bands)
        if (b.type == type)
            return b.freq;
    return 0.0;
}

// Flat spectrum with a strong boost below `edgeHz`, so the low and low-mid
// windows read hot relative to the speech-core anchor for any layout — i.e. both
// a low shelf and a low-mid peak get emitted, letting us assert their placement.
vc::SpectrumResult makeLowHeavySpectrum(double edgeHz, double boostDb) {
    vc::SpectrumResult s;
    s.sampleRate = 48000.0;
    s.fftSize = 4096;
    s.valid = true;
    const int bins = s.fftSize / 2;
    s.binPower.assign(static_cast<std::size_t>(bins), 1.0);
    s.binDb.assign(static_cast<std::size_t>(bins), 0.0f);
    const double binHz = s.sampleRate / s.fftSize;
    const int hi = std::min(bins - 1, static_cast<int>(edgeHz / binHz));
    const double power = std::pow(10.0, boostDb / 10.0);
    for (int k = 1; k <= hi; ++k) {
        s.binPower[static_cast<std::size_t>(k)] = power;
        s.binDb[static_cast<std::size_t>(k)] = static_cast<float>(boostDb);
    }
    return s;
}

// Drives a single-channel Ducker with a steady tone (the "music") and a
// constant-amplitude voice key, then returns the RMS of the second half of the
// output (past the detector/filter settling transient).
double duckedToneRms(double freqHz, double blend, double keyAmp, double maxReductionDb) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr int sr = 48000;
    constexpr int N = sr; // 1 second
    vc::Ducker d;
    d.prepare(sr, 1);
    d.setMaxReductionDb(maxReductionDb);
    d.setLookAheadMs(0.0);
    d.setBlend(blend);

    std::vector<float> music(N), key(N);
    for (int i = 0; i < N; ++i) {
        music[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(2.0 * kPi * freqHz * i / sr));
        key[static_cast<std::size_t>(i)] = static_cast<float>(keyAmp);
    }
    for (int s = 0; s < N; s += 512) {
        const int n = std::min(512, N - s);
        float* ch[1] = { music.data() + s };
        d.process(ch, 1, n, key.data() + s);
    }
    double sum = 0.0;
    int count = 0;
    for (int i = N / 2; i < N; ++i) {
        sum += static_cast<double>(music[static_cast<std::size_t>(i)]) * music[static_cast<std::size_t>(i)];
        ++count;
    }
    return std::sqrt(sum / std::max(1, count));
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

    const auto dullVoice = makeRelativeSpectrum(-2.0, -1.0, -10.0, -10.0);
    const auto cleanDry = makeRelativeSpectrum(-2.0, -1.0, -10.0, -10.0);
    const auto harshDry = makeRelativeSpectrum(-2.0, -1.0, 0.0, -10.0);
    const auto cleanEq = vc::computeNoiseAwareAutoEqBands(dullVoice, cleanDry, 1.0);
    const auto harshEq = vc::computeNoiseAwareAutoEqBands(dullVoice, harshDry, 1.0);

    auto presenceBoost = [](const std::vector<vc::EqBand>& bands) {
        double boost = 0.0;
        for (const auto& band : bands)
            if (band.type == vc::EqBand::Type::Peak && std::fabs(band.freq - 4000.0) < 1.0)
                boost = std::max(boost, band.gainDb);
        return boost;
    };

    ok &= expect(presenceBoost(cleanEq) > 1.0,
                 "clean dull vocal may receive a presence boost");
    ok &= expect(presenceBoost(harshEq) < 1.0,
                 "presence boost should be suppressed when dry signal already has harsh upper-mid energy");

    // ---- Voice fundamental (pitch) detection ----
    const auto malePitch = vc::PitchDetector::detectFundamental(makeHarmonicTone(120.0));
    ok &= expect(malePitch.valid && std::fabs(malePitch.f0Hz - 120.0) < 4.0,
                 "YIN should detect a 120 Hz fundamental within a few Hz");

    const auto femalePitch = vc::PitchDetector::detectFundamental(makeHarmonicTone(220.0));
    ok &= expect(femalePitch.valid && std::fabs(femalePitch.f0Hz - 220.0) < 6.0,
                 "YIN should detect a 220 Hz fundamental within a few Hz");

    const auto trapPitch = vc::PitchDetector::detectFundamental(makeOctaveTrap(150.0));
    ok &= expect(trapPitch.valid && std::fabs(trapPitch.f0Hz - 150.0) < 8.0,
                 "YIN should track the true fundamental despite a dominant 2nd harmonic (no octave jump)");

    const auto noisePitch = vc::PitchDetector::detectFundamental(makeWhiteNoise());
    ok &= expect(!noisePitch.valid,
                 "white noise should not yield a confident fundamental");

    // ---- F0-relative auto-EQ placement ----
    const auto lowHeavy = makeLowHeavySpectrum(900.0, 8.0);
    const auto fixedBands = vc::computeAutoEqBands(lowHeavy, 1.0, 0.0);
    ok &= expect(std::fabs(bandFreq(fixedBands, vc::EqBand::Type::LowShelf) - 200.0) < 1.0,
                 "f0=0 keeps the legacy 200 Hz low shelf");
    ok &= expect(std::fabs(bandFreq(fixedBands, vc::EqBand::Type::Peak) - 320.0) < 1.0,
                 "f0=0 keeps the legacy 320 Hz low-mid peak");

    const auto pitchedBands = vc::computeAutoEqBands(lowHeavy, 1.0, 220.0);
    ok &= expect(std::fabs(bandFreq(pitchedBands, vc::EqBand::Type::LowShelf) - 220.0) < 1.0,
                 "a 220 Hz fundamental places the low shelf at the fundamental");
    ok &= expect(std::fabs(bandFreq(pitchedBands, vc::EqBand::Type::Peak) - 550.0) < 1.0,
                 "a 220 Hz fundamental places the low-mid peak near the 2nd-3rd harmonic (~2.5x)");

    // ---- Background music ducking ----
    const double unityRms = 1.0 / std::sqrt(2.0); // RMS of a unit-amplitude sine

    // Silent voice key leaves the music essentially untouched.
    const double quietMusic = duckedToneRms(1000.0, 0.0, 0.0, 12.0);
    ok &= expect(quietMusic > unityRms * 0.9,
                 "a silent voice key leaves the backing music near unity");

    // A loud voice key ducks full-band music well below unity.
    const double duckedMusic = duckedToneRms(1000.0, 0.0, 0.5, 12.0);
    ok &= expect(duckedMusic < unityRms * 0.6,
                 "a loud voice key ducks full-band music");

    // Mid-only blend (100%) protects the low end while still ducking the mids.
    const double midOnlyLow = duckedToneRms(80.0, 1.0, 0.5, 12.0);
    const double midOnlyMid = duckedToneRms(1000.0, 1.0, 0.5, 12.0);
    ok &= expect(midOnlyLow > midOnlyMid + 0.1,
                 "mid-only ducking protects the low end more than the mid band");
    ok &= expect(midOnlyLow > unityRms * 0.75,
                 "mid-only ducking leaves a low-frequency bassline mostly intact");

    if (!ok)
        return 1;

    std::cout << "DSP behavior tests passed\n";
    return 0;
}
