#include "WavIo.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace vc {
namespace {

uint32_t readU32(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t readU16(const unsigned char* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

void writeU32(std::ostream& os, uint32_t v) {
    unsigned char b[4] = {(unsigned char)(v), (unsigned char)(v >> 8),
                          (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
    os.write(reinterpret_cast<char*>(b), 4);
}
void writeU16(std::ostream& os, uint16_t v) {
    unsigned char b[2] = {(unsigned char)(v), (unsigned char)(v >> 8)};
    os.write(reinterpret_cast<char*>(b), 2);
}

} // namespace

AudioBuffer readWav(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("readWav: cannot open " + path);

    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        throw std::runtime_error("readWav: not a RIFF/WAVE file: " + path);

    uint16_t format = 0, numChannels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    const unsigned char* dataPtr = nullptr;
    uint32_t dataLen = 0;

    std::size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const unsigned char* id = bytes.data() + pos;
        uint32_t chunkSize = readU32(bytes.data() + pos + 4);
        const unsigned char* body = bytes.data() + pos + 8;

        if (std::memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16) {
            format = readU16(body);
            numChannels = readU16(body + 2);
            sampleRate = readU32(body + 4);
            bitsPerSample = readU16(body + 14);
        } else if (std::memcmp(id, "data", 4) == 0) {
            dataPtr = body;
            dataLen = chunkSize;
            if (pos + 8 + dataLen > bytes.size())
                dataLen = static_cast<uint32_t>(bytes.size() - (pos + 8));
        }
        pos += 8 + chunkSize + (chunkSize & 1); // chunks are word-aligned
    }

    if (numChannels == 0 || sampleRate == 0 || dataPtr == nullptr)
        throw std::runtime_error("readWav: missing fmt/data chunk: " + path);

    // format 1 = PCM, 3 = IEEE float, 0xFFFE = extensible (assume PCM here).
    const bool isFloat = (format == 3);
    if (!isFloat && bitsPerSample != 16)
        throw std::runtime_error("readWav: unsupported sample format (only 16-bit PCM or 32-bit float)");
    if (isFloat && bitsPerSample != 32)
        throw std::runtime_error("readWav: float WAV must be 32-bit");

    const int bytesPerSample = bitsPerSample / 8;
    const std::size_t totalSamples = dataLen / bytesPerSample;
    const std::size_t frames = totalSamples / numChannels;

    AudioBuffer buf;
    buf.sampleRate = static_cast<int>(sampleRate);
    buf.channels.assign(numChannels, std::vector<float>(frames, 0.0f));

    for (std::size_t f = 0; f < frames; ++f) {
        for (uint16_t ch = 0; ch < numChannels; ++ch) {
            const unsigned char* s = dataPtr + (f * numChannels + ch) * bytesPerSample;
            float v;
            if (isFloat) {
                uint32_t bits = readU32(s);
                std::memcpy(&v, &bits, sizeof(v));
            } else {
                int16_t i16 = static_cast<int16_t>(readU16(s));
                v = i16 / 32768.0f;
            }
            buf.channels[ch][f] = v;
        }
    }
    return buf;
}

void writeWavFloat32(const std::string& path, const AudioBuffer& buffer) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("writeWav: cannot open " + path);

    const uint16_t numChannels = static_cast<uint16_t>(buffer.numChannels());
    const uint32_t sampleRate = static_cast<uint32_t>(buffer.sampleRate);
    const uint16_t bitsPerSample = 32;
    const uint16_t bytesPerSample = bitsPerSample / 8;
    const std::size_t frames = buffer.numFrames();
    const uint32_t dataLen = static_cast<uint32_t>(frames * numChannels * bytesPerSample);
    const uint16_t blockAlign = numChannels * bytesPerSample;
    const uint32_t byteRate = sampleRate * blockAlign;

    out.write("RIFF", 4);
    writeU32(out, 36 + dataLen);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    writeU32(out, 16);
    writeU16(out, 3); // IEEE float
    writeU16(out, numChannels);
    writeU32(out, sampleRate);
    writeU32(out, byteRate);
    writeU16(out, blockAlign);
    writeU16(out, bitsPerSample);

    out.write("data", 4);
    writeU32(out, dataLen);

    for (std::size_t f = 0; f < frames; ++f) {
        for (uint16_t ch = 0; ch < numChannels; ++ch) {
            float v = buffer.channels[ch][f];
            uint32_t bits;
            std::memcpy(&bits, &v, sizeof(bits));
            unsigned char b[4] = {(unsigned char)(bits), (unsigned char)(bits >> 8),
                                  (unsigned char)(bits >> 16), (unsigned char)(bits >> 24)};
            out.write(reinterpret_cast<char*>(b), 4);
        }
    }
    if (!out) throw std::runtime_error("writeWav: write failed for " + path);
}

void writeWavPcm16(const std::string& path, const AudioBuffer& buffer) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("writeWav: cannot open " + path);

    const uint16_t numChannels = static_cast<uint16_t>(buffer.numChannels());
    const uint32_t sampleRate = static_cast<uint32_t>(buffer.sampleRate);
    const uint16_t bitsPerSample = 16;
    const uint16_t bytesPerSample = bitsPerSample / 8;
    const std::size_t frames = buffer.numFrames();
    const uint32_t dataLen = static_cast<uint32_t>(frames * numChannels * bytesPerSample);
    const uint16_t blockAlign = numChannels * bytesPerSample;
    const uint32_t byteRate = sampleRate * blockAlign;

    out.write("RIFF", 4);
    writeU32(out, 36 + dataLen);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    writeU32(out, 16);
    writeU16(out, 1); // PCM
    writeU16(out, numChannels);
    writeU32(out, sampleRate);
    writeU32(out, byteRate);
    writeU16(out, blockAlign);
    writeU16(out, bitsPerSample);

    out.write("data", 4);
    writeU32(out, dataLen);

    for (std::size_t f = 0; f < frames; ++f) {
        for (uint16_t ch = 0; ch < numChannels; ++ch) {
            float v = buffer.channels[ch][f];
            v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
            int32_t s = static_cast<int32_t>(std::lround(v * 32767.0f));
            if (s < -32768) s = -32768;
            if (s > 32767) s = 32767;
            writeU16(out, static_cast<uint16_t>(static_cast<int16_t>(s)));
        }
    }
    if (!out) throw std::runtime_error("writeWav: write failed for " + path);
}

} // namespace vc
