#pragma once

#include "Biquad.h"
#include "SpectrumAnalyzer.h"

#include <vector>

namespace vc {

// One wide EQ move. Shelves and broad bells only — no surgical notches.
struct EqBand {
    enum class Type { LowShelf, Peak, HighShelf };
    Type type = Type::Peak;
    double freq = 1000.0;
    double gainDb = 0.0;
    double q = 0.7071;
};

// Manual tonal character (was ToneEq). Applied as offsets on top of auto-EQ.
enum class Tone { Natural, Warm, Crisp };

std::vector<EqBand> tonePresetBands(Tone tone);
std::vector<EqBand> toneAmountBands(double amount);

// Derives wide corrective EQ from a whole-file average spectrum. Balances the
// low / low-mid / presence / air regions against the speech "core" band,
// favouring low-end control. `strength` (0..1) scales how much of the computed
// correction is applied. Returns only bands that move meaningfully.
std::vector<EqBand> computeAutoEqBands(const SpectrumResult& spectrum, double strength = 0.7);

// Configure a biquad from a band.
void configureBiquad(Biquad& bq, const EqBand& band, double sampleRate);

// Combined magnitude response (dB) of a list of bands at a frequency.
double eqResponseDb(const std::vector<EqBand>& bands, double freqHz, double sampleRate);

} // namespace vc
