#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "LiveVoiceChain.h"
#include "Presets.h"

#include <atomic>

// Plays the dry source and runs the live voice chain on it in real time.
// "Before" = the dry buffer; "After" = the same buffer processed live this
// block. The chain always runs (so the meters stay live and A/B is gapless);
// the A/B flag only chooses which signal reaches the output. Linearly resamples
// from the source rate to the device rate.
class PreviewPlayer : public juce::AudioSource {
public:
    void setDrySource(const juce::AudioBuffer<float>* before, double sourceRate);
    void clearSources();

    void setParams(const vc::ChainParams& params) { chain_.setParams(params); }
    void setInputLoudness(double lufs) { chain_.setInputLoudness(lufs); }

    void start();
    void stop();
    bool isPlaying() const { return playing_.load(); }
    void setShowAfter(bool showAfter) { showAfter_.store(showAfter); }

    double getPositionNormalised() const;

    // Copies the latest `n` output samples (mono mix of what's being heard)
    // into `dest` for the GUI spectrum analyzer. Lock-free; n must be <= the
    // ring size. Safe to call from the message thread.
    void readAnalysisBlock(float* dest, int n) const;
    static constexpr int kAnalysisSize = 4096; // power of two

    // Live meters (positive dB of gain reduction).
    float compReductionDb() const { return chain_.compReductionDb(); }
    float deEssReductionDb() const { return chain_.deEssReductionDb(); }
    float limiterReductionDb() const { return chain_.limiterReductionDb(); }
    double loudnessGainDb() const { return chain_.loudnessGainDb(); }

    // AudioSource
    void prepareToPlay(int samplesPerBlock, double deviceSampleRate) override;
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

private:
    juce::CriticalSection lock_;
    const juce::AudioBuffer<float>* before_ = nullptr;

    vc::LiveVoiceChain chain_;
    juce::AudioBuffer<float> scratch_; // wet render scratch, preallocated

    std::atomic<bool> playing_ { false };
    std::atomic<bool> showAfter_ { false };

    double sourceRate_ = 48000.0;
    double deviceRate_ = 48000.0;
    int blockSize_ = 512;
    double readPos_ = 0.0; // source samples; audio thread only

    // Analysis ring for the live spectrum (single producer = audio thread).
    std::vector<float> analysisRing_ = std::vector<float>(kAnalysisSize, 0.0f);
    std::atomic<int> analysisWrite_ { 0 };
};
