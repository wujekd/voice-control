cmake_minimum_required(VERSION 3.22)

project(gui_plugin_experiment VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("../third_party/JUCE" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_plugin(GuiPluginExperiment
    COMPANY_NAME "VoiceControl"
    PRODUCT_NAME "GUI Plugin Experiment"
    PLUGIN_MANUFACTURER_CODE Vcpl
    PLUGIN_CODE Gpex
    FORMATS Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(GuiPluginExperiment PRIVATE
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp)

target_compile_definitions(GuiPluginExperiment PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(GuiPluginExperiment PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
