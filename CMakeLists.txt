# ==============================================================================
#  OPALINE — floodo's audio labs · serie free
#  Polisher vocal de un knob (Shine) que replica la cadena de brillo "camou
#  sedoso" de la voz pop (Dua Lipa): hipass → de-esser → comp óptico →
#  supresor de resonancia → multiband → exciter → air shelf → paralelo filtrado.
#  Botón MEASURE (Learn) calibra centros de la cadena a la voz concreta.
#  UI nativa JUCE pura (sin WebView2, sin Node). MOLDE de la serie free.
# ==============================================================================

cmake_minimum_required(VERSION 3.22)
project(Opaline VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("C:/JUCE" ${CMAKE_BINARY_DIR}/JUCE)

juce_add_plugin(Opaline
    PRODUCT_NAME "Opaline"
    COMPANY_NAME "Floodo's Audio Labs"
    BUNDLE_ID "com.floodos.opaline"
    PLUGIN_MANUFACTURER_CODE FloA
    PLUGIN_CODE Opln
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx
)

juce_add_binary_data(OpalineData
    SOURCES
        assets/fonts/Inter400.ttf
        assets/fonts/Inter500.ttf
        assets/fonts/Inter600.ttf
        assets/fonts/Inter700.ttf
        assets/fonts/Barlow500.ttf
        assets/fonts/Barlow600.ttf
        assets/fonts/Barlow700.ttf
        assets/fonts/Mono400.ttf
        assets/fonts/Mono500.ttf
        assets/fonts/Mono600.ttf
)

target_sources(Opaline
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UI/BrandFonts.cpp
        Source/UI/Knob.cpp
        Source/UI/LedMeter.cpp
        Source/DSP/VoiceChain.cpp
        Source/DSP/LearnEngine.cpp
        Source/DSP/GainMatcher.cpp
)

target_include_directories(Opaline PRIVATE Source)

target_compile_definitions(Opaline
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Opaline
    PRIVATE
        OpalineData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
