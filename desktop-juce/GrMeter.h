#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace {

inline const juce::Colour kMeterAccent { 0xff6ee07a };
inline const juce::Colour kMeterAccentBright { 0xff9effae };
inline const juce::Colour kMeterLedOff { 0xff253026 };
inline const juce::Colour kMeterLabel { 0xffcfe8d2 };
inline const juce::Colour kMeterReadout { 0xffd6ffdc };

// Per-cell insets inside the meter panel (keeps labels/values off the frame).
constexpr int kMeterCellPadH = 8;
constexpr int kMeterCellPadV = 0;
constexpr int kMeterLabelW = 46;
constexpr int kMeterValueW = 44;
constexpr int kLedCount = 18;

juce::Rectangle<int> meterContentArea(juce::Rectangle<int> bounds) {
    return bounds.reduced(kMeterCellPadH, kMeterCellPadV);
}

juce::Rectangle<float> ledStripArea(juce::Rectangle<int> bounds) {
    auto r = meterContentArea(bounds).toFloat();
    r.removeFromLeft(static_cast<float>(kMeterLabelW + 6));
    r.removeFromRight(static_cast<float>(kMeterValueW + 6));
    return r;
}

void drawLed(juce::Graphics& g, juce::Point<float> centre, float radius,
             juce::Colour colour, bool glow) {
    if (glow) {
        g.setColour(colour.withAlpha(0.22f));
        g.fillEllipse(centre.x - radius * 1.9f, centre.y - radius * 1.9f,
                      radius * 3.8f, radius * 3.8f);
    }
    g.setColour(colour);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
}

// LED whose brightness fades with `fill` (0 = off, 1 = fully lit). The boundary
// LED of a strip uses the fractional part so the bar reads smoothly.
void drawLedFill(juce::Graphics& g, juce::Point<float> centre, float radius,
                 juce::Colour onColour, float fill) {
    fill = juce::jlimit(0.0f, 1.0f, fill);
    if (fill <= 0.0f) {
        drawLed(g, centre, radius, kMeterLedOff, false);
        return;
    }
    if (fill >= 1.0f) {
        drawLed(g, centre, radius, onColour, true);
        return;
    }
    g.setColour(onColour.withAlpha(0.22f * fill));
    g.fillEllipse(centre.x - radius * 1.9f, centre.y - radius * 1.9f,
                  radius * 3.8f, radius * 3.8f);
    g.setColour(kMeterLedOff.interpolatedWith(onColour, fill));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
}

void drawLabelAndValue(juce::Graphics& g, juce::Rectangle<int> bounds,
                       const juce::String& label, const juce::String& value,
                       juce::Colour valueColour = kMeterReadout) {
    auto r = meterContentArea(bounds);

    g.setColour(kMeterLabel.withAlpha(0.90f));
    g.setFont(juce::Font(juce::FontOptions(10.5f)));
    g.drawText(label, r.removeFromLeft(kMeterLabelW), juce::Justification::centredLeft, true);

    g.setColour(valueColour.withAlpha(0.94f));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(value, r.removeFromRight(kMeterValueW), juce::Justification::centredRight, true);
}

void drawSingleLayerStrip(juce::Graphics& g, juce::Rectangle<float> track,
                          float activeFraction, juce::Colour activeColour) {
    const float frac = juce::jlimit(0.0f, 1.0f, activeFraction);
    const float scaled = frac * static_cast<float>(kLedCount);
    const float ledR = juce::jmin(2.6f, track.getHeight() * 0.17f);
    const float inset = ledR + 3.0f;
    const float span = juce::jmax(0.0f, track.getWidth() - inset * 2.0f);

    for (int i = 0; i < kLedCount; ++i) {
        const float t = kLedCount > 1 ? static_cast<float>(i) / static_cast<float>(kLedCount - 1) : 0.5f;
        const float x = track.getX() + inset + t * span;
        drawLedFill(g, { x, track.getCentreY() }, ledR, activeColour,
                    scaled - static_cast<float>(i));
    }
}

// Stacked strip: front layer (glue) from the left, back layer (peak) continues after it.
void drawStackedStrip(juce::Graphics& g, juce::Rectangle<float> track,
                      float frontFraction, float totalFraction,
                      juce::Colour frontColour, juce::Colour backColour) {
    const float totalFrac = juce::jlimit(0.0f, 1.0f, totalFraction);
    const float frontFrac = juce::jlimit(0.0f, totalFrac, frontFraction);
    const float ledR = juce::jmin(2.6f, track.getHeight() * 0.17f);
    const float inset = ledR + 3.0f;
    const float span = juce::jmax(0.0f, track.getWidth() - inset * 2.0f);
    // Front (glue) stays integer on/off; only the back (peak) boundary fades.
    const int frontLeds = juce::roundToInt(frontFrac * static_cast<float>(kLedCount));
    const float totalScaled = totalFrac * static_cast<float>(kLedCount);

    for (int i = 0; i < kLedCount; ++i) {
        const float t = kLedCount > 1 ? static_cast<float>(i) / static_cast<float>(kLedCount - 1) : 0.5f;
        const float x = track.getX() + inset + t * span;

        if (i < frontLeds)
            drawLed(g, { x, track.getCentreY() }, ledR, frontColour, true);
        else
            drawLedFill(g, { x, track.getCentreY() }, ledR, backColour,
                        totalScaled - static_cast<float>(i));
    }
}

void drawDualLevelStrip(juce::Graphics& g, juce::Rectangle<float> track,
                        float backFraction, float frontFraction,
                        juce::Colour backColour, juce::Colour frontColour) {
    const float backFrac = juce::jlimit(0.0f, 1.0f, backFraction);
    const float frontFrac = juce::jlimit(0.0f, 1.0f, frontFraction);
    const float ledR = juce::jmin(2.6f, track.getHeight() * 0.17f);
    const float inset = ledR + 3.0f;
    const float span = juce::jmax(0.0f, track.getWidth() - inset * 2.0f);
    // Back (peak) stays an integer marker; only the front (RMS) boundary fades.
    const float frontScaled = frontFrac * static_cast<float>(kLedCount);

    for (int i = 0; i < kLedCount; ++i) {
        const float t = kLedCount > 1 ? static_cast<float>(i) / static_cast<float>(kLedCount - 1) : 0.5f;
        const float x = track.getX() + inset + t * span;
        const float pos = static_cast<float>(i + 1) / static_cast<float>(kLedCount);
        const float frontFill = frontScaled - static_cast<float>(i);

        if (frontFill > 0.0f)
            drawLedFill(g, { x, track.getCentreY() }, ledR, frontColour, frontFill);
        else if (pos <= backFrac)
            drawLed(g, { x, track.getCentreY() }, ledR, backColour.withAlpha(0.70f), false);
        else
            drawLed(g, { x, track.getCentreY() }, ledR, kMeterLedOff, false);
    }
}

} // namespace

// Gain-reduction meter with encoder-style LED dots.
class GrMeter : public juce::Component {
public:
    GrMeter(juce::String label, float maxDb, juce::Colour colour)
        : label_(std::move(label)), maxDb_(maxDb), colour_(colour) {}

    void setReduction(float reductionDb) {
        const float target = juce::jlimit(0.0f, maxDb_, reductionDb);
        displayed_ += (target - displayed_) * (target > displayed_ ? 0.6f : 0.25f);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        drawLabelAndValue(g, getLocalBounds(), label_,
                          juce::String(displayed_, 1) + " dB", colour_.brighter(0.15f));
        const float frac = maxDb_ > 0.0f ? (displayed_ / maxDb_) : 0.0f;
        drawSingleLayerStrip(g, ledStripArea(getLocalBounds()), frac, colour_);
    }

private:
    juce::String label_;
    float maxDb_ = 18.0f;
    juce::Colour colour_;
    float displayed_ = 0.0f;
};

// Glue (green) + peak (cyan) compression stacked on one LED strip.
class CompMeter : public juce::Component {
public:
    CompMeter(juce::String label, float maxDb,
              juce::Colour glueColour, juce::Colour peakColour)
        : label_(std::move(label)), maxDb_(maxDb),
          glueColour_(glueColour), peakColour_(peakColour) {}

    void setReductions(float glueDb, float peakDb) {
        const float glueTarget = juce::jlimit(0.0f, maxDb_, glueDb);
        glueDisplayed_ += (glueTarget - glueDisplayed_) * (glueTarget > glueDisplayed_ ? 0.6f : 0.25f);
        const float peakTarget = juce::jlimit(0.0f, maxDb_, peakDb);
        peakDisplayed_ += (peakTarget - peakDisplayed_) * (peakTarget > peakDisplayed_ ? 0.6f : 0.25f);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const juce::Colour readout = glueDisplayed_ >= peakDisplayed_
            ? glueColour_.brighter(0.12f) : peakColour_.brighter(0.10f);
        drawLabelAndValue(g, getLocalBounds(), label_,
                          juce::String(glueDisplayed_ + peakDisplayed_, 1) + " dB", readout);

        const float invMax = maxDb_ > 0.0f ? (1.0f / maxDb_) : 0.0f;
        const float glueFrac = glueDisplayed_ * invMax;
        const float totalFrac = (glueDisplayed_ + peakDisplayed_) * invMax;
        drawStackedStrip(g, ledStripArea(getLocalBounds()), glueFrac, totalFrac,
                         glueColour_, peakColour_);
    }

private:
    juce::String label_;
    float maxDb_ = 12.0f;
    juce::Colour glueColour_, peakColour_;
    float glueDisplayed_ = 0.0f;
    float peakDisplayed_ = 0.0f;
};

// Output level meter: peak LEDs behind, RMS LEDs in front.
class VuMeter : public juce::Component {
public:
    VuMeter(juce::String label, float minDb = -60.0f, float maxDb = 0.0f)
        : label_(std::move(label)), minDb_(minDb), maxDb_(maxDb) {}

    void setLevelDb(float db) { setLevels(db, db); }

    void setLevels(float rmsDb, float peakDb) {
        const float rmsTarget = juce::jlimit(minDb_, maxDb_, rmsDb);
        rmsDb_ += (rmsTarget - rmsDb_) * (rmsTarget > rmsDb_ ? 0.35f : 0.12f);

        const float peakTarget = juce::jlimit(minDb_, maxDb_, peakDb);
        peakDb_ += (peakTarget - peakDb_) * (peakTarget > peakDb_ ? 0.9f : 0.5f);

        repaint();
    }

    void paint(juce::Graphics& g) override {
        drawLabelAndValue(g, getLocalBounds(), label_,
                          juce::String(rmsDb_, 1) + " dB", kMeterAccentBright);

        const float span = maxDb_ - minDb_;
        const float peakFrac = span > 0.0f ? juce::jlimit(0.0f, 1.0f, (peakDb_ - minDb_) / span) : 0.0f;
        const float rmsFrac = span > 0.0f ? juce::jlimit(0.0f, 1.0f, (rmsDb_ - minDb_) / span) : 0.0f;
        drawDualLevelStrip(g, ledStripArea(getLocalBounds()), peakFrac, rmsFrac,
                           kMeterAccent.withAlpha(0.55f), kMeterAccentBright);
    }

private:
    juce::String label_;
    float minDb_ = -60.0f;
    float maxDb_ = 0.0f;
    float rmsDb_ = -60.0f;
    float peakDb_ = -60.0f;
};
