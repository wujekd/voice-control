#include "Biquad.h"

#include <cmath>
#include <complex>

namespace vc {

void Biquad::setHighpass(double sampleRate, double freqHz, double q) {
    // RBJ Audio EQ Cookbook high-pass.
    const double w0 = 2.0 * M_PI * (freqHz / sampleRate);
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha = sinw0 / (2.0 * q);

    const double b0 = (1.0 + cosw0) / 2.0;
    const double b1 = -(1.0 + cosw0);
    const double b2 = (1.0 + cosw0) / 2.0;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cosw0;
    const double a2 = 1.0 - alpha;

    // Normalise by a0.
    b0_ = b0 / a0;
    b1_ = b1 / a0;
    b2_ = b2 / a0;
    a1_ = a1 / a0;
    a2_ = a2 / a0;
}

void Biquad::setLowpass(double sampleRate, double freqHz, double q) {
    const double w0 = 2.0 * M_PI * (freqHz / sampleRate);
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);

    const double b0 = (1.0 - cosw0) / 2.0;
    const double b1 = 1.0 - cosw0;
    const double b2 = (1.0 - cosw0) / 2.0;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cosw0;
    const double a2 = 1.0 - alpha;

    b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0;
    a1_ = a1 / a0; a2_ = a2 / a0;
}

void Biquad::setLowShelf(double sampleRate, double freqHz, double gainDb, double q) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * (freqHz / sampleRate);
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;

    const double b0 =      A * ((A + 1.0) - (A - 1.0) * cosw0 + twoSqrtAalpha);
    const double b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
    const double b2 =      A * ((A + 1.0) - (A - 1.0) * cosw0 - twoSqrtAalpha);
    const double a0 =          (A + 1.0) + (A - 1.0) * cosw0 + twoSqrtAalpha;
    const double a1 = -2.0 *   ((A - 1.0) + (A + 1.0) * cosw0);
    const double a2 =          (A + 1.0) + (A - 1.0) * cosw0 - twoSqrtAalpha;

    b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0;
    a1_ = a1 / a0; a2_ = a2 / a0;
}

void Biquad::setHighShelf(double sampleRate, double freqHz, double gainDb, double q) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * (freqHz / sampleRate);
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;

    const double b0 =      A * ((A + 1.0) + (A - 1.0) * cosw0 + twoSqrtAalpha);
    const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
    const double b2 =      A * ((A + 1.0) + (A - 1.0) * cosw0 - twoSqrtAalpha);
    const double a0 =          (A + 1.0) - (A - 1.0) * cosw0 + twoSqrtAalpha;
    const double a1 =  2.0 *   ((A - 1.0) - (A + 1.0) * cosw0);
    const double a2 =          (A + 1.0) - (A - 1.0) * cosw0 - twoSqrtAalpha;

    b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0;
    a1_ = a1 / a0; a2_ = a2 / a0;
}

void Biquad::setPeaking(double sampleRate, double freqHz, double gainDb, double q) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * (freqHz / sampleRate);
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cosw0;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cosw0;
    const double a2 = 1.0 - alpha / A;

    b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0;
    a1_ = a1 / a0; a2_ = a2 / a0;
}

double Biquad::magnitudeDb(double freqHz, double sampleRate) const {
    const double w = 2.0 * M_PI * (freqHz / sampleRate);
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = std::polar(1.0, -2.0 * w);
    const std::complex<double> num = b0_ + b1_ * z1 + b2_ * z2;
    const std::complex<double> den = 1.0 + a1_ * z1 + a2_ * z2;
    const double mag = std::abs(num) / std::abs(den);
    return 20.0 * std::log10(mag + 1e-12);
}

void Biquad::setCoefficients(double b0, double b1, double b2, double a1, double a2) {
    b0_ = b0; b1_ = b1; b2_ = b2;
    a1_ = a1; a2_ = a2;
}

void Biquad::reset() {
    z1_ = 0.0;
    z2_ = 0.0;
}

} // namespace vc
