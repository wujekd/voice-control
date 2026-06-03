#include "Eq.h"

#include <algorithm>
#include <cmath>

namespace vc {

std::vector<EqBand> tonePresetBands(Tone tone) {
    switch (tone) {
        case Tone::Warm:
            return {
                { EqBand::Type::LowShelf,  180.0,  3.0,  0.7071 }, // body
                { EqBand::Type::HighShelf, 7000.0, -2.0, 0.7071 }, // soften
            };
        case Tone::Crisp:
            return {
                { EqBand::Type::LowShelf,  220.0,  -1.5, 0.7071 }, // trim mud
                { EqBand::Type::HighShelf, 6000.0, 4.0,  0.7071 }, // air
            };
        case Tone::Natural:
        default:
            return {};
    }
}

std::vector<EqBand> computeAutoEqBands(const SpectrumResult& spectrum, double strength) {
    std::vector<EqBand> bands;
    if (!spectrum.valid) return bands;

    strength = std::clamp(strength, 0.0, 1.0);

    // Anchor: broadband speech reference the rest is balanced against.
    const double anchor = spectrum.bandDb(300.0, 3000.0);

    // Region levels relative to the anchor.
    const double lowRel  = spectrum.bandDb(60.0, 200.0)     - anchor; // boom / proximity
    const double mudRel  = spectrum.bandDb(200.0, 450.0)    - anchor; // mud
    const double presRel = spectrum.bandDb(3000.0, 6000.0)  - anchor; // presence
    const double airRel  = spectrum.bandDb(8000.0, 14000.0) - anchor; // air

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

    add(EqBand::Type::LowShelf,  200.0,  0.7071, lowRel,  lowTarget);
    add(EqBand::Type::Peak,      320.0,  0.9,    mudRel,  mudTarget);
    add(EqBand::Type::Peak,      4000.0, 0.9,    presRel, presTarget);
    add(EqBand::Type::HighShelf, 8000.0, 0.7071, airRel,  airTarget);

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
