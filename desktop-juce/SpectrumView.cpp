#include "SpectrumView.h"

#include "Biquad.h"

#include <algorithm>
#include <cmath>
#include <complex>

double SpectrumView::eqResponseAt(float freq) const {
    double db = vc::eqResponseDb(bands_, freq, sampleRate_);
    if (highpassHz_ > 0.0) {
        vc::Biquad hp;
        hp.setHighpass(sampleRate_, highpassHz_);
        db += hp.magnitudeDb(freq, sampleRate_);
    }
    return db;
}

float SpectrumView::deEssReductionAt(float freq) const {
    if (deEssReductionDb_ <= 0.001f || deEssCrossoverHz_ <= 0.0)
        return 0.0f;
    // The de-esser ducks the high band (high = x - lowpass(x)) by the metered
    // amount, leaving the low band untouched: y = low + g*high, so the transfer
    // function is H = L + g*(1 - L) = g + (1 - g)*L. L must be the *complex*
    // lowpass response -- using only its magnitude lets the low band fill the
    // dip back in, hiding the cut everywhere but the extreme top. With the
    // phase included the bands cancel around the crossover, so the dip sits
    // over the sibilance region where the de-esser actually works.
    vc::Biquad lp;
    lp.setLowpass(sampleRate_, deEssCrossoverHz_);
    const std::complex<double> L = lp.response(freq, sampleRate_);
    const double g = std::pow(10.0, -deEssReductionDb_ / 20.0);
    const std::complex<double> H = g + (1.0 - g) * L;
    return static_cast<float>(-20.0 * std::log10(std::max(1e-6, std::abs(H))));
}

juce::Path SpectrumView::eqCurvePath(float w, juce::Rectangle<float> plot,
                                     float midY, float extraDownDb) const {
    juce::Path eq;
    bool inPath = false;
    float prevPx = 0.0f, prevY = 0.0f;
    bool hadPrev = false;
    const auto yVisible = [&](float y) {
        return y >= plot.getY() && y <= plot.getBottom();
    };
    const auto clipToBoundary = [&](float x0, float y0, float x1, float y1) {
        const float yBound = (y0 > plot.getBottom() || y1 > plot.getBottom())
                                 ? plot.getBottom()
                                 : plot.getY();
        const float t = (yBound - y0) / (y1 - y0);
        return juce::Point<float>(x0 + t * (x1 - x0), yBound);
    };
    for (int px = 0; px <= (int) w; px += 2) {
        const float f = xToFreq((float) px, w);
        // The limiter pulls the whole curve down by its current broadband
        // reduction; the de-esser pulls down only the high band. extraDownDb
        // adds the per-copy compression droop. All show the dynamics working.
        const float db = (float) eqResponseAt(f) - limiterReductionDb_
                         - deEssReductionAt(f) - extraDownDb;
        const float x = (float) px;
        const float y = midY - (db / kEqRangeDb) * (plot.getHeight() * 0.5f);
        const bool visible = yVisible(y);

        if (visible) {
            if (!inPath) {
                if (hadPrev && !yVisible(prevY))
                    eq.startNewSubPath(clipToBoundary(prevPx, prevY, x, y));
                else
                    eq.startNewSubPath(x, y);
                inPath = true;
            } else {
                eq.lineTo(x, y);
            }
        } else if (inPath && hadPrev && yVisible(prevY)) {
            const auto p = clipToBoundary(prevPx, prevY, x, y);
            eq.lineTo(p.x, p.y);
            inPath = false;
        }

        prevPx = x;
        prevY = y;
        hadPrev = true;
    }
    return eq;
}

void SpectrumView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth();
    const auto plot = bounds;

    g.setColour(juce::Colour(0xff14161b));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Frequency grid (no bottom label band — plot uses the full height).
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f }) {
        const float x = freqToX(f, w);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawVerticalLine(static_cast<int>(x), plot.getY(), plot.getBottom());
    }

    // Sparse frequency labels drawn *inside* the plot so the panel keeps its
    // height. Only the decade marks (100 Hz, 1 k, 10 k) are labelled.
    g.setColour(juce::Colours::white.withAlpha(0.28f));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    for (auto [f, label] : { std::pair{ 100.0f, "100" },
                             std::pair{ 1000.0f, "1k" },
                             std::pair{ 10000.0f, "10k" } }) {
        const float x = freqToX(f, w);
        g.drawText(label, juce::Rectangle<float>(x + 3.0f, plot.getBottom() - 14.0f, 32.0f, 12.0f),
                   juce::Justification::bottomLeft);
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

    // Compression "echo" copies of the EQ curve, drawn behind the main line.
    // They rest exactly under it and droop downward as the compressors pull the
    // level: the first by the glue-comp reduction, the second further by the
    // fast-comp reduction on top, fading as they go.
    const juce::Colour eqGreen(0xff6ee07a);
    g.setColour(eqGreen.withAlpha(0.13f));
    g.strokePath(eqCurvePath(w, plot, midY, glueCompReductionDb_ + fastCompReductionDb_),
                 juce::PathStrokeType(1.5f));
    g.setColour(eqGreen.withAlpha(0.24f));
    g.strokePath(eqCurvePath(w, plot, midY, glueCompReductionDb_),
                 juce::PathStrokeType(1.5f));

    g.setColour(eqGreen);
    g.strokePath(eqCurvePath(w, plot, midY, 0.0f), juce::PathStrokeType(2.0f));

    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("EQ curve  (+/-" + juce::String((int) kEqRangeDb) + " dB)",
               getLocalBounds().reduced(4), juce::Justification::topRight);

    (void) processedSpectrumBusy_;
}
