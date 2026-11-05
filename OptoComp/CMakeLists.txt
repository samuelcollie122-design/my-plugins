cmake_minimum_required(VERSION 3.22)

# Built into the plugin, so a new PC needs no Visual C++ Redistributable. Must come before project().
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(OptoComp VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(OptoComp
  COMPANY_NAME "Personal"
  PLUGIN_MANUFACTURER_CODE Pers
  PLUGIN_CODE Optc
  FORMATS VST3
  PRODUCT_NAME "OptoComp"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(OptoComp)

target_sources(OptoComp PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_compile_definitions(OptoComp PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(OptoComp
  PRIVATE juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

# Optional headless sound and window check, off by default: cmake -DOPTOCOMP_TESTS=ON ...
option(OPTOCOMP_TESTS "Build the OptoCompTest console program" OFF)
if(OPTOCOMP_TESTS)
  juce_add_console_app(OptoCompTest PRODUCT_NAME "OptoCompTest")
  juce_generate_juce_header(OptoCompTest)
  target_sources(OptoCompTest PRIVATE
    Tests/OptoTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
  target_compile_definitions(OptoCompTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(OptoCompTest
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags)
endif()
