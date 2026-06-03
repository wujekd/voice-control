#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "Eq.h"
#include "SpectrumAnalyzer.h"

#include <vector>

// Spectrum-analyzer panel: shows the whole-file average spectrum of the input
// (grey backdrop), the combined EQ curve (HPF + auto-EQ + tone) as a bright
// line on a +/-15 dB scale, and the resulting spectrum (input + EQ). Purely a
// view; data is pushed in from MainComponent.
class SpectrumView : public juce::Component {
public:
    void setSpectrum(const vc::SpectrumResult& spectrum) {
        spectrum_ = spectrum;
        repaint();
    }

    void setEq(std::vector<vc::EqBand> bands, double highpassHz, double sampleRate) {
        bands_ = std::move(bands);
        highpassHz_ = highpassHz;
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        repaint();
    }

    // Live spectrum of the currently-playing output (drawn instead of the
    // static average while playing).
    void setLiveSpectrum(const vc::SpectrumResult& live) {
        liveSpectrum_ = live;
        showLive_ = true;
        repaint();
    }
    void setShowLive(bool show) {
        if (showLive_ != show) { showLive_ = show; repaint(); }
    }

    void paint(juce::Graphics& g) override;

private:
    static constexpr float kFMin = 20.0f;
    static constexpr float kFMax = 20000.0f;
    static constexpr float kEqRangeDb = 15.0f; // EQ curve full-scale

    float freqToX(float f, float w) const {
        const float t = std::log(f / kFMin) / std::log(kFMax / kFMin);
        return juce::jlimit(0.0f, w, t * w);
    }
    float xToFreq(float x, float w) const {
        return kFMin * std::pow(kFMax / kFMin, x / w);
    }
    double eqResponseAt(float freq) const;

    vc::SpectrumResult spectrum_;     // static whole-file average
    vc::SpectrumResult liveSpectrum_; // animated, while playing
    bool showLive_ = false;
    std::vector<vc::EqBand> bands_;
    double highpassHz_ = 0.0;
    double sampleRate_ = 48000.0;
};
