cmake_minimum_required(VERSION 3.22)

project(LightHost VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(third_party/webrtc-aec3)

set(LIGHTHOST_ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ASIOSDK" CACHE PATH "Path to Steinberg ASIO SDK")
set(LIGHTHOST_ENABLE_ASIO OFF)
if (WIN32 AND EXISTS "${LIGHTHOST_ASIO_SDK_DIR}/common/iasiodrv.h")
    set(LIGHTHOST_ENABLE_ASIO ON)
endif()

juce_add_binary_data(LightHostBinaryData SOURCES
    Resources/icon.png
    Resources/menu_icon.png
    Resources/menu_icon_white.png
)

juce_add_gui_app(LightHost
    PRODUCT_NAME "Light Host"
    COMPANY_NAME "LightHost"
    BUNDLE_ID "com.lighthost.app"
    ICON_BIG Resources/icon.png
)

target_sources(LightHost PRIVATE
    Source/HostStartup.cpp
    Source/IconMenu.cpp
    Source/PluginWindow.cpp
    Source/dsp/AecProcessor.cpp
    Source/dsp/NoiseReducerProcessor.cpp
    Source/dsp/SimpleNoiseReducer.cpp
    Source/dsp/OutputReferenceTap.cpp
    Source/dsp/CustomNoiseReducer.cpp
    Source/audio/LoopbackCapture.cpp
    Source/audio/LoopbackDevices.cpp
    Source/ui/AecMonitorWindow.cpp
    Source/ui/SettingsWindow.cpp
)

target_compile_definitions(LightHost PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_ASIO=$<IF:$<BOOL:${LIGHTHOST_ENABLE_ASIO}>,1,0>
    JUCE_ASIO_USE_EXTERNAL_SDK=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(LightHost PRIVATE
    LightHostBinaryData
    webrtc_aec3
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
)

target_include_directories(LightHost PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

if(WIN32)
    target_link_libraries(LightHost PRIVATE ole32 uuid avrt)
    target_compile_options(LightHost PRIVATE /utf-8)
    if (LIGHTHOST_ENABLE_ASIO)
        target_include_directories(LightHost PRIVATE "${LIGHTHOST_ASIO_SDK_DIR}/common")
        message(STATUS "ASIO support enabled: ${LIGHTHOST_ASIO_SDK_DIR}")
    else()
        message(STATUS "ASIO support disabled: SDK not found at ${LIGHTHOST_ASIO_SDK_DIR}")
    endif()
endif()

juce_generate_juce_header(LightHost)
