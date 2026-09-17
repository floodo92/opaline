cmake_minimum_required(VERSION 3.22)
project(OpalineCalibrate LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("C:/JUCE" ${CMAKE_BINARY_DIR}/JUCE)

add_executable(OpalineCalibrate main.cpp
    ../../Source/DSP/LearnEngine.cpp
)
target_include_directories(OpalineCalibrate PRIVATE ../../Source)
target_link_libraries(OpalineCalibrate PRIVATE juce::juce_dsp juce::juce_core)