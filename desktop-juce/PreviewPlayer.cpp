#include "PreviewPlayer.h"

#include <cmath>

PreviewPlayer::PreviewPlayer() {
    for (int i = 0; i < kMaxLiveMusicClips; ++i) {
        musicGainDb_[static_cast<std::size_t>(i)].store(-18.0f, std::memory_order_relaxed);
        musicSmoothedGain_[static_cast<std::size_t>(i)] = static_cast<float>(std::pow(10.0, -18.0 / 20.0));
    }
}

void PreviewPlayer::setDrySource(const juce::AudioBuffer<float>* before, double sourceRate) {
    const juce::ScopedLock sl(lock_);
    before_ = before;
    sourceRate_ = sourceRate > 0.0 ? sourceRate : 48000.0;
    readPos_ = 0.0;
}

void PreviewPlayer::setDenoisedSource(const vc::AudioBuffer* denoised,
                                      const std::atomic<std::uint8_t>* validHops,
                                      int numHops, int hopSize) {
    const juce::ScopedLock sl(lock_);
    denoised_ = denoised;
    denoisedValidHops_ = validHops;
    denoisedNumHops_ = numHops;
    denoisedHopSize_ = hopSize > 0 ? hopSize : 480;
}

void PreviewPlayer::setMusicClips(const std::vector<MusicClip>& clips) {
    const juce::ScopedLock sl(lock_);
    musicClips_ = clips;
    const int count = juce::jmin(static_cast<int>(clips.size()), kMaxLiveMusicClips);
    for (int i = 0; i < count; ++i) {
        const float gainDb = static_cast<float>(clips[static_cast<std::size_t>(i)].gainDb);
        musicGainDb_[static_cast<std::size_t>(i)].store(gainDb, std::memory_order_relaxed);
        musicSmoothedGain_[static_cast<std::size_t>(i)] = static_cast<float>(std::pow(10.0, gainDb / 20.0f));
    }
}

void PreviewPlayer::setMusicClipGainDb(int index, double gainDb) {
    if (index < 0 || index >= kMaxLiveMusicClips)
        return;
    musicGainDb_[static_cast<std::size_t>(index)].store(
        static_cast<float>(juce::jlimit(-60.0, 6.0, gainDb)),
        std::memory_order_relaxed);
}

void PreviewPlayer::setNoiseReductionAmount(double amount) {
    noiseReductionAmount_.store(static_cast<float>(juce::jlimit(0.0, 1.0, amount)),
                                std::memory_order_relaxed);
}

void PreviewPlayer::clearSources() {
    stop();
    const juce::ScopedLock sl(lock_);
    before_ = nullptr;
    denoised_ = nullptr;
    musicClips_.clear();
    mutedMusicClipIndex_.store(-1, std::memory_order_relaxed);
    readPos_ = 0.0;
    rmsLin_ = 0.0;
    rmsLevelDb_.store(-90.0f, std::memory_order_relaxed);
}

void PreviewPlayer::start() {
    const juce::ScopedLock sl(lock_);
    if (before_ != nullptr && readPos_ >= before_->getNumSamples())
        readPos_ = 0.0; // restart if we were at the end
    playing_.store(true);
}

void PreviewPlayer::stop() {
    playing_.store(false);
    rmsLevelDb_.store(-90.0f, std::memory_order_relaxed);
}

double PreviewPlayer::getPositionNormalised() const {
    const juce::ScopedLock sl(lock_);
    if (before_ == nullptr || before_->getNumSamples() == 0) return 0.0;
    return juce::jlimit(0.0, 1.0, readPos_ / before_->getNumSamples());
}

void PreviewPlayer::setPositionSeconds(double seconds) {
    const juce::ScopedLock sl(lock_);
    if (before_ == nullptr || before_->getNumSamples() == 0) return;
    readPos_ = juce::jlimit(0.0, static_cast<double>(before_->getNumSamples() - 1),
                           seconds * sourceRate_);
}

void PreviewPlayer::prepareToPlay(int samplesPerBlock, double deviceSampleRate) {
    deviceRate_ = deviceSampleRate > 0.0 ? deviceSampleRate : 48000.0;
    blockSize_ = juce::jmax(32, samplesPerBlock);
    scratch_.setSize(2, blockSize_, false, false, true);
    // The chain runs at the device rate (post-resample), 2 channels.
    chain_.prepare(static_cast<int>(deviceRate_), 2);
}

void PreviewPlayer::mixMusicInto(juce::AudioBuffer<float>& dest, int startSample, int numSamples,
                                 double timelineStartSeconds) {
    const int outChannels = dest.getNumChannels();
    if (outChannels <= 0 || numSamples <= 0)
        return;

    const int mutedIndex = mutedMusicClipIndex_.load(std::memory_order_relaxed);
    for (int clipIndex = 0; clipIndex < static_cast<int>(musicClips_.size()); ++clipIndex) {
        if (clipIndex == mutedIndex)
            continue;

        const auto& clip = musicClips_[static_cast<std::size_t>(clipIndex)];
        if (clip.audio.getNumSamples() <= 1 || clip.sampleRate <= 0.0)
            continue;

        const double clipStart = clip.startSeconds;
        const double clipEnd = clipStart + clip.durationSeconds();
        const double blockStart = timelineStartSeconds;
        const double blockEnd = timelineStartSeconds + static_cast<double>(numSamples) / deviceRate_;
        if (clipEnd <= blockStart || clipStart >= blockEnd)
            continue;

        const bool hasLiveGain = clipIndex < kMaxLiveMusicClips;
        const float targetGain = hasLiveGain
            ? static_cast<float>(std::pow(
                  10.0, musicGainDb_[static_cast<std::size_t>(clipIndex)].load(std::memory_order_relaxed) / 20.0f))
            : static_cast<float>(std::pow(10.0, clip.gainDb / 20.0));
        float gain = hasLiveGain
            ? musicSmoothedGain_[static_cast<std::size_t>(clipIndex)]
            : targetGain;
        const float gainStep = static_cast<float>(1.0 - std::exp(-1.0 / (deviceRate_ * 0.015)));
        for (int i = 0; i < numSamples; ++i) {
            if (hasLiveGain)
                gain += (targetGain - gain) * gainStep;

            const double timeline = timelineStartSeconds + static_cast<double>(i) / deviceRate_;
            const double clipTime = timeline - clipStart;
            if (clipTime < 0.0 || clipTime >= clip.durationSeconds())
                continue;

            double fade = 1.0;
            if (clip.fadeInSeconds > 0.0)
                fade = std::min(fade, clipTime / clip.fadeInSeconds);
            if (clip.fadeOutSeconds > 0.0)
                fade = std::min(fade, (clip.durationSeconds() - clipTime) / clip.fadeOutSeconds);
            fade = juce::jlimit(0.0, 1.0, fade);

            const double srcPos = (clip.sourceOffsetSeconds + clipTime) * clip.sampleRate;
            const int i0 = static_cast<int>(juce::jlimit(0.0, static_cast<double>(clip.audio.getNumSamples() - 2), srcPos));
            const float frac = static_cast<float>(srcPos - i0);
            for (int ch = 0; ch < outChannels; ++ch) {
                const int sc = juce::jmin(ch, clip.audio.getNumChannels() - 1);
                const float* src = clip.audio.getReadPointer(sc);
                const float sample = src[i0] + frac * (src[i0 + 1] - src[i0]);
                dest.addSample(ch, startSample + i, sample * gain * static_cast<float>(fade));
            }
        }
        if (hasLiveGain)
            musicSmoothedGain_[static_cast<std::size_t>(clipIndex)] = gain;
    }
}

void PreviewPlayer::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    info.clearActiveBufferRegion();

    if (!playing_.load()) return;

    const juce::ScopedTryLock stl(lock_);
    if (!stl.isLocked()) return;

    const auto* src = before_;
    if (src == nullptr || src->getNumSamples() == 0) return;

    const int srcChannels = src->getNumChannels();
    const int srcLen = src->getNumSamples();
    const int outChannels = info.buffer->getNumChannels();
    const int n = info.numSamples;
    const double ratio = sourceRate_ / deviceRate_;

    // 1. Render the dry signal (resampled) into the output region.
    double pos = readPos_;
    const double timelineStartSeconds = readPos_ / sourceRate_;
    int produced = 0;
    for (int i = 0; i < n; ++i) {
        if (pos >= srcLen - 1) {
            playing_.store(false);
            pos = static_cast<double>(srcLen);
            break;
        }
        const int i0 = static_cast<int>(pos);
        const float frac = static_cast<float>(pos - i0);
        for (int ch = 0; ch < outChannels; ++ch) {
            const int sc = juce::jmin(ch, srcChannels - 1);
            const float* d = src->getReadPointer(sc);
            info.buffer->setSample(ch, info.startSample + i, d[i0] + frac * (d[i0 + 1] - d[i0]));
        }
        pos += ratio;
        ++produced;
    }
    readPos_ = pos;
    if (produced == 0) return;

    // 2. Build the noise-reduction blend -> scratch and process it through the
    //    live chain. The original button still hears the untouched dry buffer.
    //    The chain always runs so the meters stay live and warm.
    const int procCh = juce::jmin(outChannels, scratch_.getNumChannels());
    const auto* den = denoised_;
    const auto* valid = denoisedValidHops_;
    const int denLen = (den != nullptr) ? static_cast<int>(den->numFrames()) : 0;
    const float amount = noiseReductionAmount_.load(std::memory_order_relaxed);
    const bool blend = den != nullptr && denLen > 1 && valid != nullptr && amount > 0.0f;
    if (!blend) {
        for (int ch = 0; ch < procCh; ++ch)
            scratch_.copyFrom(ch, 0, *info.buffer, ch, info.startSample, produced);
    } else {
        const float wet = amount;
        const float dry = 1.0f - wet;
        const int denChannels = den->numChannels();
        const int hopSize = denoisedHopSize_;
        const int numHops = denoisedNumHops_;
        double denPos = readPos_ - produced * ratio;
        for (int i = 0; i < produced; ++i) {
            const int i0 = static_cast<int>(juce::jlimit(0.0, static_cast<double>(denLen - 2), denPos));
            const float frac = static_cast<float>(denPos - i0);
            // The denoised sample and its interpolation neighbour are only usable
            // once the worker has filled their hop(s); otherwise fall back to dry.
            const int h0 = i0 / hopSize;
            const int h1 = (i0 + 1) / hopSize;
            const bool ready = h1 < numHops
                && valid[h0].load(std::memory_order_acquire) != 0
                && valid[h1].load(std::memory_order_acquire) != 0;
            for (int ch = 0; ch < procCh; ++ch) {
                const float origSample = info.buffer->getSample(ch, info.startSample + i);
                if (!ready) {
                    scratch_.setSample(ch, i, origSample);
                    continue;
                }
                const int dc = juce::jmin(ch, denChannels - 1);
                const float* d = den->channels[static_cast<std::size_t>(dc)].data();
                const float denSample = d[i0] + frac * (d[i0 + 1] - d[i0]);
                scratch_.setSample(ch, i, dry * origSample + wet * denSample);
            }
            denPos += ratio;
        }
    }

    chain_.process(scratch_.getArrayOfWritePointers(), procCh, produced);

    // 3. If listening to "after", copy the wet result back to the output.
    if (showAfter_.load()) {
        for (int ch = 0; ch < procCh; ++ch)
            info.buffer->copyFrom(ch, info.startSample, scratch_, ch, 0, produced);
    }

    mixMusicInto(*info.buffer, info.startSample, produced, timelineStartSeconds);

    // 4. Capture the heard output (mono mix) for the live spectrum analyzer.
    int w = analysisWrite_.load(std::memory_order_relaxed);
    double sumSquares = 0.0;
    for (int i = 0; i < produced; ++i) {
        float mono = 0.0f;
        for (int ch = 0; ch < outChannels; ++ch)
            mono += info.buffer->getSample(ch, info.startSample + i);
        mono /= static_cast<float>(juce::jmax(1, outChannels));
        sumSquares += static_cast<double>(mono) * mono;
        analysisRing_[static_cast<std::size_t>(w & (kAnalysisSize - 1))] = mono;
        ++w;
    }
    analysisWrite_.store(w, std::memory_order_release);

    const double blockRms = std::sqrt(sumSquares / juce::jmax(1, produced));
    const double coeff = std::exp(-static_cast<double>(produced) / (deviceRate_ * 0.30));
    rmsLin_ = coeff * rmsLin_ + (1.0 - coeff) * blockRms;
    rmsLevelDb_.store(static_cast<float>(20.0 * std::log10(rmsLin_ + 1e-9)),
                      std::memory_order_relaxed);
}

void PreviewPlayer::readAnalysisBlock(float* dest, int n) const {
    n = juce::jmin(n, kAnalysisSize);
    const int w = analysisWrite_.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        dest[i] = analysisRing_[static_cast<std::size_t>((w - n + i) & (kAnalysisSize - 1))];
}
