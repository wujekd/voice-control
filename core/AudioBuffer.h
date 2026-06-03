#pragma once

#include <cstddef>
#include <vector>

namespace vc {

// Planar float audio. Portable: no JUCE, no platform deps.
// Channels are stored separately so DSP can iterate one channel at a time.
struct AudioBuffer {
    int sampleRate = 48000;
    std::vector<std::vector<float>> channels; // channels[ch][frame]

    int numChannels() const { return static_cast<int>(channels.size()); }

    std::size_t numFrames() const {
        return channels.empty() ? 0 : channels.front().size();
    }
};

} // namespace vc
