cmake_minimum_required(VERSION 3.22)

project(LightHost VERSION 1.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Visual Studio 產生器預設一個專案內逐檔編譯；/MP 讓同一專案的檔案平行編譯（含 webrtc-aec3）
if (MSVC)
    add_compile_options(/MP)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(third_party/webrtc-aec3)

# 降噪模型（見 third_party 各資料夾的 README）
file(GLOB RNNOISE_SOURCES third_party/rnnoise/src/*.c)
add_library(rnnoise STATIC ${RNNOISE_SOURCES})
target_include_directories(rnnoise PUBLIC third_party/rnnoise/include PRIVATE third_party/rnnoise/src)
# x64 一定有 SSE2；MSVC 不會自己定義 __SSE2__，要靠這兩個巨集走 RNNoise 的 SIMD 版本
target_compile_definitions(rnnoise PRIVATE RNNOISE_BUILD _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS
    OPUS_X86_MAY_HAVE_SSE OPUS_X86_MAY_HAVE_SSE2)

add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/bin/onnxruntime.dll"
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib/onnxruntime.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/include")

add_library(deepfilternet SHARED IMPORTED)
set_target_properties(deepfilternet PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/deepfilternet/bin/df.dll"
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/deepfilternet/lib/df.dll.lib")

# 執行時要放在執行檔旁的 DLL 與模型檔（安裝檔 installer/LightHost.iss 也要同步）
set(LIGHTHOST_RUNTIME_DLLS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/bin/onnxruntime.dll"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/deepfilternet/bin/df.dll")
set(LIGHTHOST_MODEL_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/deepfilternet/DeepFilterNet3_onnx.tar.gz"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fastenhancer/fastenhancer_t.onnx"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fastenhancer/fastenhancer_b.onnx"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fastenhancer/fastenhancer_s.onnx"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fastenhancer/fastenhancer_m.onnx"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gtcrn/gtcrn_simple.onnx")

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
    Source/AppSettings.cpp
    Source/IconMenu.cpp
    Source/PluginWindow.cpp
    Source/plugins/PluginChain.cpp
    Source/audio/AudioEngine.cpp
    Source/audio/Routing.cpp
    Source/audio/ClockBridge.cpp
    Source/audio/ExternalDevice.cpp
    Source/update/Updater.cpp
    Source/dsp/noise/FramedNoiseReducer.cpp
    Source/dsp/noise/ModelNoiseReducers.cpp
    Source/dsp/noise/NoiseReducerFactory.cpp
    Source/dsp/noise/NoiseReducerBenchmark.cpp
    Source/dsp/noise/OnnxStreamingModel.cpp
    Source/dsp/AecProcessor.cpp
    Source/dsp/NoiseReducerProcessor.cpp
    Source/dsp/SimpleNoiseReducer.cpp
    Source/dsp/OutputReferenceTap.cpp
    Source/dsp/CustomNoiseReducer.cpp
    Source/audio/LoopbackCapture.cpp
    Source/audio/LoopbackDevices.cpp
    Source/ui/AecMonitorWindow.cpp
    Source/ui/SettingsWindow.cpp
    Source/ui/NoiseCompareWindow.cpp
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
    rnnoise
    onnxruntime
    deepfilternet
    juce::juce_audio_utils
    juce::juce_dsp
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

add_custom_command(TARGET LightHost POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIGHTHOST_RUNTIME_DLLS} "$<TARGET_FILE_DIR:LightHost>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LightHost>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIGHTHOST_MODEL_FILES} "$<TARGET_FILE_DIR:LightHost>/models"
    COMMENT "Copying noise reduction DLLs and models next to Light Host")

# 降噪離線檢查（不隨 App 發布）：cmake --build build-new --config Release --target NoiseBench
juce_add_console_app(NoiseBench PRODUCT_NAME "NoiseBench")
set_target_properties(NoiseBench PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(NoiseBench PRIVATE
    tests/NoiseBench.cpp
    Source/dsp/SimpleNoiseReducer.cpp
    Source/dsp/noise/FramedNoiseReducer.cpp
    Source/dsp/noise/ModelNoiseReducers.cpp
    Source/dsp/noise/NoiseReducerFactory.cpp
    Source/dsp/noise/OnnxStreamingModel.cpp)
target_include_directories(NoiseBench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_definitions(NoiseBench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(NoiseBench PRIVATE webrtc_aec3 rnnoise onnxruntime deepfilternet
    juce::juce_audio_basics juce::juce_dsp juce::juce_events)
if (MSVC)
    target_compile_options(NoiseBench PRIVATE /utf-8)
endif()
juce_generate_juce_header(NoiseBench)
add_custom_command(TARGET NoiseBench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIGHTHOST_RUNTIME_DLLS} "$<TARGET_FILE_DIR:NoiseBench>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NoiseBench>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIGHTHOST_MODEL_FILES} "$<TARGET_FILE_DIR:NoiseBench>/models")

# AEC 時脈追蹤離線檢查（不隨 App 發布）：cmake --build build-new --config Release --target AecDriftTest
juce_add_console_app(AecDriftTest PRODUCT_NAME "AecDriftTest")
set_target_properties(AecDriftTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(AecDriftTest PRIVATE tests/AecDriftTest.cpp Source/dsp/AecProcessor.cpp)
target_include_directories(AecDriftTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_definitions(AecDriftTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(AecDriftTest PRIVATE webrtc_aec3 juce::juce_audio_processors)
if (MSVC)
    target_compile_options(AecDriftTest PRIVATE /utf-8)
endif()
juce_generate_juce_header(AecDriftTest)

# 時脈橋接離線檢查（不隨 App 發布）：cmake --build build-new --config Release --target ClockBridgeTest
juce_add_console_app(ClockBridgeTest PRODUCT_NAME "ClockBridgeTest")
set_target_properties(ClockBridgeTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(ClockBridgeTest PRIVATE tests/ClockBridgeTest.cpp Source/audio/ClockBridge.cpp Source/audio/ExternalDevice.cpp)
target_include_directories(ClockBridgeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_definitions(ClockBridgeTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ClockBridgeTest PRIVATE juce::juce_audio_basics juce::juce_events)
if (WIN32)
    target_link_libraries(ClockBridgeTest PRIVATE ole32 uuid)
endif()
if (MSVC)
    target_compile_options(ClockBridgeTest PRIVATE /utf-8)
endif()
juce_generate_juce_header(ClockBridgeTest)
