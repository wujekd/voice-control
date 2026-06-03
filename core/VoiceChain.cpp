#include "VoiceChain.h"

#include <cmath>

namespace vc {

void VoiceChain::prepare(int sampleRate, int numChannels, const ChainParams& params) {
    params_ = params;

    highpass_.assign(static_cast<std::size_t>(numChannels), Biquad{});
    for (auto& bq : highpass_)
        bq.setHighpass(sampleRate, params_.highpassHz, params_.highpassQ);

    eq_.prepare(numChannels);
    eq_.configure(sampleRate, fullEqBands(params_));

    fastComp_.prepare(sampleRate, params_.fastCompThresholdDb, params_.fastCompRatio,
                      params_.fastCompAttackMs, params_.fastCompReleaseMs,
                      params_.fastCompMakeupDb, params_.fastCompKneeDb);

    glueComp_.prepare(sampleRate, params_.glueCompThresholdDb, params_.glueCompRatio,
                      params_.glueCompAttackMs, params_.glueCompReleaseMs,
                      params_.glueCompMakeupDb, params_.glueCompKneeDb);

    deEsser_.prepare(sampleRate, numChannels, params_.deEssFreqHz,
                     params_.deEssThresholdDb, params_.deEssRatio,
                     params_.deEssAttackMs, params_.deEssReleaseMs,
                     params_.deEssRangeDb);
    deEsser_.setPresenceThreshold(params_.deEssPresenceThresholdDb);

    loudness_.prepare(sampleRate, params_.targetLufs);

    limiter_.prepare(sampleRate, params_.limiterCeilingDb,
                     params_.limiterAttackMs, params_.limiterReleaseMs);
}

void VoiceChain::process(AudioBuffer& buffer) {
    const int channels = buffer.numChannels();
    const std::size_t frames = buffer.numFrames();

    // 0. Calibrated chain drive.
    const double preGainDb = params_.inputCalibrationGainDb + params_.intensityDriveDb;
    const float preGain = static_cast<float>(std::pow(10.0, preGainDb / 20.0));
    if (preGain != 1.0f) {
        for (int ch = 0; ch < channels; ++ch) {
            float* data = buffer.channels[static_cast<std::size_t>(ch)].data();
            for (std::size_t i = 0; i < frames; ++i)
                data[i] *= preGain;
        }
    }

    // 1. High-pass (rumble removal), streamed per channel.
    for (int ch = 0; ch < channels && ch < static_cast<int>(highpass_.size()); ++ch) {
        Biquad& bq = highpass_[static_cast<std::size_t>(ch)];
        float* data = buffer.channels[static_cast<std::size_t>(ch)].data();
        for (std::size_t i = 0; i < frames; ++i)
            data[i] = bq.process(data[i]);
    }

    // 2. EQ (auto-EQ + tone).
    eq_.process(buffer);

    // 3. Fast peak control, then slower glue compression.
    if (params_.fastCompEnabled)
        fastComp_.process(buffer);

    if (params_.glueCompEnabled)
        glueComp_.process(buffer);

    // 4. De-esser (sibilance), after compression which can accentuate it.
    if (params_.deEssEnabled)
        deEsser_.process(buffer);

    // 5. Loudness normalisation (absolute level toward target LUFS).
    if (params_.loudnessEnabled)
        loudness_.process(buffer);

    // 6. Limiter (guard the ceiling after the loudness gain).
    if (params_.limiterEnabled)
        limiter_.process(buffer);
}

} // namespace vc
