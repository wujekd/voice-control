#include "FFmpeg.h"

#include <cstdlib>
#include <stdexcept>

namespace vc {
namespace {

// Wrap an argument in single quotes for /bin/sh, escaping any embedded quotes.
std::string sh(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

} // namespace

FFmpeg::FFmpeg(std::string ffmpegPath) : ffmpeg_(std::move(ffmpegPath)) {}

void FFmpeg::run(const std::string& args) {
    const std::string cmd = sh(ffmpeg_) + " -hide_banner -loglevel error -y " + args;
    int rc = std::system(cmd.c_str());
    if (rc != 0)
        throw std::runtime_error("ffmpeg failed (exit " + std::to_string(rc) + "): " + cmd);
}

void FFmpeg::extractAudio(const std::string& inputVideo, const std::string& outWav) {
    run("-i " + sh(inputVideo) + " -vn -ac 2 -ar 48000 -c:a pcm_f32le " + sh(outWav));
}

void FFmpeg::remux(const std::string& inputVideo, const std::string& inWav,
                   const std::string& outputVideo) {
    run("-i " + sh(inputVideo) + " -i " + sh(inWav) +
        " -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac -b:a 192k -shortest " + sh(outputVideo));
}

void FFmpeg::encodeAudio(const std::string& inWav, const std::string& outAudio) {
    run("-i " + sh(inWav) + " " + sh(outAudio));
}

} // namespace vc
