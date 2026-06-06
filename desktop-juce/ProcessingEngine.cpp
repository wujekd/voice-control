#include "ProcessingEngine.h"

#include "Denoiser.h"           // vc::Denoiser::findDefaultModel
#include "FFmpeg.h"             // vc::FFmpeg
#include "LoudnessNormalizer.h" // vc::LoudnessNormalizer (input measurement)
#include "SpectrumAnalyzer.h"   // vc::SpectrumAnalyzer
#include "VoiceChain.h"         // vc::VoiceChain
#include "WavIo.h"              // vc::readWav / writeWavFloat32

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>

namespace {
constexpr int kAnalysisCacheVersion = 2;

bool extensionIsVideo(const juce::File& f) {
    static const juce::StringArray video { "mp4", "mov", "m4v", "mkv", "avi", "webm" };
    return video.contains(f.getFileExtension().removeCharacters(".").toLowerCase());
}

juce::File cacheFileFor(const juce::File& source) {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Voice Control")
        .getChildFile("AnalysisCache");
    dir.createDirectory();
    return dir.getChildFile(juce::String::toHexString(source.getFullPathName().hashCode64()) + ".json");
}

juce::var floatVectorToVar(const std::vector<float>& values) {
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated(static_cast<int>(values.size()));
    for (float v : values)
        arr.add(static_cast<double>(v));
    return arr;
}

juce::var doubleVectorToVar(const std::vector<double>& values) {
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated(static_cast<int>(values.size()));
    for (double v : values)
        arr.add(v);
    return arr;
}

std::vector<float> varToFloatVector(const juce::var& value) {
    std::vector<float> out;
    if (!value.isArray())
        return out;
    const auto* arr = value.getArray();
    out.reserve(static_cast<std::size_t>(arr->size()));
    for (const auto& v : *arr)
        out.push_back(static_cast<float>(static_cast<double>(v)));
    return out;
}

std::vector<double> varToDoubleVector(const juce::var& value) {
    std::vector<double> out;
    if (!value.isArray())
        return out;
    const auto* arr = value.getArray();
    out.reserve(static_cast<std::size_t>(arr->size()));
    for (const auto& v : *arr)
        out.push_back(static_cast<double>(v));
    return out;
}

juce::var spectrumToVar(const vc::SpectrumResult& spectrum) {
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("valid", spectrum.valid);
    obj->setProperty("sampleRate", spectrum.sampleRate);
    obj->setProperty("fftSize", spectrum.fftSize);
    obj->setProperty("binPower", doubleVectorToVar(spectrum.binPower));
    obj->setProperty("binDb", floatVectorToVar(spectrum.binDb));
    return juce::var(obj.release());
}

vc::SpectrumResult varToSpectrum(const juce::var& value) {
    vc::SpectrumResult out;
    if (auto* obj = value.getDynamicObject()) {
        out.valid = static_cast<bool>(obj->getProperty("valid"));
        out.sampleRate = static_cast<double>(obj->getProperty("sampleRate"));
        out.fftSize = static_cast<int>(obj->getProperty("fftSize"));
        out.binPower = varToDoubleVector(obj->getProperty("binPower"));
        out.binDb = varToFloatVector(obj->getProperty("binDb"));
        out.valid = out.valid && !out.binPower.empty() && !out.binDb.empty();
    }
    return out;
}

}

juce::AudioBuffer<float> ProcessingEngine::toJuce(const vc::AudioBuffer& src) {
    const int channels = src.numChannels();
    const int frames = static_cast<int>(src.numFrames());
    juce::AudioBuffer<float> out(juce::jmax(1, channels), juce::jmax(1, frames));
    out.clear();
    for (int ch = 0; ch < channels; ++ch)
        out.copyFrom(ch, 0, src.channels[static_cast<std::size_t>(ch)].data(), frames);
    return out;
}

std::vector<float> ProcessingEngine::computeWaveformPeaks(const vc::AudioBuffer& src, int columns) {
    const int n = std::max(1, columns);
    std::vector<float> peaks(static_cast<std::size_t>(n), 0.0f);
    const std::size_t frames = src.numFrames();
    const int channels = src.numChannels();
    if (frames == 0 || channels <= 0)
        return peaks;

    for (int x = 0; x < n; ++x) {
        const std::size_t s0 = static_cast<std::size_t>(
            static_cast<unsigned long long>(x) * frames / static_cast<unsigned long long>(n));
        const std::size_t s1 = std::max<std::size_t>(s0 + 1,
            static_cast<std::size_t>(
                static_cast<unsigned long long>(x + 1) * frames / static_cast<unsigned long long>(n)));

        float peak = 0.0f;
        for (std::size_t i = s0; i < std::min(frames, s1); ++i) {
            float mono = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                mono += src.channels[static_cast<std::size_t>(ch)][i];
            mono /= static_cast<float>(channels);
            peak = std::max(peak, std::abs(mono));
        }
        peaks[static_cast<std::size_t>(x)] = peak;
    }
    return peaks;
}

vc::AudioBuffer ProcessingEngine::blendNoiseReduction(const vc::AudioBuffer& original,
                                                      const vc::AudioBuffer& denoised,
                                                      double amount) {
    const float wet = static_cast<float>(std::clamp(amount, 0.0, 1.0));
    const float dry = 1.0f - wet;
    if (wet <= 0.0f || denoised.numFrames() == 0)
        return original;

    vc::AudioBuffer out = original;
    const int channels = std::min(original.numChannels(), denoised.numChannels());
    const std::size_t frames = std::min(original.numFrames(), denoised.numFrames());
    for (int ch = 0; ch < channels; ++ch) {
        auto& dst = out.channels[static_cast<std::size_t>(ch)];
        const auto& src = original.channels[static_cast<std::size_t>(ch)];
        const auto& den = denoised.channels[static_cast<std::size_t>(ch)];
        for (std::size_t i = 0; i < frames; ++i)
            dst[i] = dry * src[i] + wet * den[i];
    }
    return out;
}

bool ProcessingEngine::loadAnalysisCache(const juce::File& media) {
    const auto cache = cacheFileFor(media);
    if (!cache.existsAsFile())
        return false;

    juce::var parsed;
    juce::JSON::parse(cache.loadFileAsString(), parsed);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return false;

    if (static_cast<int>(root->getProperty("schemaVersion")) != kAnalysisCacheVersion)
        return false;
    if (root->getProperty("path").toString() != media.getFullPathName())
        return false;
    if (static_cast<juce::int64>(root->getProperty("sizeBytes")) != media.getSize())
        return false;
    if (static_cast<juce::int64>(root->getProperty("modifiedMs"))
        != media.getLastModificationTime().toMilliseconds())
        return false;

    inputLufs_ = static_cast<double>(root->getProperty("inputLufs"));
    inputPeakDb_ = static_cast<double>(root->getProperty("inputPeakDb"));
    drySpectrum_ = varToSpectrum(root->getProperty("drySpectrum"));
    spectrum_ = varToSpectrum(root->getProperty("activeSpectrum"));
    voiceProfileUsesDenoised_ = static_cast<bool>(root->getProperty("voiceProfileUsesDenoised"));
    voiceWaveformPeaks_ = varToFloatVector(root->getProperty("voiceWaveformPeaks"));
    processedVoiceWaveformPeaks_ = varToFloatVector(root->getProperty("processedVoiceWaveformPeaks"));

    if (!drySpectrum_.valid || !spectrum_.valid || voiceWaveformPeaks_.empty()) {
        voiceProfileUsesDenoised_ = false;
        processedVoiceWaveformPeaks_.clear();
        return false;
    }

    autoEqBands_ = autoEqBands(0.6);
    return true;
}

void ProcessingEngine::saveAnalysisCache() const {
    if (sourceFile_ == juce::File() || !drySpectrum_.valid || !spectrum_.valid || voiceWaveformPeaks_.empty())
        return;

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("schemaVersion", kAnalysisCacheVersion);
    root->setProperty("path", sourceFile_.getFullPathName());
    root->setProperty("sizeBytes", static_cast<double>(sourceFile_.getSize()));
    root->setProperty("modifiedMs", static_cast<double>(sourceFile_.getLastModificationTime().toMilliseconds()));
    root->setProperty("inputLufs", inputLufs_);
    root->setProperty("inputPeakDb", inputPeakDb_);
    root->setProperty("drySpectrum", spectrumToVar(drySpectrum_));
    root->setProperty("activeSpectrum", spectrumToVar(spectrum_));
    root->setProperty("voiceProfileUsesDenoised", voiceProfileUsesDenoised_);
    root->setProperty("voiceWaveformPeaks", floatVectorToVar(voiceWaveformPeaks_));
    root->setProperty("processedVoiceWaveformPeaks", floatVectorToVar(processedVoiceWaveformPeaks_));

    cacheFileFor(sourceFile_).replaceWithText(juce::JSON::toString(juce::var(root.release())));
}

void ProcessingEngine::mixMusicInto(vc::AudioBuffer& dest, const std::vector<MusicClip>& clips,
                                    double masterGainDb) {
    const int destChannels = dest.numChannels();
    const std::size_t destFrames = dest.numFrames();
    if (destChannels <= 0 || destFrames == 0)
        return;

    const float masterGain = static_cast<float>(std::pow(10.0, std::clamp(masterGainDb, -60.0, 6.0) / 20.0));
    for (const auto& clip : clips) {
        if (clip.audio.getNumSamples() <= 1 || clip.sampleRate <= 0.0)
            continue;

        const int startFrame = static_cast<int>(std::max(0.0, clip.startSeconds) * dest.sampleRate);
        if (startFrame >= static_cast<int>(destFrames))
            continue;

        const float gain = masterGain * static_cast<float>(std::pow(10.0, clip.gainDb / 20.0));
        const double srcRatio = clip.sampleRate / static_cast<double>(dest.sampleRate);
        const int clipSamples = clip.audio.getNumSamples();
        const int maxFrames = static_cast<int>(std::min<std::size_t>(
            destFrames - static_cast<std::size_t>(startFrame),
            static_cast<std::size_t>(std::ceil(clip.durationSeconds() * dest.sampleRate))));
        const double clipDuration = clip.durationSeconds();

        for (int i = 0; i < maxFrames; ++i) {
            const double clipTime = static_cast<double>(i) / dest.sampleRate;
            double fade = 1.0;
            if (clip.fadeInSeconds > 0.0)
                fade = std::min(fade, clipTime / clip.fadeInSeconds);
            if (clip.fadeOutSeconds > 0.0)
                fade = std::min(fade, (clipDuration - clipTime) / clip.fadeOutSeconds);
            fade = std::clamp(fade, 0.0, 1.0);

            const double srcPos = clip.sourceOffsetSeconds * clip.sampleRate + i * srcRatio;
            const int i0 = static_cast<int>(std::clamp(srcPos, 0.0, static_cast<double>(clipSamples - 2)));
            const float frac = static_cast<float>(srcPos - i0);
            for (int ch = 0; ch < destChannels; ++ch) {
                const int sc = std::min(ch, clip.audio.getNumChannels() - 1);
                const float* src = clip.audio.getReadPointer(sc);
                const float sample = src[i0] + frac * (src[i0 + 1] - src[i0]);
                auto& channel = dest.channels[static_cast<std::size_t>(ch)];
                channel[static_cast<std::size_t>(startFrame + i)] += sample * gain * static_cast<float>(fade);
            }
        }
    }
}

bool ProcessingEngine::loadMedia(const juce::File& media, juce::String& error) {
    // Stop any in-flight denoise from a previous file before original_ (which
    // the worker references) is replaced below.
    streamer_.stop();

    // ffmpeg decodes both video and bare audio files; -vn just no-ops on audio.
    auto temp = juce::File::createTempFile(".wav");
    try {
        vc::FFmpeg ffmpeg;
        ffmpeg.extractAudio(media.getFullPathName().toStdString(),
                            temp.getFullPathName().toStdString());
        original_ = vc::readWav(temp.getFullPathName().toStdString());
    } catch (const std::exception& e) {
        temp.deleteFile();
        error = e.what();
        return false;
    }
    temp.deleteFile();
    sourceFile_ = media;
    sourceHasVideo_ = extensionIsVideo(media);
    beforeJuce_ = toJuce(original_);
    afterJuce_ = beforeJuce_; // until processed

    const bool cacheLoaded = loadAnalysisCache(media);
    if (!cacheLoaded) {
        voiceWaveformPeaks_ = computeWaveformPeaks(original_, 2048);
        processedVoiceWaveformPeaks_.clear();

        // Measure input loudness once; the live chain uses it for its cached gain.
        vc::LoudnessNormalizer meter;
        meter.prepare(original_.sampleRate, 0.0);
        inputLufs_ = meter.measureIntegratedLufs(original_);

        double peak = 0.0;
        for (const auto& ch : original_.channels)
            for (float s : ch)
                peak = std::max(peak, static_cast<double>(std::fabs(s)));
        inputPeakDb_ = 20.0 * std::log10(peak + 1e-12);

        // Analyse the dry whole-file spectrum immediately so the UI can show a
        // profile before denoise finishes. It is replaced with a denoised profile
        // by refreshVoiceProfileFromDenoised() once the background pass completes.
        drySpectrum_ = vc::SpectrumAnalyzer::analyze(original_, 12);
        spectrum_ = drySpectrum_;
        voiceProfileUsesDenoised_ = false;
        autoEqBands_ = autoEqBands(0.6);
        saveAnalysisCache();
    }

    // Kick off the in-process neural denoise in the background. Playback can
    // start immediately on the dry signal; the denoised version fills in (faster
    // than real time) and is blended live per the noise-reduction amount.
    streamer_.start(original_, vc::Denoiser::findDefaultModel());

    processedReady_ = false;
    return true;
}

void ProcessingEngine::clear() {
    streamer_.stop();
    original_ = {};
    processed_ = {};
    beforeJuce_.setSize(1, 1);
    beforeJuce_.clear();
    afterJuce_.setSize(1, 1);
    afterJuce_.clear();
    voiceWaveformPeaks_.clear();
    processedVoiceWaveformPeaks_.clear();
    processedReady_ = false;
    sourceFile_ = juce::File();
    sourceHasVideo_ = false;
    musicClips_.clear();
    musicMasterGainDb_ = 0.0;
    spectrum_ = {};
    drySpectrum_ = {};
    autoEqBands_.clear();
    voiceProfileUsesDenoised_ = false;
    inputLufs_ = 0.0;
    inputPeakDb_ = -120.0;
    lastInputLufs_ = 0.0;
    lastGainDb_ = 0.0;
}

std::vector<vc::EqBand> ProcessingEngine::autoEqBands(double strength) const {
    if (voiceProfileUsesDenoised_)
        return vc::computeNoiseAwareAutoEqBands(spectrum_, drySpectrum_, strength);
    return vc::computeAutoEqBands(spectrum_, strength);
}

vc::SpectrumResult ProcessingEngine::previewSpectrum(double noiseReductionAmount) const {
    if (!drySpectrum_.valid)
        return spectrum_;
    if (!voiceProfileUsesDenoised_ || !spectrum_.valid)
        return drySpectrum_;

    vc::SpectrumResult out = drySpectrum_;
    const float wet = static_cast<float>(std::clamp(noiseReductionAmount, 0.0, 1.0));
    const float dry = 1.0f - wet;
    const std::size_t bins = std::min(drySpectrum_.binPower.size(), spectrum_.binPower.size());
    out.binPower.assign(bins, 0.0);
    out.binDb.assign(bins, -120.0f);
    for (std::size_t i = 0; i < bins; ++i) {
        const double p = dry * dry * drySpectrum_.binPower[i] + wet * wet * spectrum_.binPower[i];
        out.binPower[i] = p;
        out.binDb[i] = static_cast<float>(10.0 * std::log10(p + 1e-12));
    }
    out.valid = bins > 0;
    return out;
}

bool ProcessingEngine::refreshVoiceProfileFromDenoised() {
    if (voiceProfileUsesDenoised_ || original_.numFrames() == 0 || !streamer_.isComplete())
        return false;

    const auto& denoised = streamer_.denoised();
    if (denoised.numFrames() == 0)
        return false;

    spectrum_ = vc::SpectrumAnalyzer::analyze(denoised, 12);
    voiceProfileUsesDenoised_ = spectrum_.valid;
    processedVoiceWaveformPeaks_ = computeWaveformPeaks(denoised, 2048);
    autoEqBands_ = autoEqBands(0.6);
    saveAnalysisCache();
    return voiceProfileUsesDenoised_;
}

bool ProcessingEngine::addMusicClip(const juce::File& audioFile, juce::String& error) {
    auto temp = juce::File::createTempFile(".wav");
    vc::AudioBuffer decoded;
    try {
        vc::FFmpeg ffmpeg;
        ffmpeg.extractAudio(audioFile.getFullPathName().toStdString(),
                            temp.getFullPathName().toStdString());
        decoded = vc::readWav(temp.getFullPathName().toStdString());
    } catch (const std::exception& e) {
        temp.deleteFile();
        error = e.what();
        return false;
    }
    temp.deleteFile();

    MusicClip clip;
    clip.name = audioFile.getFileName();
    clip.sourcePath = audioFile.getFullPathName();
    clip.sampleRate = decoded.sampleRate;
    clip.audio = toJuce(decoded);
    musicClips_.push_back(std::move(clip));
    return true;
}

void ProcessingEngine::setMusicClipParams(int index, double startSeconds, double sourceOffsetSeconds, double gainDb,
                                          double fadeInSeconds, double fadeOutSeconds,
                                          double lengthSeconds) {
    if (index < 0 || index >= static_cast<int>(musicClips_.size()))
        return;
    auto& clip = musicClips_[static_cast<std::size_t>(index)];
    clip.startSeconds = std::max(0.0, startSeconds);
    const double sourceDuration = clip.sourceDurationSeconds();
    clip.sourceOffsetSeconds = std::clamp(sourceOffsetSeconds, 0.0, std::max(0.0, sourceDuration - 0.1));
    clip.gainDb = std::clamp(gainDb, -60.0, 6.0);
    clip.fadeInSeconds = std::max(0.0, fadeInSeconds);
    clip.fadeOutSeconds = std::max(0.0, fadeOutSeconds);
    const double available = std::max(0.1, sourceDuration - clip.sourceOffsetSeconds);
    clip.lengthSeconds = lengthSeconds <= 0.0
        ? available
        : std::clamp(lengthSeconds, 0.1, available);
}

bool ProcessingEngine::processMusicWaveformChunks(int maxColumns) {
    constexpr int kWaveformColumns = 4096;
    bool changed = false;
    int remaining = std::max(0, maxColumns);

    for (auto& clip : musicClips_) {
        if (remaining <= 0)
            break;
        if (clip.audio.getNumSamples() <= 0 || clip.audio.getNumChannels() <= 0)
            continue;
        if (static_cast<int>(clip.waveformPeaks.size()) != kWaveformColumns) {
            clip.waveformPeaks.assign(kWaveformColumns, 0.0f);
            clip.waveformProcessedColumns = 0;
        }

        const int samples = clip.audio.getNumSamples();
        const int channels = clip.audio.getNumChannels();
        while (remaining > 0 && clip.waveformProcessedColumns < static_cast<int>(clip.waveformPeaks.size())) {
            const int col = clip.waveformProcessedColumns;
            const int s0 = static_cast<int>(static_cast<int64_t>(col) * samples / kWaveformColumns);
            const int s1 = std::max(s0 + 1,
                static_cast<int>(static_cast<int64_t>(col + 1) * samples / kWaveformColumns));

            float peak = 0.0f;
            for (int s = s0; s < std::min(samples, s1); ++s)
                for (int ch = 0; ch < channels; ++ch)
                    peak = std::max(peak, std::abs(clip.audio.getSample(ch, s)));

            clip.waveformPeaks[static_cast<std::size_t>(col)] = juce::jlimit(0.0f, 1.0f, peak);
            ++clip.waveformProcessedColumns;
            --remaining;
            changed = true;
        }
    }

    return changed;
}

void ProcessingEngine::removeMusicClip(int index) {
    if (index < 0 || index >= static_cast<int>(musicClips_.size()))
        return;
    musicClips_.erase(musicClips_.begin() + index);
}

void ProcessingEngine::setMusicClips(std::vector<MusicClip> clips) {
    musicClips_ = std::move(clips);
}

void ProcessingEngine::setMusicMasterGainDb(double gainDb) {
    musicMasterGainDb_ = std::clamp(gainDb, -60.0, 6.0);
}

double ProcessingEngine::measureChainLoudness(const vc::ChainParams& params) const {
    if (original_.numFrames() == 0)
        return -std::numeric_limits<double>::infinity();

    vc::ChainParams p = params;
    p.loudnessEnabled = false; // measure the level arriving at the loudness stage
    p.limiterEnabled = false;

    // Measured at load before denoise is ready; the dry signal is a fine
    // reference for the loudness-gain estimate.
    vc::AudioBuffer copy = original_;
    vc::VoiceChain chain;
    chain.prepare(copy.sampleRate, copy.numChannels(), p);
    chain.process(copy);

    vc::LoudnessNormalizer meter;
    meter.prepare(copy.sampleRate, 0.0);
    return meter.measureIntegratedLufs(copy);
}

vc::SpectrumResult ProcessingEngine::analyzeProcessedVoiceSpectrum(const vc::ChainParams& params) const {
    if (original_.numFrames() == 0)
        return {};

    vc::AudioBuffer preview = streamer_.modelReady()
        ? blendNoiseReduction(original_, streamer_.denoised(), params.noiseReductionAmount)
        : original_;

    vc::VoiceChain chain;
    chain.prepare(preview.sampleRate, preview.numChannels(), params);
    chain.process(preview);
    return vc::SpectrumAnalyzer::analyze(preview, 12);
}

void ProcessingEngine::process(const vc::ChainParams& params) {
    if (!hasAudio()) return;

    processed_ = streamer_.modelReady()
        ? blendNoiseReduction(original_, streamer_.denoised(), params.noiseReductionAmount)
        : original_;
    vc::VoiceChain chain;
    chain.prepare(processed_.sampleRate, processed_.numChannels(), params);
    chain.process(processed_);
    mixMusicInto(processed_, musicClips_, musicMasterGainDb_);

    lastInputLufs_ = chain.measuredInputLufs();
    lastGainDb_ = chain.appliedLoudnessGainDb();

    afterJuce_ = toJuce(processed_);
    processedReady_ = true;
}

bool ProcessingEngine::exportTo(const juce::File& output, const vc::ChainParams& params,
                                bool muxVideo, juce::String& error) {
    if (!hasAudio()) {
        error = "No audio loaded.";
        return false;
    }

    // Export needs the whole file denoised, not just the played region.
    streamer_.waitUntilComplete();
    process(params); // exact offline render with the chosen settings (incl. auto-EQ)

    const bool outIsWav = output.getFileExtension().equalsIgnoreCase(".wav");
    // For a WAV destination we can write directly; otherwise stage a temp WAV.
    auto temp = outIsWav ? output : juce::File::createTempFile(".wav");

    try {
        vc::writeWavFloat32(temp.getFullPathName().toStdString(), processed_);

        vc::FFmpeg ffmpeg;
        if (muxVideo) {
            ffmpeg.remux(sourceFile_.getFullPathName().toStdString(),
                         temp.getFullPathName().toStdString(),
                         output.getFullPathName().toStdString());
        } else if (!outIsWav) {
            ffmpeg.encodeAudio(temp.getFullPathName().toStdString(),
                               output.getFullPathName().toStdString());
        }
        // (audio source + WAV output: already written directly to `output`)
    } catch (const std::exception& e) {
        if (!outIsWav) temp.deleteFile();
        error = e.what();
        return false;
    }
    if (!outIsWav) temp.deleteFile();
    return true;
}
