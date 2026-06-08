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
//
// `f0` is the detected voice fundamental (Hz); when > 0 the low shelf and
// low-mid peak — and the windows used to measure them — track the speaker's
// pitch instead of assuming a fixed (male) range. f0 <= 0 reproduces the legacy
// fixed-frequency behaviour, used as a fallback for non-speech or when pitch
// detection fails.
std::vector<EqBand> computeAutoEqBands(const SpectrumResult& spectrum,
                                       double strength = 0.7, double f0 = 0.0);

// Noise-aware variant for when both the original and denoised voice profiles
// are available. The denoised profile drives vocal correction intent, while
// the dry profile constrains boosts that could amplify background material when
// the user lowers the noise-reduction blend.
std::vector<EqBand> computeNoiseAwareAutoEqBands(const SpectrumResult& voiceSpectrum,
                                                 const SpectrumResult& drySpectrum,
                                                 double strength = 0.7, double f0 = 0.0);

// Configure a biquad from a band.
void configureBiquad(Biquad& bq, const EqBand& band, double sampleRate);

// Combined magnitude response (dB) of a list of bands at a frequency.
double eqResponseDb(const std::vector<EqBand>& bands, double freqHz, double sampleRate);

} // namespace vc
