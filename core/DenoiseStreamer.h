#pragma once

#include "AudioBuffer.h"
#include "Denoiser.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vc {

// Progressively denoises a 48 kHz source buffer on a background thread, filling
// an output buffer hop-by-hop and marking each hop valid as it completes. The
// worker prioritizes whichever region the playhead is in (set via
// setPlayheadFrame), so audio you're about to hear is denoised first; it then
// backfills the rest. Faster than real time, so playback hears denoise engage
// within a moment of pressing play.
//
// Lifetime: the source buffer passed to start() must outlive the streamer; call
// stop() before destroying or freeing the source. JUCE-free (lives in vc_core).
class DenoiseStreamer {
public:
    DenoiseStreamer() = default;
    ~DenoiseStreamer();

    DenoiseStreamer(const DenoiseStreamer&) = delete;
    DenoiseStreamer& operator=(const DenoiseStreamer&) = delete;

    // Allocate the output buffer to match `source` and launch the worker.
    // `source` is referenced, not copied. modelPath is the DeepFilterNet tar.gz.
    void start(const AudioBuffer& source, const std::string& modelPath);

    // Adopt an already-denoised buffer (e.g. restored from the on-disk cache)
    // instead of running the model: copies it in, marks every hop valid, and
    // reports complete immediately. Returns false (and does nothing) if the
    // buffer's geometry doesn't match `source`, so the caller can fall back to
    // start(). Neither buffer is referenced after the call.
    bool loadPrecomputed(const AudioBuffer& source, const AudioBuffer& denoised);

    void stop();

    bool active() const { return active_.load(std::memory_order_acquire); }
    bool modelReady() const { return modelReady_.load(std::memory_order_acquire); }

    int hopSize() const { return hop_; }
    int numHops() const { return numHops_; }

    // Per-hop validity (length numHops()). A set flag, read with acquire,
    // guarantees every denoised sample of that hop is visible.
    const std::atomic<std::uint8_t>* validHops() const { return validHops_.data(); }
    const AudioBuffer& denoised() const { return denoised_; }

    // Hint the worker toward the currently-playing source frame.
    void setPlayheadFrame(std::int64_t frame);

    bool isComplete() const;
    void waitUntilComplete(); // blocks; for the offline export pass

private:
    void workerLoop();
    int nextHopToFill(int hint) const; // first invalid hop at/after hint, else wrapped
    void fillHop(int g, bool discard); // feed input hop g+delayHops_, optionally discard output

    const AudioBuffer* source_ = nullptr;
    std::string modelPath_;
    AudioBuffer denoised_;
    std::vector<std::atomic<std::uint8_t>> validHops_;

    int hop_ = 480;
    int numHops_ = 0;
    std::int64_t numFrames_ = 0;
    int channels_ = 0;

    // Hop-granular compensation for the model's algorithmic lookahead: denoised
    // hop g is produced by feeding input hop (g + delayHops_), keeping denoised
    // aligned with the dry signal so partial-amount blends don't comb-filter.
    // Measured at exactly 3 hops (1440 samples / 30 ms) for DeepFilterNet3 — see
    // tests/DenoiseCalib.cpp.
    int delayHops_ = 3;
    // Discarded hops fed before the first real output after a jump: flush the
    // model's lookahead buffers (a few hops) and let normalization re-adapt. We
    // don't reload the model on seeks (the C API's only reset is a full reload).
    static constexpr int kWarmupHops = 20;

    std::thread worker_;
    std::atomic<bool> active_ { false };
    std::atomic<bool> stop_ { false };
    std::atomic<bool> modelReady_ { false };
    std::atomic<std::int64_t> playheadFrame_ { 0 };
    std::atomic<int> remaining_ { 0 };

    std::mutex cvMutex_;
    std::condition_variable cv_;

    std::unique_ptr<Denoiser> denoiser_;
    std::vector<float> monoIn_, monoOut_; // worker scratch, hop_ long
};

} // namespace vc
