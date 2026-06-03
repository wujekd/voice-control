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
