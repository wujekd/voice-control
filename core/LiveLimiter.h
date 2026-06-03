#pragma once

#include <atomic>
#include <vector>

namespace vc {

// Causal look-ahead brick-wall limiter for real-time use.
//
// The output is delayed by the look-ahead length L. For each output sample we
// take the *maximum* linked peak over the look-ahead window (computed in O(1)
// with a monotonic deque) and set the gain to keep that peak under the ceiling.
// Because the window always contains the sample being output, the ceiling is
// mathematically guaranteed. The gain drops instantly (on the delayed, quieter
// signal — so it pre-ducks) and releases on a linear slew. Allocation-free in
// process(); adds `lookaheadMs` of latency. The offline Limiter is still used
// for exact export.
class LiveLimiter {
public:
    void prepare(int sampleRate, int numChannels, double lookaheadMs);
    void configure(int sampleRate, double ceilingDb, double releaseMs);
    void reset();

    void process(float* const* channels, int numChannels, int numFrames);

    int latencySamples() const { return lookahead_; }
    float currentReductionDb() const { return meterReductionDb_.load(std::memory_order_relaxed); }

private:
    std::vector<std::vector<float>> delay_; // per-channel look-ahead ring
    int lookahead_ = 1;
    std::size_t pos_ = 0;
    long sampleIndex_ = 0;

    // Monotonic deque (decreasing) over the look-ahead window of linked peaks.
    std::vector<float> dqVal_;
    std::vector<long> dqIdx_;
    int dqCap_ = 1;
    int dqHead_ = 0;
    int dqSize_ = 0;

    double ceilingLin_ = 0.89125; // -1 dBFS
    double releaseStep_ = 0.001;  // max linear gain rise per sample

    double gain_ = 1.0;
    std::atomic<float> meterReductionDb_ { 0.0f };
};

} // namespace vc
