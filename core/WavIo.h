#pragma once

#include "AudioBuffer.h"

#include <string>

namespace vc {

// Minimal portable WAV I/O. Reads 16-bit PCM and 32-bit IEEE float WAVs;
// always writes 32-bit IEEE float. Throws std::runtime_error on failure.
AudioBuffer readWav(const std::string& path);
void writeWavFloat32(const std::string& path, const AudioBuffer& buffer);

} // namespace vc
