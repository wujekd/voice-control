#include "Eq.h"

#include <algorithm>
#include <cmath>

namespace vc {

namespace {

struct BalanceBands {
    double lowRel = 0.0;
    double mudRel = 0.0;
    double presRel = 0.0;
    double airRel = 0.0;
};

BalanceBands measureBalanceBands(const SpectrumResult& spectrum) {
    const double anchor = spectrum.bandDb(300.0, 3000.0);
    return {
        spectrum.bandDb(60.0, 200.0)     - anchor,
        spectrum.bandDb(200.0, 450.0)    - anchor,
        spectrum.bandDb(3000.0, 6000.0)  - anchor,
        spectrum.bandDb(8000.0, 14000.0) - anchor,
    };
}

} // namespace

std::vector<EqBand> tonePresetBands(Tone tone) {
    switch (tone) {
        case Tone::Warm:
            return toneAmountBands(-1.0);
        case Tone::Crisp:
            return toneAmountBands(1.0);
        case Tone::Natural:
        default:
            return {};
    }
}

std::vector<EqBand> toneAmountBands(double amount) {
    amount = std::clamp(amount, -1.0, 1.0);
    if (std::fabs(amount) < 0.001)
        return {};

    if (amount < 0.0) {
        const double s = -amount;
        return {
            { EqBand::Type::LowShelf,  180.0,  3.0 * s,  0.7071 }, // body
            { EqBand::Type::HighShelf, 7000.0, -2.0 * s, 0.7071 }, // soften
        };
    }

    return {
        { EqBand::Type::LowShelf,  220.0,  -1.5 * amount, 0.7071 }, // trim mud
        { EqBand::Type::HighShelf, 6000.0,  4.0 * amount, 0.7071 }, // air
    };
}

std::vector<EqBand> computeAutoEqBands(const SpectrumResult& spectrum, double strength) {
    std::vector<EqBand> bands;
    if (!spectrum.valid) return bands;

    strength = std::clamp(strength, 0.0, 1.0);

    const auto balance = measureBalanceBands(spectrum);

    // Target balance for clean speech (relative to anchor). Favour low-end
    // control; high bands sit naturally well below the anchor.
    constexpr double lowTarget  = -2.0;
    constexpr double mudTarget  = -1.0;
    constexpr double presTarget = -3.0;
    constexpr double airTarget  = -10.0;

    constexpr double kDeadzone = 1.0;
    constexpr double kMaxCut = 6.0;     // generous cuts (controlling excess is safe)
    constexpr double kMaxBoost = 4.0;   // conservative boosts
    constexpr double kEmptyFloor = -18.0; // below this a band is ~empty: don't boost it

    auto add = [&](EqBand::Type type, double freq, double q, double measuredRel,
                   double target) {
        double gain = (target - measuredRel) * strength;
        // Never boost a near-empty band (would just amplify noise/hiss).
        if (measuredRel < kEmptyFloor) gain = std::min(gain, 0.0);
        gain = std::clamp(gain, -kMaxCut, kMaxBoost);
        if (std::fabs(gain) >= kDeadzone)
            bands.push_back({ type, freq, gain, q });
    };

    add(EqBand::Type::LowShelf,  200.0,  0.7071, balance.lowRel,  lowTarget);
    add(EqBand::Type::Peak,      320.0,  0.9,    balance.mudRel,  mudTarget);
    add(EqBand::Type::Peak,      4000.0, 0.9,    balance.presRel, presTarget);
    add(EqBand::Type::HighShelf, 8000.0, 0.7071, balance.airRel,  airTarget);

    return bands;
}

std::vector<EqBand> computeNoiseAwareAutoEqBands(const SpectrumResult& voiceSpectrum,
                                                 const SpectrumResult& drySpectrum,
                                                 double strength) {
    if (!voiceSpectrum.valid)
        return computeAutoEqBands(drySpectrum, strength);
    if (!drySpectrum.valid)
        return computeAutoEqBands(voiceSpectrum, strength);

    std::vector<EqBand> bands;
    strength = std::clamp(strength, 0.0, 1.0);

    const auto voice = measureBalanceBands(voiceSpectrum);
    const auto dry = measureBalanceBands(drySpectrum);

    constexpr double lowTarget  = -2.0;
    constexpr double mudTarget  = -1.0;
    constexpr double presTarget = -3.0;
    constexpr double airTarget  = -10.0;

    constexpr double kDeadzone = 1.0;
    constexpr double kMaxCut = 6.0;
    constexpr double kMaxBoost = 4.0;
    constexpr double kEmptyFloor = -18.0;

    auto backgroundBoostScale = [](double voiceRel, double dryRel, double target) {
        double scale = 1.0;

        // If the dry signal already has enough energy in this band, boosting
        // the isolated voice would mostly brighten whatever noise reduction
        // later blends back in.
        if (dryRel >= target - 0.5)
            scale *= 0.25;

        // If denoise removed a lot from the band, assume the missing energy was
        // background content and become progressively more conservative.
        const double removedByDenoise = dryRel - voiceRel;
        if (removedByDenoise > 2.0)
            scale *= std::clamp(1.0 - (removedByDenoise - 2.0) / 8.0, 0.0, 1.0);

        return scale;
    };

    auto add = [&](EqBand::Type type, double freq, double q, double voiceRel,
                   double dryRel, double target, bool riskyBoost) {
        double gain = (target - voiceRel) * strength;
        if (voiceRel < kEmptyFloor)
            gain = std::min(gain, 0.0);

        if (gain > 0.0 && riskyBoost)
            gain *= backgroundBoostScale(voiceRel, dryRel, target);

        // Low boosts can bring up rumble just as easily as body. If the original
        // signal is already low-heavy, do not add more low end.
        if (gain > 0.0 && type != EqBand::Type::Peak && dryRel >= target)
            gain = 0.0;

        gain = std::clamp(gain, -kMaxCut, kMaxBoost);
        if (std::fabs(gain) >= kDeadzone)
            bands.push_back({ type, freq, gain, q });
    };

    add(EqBand::Type::LowShelf,  200.0,  0.7071, voice.lowRel,  dry.lowRel,  lowTarget,  false);
    add(EqBand::Type::Peak,      320.0,  0.9,    voice.mudRel,  dry.mudRel,  mudTarget,  false);
    add(EqBand::Type::Peak,      4000.0, 0.9,    voice.presRel, dry.presRel, presTarget, true);
    add(EqBand::Type::HighShelf, 8000.0, 0.7071, voice.airRel,  dry.airRel,  airTarget,  true);

    return bands;
}

void configureBiquad(Biquad& bq, const EqBand& band, double sampleRate) {
    switch (band.type) {
        case EqBand::Type::LowShelf:
            bq.setLowShelf(sampleRate, band.freq, band.gainDb, band.q);
            break;
        case EqBand::Type::HighShelf:
            bq.setHighShelf(sampleRate, band.freq, band.gainDb, band.q);
            break;
        case EqBand::Type::Peak:
        default:
            bq.setPeaking(sampleRate, band.freq, band.gainDb, band.q);
            break;
    }
}

double eqResponseDb(const std::vector<EqBand>& bands, double freqHz, double sampleRate) {
    double total = 0.0;
    Biquad bq;
    for (const auto& band : bands) {
        configureBiquad(bq, band, sampleRate);
        total += bq.magnitudeDb(freqHz, sampleRate);
    }
    return total;
}

} // namespace vc
