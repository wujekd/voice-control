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
            g.setColour(colour_);
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

// Horizontal RMS/VU level meter. Shows dBFS from silence up to 0 dBFS with
// slower easing than the gain-reduction meters, so it reads like average level
// rather than sample peaks.
class VuMeter : public juce::Component {
public:
    VuMeter(juce::String label, float minDb = -60.0f, float maxDb = 0.0f)
        : label_(std::move(label)), minDb_(minDb), maxDb_(maxDb) {}

    void setLevelDb(float db) {
        const float target = juce::jlimit(minDb_, maxDb_, db);
        displayedDb_ += (target - displayedDb_) * (target > displayedDb_ ? 0.35f : 0.12f);
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

        const float frac = (displayedDb_ - minDb_) / (maxDb_ - minDb_);
        auto fill = track.withWidth(track.getWidth() * juce::jlimit(0.0f, 1.0f, frac));
        if (fill.getWidth() > 1.0f) {
            g.setColour(levelColour(displayedDb_));
            g.fillRoundedRectangle(fill, 3.0f);
        }

        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(juce::String(displayedDb_, 1) + " dB", r,
                   juce::Justification::centredRight);
    }

private:
    static juce::Colour levelColour(float db) {
        if (db > -6.0f) return juce::Colour(0xffff5d5d);
        if (db > -18.0f) return juce::Colour(0xffffc14d);
        return juce::Colour(0xff4fd37a);
    }

    juce::String label_;
    float minDb_ = -60.0f;
    float maxDb_ = 0.0f;
    float displayedDb_ = -60.0f;
};
