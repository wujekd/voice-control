cmake_minimum_required(VERSION 3.16)
# C / Objective-C(++) are needed by JUCE on macOS (.c and .mm sources).
project(voice_control VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Portable, JUCE-free audio engine. Reusable from CLI, JUCE, or a backend.
add_library(vc_core STATIC
    core/Biquad.cpp
    core/Compressor.cpp
    core/DeEsser.cpp
    core/Ducker.cpp
    core/Eq.cpp
    core/EqSection.cpp
    core/Limiter.cpp
    core/LiveLimiter.cpp
    core/LiveVoiceChain.cpp
    core/LoudnessNormalizer.cpp
    core/SpectrumAnalyzer.cpp
    core/VoiceChain.cpp
    core/WavIo.cpp
)
target_include_directories(vc_core PUBLIC core)

# Video layer: thin wrapper over the ffmpeg CLI.
add_library(vc_video STATIC
    video/FFmpeg.cpp
)
target_include_directories(vc_video PUBLIC video)

# ---- Native DeepFilterNet inference (Rust crate, built via cargo-c) ----
# The deep_filter crate's `capi` feature emits a C static lib (libdeepfilter.a)
# plus a generated header. We build it once with `cargo cbuild` and link it into
# the real-time denoiser (vc_denoise). Requires a Rust toolchain + cargo-c.
# NOTE: a clean checkout may need `cargo update -p time` once in the submodule
# (old `time` 0.3.28 doesn't compile on recent rustc); see README.
find_program(CARGO_EXECUTABLE cargo REQUIRED
    HINTS /opt/homebrew/bin /usr/local/bin $ENV{HOME}/.cargo/bin)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(DF_RUST_TARGET "aarch64-apple-darwin")
else()
    set(DF_RUST_TARGET "x86_64-apple-darwin")
endif()

set(DF_DIR "${CMAKE_SOURCE_DIR}/third_party/DeepFilterNet")
set(DF_MANIFEST "${DF_DIR}/libDF/Cargo.toml")
set(DF_OUT "${DF_DIR}/target/${DF_RUST_TARGET}/release")
set(DF_STATIC "${DF_OUT}/libdeepfilter.a")
set(DF_INCLUDE "${DF_OUT}/include/deep_filter")

add_custom_command(
    OUTPUT ${DF_STATIC}
    COMMAND ${CARGO_EXECUTABLE} cbuild --release --no-default-features
            --features capi --manifest-path ${DF_MANIFEST}
    WORKING_DIRECTORY ${DF_DIR}
    COMMENT "Building native DeepFilterNet (libdeepfilter.a)"
    VERBATIM)
add_custom_target(deepfilter_build DEPENDS ${DF_STATIC})

# Real-time denoiser: thin C++ wrapper + progressive streaming worker.
add_library(vc_denoise STATIC
    core/Denoiser.cpp
    core/DenoiseStreamer.cpp
)
add_dependencies(vc_denoise deepfilter_build)
target_include_directories(vc_denoise PUBLIC core PRIVATE ${DF_INCLUDE})
target_link_libraries(vc_denoise PUBLIC vc_core PRIVATE ${DF_STATIC} "-liconv")

# CLI driver — headless front-end over the same engine (handy for batch/tests).
add_executable(voice-control app/main.cpp)
target_link_libraries(voice-control PRIVATE vc_core vc_video)

add_executable(vc-dsp-tests tests/DspBehaviorTests.cpp)
target_link_libraries(vc-dsp-tests PRIVATE vc_core)

# Dev-only: measure the native denoiser's net delay (sets DenoiseStreamer::delayHops_).
add_executable(denoise-calib tests/DenoiseCalib.cpp)
target_link_libraries(denoise-calib PRIVATE vc_denoise)

# JUCE desktop app (drag/drop, A/B preview, export).
option(VC_BUILD_APP "Build the JUCE desktop app" ON)
if(VC_BUILD_APP)
    add_subdirectory(third_party/JUCE)

    juce_add_gui_app(voice-control-app
        VERSION 0.1.0
        PRODUCT_NAME "Voice Control"
        COMPANY_NAME "VoiceControl")

    target_sources(voice-control-app PRIVATE
        desktop-juce/Main.cpp
        desktop-juce/MainComponent.cpp
        desktop-juce/ExportComponent.cpp
        desktop-juce/FileDropComponent.cpp
        desktop-juce/MusicTimeline.cpp
        desktop-juce/PreviewPlayer.cpp
        desktop-juce/ProcessingEngine.cpp
        desktop-juce/SpectrumView.cpp)

    target_compile_definitions(voice-control-app PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="Voice Control"
        JUCE_APPLICATION_VERSION_STRING="0.1.0")

    target_link_libraries(voice-control-app PRIVATE
        vc_core
        vc_video
        vc_denoise
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    find_program(FFMPEG_EXECUTABLE ffmpeg
        HINTS /opt/homebrew/bin /usr/local/bin)

    if(FFMPEG_EXECUTABLE)
        add_custom_command(TARGET voice-control-app POST_BUILD
            COMMAND /bin/bash
                "${CMAKE_SOURCE_DIR}/scripts/bundle_ffmpeg_macos.sh"
                "${FFMPEG_EXECUTABLE}"
                "$<TARGET_FILE_DIR:voice-control-app>/../Resources"
            COMMENT "Bundling FFmpeg and dependent libraries into Voice Control.app")
    else()
        message(WARNING "ffmpeg was not found; Voice Control.app will rely on ffmpeg from PATH.")
    endif()

    set(DEEPFILTER_MODEL
        "${CMAKE_SOURCE_DIR}/third_party/DeepFilterNet/models/DeepFilterNet3_onnx.tar.gz")

    if(EXISTS "${DEEPFILTER_MODEL}")
        add_custom_command(TARGET voice-control-app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:voice-control-app>/../Resources/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DEEPFILTER_MODEL}"
                "$<TARGET_FILE_DIR:voice-control-app>/../Resources/models/DeepFilterNet3_onnx.tar.gz"
            COMMENT "Bundling DeepFilterNet model into Voice Control.app")
    else()
        message(WARNING "DeepFilterNet model was not found; denoising will fall back to source-tree lookup.")
    endif()
endif()
