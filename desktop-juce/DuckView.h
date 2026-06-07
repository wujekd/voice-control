#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "Ducker.h" // crossover constants
#include "SpectrumAnalyzer.h" // vc::SpectrumResult

#include <cmath>
#include <cstdint>
#include <vector>

// Compact two-lane music monitor shown between the music-volume knobs and the
// ducking knobs. Top lane: a small waveform of the heard (post-duck) backing
// music. Bottom lane: a log-frequency line that dips downward as the duck
// engages — across the whole width when full-band (blend 0), only the mid
// section (Ducker::kCrossLoHz..kCrossHiHz) when mid-only (blend 1). Purely a
// view; data is pushed in from the UI timer and eased so it reads smoothly.
class DuckView : public juce::Component {
public:
    void setMusicWaveform(const float* samples, int n) {
        wave_.assign(samples, samples + juce::jmax(0, n));
        repaint();
    }

    // Live spectrum of the heard backing music, drawn behind the duck line so
    // the dip lines up with the frequencies it acts on. An invalid result
    // clears the backdrop (e.g. when stopped).
    void setMusicSpectrum(const vc::SpectrumResult& spectrum) {
        spectrum_ = spectrum;
        repaint();
    }

    // When false the duck curve is not drawn; the music waveform and layout stay.
    void setDuckingEnabled(bool enabled) {
        if (duckingEnabled_ != enabled) {
            duckingEnabled_ = enabled;
            repaint();
        }
    }

    // reductionDb: positive dB of current music gain reduction. blend01: the
    // Filter knob (0 = full-band, 1 = mid-only).
    void setDuckState(float reductionDb, float blend01) {
        const float target = juce::jlimit(0.0f, kMaxDb, reductionDb);
        displayedDb_ += (target - displayedDb_) * (target > displayedDb_ ? 0.6f : 0.25f);
        blend_ = juce::jlimit(0.0f, 1.0f, blend01);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff14161b));
        g.fillRoundedRectangle(bounds, 4.0f);

        auto r = bounds.reduced(6.0f);
        const float gap = 6.0f;
        auto waveLane = r.removeFromTop((r.getHeight() - gap) * 0.5f);
        r.removeFromTop(gap);
        auto lineLane = r;

        drawWaveform(g, waveLane);
        drawDuckLine(g, lineLane);

        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawText("Music out", waveLane.toNearestInt(), juce::Justification::topLeft);
    }

private:
    static constexpr float kFMin = 20.0f;
    static constexpr float kFMax = 20000.0f;
    static constexpr float kMaxDb = 24.0f; // full-scale of the duck dip
    static constexpr float kMaxWaveGain = 12.0f; // cap on scope auto-gain
    inline static const juce::Colour kGreen { 0xff6ee07a };

    float freqToX(float f, float x0, float w) const {
        const float t = std::log(f / kFMin) / std::log(kFMax / kFMin);
        return x0 + juce::jlimit(0.0f, 1.0f, t) * w;
    }
    float xToFreq(float x, float x0, float w) const {
        const float t = w > 0.0f ? (x - x0) / w : 0.0f;
        return kFMin * std::pow(kFMax / kFMin, juce::jlimit(0.0f, 1.0f, t));
    }

    void drawWaveform(juce::Graphics& g, juce::Rectangle<float> lane) {
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        const float mid = lane.getCentreY();
        g.drawHorizontalLine(juce::roundToInt(mid), lane.getX(), lane.getRight());

        const int cols = juce::jmax(1, juce::roundToInt(lane.getWidth()));
        const int n = static_cast<int>(wave_.size());
        if (n <= 0)
            return;

        // The backing music is usually quiet, so a raw trace barely moves.
        // Auto-gain to the block peak (capped) so the scope stays lively
        // without clipping on louder passages; fast fall, slower rise.
        float peak = 0.0f;
        for (float v : wave_)
            peak = juce::jmax(peak, std::abs(v));
        const float targetGain = peak > 1.0e-4f
            ? juce::jlimit(1.0f, kMaxWaveGain, 0.9f / peak)
            : kMaxWaveGain;
        waveGain_ += (targetGain - waveGain_) * (targetGain < waveGain_ ? 0.3f : 0.1f);

        const float half = lane.getHeight() * 0.5f;
        g.setColour(kGreen.withAlpha(0.6f));
        for (int c = 0; c < cols; ++c) {
            const int i0 = static_cast<int>(static_cast<std::int64_t>(c) * n / cols);
            const int i1 = juce::jmax(i0 + 1, static_cast<int>(static_cast<std::int64_t>(c + 1) * n / cols));
            float lo = 0.0f, hi = 0.0f;
            for (int i = i0; i < i1 && i < n; ++i) {
                lo = juce::jmin(lo, wave_[static_cast<std::size_t>(i)]);
                hi = juce::jmax(hi, wave_[static_cast<std::size_t>(i)]);
            }
            const float x = lane.getX() + static_cast<float>(c);
            const float yLo = mid - juce::jlimit(-half, half, hi * half * waveGain_);
            const float yHi = mid - juce::jlimit(-half, half, lo * half * waveGain_);
            g.drawVerticalLine(juce::roundToInt(x), juce::jmin(yLo, yHi), juce::jmax(yLo, yHi) + 1.0f);
        }
    }

    // Mid-band membership with smooth edges (in log-frequency, ~1/3 octave).
    float midWeight(float f) const {
        const float loEdge = static_cast<float>(vc::Ducker::kCrossLoHz);
        const float hiEdge = static_cast<float>(vc::Ducker::kCrossHiHz);
        const float ramp = 1.26f; // ~1/3 octave fade
        const float lo = juce::jlimit(0.0f, 1.0f, std::log(f / (loEdge / ramp)) / std::log(ramp * ramp));
        const float hi = juce::jlimit(0.0f, 1.0f, std::log((hiEdge * ramp) / f) / std::log(ramp * ramp));
        return juce::jmin(lo, hi);
    }

    void drawDuckLine(juce::Graphics& g, juce::Rectangle<float> lane) {
        const float x0 = lane.getX();
        const float w = lane.getWidth();
        const float top = lane.getY() + 2.0f;
        const float usable = lane.getHeight() - 4.0f;

        // Spectrum of the heard music behind the duck line, on the same
        // log-frequency x-axis so the dip lines up with the band it acts on.
        drawSpectrumBackdrop(g, lane);

        // Baseline (music at full level), faint.
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.drawHorizontalLine(juce::roundToInt(top), x0, lane.getRight());

        juce::Path line;
        const int steps = juce::jmax(2, juce::roundToInt(w));
        for (int s = 0; s <= steps; ++s) {
            const float x = x0 + w * static_cast<float>(s) / static_cast<float>(steps);
            const float f = xToFreq(x, x0, w);
            const float shape = (1.0f - blend_) + blend_ * midWeight(f);
            const float dipDb = displayedDb_ * shape;
            const float y = top + usable * juce::jlimit(0.0f, 1.0f, dipDb / kMaxDb);
            if (s == 0) line.startNewSubPath(x, y);
            else line.lineTo(x, y);
        }
        if (duckingEnabled_) {
            g.setColour(kGreen.withAlpha(0.85f));
            g.strokePath(line, juce::PathStrokeType(1.6f));
        }
    }

    void drawSpectrumBackdrop(juce::Graphics& g, juce::Rectangle<float> lane) {
        if (!spectrum_.valid)
            return;
        const float x0 = lane.getX();
        const float w = lane.getWidth();
        if (w <= 0.0f)
            return;

        // Auto-scale to the loudest visible point so quiet music still reads.
        float maxDb = -120.0f;
        for (int px = 0; px <= static_cast<int>(w); px += 2)
            maxDb = juce::jmax(maxDb, spectrum_.dbAt(xToFreq(x0 + static_cast<float>(px), x0, w)));
        const float topDb = maxDb + 6.0f, botDb = maxDb - 60.0f;
        const float yTop = lane.getY();
        const float yBot = lane.getBottom();
        auto mapDb = [&](float db) { return juce::jmap(db, botDb, topDb, yBot, yTop); };

        juce::Path fill;
        fill.startNewSubPath(x0, yBot);
        for (int px = 0; px <= static_cast<int>(w); px += 2) {
            const float x = x0 + static_cast<float>(px);
            const float f = xToFreq(x, x0, w);
            fill.lineTo(x, juce::jlimit(yTop, yBot, mapDb(spectrum_.dbAt(f))));
        }
        fill.lineTo(lane.getRight(), yBot);
        fill.closeSubPath();
        g.setColour(kGreen.withAlpha(0.14f));
        g.fillPath(fill);
    }

    std::vector<float> wave_;
    vc::SpectrumResult spectrum_;
    bool duckingEnabled_ = true;
    float displayedDb_ = 0.0f;
    float blend_ = 0.0f;
    float waveGain_ = 1.0f;
};
