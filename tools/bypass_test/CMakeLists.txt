cmake_minimum_required(VERSION 3.22)
project(OpalineBypassTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("C:/JUCE" ${CMAKE_BINARY_DIR}/JUCE)

add_executable(OpalineBypassTest main.cpp
    ../../Source/DSP/VoiceChain.cpp
    ../../Source/DSP/GainMatcher.cpp
)
target_include_directories(OpalineBypassTest PRIVATE ../../Source)
target_link_libraries(OpalineBypassTest PRIVATE juce::juce_dsp juce::juce_core juce::juce_graphics)