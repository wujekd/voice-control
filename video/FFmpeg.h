#pragma once

#include <string>

namespace vc {

// Thin wrapper over the `ffmpeg` CLI. Keeps the video layer isolated so the
// core engine stays codec-free. Each call throws std::runtime_error on failure.
class FFmpeg {
public:
    explicit FFmpeg(std::string ffmpegPath = "ffmpeg");

    // Decodes the input's audio to a 48 kHz stereo 32-bit float WAV.
    void extractAudio(const std::string& inputVideo, const std::string& outWav);

    // Copies the input's video stream untouched and muxes in the new audio.
    void remux(const std::string& inputVideo, const std::string& inWav,
               const std::string& outputVideo);

    // Transcodes a WAV to another audio file; codec is inferred from the
    // output extension (used when the source had no video to mux into).
    void encodeAudio(const std::string& inWav, const std::string& outAudio);

private:
    void run(const std::string& args);
    std::string ffmpeg_;
};

} // namespace vc
