#include "FFmpeg.h"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

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

std::string bundledFFmpegPath() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);

    std::string executable(size, '\0');
    if (_NSGetExecutablePath(executable.data(), &size) == 0) {
        namespace fs = std::filesystem;
        const fs::path executablePath(executable.c_str());
        const fs::path candidate =
            executablePath.parent_path().parent_path() / "Resources" / "ffmpeg";

        std::error_code ec;
        if (fs::exists(candidate, ec))
            return candidate.string();
    }
#endif

    return "ffmpeg";
}

} // namespace

FFmpeg::FFmpeg(std::string ffmpegPath)
    : ffmpeg_(ffmpegPath == "ffmpeg" ? bundledFFmpegPath() : std::move(ffmpegPath)) {}

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
