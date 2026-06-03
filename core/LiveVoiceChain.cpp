#include "LiveVoiceChain.h"

#include <algorithm>
#include <cmath>

namespace vc {

void LiveVoiceChain::prepare(int sampleRate, int numChannels) {
    sampleRate_ = sampleRate;
    numChannels_ = std::max(1, numChannels);

    highpass_.assign(static_cast<std::size_t>(numChannels_), Biquad{});

    eq_.prepare(numChannels_);
    fastComp_.prepare(sampleRate_, active_.fastCompThresholdDb, active_.fastCompRatio,
                      active_.fastCompAttackMs, active_.fastCompReleaseMs,
                      active_.fastCompMakeupDb, active_.fastCompKneeDb);
    glueComp_.prepare(sampleRate_, active_.glueCompThresholdDb, active_.glueCompRatio,
                      active_.glueCompAttackMs, active_.glueCompReleaseMs,
                      active_.glueCompMakeupDb, active_.glueCompKneeDb);
    deEsser_.prepare(sampleRate_, numChannels_, active_.deEssFreqHz,
                     active_.deEssThresholdDb, active_.deEssRatio,
                     active_.deEssAttackMs, active_.deEssReleaseMs, active_.deEssRangeDb);
    deEsser_.setPresenceThreshold(active_.deEssPresenceThresholdDb);
    limiter_.prepare(sampleRate_, numChannels_, 5.0 /* ms look-ahead */);

    applyParams(active_);
}

void LiveVoiceChain::setParams(const ChainParams& params) {
    std::lock_guard<std::mutex> lg(paramMutex_);
    pending_ = params;
    paramsDirty_ = true;
}

void LiveVoiceChain::setInputLoudness(double integratedLufs) {
    inputLufs_.store(integratedLufs, std::memory_order_relaxed);
    inputLufsValid_.store(std::isfinite(integratedLufs), std::memory_order_relaxed);
    std::lock_guard<std::mutex> lg(paramMutex_);
    loudnessDirty_ = true;
}

void LiveVoiceChain::recomputeLoudnessGain() {
    if (!inputLufsValid_.load(std::memory_order_relaxed)) {
        loudnessGainLin_ = 1.0;
        loudnessGainDb_.store(0.0, std::memory_order_relaxed);
        return;
    }
    double gainDb = active_.targetLufs - inputLufs_.load(std::memory_order_relaxed);
    gainDb = std::clamp(gainDb, -30.0, 30.0);
    loudnessGainLin_ = std::pow(10.0, gainDb / 20.0);
    loudnessGainDb_.store(gainDb, std::memory_order_relaxed);
}

void LiveVoiceChain::applyParams(const ChainParams& p) {
    for (auto& bq : highpass_)
        bq.setHighpass(sampleRate_, p.highpassHz, p.highpassQ);
    eq_.configure(sampleRate_, fullEqBands(p));
    fastComp_.configure(sampleRate_, p.fastCompThresholdDb, p.fastCompRatio,
                        p.fastCompAttackMs, p.fastCompReleaseMs,
                        p.fastCompMakeupDb, p.fastCompKneeDb);
    glueComp_.configure(sampleRate_, p.glueCompThresholdDb, p.glueCompRatio,
                        p.glueCompAttackMs, p.glueCompReleaseMs,
                        p.glueCompMakeupDb, p.glueCompKneeDb);
    deEsser_.configure(sampleRate_, p.deEssFreqHz, p.deEssThresholdDb, p.deEssRatio,
                       p.deEssAttackMs, p.deEssReleaseMs, p.deEssRangeDb);
    deEsser_.setPresenceThreshold(p.deEssPresenceThresholdDb);
    limiter_.configure(sampleRate_, p.limiterCeilingDb, p.limiterReleaseMs);
    recomputeLoudnessGain();
}

void LiveVoiceChain::process(float* const* channels, int numChannels, int numFrames) {
    // Pick up parameter / loudness changes without blocking the audio thread.
    if (paramMutex_.try_lock()) {
        if (paramsDirty_) {
            active_ = pending_;
            paramsDirty_ = false;
            applyParams(active_);
        }
        if (loudnessDirty_) {
            recomputeLoudnessGain();
            loudnessDirty_ = false;
        }
        paramMutex_.unlock();
    }

    const int nch = std::min(numChannels, static_cast<int>(highpass_.size()));
    if (nch <= 0) return;

    // 0. Calibrated chain drive.
    const double preGainDb = active_.inputCalibrationGainDb + active_.intensityDriveDb;
    const float preGain = static_cast<float>(std::pow(10.0, preGainDb / 20.0));
    if (preGain != 1.0f) {
        for (int ch = 0; ch < nch; ++ch) {
            float* d = channels[ch];
            for (int i = 0; i < numFrames; ++i)
                d[i] *= preGain;
        }
    }

    // 1. High-pass.
    for (int ch = 0; ch < nch; ++ch) {
        Biquad& bq = highpass_[static_cast<std::size_t>(ch)];
        float* d = channels[ch];
        for (int i = 0; i < numFrames; ++i)
            d[i] = bq.process(d[i]);
    }

    // 2. EQ (auto-EQ + tone).
    eq_.process(channels, numChannels, numFrames);

    // 3. Fast peak control, then slower glue compression.
    if (active_.fastCompEnabled)
        fastComp_.process(channels, numChannels, numFrames);

    if (active_.glueCompEnabled)
        glueComp_.process(channels, numChannels, numFrames);

    // 4. De-esser.
    if (active_.deEssEnabled)
        deEsser_.process(channels, numChannels, numFrames);

    // 5. Loudness gain (cached).
    if (active_.loudnessEnabled && loudnessGainLin_ != 1.0) {
        const float g = static_cast<float>(loudnessGainLin_);
        for (int ch = 0; ch < numChannels; ++ch) {
            float* d = channels[ch];
            for (int i = 0; i < numFrames; ++i)
                d[i] *= g;
        }
    }

    // 6. Limiter (causal look-ahead).
    if (active_.limiterEnabled)
        limiter_.process(channels, numChannels, numFrames);
}

} // namespace vc
