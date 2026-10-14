cmake_minimum_required(VERSION 3.22)
project(XDEditor VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    # sources contain Polish UI strings - make MSVC read them as UTF-8
    add_compile_options(/utf-8)
endif()

# JUCE: either a sibling checkout (JUCE_DIR) or fetched automatically
set(JUCE_DIR "" CACHE PATH "Path to a JUCE checkout (optional)")
if(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(XD_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND XD_FORMATS AU)
endif()

set(CORE_SOURCES
    Source/core/Params.cpp
    Source/core/Program.cpp
    Source/core/SysEx.cpp
    Source/core/Bank.cpp
    Source/core/Reports.cpp)

option(XD_BUILD_PLUGIN "Build the plugin" ON)
if(XD_BUILD_PLUGIN)
juce_add_plugin(XDEditor
    COMPANY_NAME "Open XD Tools"
    COMPANY_WEBSITE "https://github.com/"
    BUNDLE_ID "org.openxdtools.xdeditor"
    PLUGIN_MANUFACTURER_CODE Oxdt
    PLUGIN_CODE Xded
    FORMATS ${XD_FORMATS}
    PRODUCT_NAME "XD Editor"
    DESCRIPTION "Editor and librarian for the Korg minilogue xd"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth External
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(XDEditor PRIVATE
    ${CORE_SOURCES}
    Source/Settings.cpp
    Source/MidiEngine.cpp
    Source/Librarian.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/gui/Look.cpp
    Source/gui/Widgets.cpp
    Source/gui/MainTab.cpp
    Source/gui/ProgramTab.cpp
    Source/gui/LibrarianTab.cpp
    Source/gui/SequencerTab.cpp
    Source/gui/SettingsTab.cpp)

target_include_directories(XDEditor PRIVATE Source)

target_compile_definitions(XDEditor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)

target_link_libraries(XDEditor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------- unit tests
option(XD_BUILD_TESTS "Build core unit tests" ON)
if(XD_BUILD_TESTS)
    juce_add_console_app(XDCoreTests PRODUCT_NAME "XDCoreTests")
    target_sources(XDCoreTests PRIVATE ${CORE_SOURCES} Tests/CoreTests.cpp)
    target_include_directories(XDCoreTests PRIVATE Source)
    target_compile_definitions(XDCoreTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(XDCoreTests PRIVATE juce::juce_core juce::juce_audio_basics juce::juce_data_structures
                          PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

if(XD_BUILD_TESTS AND XD_BUILD_PLUGIN)
    juce_add_console_app(XDProcessorTests PRODUCT_NAME "XDProcessorTests")
    get_target_property(XD_PLUGIN_SOURCES XDEditor SOURCES)
    target_sources(XDProcessorTests PRIVATE ${XD_PLUGIN_SOURCES} Tests/ProcessorTests.cpp)
    target_include_directories(XDProcessorTests PRIVATE Source)
    target_compile_definitions(XDProcessorTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(XDProcessorTests PRIVATE juce::juce_audio_utils juce::juce_gui_extra
                          PUBLIC juce::juce_recommended_config_flags)
endif()
