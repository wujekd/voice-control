#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// A small horizontal gain-reduction meter. Shows positive dB of reduction
// growing from the right (like a compressor's GR meter). setReduction() is
// called from the UI timer; the bar eases toward the new value so it reads
// smoothly rather than flickering.
class GrMeter : public juce::Component {
public:
    GrMeter(juce::String label, float maxDb, juce::Colour colour)
        : label_(std::move(label)), maxDb_(maxDb), colour_(colour) {}

    // Target reduction in positive dB (0 = no reduction). Eased on each call.
    void setReduction(float reductionDb) {
        const float target = juce::jlimit(0.0f, maxDb_, reductionDb);
        displayed_ += (target - displayed_) * (target > displayed_ ? 0.6f : 0.25f);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds();
        auto labelArea = r.removeFromLeft(58);

        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(label_, labelArea, juce::Justification::centredLeft);

        auto track = r.reduced(0, 3).toFloat();
        g.setColour(juce::Colour(0xff14161b));
        g.fillRoundedRectangle(track, 3.0f);

        const float frac = maxDb_ > 0.0f ? (displayed_ / maxDb_) : 0.0f;
        if (frac > 0.001f) {
            auto fill = track.withWidth(track.getWidth() * frac);
            g.setColour(colour_.withAlpha(0.62f));
            g.fillRoundedRectangle(fill, 3.0f);
        }

        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(juce::String(displayed_, 1) + " dB", r,
                   juce::Justification::centredRight);
    }

private:
    juce::String label_;
    float maxDb_ = 18.0f;
    juce::Colour colour_;
    float displayed_ = 0.0f;
};

// Horizontal level meter. Two layers share one bar: a lighter-green peak layer
// behind and a deep-green RMS (average) layer in front, so you can read both
// average and peak at a glance. The numeric readout holds the highest peak for
// a short while before easing back down, so brief transients stay visible.
class VuMeter : public juce::Component {
public:
    VuMeter(juce::String label, float minDb = -60.0f, float maxDb = 0.0f)
        : label_(std::move(label)), minDb_(minDb), maxDb_(maxDb) {}

    // Average level layer (deep green) — kept for compatibility.
    void setLevelDb(float db) { setLevels(db, db); }

    // Average (RMS) and peak in dBFS. RMS draws as the deep-green front layer,
    // peak as the lighter-green back layer.
    void setLevels(float rmsDb, float peakDb) {
        const float rmsTarget = juce::jlimit(minDb_, maxDb_, rmsDb);
        rmsDb_ += (rmsTarget - rmsDb_) * (rmsTarget > rmsDb_ ? 0.35f : 0.12f);

        const float peakTarget = juce::jlimit(minDb_, maxDb_, peakDb);
        peakDb_ += (peakTarget - peakDb_) * (peakTarget > peakDb_ ? 0.9f : 0.5f);

        // Hold the highest peak for a moment, then let it ease back down so the
        // readout doesn't snap away from a transient before you can see it.
        if (peakDb_ >= holdDb_) {
            holdDb_ = peakDb_;
            holdFrames_ = 26; // ~kUiHz frames of hold before release begins
        } else if (holdFrames_ > 0) {
            --holdFrames_;
        } else {
            holdDb_ += (peakDb_ - holdDb_) * 0.08f;
        }

        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds();
        auto labelArea = r.removeFromLeft(58);

        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(label_, labelArea, juce::Justification::centredLeft);

        auto track = r.reduced(0, 1).toFloat();
        g.setColour(juce::Colour(0xff14161b));
        g.fillRoundedRectangle(track, 3.0f);

        const float span = maxDb_ - minDb_;
        const float peakFrac = juce::jlimit(0.0f, 1.0f, (peakDb_ - minDb_) / span);
        const float rmsFrac = juce::jlimit(0.0f, 1.0f, (rmsDb_ - minDb_) / span);

        // Peak layer (grayed-out green, like the encoder's inactive arc), behind.
        if (peakFrac * track.getWidth() > 1.0f) {
            g.setColour(kGreen.withAlpha(0.24f));
            g.fillRoundedRectangle(track.withWidth(track.getWidth() * peakFrac), 3.0f);
        }
        // RMS layer (active encoder green), in front.
        if (rmsFrac * track.getWidth() > 1.0f) {
            g.setColour(kGreen.withAlpha(0.95f));
            g.fillRoundedRectangle(track.withWidth(track.getWidth() * rmsFrac), 3.0f);
        }

        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(juce::String(holdDb_, 1) + " dB", r,
                   juce::Justification::centredRight);
    }

private:
    // Same green as the encoder: RMS uses the active fill, peak the grayed-out tint.
    inline static const juce::Colour kGreen { 0xff6ee07a };

    juce::String label_;
    float minDb_ = -60.0f;
    float maxDb_ = 0.0f;
    float rmsDb_ = -60.0f;
    float peakDb_ = -60.0f;
    float holdDb_ = -60.0f;
    int holdFrames_ = 0;
};
