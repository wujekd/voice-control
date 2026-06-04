#include "Denoiser.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// DeepFilterNet C API (libdeepfilter.a). Declared here rather than via the
// generated header so the wrapper isn't coupled to cargo's per-target output
// layout. Signatures must match third_party/DeepFilterNet/libDF/src/capi.rs.
extern "C" {
::DFState* df_create(const char* path, float atten_lim);
std::uintptr_t df_get_frame_length(::DFState* st);
void df_set_atten_lim(::DFState* st, float lim_db);
void df_set_post_filter_beta(::DFState* st, float beta);
float df_process_frame(::DFState* st, float* input, float* output);
void df_free(::DFState* model);
}

namespace vc {
namespace {

constexpr const char* kModelFileName = "DeepFilterNet3_onnx.tar.gz";

std::filesystem::path bundledModelPath() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);

    std::string executable(size, '\0');
    if (_NSGetExecutablePath(executable.data(), &size) == 0) {
        const std::filesystem::path executablePath(executable.c_str());
        return executablePath.parent_path().parent_path() / "Resources" / "models" / kModelFileName;
    }
#endif

    return {};
}

} // namespace

Denoiser::Denoiser(std::string modelPath, float attenLimDb, float postFilterBeta)
    : modelPath_(std::move(modelPath)), attenLimDb_(attenLimDb), postFilterBeta_(postFilterBeta) {
    create();
}

Denoiser::~Denoiser() {
    destroy();
}

void Denoiser::create() {
    if (modelPath_.empty())
        return;
    state_ = df_create(modelPath_.c_str(), attenLimDb_);
    if (state_ != nullptr) {
        if (postFilterBeta_ > 0.0f)
            df_set_post_filter_beta(state_, postFilterBeta_); // suppress musical noise (--pf)
        hop_ = static_cast<int>(df_get_frame_length(state_));
        scratch_.assign(static_cast<std::size_t>(hop_), 0.0f);
    }
}

void Denoiser::destroy() {
    if (state_ != nullptr) {
        df_free(state_);
        state_ = nullptr;
    }
}

void Denoiser::reset() {
    destroy();
    create();
}

float Denoiser::processHop(const float* in, float* out) {
    if (state_ == nullptr)
        return 0.0f;
    // df_process_frame takes a non-const input; copy into scratch to be safe.
    std::copy(in, in + hop_, scratch_.begin());
    return df_process_frame(state_, scratch_.data(), out);
}

std::string Denoiser::findDefaultModel() {
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path bundled = bundledModelPath();
    if (!bundled.empty() && fs::exists(bundled, ec))
        return bundled.string();

    const fs::path rel =
        fs::path("third_party") / "DeepFilterNet" / "models" / kModelFileName;
    for (fs::path dir = fs::current_path(ec); !dir.empty(); dir = dir.parent_path()) {
        const fs::path candidate = dir / rel;
        if (fs::exists(candidate, ec))
            return candidate.string();
        if (dir == dir.root_path())
            break;
    }
    return {};
}

} // namespace vc
