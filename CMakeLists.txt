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

# CLI driver — headless front-end over the same engine (handy for batch/tests).
add_executable(voice-control app/main.cpp)
target_link_libraries(voice-control PRIVATE vc_core vc_video)

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
        desktop-juce/FileDropComponent.cpp
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
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
