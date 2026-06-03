#include "ProcessingEngine.h"

#include "FFmpeg.h"             // vc::FFmpeg
#include "LoudnessNormalizer.h" // vc::LoudnessNormalizer (input measurement)
#include "SpectrumAnalyzer.h"   // vc::SpectrumAnalyzer
#include "VoiceChain.h"         // vc::VoiceChain
#include "WavIo.h"              // vc::readWav / writeWavFloat32

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <cstdlib>

namespace {
bool extensionIsVideo(const juce::File& f) {
    static const juce::StringArray video { "mp4", "mov", "m4v", "mkv", "avi", "webm" };
    return video.contains(f.getFileExtension().removeCharacters(".").toLowerCase());
}

juce::String sh(const juce::String& s) {
    juce::String out = "'";
    for (auto c : s) {
        if (c == '\'') out += "'\\''";
        else out += juce::String::charToString(c);
    }
    out += "'";
    return out;
}

juce::File findDeepFilterTool() {
    const juce::File cwd = juce::File::getCurrentWorkingDirectory();
    for (juce::File dir = cwd; dir.exists(); dir = dir.getParentDirectory()) {
        auto local = dir.getChildFile("research/noise_removal/.venv/bin/deepFilter");
        if (local.existsAsFile())
            return local;
        if (dir == dir.getParentDirectory())
            break;
    }

    const char* path = std::getenv("PATH");
    if (path == nullptr)
        return {};
    juce::StringArray parts;
    parts.addTokens(path, ":", "");
    for (const auto& p : parts) {
        auto tool = juce::File(p).getChildFile("deepFilter");
        if (tool.existsAsFile())
            return tool;
    }
    return {};
}

juce::File findRepoRoot() {
    const juce::File cwd = juce::File::getCurrentWorkingDirectory();
    for (juce::File dir = cwd; dir.exists(); dir = dir.getParentDirectory()) {
        if (dir.getChildFile("research/noise_removal").isDirectory())
            return dir;
        if (dir == dir.getParentDirectory())
            break;
    }
    return cwd;
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

bool ProcessingEngine::buildDenoisedCache(const juce::File& inputWav, juce::String& error) {
    denoisedReady_ = false;
    denoised_ = {};
    denoisedJuce_.setSize(0, 0);

    const auto tool = findDeepFilterTool();
    if (!tool.existsAsFile()) {
        error = "DeepFilterNet tool not found; noise reduction cache skipped.";
        return false;
    }

    auto outDir = juce::File::createTempFile("_df_out");
    outDir.deleteFile();
    if (!outDir.createDirectory()) {
        error = "Could not create DeepFilterNet output folder.";
        return false;
    }

    const juce::String cmd =
        "XDG_CACHE_HOME=" + sh(findRepoRoot()
                                   .getChildFile("research/noise_removal/.cache")
                                   .getFullPathName())
        + " " + sh(tool.getFullPathName())
        + " --pf --no-suffix --log-level error --output-dir " + sh(outDir.getFullPathName())
        + " " + sh(inputWav.getFullPathName());

    const int rc = std::system(cmd.toRawUTF8());
    const auto outWav = outDir.getChildFile(inputWav.getFileName());
    if (rc != 0 || !outWav.existsAsFile()) {
        outDir.deleteRecursively();
        error = "DeepFilterNet processing failed; noise reduction cache skipped.";
        return false;
    }

    try {
        denoised_ = vc::readWav(outWav.getFullPathName().toStdString());
        denoisedJuce_ = toJuce(denoised_);
        denoisedReady_ = denoised_.sampleRate == original_.sampleRate
                         && denoised_.numChannels() == original_.numChannels()
                         && denoised_.numFrames() > 0;
    } catch (const std::exception& e) {
        error = e.what();
        denoisedReady_ = false;
    }

    outDir.deleteRecursively();
    return denoisedReady_;
}

void ProcessingEngine::mixMusicInto(vc::AudioBuffer& dest, const std::vector<MusicClip>& clips) {
    const int destChannels = dest.numChannels();
    const std::size_t destFrames = dest.numFrames();
    if (destChannels <= 0 || destFrames == 0)
        return;

    for (const auto& clip : clips) {
        if (clip.audio.getNumSamples() <= 1 || clip.sampleRate <= 0.0)
            continue;

        const int startFrame = static_cast<int>(std::max(0.0, clip.startSeconds) * dest.sampleRate);
        if (startFrame >= static_cast<int>(destFrames))
            continue;

        const float gain = static_cast<float>(std::pow(10.0, clip.gainDb / 20.0));
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

            const double srcPos = i * srcRatio;
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
    sourceFile_ = media;
    sourceHasVideo_ = extensionIsVideo(media);
    musicClips_.clear();
    beforeJuce_ = toJuce(original_);
    denoisedJuce_ = beforeJuce_;
    afterJuce_ = beforeJuce_; // until processed

    // Measure input loudness once; the live chain uses it for its cached gain.
    vc::LoudnessNormalizer meter;
    meter.prepare(original_.sampleRate, 0.0);
    inputLufs_ = meter.measureIntegratedLufs(original_);

    double peak = 0.0;
    for (const auto& ch : original_.channels)
        for (float s : ch)
            peak = std::max(peak, static_cast<double>(std::fabs(s)));
    inputPeakDb_ = 20.0 * std::log10(peak + 1e-12);

    // Analyse the whole-file spectrum and derive the wide auto-EQ curve.
    spectrum_ = vc::SpectrumAnalyzer::analyze(original_, 12);
    autoEqBands_ = vc::computeAutoEqBands(spectrum_, 0.6);

    juce::String denoiseError;
    buildDenoisedCache(temp, denoiseError);

    temp.deleteFile();
    processedReady_ = false;
    return true;
}

bool ProcessingEngine::addMusicClip(const juce::File& audioFile, juce::String& error) {
    if (!hasAudio()) {
        error = "Load voice/video audio before adding music.";
        return false;
    }

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
    clip.sampleRate = decoded.sampleRate;
    clip.audio = toJuce(decoded);
    musicClips_.push_back(std::move(clip));
    return true;
}

void ProcessingEngine::setMusicClipParams(int index, double startSeconds, double gainDb,
                                          double fadeInSeconds, double fadeOutSeconds,
                                          double lengthSeconds) {
    if (index < 0 || index >= static_cast<int>(musicClips_.size()))
        return;
    auto& clip = musicClips_[static_cast<std::size_t>(index)];
    clip.startSeconds = std::max(0.0, startSeconds);
    clip.gainDb = std::clamp(gainDb, -60.0, 6.0);
    clip.fadeInSeconds = std::max(0.0, fadeInSeconds);
    clip.fadeOutSeconds = std::max(0.0, fadeOutSeconds);
    clip.lengthSeconds = lengthSeconds <= 0.0
        ? clip.sourceDurationSeconds()
        : std::clamp(lengthSeconds, 0.1, clip.sourceDurationSeconds());
}

void ProcessingEngine::removeMusicClip(int index) {
    if (index < 0 || index >= static_cast<int>(musicClips_.size()))
        return;
    musicClips_.erase(musicClips_.begin() + index);
}

double ProcessingEngine::measureChainLoudness(const vc::ChainParams& params) const {
    if (original_.numFrames() == 0)
        return -std::numeric_limits<double>::infinity();

    vc::ChainParams p = params;
    p.loudnessEnabled = false; // measure the level arriving at the loudness stage
    p.limiterEnabled = false;

    vc::AudioBuffer copy = denoisedReady_
        ? blendNoiseReduction(original_, denoised_, params.noiseReductionAmount)
        : original_;
    vc::VoiceChain chain;
    chain.prepare(copy.sampleRate, copy.numChannels(), p);
    chain.process(copy);

    vc::LoudnessNormalizer meter;
    meter.prepare(copy.sampleRate, 0.0);
    return meter.measureIntegratedLufs(copy);
}

void ProcessingEngine::process(const vc::ChainParams& params) {
    if (!hasAudio()) return;

    processed_ = denoisedReady_
        ? blendNoiseReduction(original_, denoised_, params.noiseReductionAmount)
        : original_;
    vc::VoiceChain chain;
    chain.prepare(processed_.sampleRate, processed_.numChannels(), params);
    chain.process(processed_);
    mixMusicInto(processed_, musicClips_);

    lastInputLufs_ = chain.measuredInputLufs();
    lastGainDb_ = chain.appliedLoudnessGainDb();

    afterJuce_ = toJuce(processed_);
    processedReady_ = true;
}

bool ProcessingEngine::exportTo(const juce::File& output, const vc::ChainParams& params,
                                juce::String& error) {
    if (!hasAudio()) {
        error = "No audio loaded.";
        return false;
    }

    process(params); // exact offline render with the chosen settings (incl. auto-EQ)

    const bool outIsWav = output.getFileExtension().equalsIgnoreCase(".wav");
    // For a WAV destination we can write directly; otherwise stage a temp WAV.
    auto temp = outIsWav ? output : juce::File::createTempFile(".wav");

    try {
        vc::writeWavFloat32(temp.getFullPathName().toStdString(), processed_);

        vc::FFmpeg ffmpeg;
        if (sourceHasVideo_) {
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
