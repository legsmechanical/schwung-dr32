# The ChowKick REFERENCE renderer: ChowKick's own DSP classes (src/dsp/*.cpp,
# unmodified) built against the JUCE and chowdsp_utils it pins, driven the way
# ChowKick::processSynth drives them. tests/test_chowkick.c A/Bs DR32's port
# against what this writes. Off-device only; nothing here ships.
#
#   cmake -S tools/chowkick_ref -B build/chowkick_ref -DCHOWKICK=<clone> && cmake --build build/chowkick_ref
cmake_minimum_required(VERSION 3.15)
project(chowkick_ref VERSION 1.2.0)
set(CMAKE_CXX_STANDARD 17)
if(NOT CHOWKICK)
    message(FATAL_ERROR "-DCHOWKICK=<path to a ChowKick clone with its submodules>")
endif()
add_subdirectory(${CHOWKICK}/modules/JUCE JUCE)
add_subdirectory(${CHOWKICK}/modules/chowdsp_utils chowdsp_utils)
add_subdirectory(${CHOWKICK}/modules/chowdsp_wdf chowdsp_wdf)
add_subdirectory(${CHOWKICK}/modules/tuning-library tuning EXCLUDE_FROM_ALL)
juce_add_console_app(ckref PRODUCT_NAME ckref)
juce_generate_juce_header(ckref)
target_sources(ckref PRIVATE main.cpp
    ${CHOWKICK}/src/dsp/Noise.cpp ${CHOWKICK}/src/dsp/OutputFilter.cpp
    ${CHOWKICK}/src/dsp/PulseShaper.cpp ${CHOWKICK}/src/dsp/ResonantFilter.cpp
    ${CHOWKICK}/src/dsp/Trigger.cpp)
target_include_directories(ckref PRIVATE ${CHOWKICK}/src ${CHOWKICK}/modules/tuning-library/include)
target_compile_definitions(ckref PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JucePlugin_Name="ChowKick" JucePlugin_VersionString="1.2.0" JucePlugin_Manufacturer="chowdsp"
    CHOW_KICK_WITH_MTS=0)
target_link_libraries(ckref PRIVATE juce::juce_audio_processors juce::juce_dsp
    chowdsp::chowdsp_filters chowdsp::chowdsp_sources chowdsp::chowdsp_plugin_base
    chowdsp::chowdsp_wdf tuning-library juce::juce_recommended_config_flags)
