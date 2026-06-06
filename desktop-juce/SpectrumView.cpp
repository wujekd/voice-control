#include "SpectrumView.h"

#include "Biquad.h"

double SpectrumView::eqResponseAt(float freq) const {
    double db = vc::eqResponseDb(bands_, freq, sampleRate_);
    if (highpassHz_ > 0.0) {
        vc::Biquad hp;
        hp.setHighpass(sampleRate_, highpassHz_);
        db += hp.magnitudeDb(freq, sampleRate_);
    }
    return db;
}

void SpectrumView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    const auto plot = bounds.reduced(0.0f, 8.0f).withTrimmedBottom(10.0f);

    g.setColour(juce::Colour(0xff14161b));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Frequency grid + labels.
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f }) {
        const float x = freqToX(f, w);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawVerticalLine(static_cast<int>(x), 0.0f, h);
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawText(f >= 1000.0f ? juce::String(f / 1000.0f, 0) + "k" : juce::String((int) f),
                   juce::Rectangle<float>(x + 2.0f, h - 14.0f, 34.0f, 12.0f),
                   juce::Justification::centredLeft);
    }

    // Spectrum fill: live output while playing, otherwise the static input average.
    const bool live = showLive_ && liveSpectrum_.valid;
    const vc::SpectrumResult& spec = live ? liveSpectrum_ : spectrum_;
    if (spec.valid) {
        float maxDb = -120.0f;
        for (int px = 0; px <= (int) w; px += 2)
            maxDb = juce::jmax(maxDb, spec.dbAt(xToFreq((float) px, w)));
        const float top = maxDb + 10.0f, bot = maxDb - 70.0f;
        auto mapDb = [&](float db) { return juce::jmap(db, bot, top, plot.getBottom(), plot.getY()); };

        juce::Path path;
        path.startNewSubPath(0.0f, plot.getBottom());
        for (int px = 0; px <= (int) w; px += 2) {
            const float f = xToFreq((float) px, w);
            path.lineTo((float) px, juce::jlimit(plot.getY(), plot.getBottom(), mapDb(spec.dbAt(f))));
        }
        path.lineTo(w, plot.getBottom());
        path.closeSubPath();
        g.setColour(live ? juce::Colour(0xff4da6ff).withAlpha(0.22f)
                         : juce::Colours::white.withAlpha(0.10f));
        g.fillPath(path);

        // Static preview: one immediate model only. The base spectrum already
        // reflects the current noise-reduction blend; the EQ response is added
        // directly so control changes do not switch between delayed states.
        if (!live) {
            juce::Path after;
            bool started = false;
            for (int px = 0; px <= (int) w; px += 2) {
                const float f = xToFreq((float) px, w);
                const float db = spec.dbAt(f) + static_cast<float>(eqResponseAt(f));
                const float y = juce::jlimit(plot.getY(), plot.getBottom(), mapDb(db));
                if (!started) { after.startNewSubPath((float) px, y); started = true; }
                else after.lineTo((float) px, y);
            }
            g.setColour(juce::Colour(0xff4da6ff).withAlpha(0.5f));
            g.strokePath(after, juce::PathStrokeType(1.0f));
        }
    }

    // EQ curve on a fixed dB scale (0 dB at vertical centre).
    const float midY = plot.getCentreY();
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawHorizontalLine(static_cast<int>(midY), 0.0f, w);

    juce::Path eq;
    bool started = false;
    for (int px = 0; px <= (int) w; px += 2) {
        const float f = xToFreq((float) px, w);
        const float db = (float) eqResponseAt(f);
        const float y = juce::jlimit(plot.getY(), plot.getBottom(),
                                     midY - (db / kEqRangeDb) * (plot.getHeight() * 0.5f));
        if (!started) { eq.startNewSubPath((float) px, y); started = true; }
        else eq.lineTo((float) px, y);
    }
    g.setColour(juce::Colour(0xff6ee07a));
    g.strokePath(eq, juce::PathStrokeType(2.0f));

    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("EQ curve  (+/-" + juce::String((int) kEqRangeDb) + " dB)",
               getLocalBounds().reduced(4), juce::Justification::topRight);

    (void) processedSpectrumBusy_;
}
