#include "AudioBuffer.h"
#include "FFmpeg.h"
#include "Presets.h"
#include "VoiceChain.h"
#include "WavIo.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <input-video> <output-video> [preset] [tone]\n"
              << "  preset: light | balanced | strong       (default: balanced)\n"
              << "  tone:   natural | warm | crisp          (default: natural)\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 2;
    }

    const std::string input = argv[1];
    const std::string output = argv[2];
    const std::string presetName = (argc >= 4) ? argv[3] : "balanced";
    const std::string toneName = (argc >= 5) ? argv[4] : "natural";

    vc::Preset preset;
    if (!vc::presetFromString(presetName, preset)) {
        std::cerr << "Unknown preset: " << presetName << "\n";
        usage(argv[0]);
        return 2;
    }

    vc::Tone tone;
    if (!vc::toneFromString(toneName, tone)) {
        std::cerr << "Unknown tone: " << toneName << "\n";
        usage(argv[0]);
        return 2;
    }

    try {
        namespace fs = std::filesystem;
        const fs::path tmpDir = fs::temp_directory_path();
        const fs::path rawWav = tmpDir / "vc_raw.wav";
        const fs::path procWav = tmpDir / "vc_processed.wav";

        vc::FFmpeg ffmpeg;

        std::cout << "[1/4] Extracting audio...\n";
        ffmpeg.extractAudio(input, rawWav.string());

        std::cout << "[2/4] Processing (" << presetName << " / " << toneName << ")...\n";
        vc::AudioBuffer audio = vc::readWav(rawWav.string());
        vc::ChainParams params = vc::paramsForPreset(preset);
        params.tone = tone;

        vc::VoiceChain chain;
        chain.prepare(audio.sampleRate, audio.numChannels(), params);
        chain.process(audio);

        std::printf("      loudness: %.1f LUFS in -> %+.1f dB gain -> %.1f LUFS target\n",
                    chain.measuredInputLufs(), chain.appliedLoudnessGainDb(),
                    params.targetLufs);

        vc::writeWavFloat32(procWav.string(), audio);

        std::cout << "[3/4] Muxing enhanced audio back into video...\n";
        ffmpeg.remux(input, procWav.string(), output);

        std::cout << "[4/4] Done -> " << output << "\n";

        std::error_code ec;
        fs::remove(rawWav, ec);
        fs::remove(procWav, ec);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
