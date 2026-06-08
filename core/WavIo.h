#pragma once

#include "AudioBuffer.h"

#include <string>

namespace vc {

// Minimal portable WAV I/O. Reads 16-bit PCM and 32-bit IEEE float WAVs.
// Throws std::runtime_error on failure.
AudioBuffer readWav(const std::string& path);
void writeWavFloat32(const std::string& path, const AudioBuffer& buffer);
// 16-bit PCM writer — half the size of float32, transparent for voice. Used for
// the denoised-audio cache. Samples are clamped to [-1, 1].
void writeWavPcm16(const std::string& path, const AudioBuffer& buffer);

} // namespace vc
