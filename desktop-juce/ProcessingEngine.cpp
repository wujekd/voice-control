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

namespace {
bool extensionIsVideo(const juce::File& f) {
    static const juce::StringArray video { "mp4", "mov", "m4v", "mkv", "avi", "webm" };
    return video.contains(f.getFileExtension().removeCharacters(".").toLowerCase());
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
    temp.deleteFile();

    sourceFile_ = media;
    sourceHasVideo_ = extensionIsVideo(media);
    beforeJuce_ = toJuce(original_);
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

    processedReady_ = false;
    return true;
}

double ProcessingEngine::measureChainLoudness(const vc::ChainParams& params) const {
    if (original_.numFrames() == 0)
        return -std::numeric_limits<double>::infinity();

    vc::ChainParams p = params;
    p.loudnessEnabled = false; // measure the level arriving at the loudness stage
    p.limiterEnabled = false;

    vc::AudioBuffer copy = original_; // const-safe: work on a copy
    vc::VoiceChain chain;
    chain.prepare(copy.sampleRate, copy.numChannels(), p);
    chain.process(copy);

    vc::LoudnessNormalizer meter;
    meter.prepare(copy.sampleRate, 0.0);
    return meter.measureIntegratedLufs(copy);
}

void ProcessingEngine::process(const vc::ChainParams& params) {
    if (!hasAudio()) return;

    processed_ = original_; // copy, then process in place
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
