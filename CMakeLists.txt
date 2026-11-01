# ==============================================================================
#  OPALINE — floodo's audio labs · serie free
#  Polisher vocal de un knob (Shine) que replica la cadena de brillo "camou
#  sedoso" de la voz pop (Dua Lipa): hipass → de-esser → comp óptico →
#  supresor de resonancia → multiband → exciter → air shelf → paralelo filtrado.
#  Botón MEASURE (Learn) calibra centros de la cadena a la voz concreta.
#  UI nativa JUCE pura (sin WebView2, sin Node). MOLDE de la serie free.
# ==============================================================================

cmake_minimum_required(VERSION 3.22)
# JUCE 9 exige que el lenguaje C este habilitado (el framework tiene fuentes C).
project(Opaline VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- macOS -------------------------------------------------------------------
# Universal (Apple Silicon + Intel) y deployment target fijo: sin esto el binario
# sale con la arquitectura de la maquina que compila. No tiene efecto en Windows.
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    endif()
endif()

# --- JUCE --------------------------------------------------------------------
# Ruta portable: la misma que usan los demas plugins de la familia. Antes estaba
# hardcodeada a "/Users/Shared/JUCE", lo que rompia el configure en Windows y en
# cualquier runner de CI. Se puede sobreescribir con -DJUCE_PATH=... o con la
# variable de entorno JUCE_PATH.
if(NOT JUCE_PATH)
    if(DEFINED ENV{JUCE_PATH} AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
        set(JUCE_PATH "$ENV{JUCE_PATH}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
        set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    elseif(EXISTS "C:/JUCE/CMakeLists.txt")
        set(JUCE_PATH "C:/JUCE")
    else()
        set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    endif()
endif()
if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE no encontrado en '${JUCE_PATH}'. Pasa -DJUCE_PATH=<ruta a JUCE>.")
endif()
add_subdirectory("${JUCE_PATH}" ${CMAKE_BINARY_DIR}/JUCE)

# --- formatos -----------------------------------------------------------------
# AU solo se construye en Apple: JUCE linkea los frameworks AudioUnit/CoreAudioKit,
# que no existen en Windows (error de link). En Windows queda VST3 + Standalone.
set(OPALINE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND OPALINE_FORMATS AU)
endif()

# Los knobs son sliders accesibles (flechas/Home/End + Narrator, ver
# Source/UI/Knob.cpp). En Apple el host AU/AUval entrega las teclas al editor
# solo si este los pide: por eso se activa SOLO en Apple. En Windows queda como
# estaba (FALSE) para no cambiar el comportamiento del plugin.
set(OPALINE_EDITOR_WANTS_KEYBOARD_FOCUS FALSE)
if(APPLE)
    set(OPALINE_EDITOR_WANTS_KEYBOARD_FOCUS TRUE)
endif()

juce_add_plugin(Opaline
    PRODUCT_NAME "Opaline"
    COMPANY_NAME "Floodo's Audio Labs"
    BUNDLE_ID "com.floodos.opaline"
    PLUGIN_MANUFACTURER_CODE FloA
    PLUGIN_CODE Opln
    FORMATS ${OPALINE_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS ${OPALINE_EDITOR_WANTS_KEYBOARD_FOCUS}
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
        assets/fal_mark.svg
)

target_sources(Opaline
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/InstallPing.cpp
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

# ==============================================================================
#  snapshot — harness headless para el workflow juce-ui-fidelity.
#  Opt-in (el build por defecto no se toca):
#    cmake -B Build_Snapshot -G Ninja -DENABLE_SNAPSHOT_TESTS=ON
#  El target renderiza el editor real a PNG sin host ni ventana.
# ==============================================================================
option(ENABLE_SNAPSHOT_TESTS "Build the component snapshot harness" OFF)

if(ENABLE_SNAPSHOT_TESTS)
    add_executable(snapshot
        tests/snapshots/main.cpp
        tests/snapshots/stories/Opaline_stories.cpp
    )

    set_target_properties(snapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)

    target_compile_definitions(snapshot PRIVATE
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_SNAPSHOT_HARNESS=1
        JUCE_DISABLE_COREGRAPHICS_FONT_SMOOTHING=1
        # El harness no es un target de juce_add_plugin, asi que no recibe los
        # JucePlugin_* que define JUCE para los plugins. Source/InstallPing.cpp
        # usa JucePlugin_VersionString: se define aqui con la version del proyecto.
        JucePlugin_VersionString="${PROJECT_VERSION}"
    )

    target_sources(snapshot PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/InstallPing.cpp
        Source/UI/BrandFonts.cpp
        Source/UI/Knob.cpp
        Source/UI/LedMeter.cpp
        Source/DSP/VoiceChain.cpp
        Source/DSP/LearnEngine.cpp
        Source/DSP/GainMatcher.cpp
    )

    target_include_directories(snapshot PRIVATE Source)

    target_link_libraries(snapshot PRIVATE
        OpalineData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
endif()
