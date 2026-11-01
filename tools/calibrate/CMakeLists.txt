cmake_minimum_required(VERSION 3.22)
# JUCE 9 exige el lenguaje C (tiene fuentes C).
project(OpalineCalibrate LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ruta de JUCE portable: antes estaba fija en "C:/JUCE", que no existe en Mac
# ni en un runner de CI (el self-test no se podia compilar fuera de Windows).
if(NOT JUCE_PATH)
    if(DEFINED ENV{JUCE_PATH} AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
        set(JUCE_PATH "$ENV{JUCE_PATH}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE/CMakeLists.txt")
        set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE")
    elseif(EXISTS "C:/JUCE/CMakeLists.txt")
        set(JUCE_PATH "C:/JUCE")
    else()
        set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE")
    endif()
endif()
if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE no encontrado en '${JUCE_PATH}'. Pasa -DJUCE_PATH=<ruta a JUCE>.")
endif()
add_subdirectory("${JUCE_PATH}" ${CMAKE_BINARY_DIR}/JUCE)

add_executable(OpalineCalibrate main.cpp
    ../../Source/DSP/LearnEngine.cpp
)
target_include_directories(OpalineCalibrate PRIVATE ../../Source)
target_link_libraries(OpalineCalibrate PRIVATE juce::juce_dsp juce::juce_core)