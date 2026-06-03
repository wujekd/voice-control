#pragma once

#include "Eq.h"

#include <string>
#include <vector>

namespace vc {

// Parameters for the voice chain. Each stage reads its own fields; adding a
// stage (de-esser, tone EQ, ...) means adding fields here, not changing the
// chain's interface.
struct ChainParams {
    // High-pass (rumble removal)
    double highpassHz = 90.0;
    double highpassQ = 0.7071;

    // Tone EQ (tonal character; independent user choice)
    Tone tone = Tone::Natural;

    // Auto-EQ (wide corrective curve derived from the source spectrum).
    // `autoEqBands` is computed at load and injected here; empty = none.
    bool autoEqEnabled = true;
    std::vector<EqBand> autoEqBands;

    // De-esser (sibilance control)
    bool deEssEnabled = true;
    double deEssFreqHz = 6000.0;
    double deEssThresholdDb = -30.0;
    double deEssRatio = 4.0;
    double deEssAttackMs = 1.0;
    double deEssReleaseMs = 60.0;
    double deEssRangeDb = 12.0;

    // Compressor (dynamics)
    bool compEnabled = true;
    double compThresholdDb = -20.0;
    double compRatio = 3.0;
    double compAttackMs = 10.0;
    double compReleaseMs = 120.0;
    double compMakeupDb = 0.0; // loudness stage handles overall level
    double compKneeDb = 6.0;

    // Loudness normalisation (absolute level, LUFS)
    bool loudnessEnabled = true;
    double targetLufs = -16.0;

    // Limiter (ceiling)
    bool limiterEnabled = true;
    double limiterCeilingDb = -1.0;
    double limiterAttackMs = 2.0;
    double limiterReleaseMs = 100.0;
};

// Full EQ band list a chain should apply: auto-EQ (if enabled) then tone.
inline std::vector<EqBand> fullEqBands(const ChainParams& p) {
    std::vector<EqBand> bands;
    if (p.autoEqEnabled)
        bands.insert(bands.end(), p.autoEqBands.begin(), p.autoEqBands.end());
    const auto tone = tonePresetBands(p.tone);
    bands.insert(bands.end(), tone.begin(), tone.end());
    return bands;
}

enum class Preset { Light, Balanced, Strong };

inline ChainParams paramsForPreset(Preset p) {
    ChainParams params;
    switch (p) {
        case Preset::Light:
            params.highpassHz = 70.0;
            params.compThresholdDb = -18.0;
            params.compRatio = 2.0;
            params.targetLufs = -16.0;
            params.deEssThresholdDb = -26.0; // gentle
            params.deEssRatio = 3.0;
            params.deEssRangeDb = 8.0;
            break;
        case Preset::Balanced:
            params.highpassHz = 90.0;
            params.compThresholdDb = -20.0;
            params.compRatio = 3.0;
            params.targetLufs = -16.0;
            params.deEssThresholdDb = -30.0;
            params.deEssRatio = 4.0;
            params.deEssRangeDb = 12.0;
            break;
        case Preset::Strong:
            params.highpassHz = 110.0;
            params.compThresholdDb = -24.0;
            params.compRatio = 4.0;
            params.targetLufs = -14.0; // a touch louder/denser
            params.deEssThresholdDb = -34.0; // aggressive
            params.deEssRatio = 6.0;
            params.deEssRangeDb = 16.0;
            break;
    }
    return params;
}

// Returns true and sets `out` if `name` matches a known preset.
inline bool presetFromString(const std::string& name, Preset& out) {
    if (name == "light")    { out = Preset::Light;    return true; }
    if (name == "balanced") { out = Preset::Balanced; return true; }
    if (name == "strong")   { out = Preset::Strong;   return true; }
    return false;
}

// Returns true and sets `out` if `name` matches a known tone.
inline bool toneFromString(const std::string& name, Tone& out) {
    if (name == "natural") { out = Tone::Natural; return true; }
    if (name == "warm")    { out = Tone::Warm;    return true; }
    if (name == "crisp")   { out = Tone::Crisp;   return true; }
    return false;
}

} // namespace vc
