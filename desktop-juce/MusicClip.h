#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <vector>

struct MusicClip {
    juce::String name;
    juce::AudioBuffer<float> audio;
    double sampleRate = 48000.0;
    double startSeconds = 0.0;
    double sourceOffsetSeconds = 0.0;
    double lengthSeconds = 0.0;
    double gainDb = -18.0;
    double fadeInSeconds = 1.0;
    double fadeOutSeconds = 1.0;
    std::vector<float> waveformPeaks;
    int waveformProcessedColumns = 0;

    double sourceDurationSeconds() const {
        return sampleRate > 0.0 ? static_cast<double>(audio.getNumSamples()) / sampleRate : 0.0;
    }

    double durationSeconds() const {
        const double source = sourceDurationSeconds();
        const double available = juce::jmax(0.0, source - juce::jlimit(0.0, source, sourceOffsetSeconds));
        if (lengthSeconds <= 0.0)
            return available;
        return juce::jlimit(0.0, available, lengthSeconds);
    }
};
