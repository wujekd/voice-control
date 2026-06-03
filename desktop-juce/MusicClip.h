#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

struct MusicClip {
    juce::String name;
    juce::AudioBuffer<float> audio;
    double sampleRate = 48000.0;
    double startSeconds = 0.0;
    double lengthSeconds = 0.0;
    double gainDb = -18.0;
    double fadeInSeconds = 1.0;
    double fadeOutSeconds = 1.0;

    double sourceDurationSeconds() const {
        return sampleRate > 0.0 ? static_cast<double>(audio.getNumSamples()) / sampleRate : 0.0;
    }

    double durationSeconds() const {
        const double source = sourceDurationSeconds();
        if (lengthSeconds <= 0.0)
            return source;
        return juce::jlimit(0.0, source, lengthSeconds);
    }
};
