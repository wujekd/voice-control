#include "DenoiseStreamer.h"

#include <algorithm>

namespace vc {

namespace {
constexpr int kHopFallback = 480;   // DeepFilterNet3 hop @ 48 kHz
constexpr float kAttenLimitDb = 100.0f; // ~unlimited attenuation
// Post-filter beta, matching the clean reference `deep-filter --pf` render.
// The C API runtime defaults are patched in the vendored DeepFilterNet submodule
// to match the same CLI thresholds and mask reduction.
constexpr float kPostFilterBeta = 0.02f;
} // namespace

DenoiseStreamer::~DenoiseStreamer() {
    stop();
}

void DenoiseStreamer::start(const AudioBuffer& source, const std::string& modelPath) {
    stop();

    source_ = &source;
    modelPath_ = modelPath;
    channels_ = std::max(1, source.numChannels());
    numFrames_ = static_cast<std::int64_t>(source.numFrames());
    hop_ = kHopFallback;
    numHops_ = numFrames_ > 0
        ? static_cast<int>((numFrames_ + hop_ - 1) / hop_)
        : 0;

    // Output buffer mirrors the source; the worker fills it hop-by-hop.
    denoised_.sampleRate = source.sampleRate;
    denoised_.channels.assign(static_cast<std::size_t>(channels_),
                              std::vector<float>(static_cast<std::size_t>(numFrames_), 0.0f));
    validHops_ = std::vector<std::atomic<std::uint8_t>>(static_cast<std::size_t>(numHops_));

    monoIn_.assign(static_cast<std::size_t>(hop_), 0.0f);
    monoOut_.assign(static_cast<std::size_t>(hop_), 0.0f);

    stop_.store(false);
    modelReady_.store(false);
    playheadFrame_.store(0);
    remaining_.store(numHops_);

    if (numHops_ == 0)
        return;

    active_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { workerLoop(); });
}

bool DenoiseStreamer::loadPrecomputed(const AudioBuffer& source, const AudioBuffer& denoised) {
    stop();

    channels_ = std::max(1, source.numChannels());
    numFrames_ = static_cast<std::int64_t>(source.numFrames());
    hop_ = kHopFallback;
    numHops_ = numFrames_ > 0
        ? static_cast<int>((numFrames_ + hop_ - 1) / hop_)
        : 0;

    // The cached buffer must match the freshly-decoded source, else bail so the
    // caller runs the model.
    if (denoised.numChannels() != channels_
        || static_cast<std::int64_t>(denoised.numFrames()) != numFrames_)
        return false;

    denoised_ = denoised;
    denoised_.sampleRate = source.sampleRate;
    validHops_ = std::vector<std::atomic<std::uint8_t>>(static_cast<std::size_t>(numHops_));
    for (auto& v : validHops_)
        v.store(1, std::memory_order_release);

    stop_.store(false);
    modelReady_.store(true, std::memory_order_release);
    playheadFrame_.store(0);
    remaining_.store(0);
    active_.store(false, std::memory_order_release);
    return true;
}

void DenoiseStreamer::stop() {
    if (worker_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(cvMutex_);
            stop_.store(true);
        }
        cv_.notify_all();
        worker_.join();
    }
    active_.store(false, std::memory_order_release);
    denoiser_.reset();
    source_ = nullptr;
}

void DenoiseStreamer::setPlayheadFrame(std::int64_t frame) {
    playheadFrame_.store(std::max<std::int64_t>(0, frame), std::memory_order_relaxed);
}

bool DenoiseStreamer::isComplete() const {
    return numHops_ == 0 || remaining_.load(std::memory_order_acquire) == 0;
}

void DenoiseStreamer::waitUntilComplete() {
    std::unique_lock<std::mutex> lock(cvMutex_);
    cv_.wait(lock, [this] {
        return remaining_.load(std::memory_order_acquire) == 0
            || !active_.load(std::memory_order_acquire);
    });
}

int DenoiseStreamer::nextHopToFill(int hint) const {
    const int start = std::clamp(hint, 0, numHops_ - 1);
    for (int g = start; g < numHops_; ++g)
        if (validHops_[static_cast<std::size_t>(g)].load(std::memory_order_acquire) == 0)
            return g;
    for (int g = 0; g < start; ++g)
        if (validHops_[static_cast<std::size_t>(g)].load(std::memory_order_acquire) == 0)
            return g;
    return -1;
}

void DenoiseStreamer::fillHop(int g, bool discard) {
    const int inHop = g + delayHops_;
    const std::int64_t inBase = static_cast<std::int64_t>(inHop) * hop_;

    // Downmix the source input hop to mono (zero-padded outside the buffer).
    for (int j = 0; j < hop_; ++j) {
        const std::int64_t f = inBase + j;
        float mono = 0.0f;
        if (f >= 0 && f < numFrames_) {
            for (int ch = 0; ch < channels_; ++ch)
                mono += source_->channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(f)];
            mono /= static_cast<float>(channels_);
        }
        monoIn_[static_cast<std::size_t>(j)] = mono;
    }

    denoiser_->processHop(monoIn_.data(), monoOut_.data());

    if (discard)
        return;

    // Write the (mono) denoised hop to every output channel, aligned to g.
    const std::int64_t outBase = static_cast<std::int64_t>(g) * hop_;
    for (int j = 0; j < hop_; ++j) {
        const std::int64_t f = outBase + j;
        if (f >= numFrames_)
            break;
        const float v = monoOut_[static_cast<std::size_t>(j)];
        for (int ch = 0; ch < channels_; ++ch)
            denoised_.channels[static_cast<std::size_t>(ch)][static_cast<std::size_t>(f)] = v;
    }
    validHops_[static_cast<std::size_t>(g)].store(1, std::memory_order_release);
}

void DenoiseStreamer::workerLoop() {
    denoiser_ = std::make_unique<Denoiser>(modelPath_, kAttenLimitDb, kPostFilterBeta);
    if (!denoiser_->valid() || denoiser_->hop() != hop_) {
        // Model unavailable (or unexpected hop): leave everything dry.
        active_.store(false, std::memory_order_release);
        cv_.notify_all();
        return;
    }
    modelReady_.store(true, std::memory_order_release);

    // -1 → the first hop (input hop 0) is "contiguous", reusing the freshly
    // created cold model (equivalent to a from-start pass, no extra reload).
    int lastInputHop = -1;

    while (!stop_.load(std::memory_order_acquire)) {
        const int hint = static_cast<int>(playheadFrame_.load(std::memory_order_relaxed) / hop_);
        const int g = nextHopToFill(hint);
        if (g < 0) {
            // Fully denoised: idle until stopped (no further work on seeks).
            std::unique_lock<std::mutex> lock(cvMutex_);
            cv_.wait(lock, [this] { return stop_.load(std::memory_order_acquire); });
            continue;
        }

        const int inHop = g + delayHops_;
        if (inHop != lastInputHop + 1) {
            // Discontinuity (start or seek): prime the model from the new region
            // with discarded warmup hops so the first real output isn't a stale-
            // state / cold-start transient. Cheaper than a model reload.
            for (int k = std::max(0, inHop - kWarmupHops); k < inHop; ++k)
                fillHop(k - delayHops_, /*discard=*/true);
        }

        fillHop(g, /*discard=*/false);
        lastInputHop = inHop;

        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) - 1 == 0)
            cv_.notify_all();
    }
}

} // namespace vc
