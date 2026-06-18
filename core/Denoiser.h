#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Forward-declared opaque handle from the DeepFilterNet C API (libdeepfilter).
extern "C" {
struct DFState;
}

namespace vc {

// Thin RAII wrapper over the native DeepFilterNet C API. Operates strictly at
// 48 kHz mono in fixed hops of hop() samples; resampling / channel handling is
// the caller's job (see DenoiseStreamer). Not thread-safe: a single Denoiser is
// driven by exactly one worker thread.
class Denoiser {
public:
    // Loads the model tar.gz at modelPath. attenLimDb caps how much the model is
    // allowed to attenuate (>=100 ≈ unlimited). postFilterBeta>0 enables the
    // optional post-filter (the `--pf` CLI option). The generic wrapper leaves it
    // off unless requested; app callers pass the CLI-matching beta explicitly.
    // Sets valid()==false on failure rather than throwing, so the caller can fall
    // back to the dry signal.
    explicit Denoiser(std::string modelPath, float attenLimDb = 100.0f,
                      float postFilterBeta = 0.0f);
    ~Denoiser();

    Denoiser(const Denoiser&) = delete;
    Denoiser& operator=(const Denoiser&) = delete;

    bool valid() const { return state_ != nullptr; }
    int hop() const { return hop_; } // frame length in samples (480 @ 48 kHz)

    // Denoise exactly hop() mono samples. `in` and `out` are each hop() long and
    // may be the same buffer. Returns the frame's local SNR (unused by callers).
    float processHop(const float* in, float* out);

    // Restart model state for a discontinuity (e.g. after a seek to a
    // non-contiguous position). Recreates the runtime so normalization stats
    // don't bleed across the jump.
    void reset();

    // Search upward from the current working directory for the bundled
    // DeepFilterNet3 model. Returns "" if not found.
    static std::string findDefaultModel();

private:
    void create();
    void destroy();

    ::DFState* state_ = nullptr;
    std::string modelPath_;
    float attenLimDb_;
    float postFilterBeta_;
    int hop_ = 480;
    std::vector<float> scratch_; // hop()-length, mutable input copy for the C API
};

} // namespace vc
