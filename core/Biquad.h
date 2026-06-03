#pragma once

namespace vc {

// Single biquad section, transposed direct-form II.
// One instance is one channel of one filter stage (it carries state),
// so create one per channel.
class Biquad {
public:
    void setHighpass(double sampleRate, double freqHz, double q = 0.7071);
    void setLowpass(double sampleRate, double freqHz, double q = 0.7071);
    void setLowShelf(double sampleRate, double freqHz, double gainDb, double q = 0.7071);
    void setHighShelf(double sampleRate, double freqHz, double gainDb, double q = 0.7071);
    void setPeaking(double sampleRate, double freqHz, double gainDb, double q = 1.0);

    // Magnitude response in dB at freqHz for the current coefficients.
    double magnitudeDb(double freqHz, double sampleRate) const;

    // Direct coefficient set (denominator normalised so a0 == 1).
    // Used for the K-weighting filters in the loudness meter.
    void setCoefficients(double b0, double b1, double b2, double a1, double a2);

    void reset();

    inline float process(float x) {
        const double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return static_cast<float>(y);
    }

private:
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0;
    double a1_ = 0.0, a2_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0;
};

} // namespace vc
