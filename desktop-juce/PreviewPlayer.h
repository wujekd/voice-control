#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioBuffer.h"
#include "LiveVoiceChain.h"
#include "MusicClip.h"
#include "Presets.h"

#include <atomic>
#include <cstdint>

// Plays the dry source and runs the live voice chain on it in real time.
// "Before" = the dry buffer; "After" = the same buffer processed live this
// block. The chain always runs (so the meters stay live and A/B is gapless);
// the A/B flag only chooses which signal reaches the output. Linearly resamples
// from the source rate to the device rate.
class PreviewPlayer : public juce::AudioSource {
public:
    void setDrySource(const juce::AudioBuffer<float>* before, double sourceRate);
    // Progressive denoise: a planar buffer filled in the background plus per-hop
    // validity flags. The blend uses a denoised sample only where its hop is
    // marked valid, falling back to dry elsewhere. Pass nullptrs to disable.
    void setDenoisedSource(const vc::AudioBuffer* denoised,
                           const std::atomic<std::uint8_t>* validHops,
                           int numHops, int hopSize);
    void setMusicClips(const std::vector<MusicClip>& clips);
    void setMutedMusicClipIndex(int index) { mutedMusicClipIndex_.store(index, std::memory_order_relaxed); }
    void clearSources();

    void setParams(const vc::ChainParams& params) { chain_.setParams(params); }
    void setNoiseReductionAmount(double amount);
    void setInputLoudness(double lufs) { chain_.setInputLoudness(lufs); }

    void start();
    void stop();
    bool isPlaying() const { return playing_.load(); }
    void setShowAfter(bool showAfter) { showAfter_.store(showAfter); }

    double getPositionNormalised() const;
    void setPositionSeconds(double seconds);
    // Current playback position in source frames (a lock-free hint for the
    // background denoiser; may be a block stale).
    std::int64_t currentSourceFrame() const {
        return static_cast<std::int64_t>(readPos_);
    }

    // Copies the latest `n` output samples (mono mix of what's being heard)
    // into `dest` for the GUI spectrum analyzer. Lock-free; n must be <= the
    // ring size. Safe to call from the message thread.
    void readAnalysisBlock(float* dest, int n) const;
    static constexpr int kAnalysisSize = 4096; // power of two

    // Live meters (positive dB of gain reduction).
    float fastCompReductionDb() const { return chain_.fastCompReductionDb(); }
    float glueCompReductionDb() const { return chain_.glueCompReductionDb(); }
    float deEssReductionDb() const { return chain_.deEssReductionDb(); }
    float limiterReductionDb() const { return chain_.limiterReductionDb(); }
    double loudnessGainDb() const { return chain_.loudnessGainDb(); }
    float rmsLevelDb() const { return rmsLevelDb_.load(std::memory_order_relaxed); }

    // AudioSource
    void prepareToPlay(int samplesPerBlock, double deviceSampleRate) override;
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

private:
    juce::CriticalSection lock_;
    const juce::AudioBuffer<float>* before_ = nullptr;
    const vc::AudioBuffer* denoised_ = nullptr;
    const std::atomic<std::uint8_t>* denoisedValidHops_ = nullptr;
    int denoisedNumHops_ = 0;
    int denoisedHopSize_ = 480;
    std::vector<MusicClip> musicClips_;

    vc::LiveVoiceChain chain_;
    juce::AudioBuffer<float> scratch_; // wet render scratch, preallocated

    std::atomic<bool> playing_ { false };
    std::atomic<bool> showAfter_ { false };
    std::atomic<int> mutedMusicClipIndex_ { -1 };
    std::atomic<float> noiseReductionAmount_ { 1.0f };
    std::atomic<float> rmsLevelDb_ { -90.0f };

    double sourceRate_ = 48000.0;
    double deviceRate_ = 48000.0;
    int blockSize_ = 512;
    double readPos_ = 0.0; // source samples; audio thread only
    double rmsLin_ = 0.0;  // audio thread only

    void mixMusicInto(juce::AudioBuffer<float>& dest, int startSample, int numSamples,
                      double timelineStartSeconds);

    // Analysis ring for the live spectrum (single producer = audio thread).
    std::vector<float> analysisRing_ = std::vector<float>(kAnalysisSize, 0.0f);
    std::atomic<int> analysisWrite_ { 0 };
};
