cmake_minimum_required(VERSION 3.22)

# Built into each plugin, so a new PC needs no Visual C++ Redistributable. Must come before project().
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(SpaceTrack VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

set(SHARED_SOURCES Source/SharedRegistry.cpp Source/SharedRegistry.h)

# Measured head responses for the 3D sound. See Assets/README.txt
juce_add_binary_data(SpaceTrackHrtf
  HEADER_NAME "HrtfData.h"
  NAMESPACE HrtfData
  SOURCES Assets/hrtf_kemar.bin)

# ---- Per-track plugin ----
juce_add_plugin(SpaceTrack
  COMPANY_NAME "Personal"
  PLUGIN_MANUFACTURER_CODE Pers
  PLUGIN_CODE Sptk
  FORMATS VST3
  PRODUCT_NAME "SpaceTrack"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(SpaceTrack)

target_sources(SpaceTrack PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  ${SHARED_SOURCES})

target_compile_definitions(SpaceTrack PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SpaceTrack
  PRIVATE SpaceTrackHrtf juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

# ---- Master control plugin ----
juce_add_plugin(SpaceMaster
  COMPANY_NAME "Personal"
  PLUGIN_MANUFACTURER_CODE Pers
  PLUGIN_CODE Spms
  FORMATS VST3
  PRODUCT_NAME "SpaceMaster"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(SpaceMaster)

target_sources(SpaceMaster PRIVATE
  Source/MasterProcessor.cpp
  Source/MasterEditor.cpp
  ${SHARED_SOURCES})

target_compile_definitions(SpaceMaster PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SpaceMaster
  PRIVATE juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

# Optional headless check of SpaceMaster, off by default: cmake -DSPACETRACK_TESTS=ON ...
option(SPACETRACK_TESTS "Build the SpaceMasterTest console program" OFF)
if(SPACETRACK_TESTS)
  juce_add_console_app(SpaceMasterTest PRODUCT_NAME "SpaceMasterTest")
  juce_generate_juce_header(SpaceMasterTest)
  target_sources(SpaceMasterTest PRIVATE
    Tests/MasterTest.cpp
    Source/MasterProcessor.cpp
    Source/MasterEditor.cpp
    ${SHARED_SOURCES})
  target_compile_definitions(SpaceMasterTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    "SPACETRACK_MAPPING_NAME=\"Local\\\\SpaceTrackRegistry_test\"")
  target_link_libraries(SpaceMasterTest
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags)

  juce_add_console_app(SpaceTrackDspTest PRODUCT_NAME "SpaceTrackDspTest")
  juce_generate_juce_header(SpaceTrackDspTest)
  target_sources(SpaceTrackDspTest PRIVATE
    Tests/DspTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${SHARED_SOURCES})
  target_compile_definitions(SpaceTrackDspTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    "SPACETRACK_MAPPING_NAME=\"Local\\\\SpaceTrackRegistry_dsptest\"")
  target_link_libraries(SpaceTrackDspTest
    PRIVATE SpaceTrackHrtf juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
endif()
