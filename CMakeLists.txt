cmake_minimum_required(VERSION 3.22)

project(Trigon6Control VERSION 0.1.0 LANGUAGES C CXX)
set(T6_VERSION_LABEL "0.1.0")  # the next release; raise it right after each release

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8)
endif()
# Static MSVC runtime, so the app runs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Parameter and globals tables, NRPN/SysEx/packed coding, send queue, connection checklist, panel layout:
# plain C++, no JUCE, tested on its own.
add_library(t6_core STATIC
    src/core/ParamTable.cpp
    src/core/Nrpn.cpp
    src/core/Packed.cpp
    src/core/Sysex.cpp
    src/core/SendQueue.cpp
    src/core/Globals.cpp
    src/core/Connection.cpp
    src/core/PortMatch.cpp
    src/core/Presets.cpp
    src/ui/PanelLayout.cpp)
target_include_directories(t6_core PUBLIC src)
set_target_properties(t6_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(Trigon6Control
    COMPANY_NAME "bFunc"
    COMPANY_CODE Bfnc
    PLUGIN_MANUFACTURER_CODE Bfnc
    PLUGIN_CODE Tg6c
    PRODUCT_NAME "Trigon6 Control"
    BUNDLE_ID cc.bfunc.trigon6control
    FORMATS Standalone VST3
    VST3_CATEGORIES Instrument Synth
    # A generator in FL Studio: its MIDI goes out through the wrapper's MIDI output port.
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

target_sources(Trigon6Control PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/midi/SynthLink.cpp
    src/ui/ConnectionPage.cpp
    src/ui/PresetsPage.cpp
    src/ui/HardwareLook.cpp)

# Panel fonts (SIL Open Font License, see assets/fonts/OFL-*.txt), compiled in as BinaryData.
juce_add_binary_data(t6_fonts SOURCES
    assets/fonts/Michroma-Regular.ttf
    assets/fonts/ShareTechMono-Regular.ttf)
set_target_properties(t6_fonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(Trigon6Control PRIVATE src)

target_compile_definitions(Trigon6Control PUBLIC
    T6_VERSION_LABEL="${T6_VERSION_LABEL}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)

target_link_libraries(Trigon6Control
    PRIVATE
        t6_core
        t6_fonts
        juce::juce_audio_utils
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_executable(t6_tests tests/CoreTests.cpp)
target_link_libraries(t6_tests PRIVATE t6_core)
add_test(NAME core COMMAND t6_tests)
