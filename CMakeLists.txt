cmake_minimum_required(VERSION 3.22)

# macOS: Universal + min 10.13 (must be set before project())
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()

project(CHROMA5YNTH VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(CHROMA_BUILD_TESTS "Build Catch2 tests" ON)

# ---------------------------------------------------------------- Plugin identity
# Single source of truth: CLAUDE.md "Identità plugin".
set(CHROMA_COMPANY_NAME     "TMFU audiovibe")
set(CHROMA_PRODUCT_NAME     "CHROMA5YNTH")
set(CHROMA_MANUFACTURER_CODE "Tmfu")
set(CHROMA_PLUGIN_CODE      "Chr5")
set(CHROMA_BUNDLE_ID        "com.tmfuaudiovibe.chroma5ynth")

# ---------------------------------------------------------------- Dependencies
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------- Generated params
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} -c "import yaml" RESULT_VARIABLE CHROMA_YAML_RESULT OUTPUT_QUIET ERROR_QUIET)
if(NOT CHROMA_YAML_RESULT EQUAL 0)
    message(FATAL_ERROR "PyYAML not found for ${Python3_EXECUTABLE}: run '${Python3_EXECUTABLE} -m pip install pyyaml'")
endif()
file(GLOB CHROMA_PARAM_YAMLS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/params/*.yaml)
set(CHROMA_GEN_DIR ${CMAKE_BINARY_DIR}/generated)
set(CHROMA_GEN_FILES
    ${CHROMA_GEN_DIR}/ParamIDs.h
    ${CHROMA_GEN_DIR}/ParamLayout.cpp
    ${CHROMA_GEN_DIR}/GuiLayout.h
    ${CHROMA_GEN_DIR}/LayerParams.h)
add_custom_command(
    OUTPUT ${CHROMA_GEN_FILES}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gen_params.py
            --params ${CMAKE_SOURCE_DIR}/params
            --out ${CHROMA_GEN_DIR}
            --docs ${CMAKE_SOURCE_DIR}/docs/manual/parameters.md
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_params.py ${CHROMA_PARAM_YAMLS}
    COMMENT "Generating parameter code from params/*.yaml")
add_custom_target(chroma_gen_params DEPENDS ${CHROMA_GEN_FILES})

# ---------------------------------------------------------------- GUI assets (Inter, SIL OFL 1.1: assets/fonts/OFL.txt)
juce_add_binary_data(chroma_assets
    HEADER_NAME BinaryData.h
    NAMESPACE   ChromaAssets
    SOURCES     assets/fonts/Inter-Regular.ttf
                assets/fonts/Inter-SemiBold.ttf)
set_target_properties(chroma_assets PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- Shared sources (plugin + tests)
# JUCE modules are compiled per-target, so sources are added to each target
# instead of a static library (avoids duplicate JUCE symbols).
set(CHROMA_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/gui/StyleLookAndFeel.cpp
    src/gui/WavetableEditor.cpp
    src/gui/PresetBrowser.cpp
    src/core/Synth.cpp
    src/core/Randomizer.cpp
    src/preset/PresetManager.cpp
    src/shared/Voice.cpp
    src/shared/Filter.cpp
    src/shared/Tuning.cpp
    src/engines/sine/SineEngine.cpp
    src/engines/analog/AnalogEngine.cpp
    src/engines/fm/FmEngine.cpp
    src/engines/additive/AdditiveSpectrum.cpp
    src/engines/additive/AdditiveEngine.cpp
    src/engines/fm/FmSysex.cpp
    src/engines/wavetable/Wavetable.cpp
    src/engines/wavetable/WavetableFactory.cpp
    src/engines/wavetable/WavetableEngine.cpp
    src/engines/wavetable/WavetableDoc.cpp
    src/engines/pd/PdEngine.cpp
    ${CHROMA_GEN_DIR}/ParamLayout.cpp)

function(chroma_configure_target tgt)
    target_sources(${tgt} PRIVATE ${CHROMA_SOURCES})
    add_dependencies(${tgt} chroma_gen_params)
    target_include_directories(${tgt} PRIVATE src ${CHROMA_GEN_DIR})
    target_compile_definitions(${tgt} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        CHROMA_COMPANY_NAME="${CHROMA_COMPANY_NAME}"
        CHROMA_PRODUCT_NAME="${CHROMA_PRODUCT_NAME}"
        CHROMA_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(${tgt} PRIVATE
        chroma_assets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endfunction()

# ---------------------------------------------------------------- Plugin
set(CHROMA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND CHROMA_FORMATS AU)
endif()

juce_add_plugin(CHROMA5YNTH
    PRODUCT_NAME                "${CHROMA_PRODUCT_NAME}"
    COMPANY_NAME                "${CHROMA_COMPANY_NAME}"
    BUNDLE_ID                   "${CHROMA_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE    ${CHROMA_MANUFACTURER_CODE}
    PLUGIN_CODE                 ${CHROMA_PLUGIN_CODE}
    FORMATS                     ${CHROMA_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    DESCRIPTION                 "Multi-engine layered synthesizer"
    HARDENED_RUNTIME_ENABLED    TRUE)

chroma_configure_target(CHROMA5YNTH)

# ---------------------------------------------------------------- Tests
if(CHROMA_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(Catch2)

    juce_add_console_app(chroma_tests PRODUCT_NAME "chroma_tests")
    target_sources(chroma_tests PRIVATE tests/test_params.cpp tests/test_processor.cpp tests/test_core.cpp tests/test_limiter.cpp tests/test_filter.cpp tests/test_analog.cpp tests/test_fx.cpp tests/test_fm.cpp tests/test_wavetable.cpp tests/test_additive.cpp tests/test_pd.cpp tests/test_global.cpp)
    chroma_configure_target(chroma_tests)
    target_link_libraries(chroma_tests PRIVATE Catch2::Catch2WithMain)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(chroma_tests)
endif()
