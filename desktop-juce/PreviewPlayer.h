#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioBuffer.h"
#include "Ducker.h"
#include "LiveVoiceChain.h"
#include "MusicClip.h"
#include "Presets.h"

#include <array>
#include <atomic>
#include <cstdint>

// Plays the dry source and runs the live voice chain on it in real time.
// "Before" = the dry buffer; "After" = the same buffer processed live this
// block. The chain always runs (so the meters stay live and A/B is gapless);
// the A/B flag only chooses which signal reaches the output. Linearly resamples
// from the source rate to the device rate.
class PreviewPlayer : public juce::AudioSource {
public:
    PreviewPlayer();

    void setDrySource(const juce::AudioBuffer<float>* before, double sourceRate);
    // Progressive denoise: a planar buffer filled in the background plus per-hop
    // validity flags. The blend uses a denoised sample only where its hop is
    // marked valid, falling back to dry elsewhere. Pass nullptrs to disable.
    void setDenoisedSource(const vc::AudioBuffer* denoised,
                           const std::atomic<std::uint8_t>* validHops,
                           int numHops, int hopSize);
    void setMusicClips(const std::vector<MusicClip>& clips);
    void setMusicClipGainDb(int index, double gainDb);
    void setMusicMasterGainDb(double gainDb);

    // Background ducking: the voice sidechains the backing music. Look-ahead in
    // ms, max reduction in dB, blend 0 (full-band) .. 1 (mid band only).
    void setDuckLookAheadMs(double ms) {
        duckLookAheadMs_.store(static_cast<float>(juce::jlimit(0.0, 50.0, ms)), std::memory_order_relaxed);
    }
    void setDuckReductionDb(double db) {
        duckMaxReductionDb_.store(static_cast<float>(juce::jlimit(0.0, 24.0, db)), std::memory_order_relaxed);
    }
    void setDuckBlend(double blend01) {
        duckBlend_.store(static_cast<float>(juce::jlimit(0.0, 1.0, blend01)), std::memory_order_relaxed);
    }
    // Current smoothed music gain reduction (positive dB) for the UI display.
    float musicDuckReductionDb() const { return musicDuckReductionDb_.load(std::memory_order_relaxed); }
    // Latest post-duck music output (mono) for the DuckView waveform.
    void readMusicAnalysisBlock(float* dest, int n) const;
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
    float peakLevelDb() const { return peakLevelDb_.load(std::memory_order_relaxed); }

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
    static constexpr int kMaxLiveMusicClips = 64;
    std::array<std::atomic<float>, kMaxLiveMusicClips> musicGainDb_;
    std::array<float, kMaxLiveMusicClips> musicSmoothedGain_;
    std::atomic<float> musicMasterGainDb_ { 0.0f };
    float musicSmoothedMasterGain_ = 1.0f;

    vc::LiveVoiceChain chain_;
    juce::AudioBuffer<float> scratch_; // wet render scratch, preallocated

    // Backing-music ducking. Music is rendered into musicScratch_, ducked by the
    // voice key, then summed into the output.
    vc::Ducker ducker_;
    juce::AudioBuffer<float> musicScratch_; // music render scratch, preallocated
    std::vector<float> keyMono_;            // voice key (mono), preallocated
    std::atomic<float> duckLookAheadMs_ { 5.0f };
    std::atomic<float> duckMaxReductionDb_ { 9.0f };
    std::atomic<float> duckBlend_ { 0.0f };
    std::atomic<float> musicDuckReductionDb_ { 0.0f };

    std::atomic<bool> playing_ { false };
    std::atomic<bool> showAfter_ { false };
    std::atomic<int> mutedMusicClipIndex_ { -1 };
    std::atomic<float> noiseReductionAmount_ { 1.0f };
    std::atomic<float> rmsLevelDb_ { -90.0f };
    std::atomic<float> peakLevelDb_ { -90.0f };

    double sourceRate_ = 48000.0;
    double deviceRate_ = 48000.0;
    int blockSize_ = 512;
    double readPos_ = 0.0; // source samples; audio thread only
    double rmsLin_ = 0.0;  // audio thread only
    double peakLin_ = 0.0; // audio thread only

    void mixMusicInto(juce::AudioBuffer<float>& dest, int startSample, int numSamples,
                      double timelineStartSeconds);

    // Analysis ring for the live spectrum (single producer = audio thread).
    std::vector<float> analysisRing_ = std::vector<float>(kAnalysisSize, 0.0f);
    std::atomic<int> analysisWrite_ { 0 };

    // Parallel ring for the post-duck backing-music output (DuckView waveform).
    std::vector<float> musicAnalysisRing_ = std::vector<float>(kAnalysisSize, 0.0f);
    std::atomic<int> musicAnalysisWrite_ { 0 };
};
