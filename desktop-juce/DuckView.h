#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "Ducker.h" // crossover constants

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

        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("Music out", waveLane.toNearestInt(), juce::Justification::topLeft);
        g.drawText("Duck", lineLane.toNearestInt(), juce::Justification::topLeft);
    }

private:
    static constexpr float kFMin = 20.0f;
    static constexpr float kFMax = 20000.0f;
    static constexpr float kMaxDb = 24.0f; // full-scale of the duck dip
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
            const float yLo = mid - juce::jlimit(-half, half, hi * half);
            const float yHi = mid - juce::jlimit(-half, half, lo * half);
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
        g.setColour(kGreen.withAlpha(0.85f));
        g.strokePath(line, juce::PathStrokeType(1.6f));
    }

    std::vector<float> wave_;
    float displayedDb_ = 0.0f;
    float blend_ = 0.0f;
};
