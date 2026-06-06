#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioBuffer.h"        // vc::AudioBuffer
#include "DenoiseStreamer.h"    // vc::DenoiseStreamer (real-time denoise)
#include "Eq.h"                 // vc::EqBand
#include "MusicClip.h"
#include "Presets.h"            // vc::Preset, vc::Tone, vc::ChainParams
#include "SpectrumAnalyzer.h"   // vc::SpectrumResult

#include <atomic>
#include <cstdint>
#include <vector>

// Glue between the JUCE UI and the portable engine. Owns the decoded audio,
// runs the VoiceChain, and drives FFmpeg for extract/export. Knows nothing
// about widgets — the UI calls these methods (off the message thread for the
// slow ones). All the DSP lives in vc_core; this class never reimplements it.
class ProcessingEngine {
public:
    // Decodes a video OR audio file's audio to the "before" buffer (via a temp
    // WAV). Returns false and fills `error` on failure. Slow (calls ffmpeg).
    bool loadMedia(const juce::File& media, juce::String& error);

    // Runs the chain for the given params, producing the "after" buffer.
    void process(const vc::ChainParams& params);

    // Re-runs the chain and writes the result. For a video source it muxes the
    // audio back into the video; for an audio source it writes an audio file
    // (format inferred from the output extension). Slow (calls ffmpeg).
    bool exportTo(const juce::File& output, const vc::ChainParams& params,
                  juce::String& error);

    bool addMusicClip(const juce::File& audioFile, juce::String& error);
    void setMusicClipParams(int index, double startSeconds, double sourceOffsetSeconds, double gainDb,
                            double fadeInSeconds, double fadeOutSeconds,
                            double lengthSeconds = 0.0);
    bool processMusicWaveformChunks(int maxColumns);
    void removeMusicClip(int index);
    void setMusicClips(std::vector<MusicClip> clips);
    const std::vector<MusicClip>& musicClips() const { return musicClips_; }
    void setMusicMasterGainDb(double gainDb);
    double musicMasterGainDb() const { return musicMasterGainDb_; }

    // Whole-file average spectrum and the auto-EQ curve derived from it,
    // computed once at load. Used by the chain and the GUI spectrum view.
    const vc::SpectrumResult& spectrum() const { return spectrum_; }
    const std::vector<vc::EqBand>& autoEqBands() const { return autoEqBands_; }

    bool hasAudio() const { return original_.numFrames() > 0; }
    bool hasDenoised() const { return streamer_.modelReady(); }
    bool hasProcessed() const { return processedReady_; }
    const juce::AudioBuffer<float>& beforeBuffer() const { return beforeJuce_; }
    const juce::AudioBuffer<float>& afterBuffer() const { return afterJuce_; }
    double sampleRate() const { return static_cast<double>(original_.sampleRate); }

    // Progressive denoise output for the live preview blend. The planar buffer
    // and per-hop validity flags are filled in the background; a set flag (read
    // with acquire) means that hop's denoised samples are visible.
    const vc::AudioBuffer* denoisedPlanar() const { return &streamer_.denoised(); }
    const std::atomic<std::uint8_t>* denoisedValidHops() const { return streamer_.validHops(); }
    int denoisedNumHops() const { return streamer_.numHops(); }
    int denoisedHopSize() const { return streamer_.hopSize(); }

    // Steer the background denoiser toward the currently-playing source frame.
    void setPlayheadFrame(std::int64_t frame) { streamer_.setPlayheadFrame(frame); }

    // Integrated loudness of the (unprocessed) input, measured once at load.
    // May be -inf for silence.
    double inputLufs() const { return inputLufs_; }
    double inputPeakDb() const { return inputPeakDb_; }

    // Integrated loudness of the signal *reaching* the loudness stage, i.e.
    // after HPF -> EQ -> compressor -> de-esser for the given params (loudness
    // and limiter disabled). The live chain uses `target - this` as its gain so
    // the preview hits the loudness target despite the compression. Runs an
    // offline pass on a copy of the input; call off the audio thread.
    double measureChainLoudness(const vc::ChainParams& params) const;
    vc::SpectrumResult analyzeProcessedVoiceSpectrum(const vc::ChainParams& params) const;
    juce::File sourceFile() const { return sourceFile_; }
    bool sourceHasVideo() const { return sourceHasVideo_; }

    double lastInputLufs() const { return lastInputLufs_; }
    double lastGainDb() const { return lastGainDb_; }

private:
    static juce::AudioBuffer<float> toJuce(const vc::AudioBuffer& src);
    static vc::AudioBuffer blendNoiseReduction(const vc::AudioBuffer& original,
                                               const vc::AudioBuffer& denoised,
                                               double amount);
    static void mixMusicInto(vc::AudioBuffer& dest, const std::vector<MusicClip>& clips, double masterGainDb);

    vc::AudioBuffer original_;
    vc::AudioBuffer processed_;
    vc::DenoiseStreamer streamer_;
    juce::AudioBuffer<float> beforeJuce_;
    juce::AudioBuffer<float> afterJuce_;
    bool processedReady_ = false;
    juce::File sourceFile_;
    bool sourceHasVideo_ = false;
    std::vector<MusicClip> musicClips_;
    double musicMasterGainDb_ = 0.0;
    vc::SpectrumResult spectrum_;
    std::vector<vc::EqBand> autoEqBands_;
    double inputLufs_ = 0.0;
    double inputPeakDb_ = -120.0;
    double lastInputLufs_ = 0.0;
    double lastGainDb_ = 0.0;
};
