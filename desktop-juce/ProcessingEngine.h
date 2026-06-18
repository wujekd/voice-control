#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioBuffer.h"        // vc::AudioBuffer
#include "DenoiseStreamer.h"    // vc::DenoiseStreamer (real-time denoise)
#include "Eq.h"                 // vc::EqBand
#include "MusicClip.h"
#include "Presets.h"            // vc::Preset, vc::Tone, vc::ChainParams
#include "SpectrumAnalyzer.h"   // vc::SpectrumResult

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
    void clear();

    // Runs the chain for the given params, producing the "after" buffer.
    void process(const vc::ChainParams& params);

    // Re-runs the chain and writes the result. For a video source it muxes the
    // audio back into the video; for an audio source it writes an audio file
    // (format inferred from the output extension). Slow (calls ffmpeg).
    bool exportTo(const juce::File& output, const vc::ChainParams& params,
                  bool muxVideo, juce::String& error);

    bool addMusicClip(const juce::File& audioFile, juce::String& error);
    void setMusicClipParams(int index, double startSeconds, double sourceOffsetSeconds, double gainDb,
                            double fadeInSeconds, double fadeOutSeconds,
                            double lengthSeconds = 0.0);
    void setMusicClipWaveformPeaks(int index, std::vector<float> peaks, int processedColumns);
    bool processMusicWaveformChunks(int maxColumns);
    void removeMusicClip(int index);
    void setMusicClips(std::vector<MusicClip> clips);
    const std::vector<MusicClip>& musicClips() const { return musicClips_; }
    void setMusicMasterGainDb(double gainDb);
    double musicMasterGainDb() const { return musicMasterGainDb_; }

    // Whole-file average spectrum and the auto-EQ curve derived from it,
    // computed once at load. Used by the chain and the GUI spectrum view.
    const vc::SpectrumResult& spectrum() const { return spectrum_; }
    const vc::SpectrumResult& drySpectrum() const { return drySpectrum_; }
    const std::vector<vc::EqBand>& autoEqBands() const { return autoEqBands_; }
    std::vector<vc::EqBand> autoEqBands(double strength) const;

    // Detected voice fundamental (Hz); 0 when unknown / not speech.
    double voiceFundamentalHz() const { return voiceFundamentalHz_; }
    vc::SpectrumResult previewSpectrum(double noiseReductionAmount) const;

    bool hasAudio() const { return original_.numFrames() > 0; }
    bool hasDenoised() const { return streamer_.modelReady(); }
    bool voiceProfileUsesDenoised() const { return voiceProfileUsesDenoised_; }
    bool hasProcessed() const { return processedReady_; }
    const juce::AudioBuffer<float>& beforeBuffer() const { return beforeJuce_; }
    const juce::AudioBuffer<float>& afterBuffer() const { return afterJuce_; }
    const std::vector<float>& voiceWaveformPeaks() const { return voiceWaveformPeaks_; }
    const std::vector<float>& processedVoiceWaveformPeaks() const { return processedVoiceWaveformPeaks_; }
    // Linear gain that maps the raw stored peaks to the loudness-normalized
    // (heard) level, so the displayed waveform matches playback rather than the
    // raw input amplitude. 1.0 when loudness hasn't been measured yet.
    float waveformDisplayGain() const;
    double sampleRate() const { return static_cast<double>(original_.sampleRate); }

    // Complete preprocessed denoise output for the live preview blend. The
    // buffer is populated before playback is enabled.
    const vc::AudioBuffer* denoisedPlanar() const { return &streamer_.denoised(); }

    // Once the full-file denoise pass has completed, remeasure the balance
    // profile from the 100% denoised voice so auto-EQ ignores room noise/rumble.
    // Returns true only when it swaps in a newly-denoised profile.
    bool refreshVoiceProfileFromDenoised();

    // Once the background model pass is complete, write the denoised audio to
    // disk (16-bit) so the next load can skip denoising entirely. No-op if the
    // audio was restored from cache, already written, or not yet complete.
    // Returns true only on the tick it performs the write. Cheap to poll.
    bool persistDenoisedAudioIfReady();

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

    // Cached intensity loudness references (the loudness reaching the loudness
    // stage at min/max intensity). Measuring these means two full offline chain
    // renders of the whole file, so they are persisted with the analysis cache
    // and reused on reload instead of recomputed. `hasIntensityLoudnessRefs()`
    // is false until measured (or restored from cache).
    bool hasIntensityLoudnessRefs() const { return hasIntensityRefs_; }
    double intensityMinLoudnessRef() const { return intensityMinLoudnessRef_; }
    double intensityMaxLoudnessRef() const { return intensityMaxLoudnessRef_; }
    void setIntensityLoudnessRefs(double minRef, double maxRef);

    juce::var makeAnalysisCacheState() const;
    void setProjectAnalysisCache(const juce::var& state);
    void saveAnalysisCacheNow() const { saveAnalysisCache(); }

private:
    static juce::AudioBuffer<float> toJuce(const vc::AudioBuffer& src);
    static std::vector<float> computeWaveformPeaks(const vc::AudioBuffer& src, int columns);
    static vc::AudioBuffer blendNoiseReduction(const vc::AudioBuffer& original,
                                               const vc::AudioBuffer& denoised,
                                               double amount);
    static void mixMusicInto(vc::AudioBuffer& dest, const std::vector<MusicClip>& clips, double masterGainDb);
    bool loadAnalysisCache(const juce::File& media);
    bool applyAnalysisCacheState(const juce::var& state, const juce::File& media);
    void saveAnalysisCache() const;

    vc::AudioBuffer original_;
    vc::AudioBuffer processed_;
    vc::DenoiseStreamer streamer_;
    juce::AudioBuffer<float> beforeJuce_;
    juce::AudioBuffer<float> afterJuce_;
    std::vector<float> voiceWaveformPeaks_;
    std::vector<float> processedVoiceWaveformPeaks_;
    bool processedReady_ = false;
    juce::File sourceFile_;
    bool sourceHasVideo_ = false;
    std::vector<MusicClip> musicClips_;
    double musicMasterGainDb_ = 0.0;
    vc::SpectrumResult spectrum_;
    vc::SpectrumResult drySpectrum_;
    double voiceFundamentalHz_ = 0.0;
    std::vector<vc::EqBand> autoEqBands_;
    bool voiceProfileUsesDenoised_ = false;
    double inputLufs_ = 0.0;
    double inputPeakDb_ = -120.0;
    double intensityMinLoudnessRef_ = 0.0;
    double intensityMaxLoudnessRef_ = 0.0;
    bool hasIntensityRefs_ = false;
    bool denoisedAudioSaved_ = false;
    double lastInputLufs_ = 0.0;
    double lastGainDb_ = 0.0;
    juce::var pendingProjectAnalysisCache_;
};
