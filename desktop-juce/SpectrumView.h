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

    void setProcessedSpectrum(const vc::SpectrumResult& spectrum) {
        processedSpectrum_ = spectrum;
        repaint();
    }

    void invalidateProcessedSpectrumPreview() {
        processedSpectrum_ = {};
        repaint();
    }

    void setProcessedSpectrumBusy(bool busy) {
        processedSpectrumBusy_ = busy;
        repaint();
    }

    bool tickAnimation() {
        return false;
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

    // Live broadband gain reduction from the limiter (positive dB). The EQ
    // curve is pulled down by this amount so the display shows the limiter
    // working. Eased toward the target so it reads smoothly.
    void setLimiterReductionDb(float reductionDb) {
        const float target = juce::jmax(0.0f, reductionDb);
        limiterReductionDb_ += (target - limiterReductionDb_)
            * (target > limiterReductionDb_ ? 0.6f : 0.25f);
        repaint();
    }

    // Live gain reduction from the de-esser (positive dB). Unlike the limiter
    // this only attenuates the high (sibilance) band above the crossover, so it
    // pulls down just the top end of the curve. Eased like the limiter.
    void setDeEssReductionDb(float reductionDb) {
        const float target = juce::jmax(0.0f, reductionDb);
        deEssReductionDb_ += (target - deEssReductionDb_)
            * (target > deEssReductionDb_ ? 0.6f : 0.25f);
        repaint();
    }

    // De-esser crossover frequency (Hz); 0 disables the de-ess dip in the view.
    void setDeEsserCrossover(double crossoverHz) {
        deEssCrossoverHz_ = crossoverHz;
        repaint();
    }

    // Live gain reduction from the two compressor stages (glue + fast, positive
    // dB). Two faint copies of the EQ curve droop downward by these amounts as a
    // background animation. Eased like the other dynamics.
    void setCompReductionDb(float glueDb, float fastDb) {
        const float glueTarget = juce::jmax(0.0f, glueDb);
        glueCompReductionDb_ += (glueTarget - glueCompReductionDb_)
            * (glueTarget > glueCompReductionDb_ ? 0.6f : 0.25f);
        const float fastTarget = juce::jmax(0.0f, fastDb);
        fastCompReductionDb_ += (fastTarget - fastCompReductionDb_)
            * (fastTarget > fastCompReductionDb_ ? 0.6f : 0.25f);
        repaint();
    }

    void paint(juce::Graphics& g) override;

private:
    static constexpr float kFMin = 20.0f;
    static constexpr float kFMax = 20000.0f;
    static constexpr float kEqRangeDb = 12.0f; // EQ curve full-scale

    float freqToX(float f, float w) const {
        const float t = std::log(f / kFMin) / std::log(kFMax / kFMin);
        return juce::jlimit(0.0f, w, t * w);
    }
    float xToFreq(float x, float w) const {
        return kFMin * std::pow(kFMax / kFMin, x / w);
    }
    double eqResponseAt(float freq) const;
    float deEssReductionAt(float freq) const;
    // Builds the EQ curve as a path, pulled down by extraDownDb (used both for
    // the main curve and the compression "echo" copies). Clips to the plot.
    juce::Path eqCurvePath(float w, juce::Rectangle<float> plot,
                           float midY, float extraDownDb) const;

    vc::SpectrumResult spectrum_;     // static whole-file average
    vc::SpectrumResult processedSpectrum_; // static whole-file processed voice
    vc::SpectrumResult liveSpectrum_; // animated, while playing
    bool showLive_ = false;
    bool processedSpectrumBusy_ = false;
    std::vector<vc::EqBand> bands_;
    double highpassHz_ = 0.0;
    double sampleRate_ = 48000.0;
    float limiterReductionDb_ = 0.0f; // live limiter gain reduction, eased
    float deEssReductionDb_ = 0.0f;   // live de-esser high-band reduction, eased
    double deEssCrossoverHz_ = 0.0;   // de-esser band split; 0 = no de-ess dip
    float glueCompReductionDb_ = 0.0f; // live glue-comp reduction, eased
    float fastCompReductionDb_ = 0.0f; // live fast-comp reduction, eased
};
