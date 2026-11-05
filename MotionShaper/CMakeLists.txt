cmake_minimum_required(VERSION 3.22)

# Built into the plugin, so a new PC needs no Visual C++ Redistributable. Must come before project().
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(MotionShaper VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MotionShaper
  COMPANY_NAME "Personal"
  PLUGIN_MANUFACTURER_CODE Pers
  PLUGIN_CODE Mtsh
  FORMATS VST3
  PRODUCT_NAME "MotionShaper"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(MotionShaper)

target_sources(MotionShaper PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_compile_definitions(MotionShaper PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MotionShaper
  PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

# Optional headless engine and window check, off by default: cmake -DMOTIONSHAPER_TESTS=ON ...
option(MOTIONSHAPER_TESTS "Build the MotionShaperTest console program" OFF)
if(MOTIONSHAPER_TESTS)
  juce_add_console_app(MotionShaperTest PRODUCT_NAME "MotionShaperTest")
  juce_generate_juce_header(MotionShaperTest)
  target_sources(MotionShaperTest PRIVATE
    Tests/ProcessorTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
  target_compile_definitions(MotionShaperTest PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(MotionShaperTest
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
endif()
