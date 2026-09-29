cmake_minimum_required(VERSION 3.22)

project(webrtc_aec3 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    add_compile_definitions(WEBRTC_MAC WEBRTC_POSIX)
elseif(UNIX AND NOT APPLE)
    add_compile_definitions(WEBRTC_LINUX WEBRTC_POSIX)
elseif(WIN32)
    add_compile_definitions(WEBRTC_WIN NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_compile_definitions(RTC_DISABLE_CHECK_MSG WEBRTC_APM_DEBUG_DUMP=0)

if(MSVC)
    add_compile_options(/utf-8 /EHsc /W3)
else()
    add_compile_options(-Wall -fPIC -Wno-deprecated -fexceptions)
endif()

file(GLOB AEC3_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/api/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/aec3/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/include/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/logging/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/resampler/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/base/system_wrappers/source/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/base/system_wrappers/source/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/base/rtc_base/memory/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/base/rtc_base/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/base/rtc_base/strings/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/base/rtc_base/system/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/base/jsoncpp/src/lib_json/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/utility/cascaded_biquad_filter.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/utility/ooura_fft.cc
)

list(FILTER AEC3_SRC EXCLUDE REGEX ".*unittest\\.cc$")

if(WIN32 OR (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|X86_64"))
    list(APPEND AEC3_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/utility/ooura_fft_sse2.cc
    )
elseif(APPLE OR (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64"))
    list(APPEND AEC3_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/utility/ooura_fft_neon.cc
    )
endif()

add_library(webrtc_aec3 STATIC ${AEC3_SRC})

target_include_directories(webrtc_aec3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/api
    ${CMAKE_CURRENT_SOURCE_DIR}/base/rtc_base
    ${CMAKE_CURRENT_SOURCE_DIR}/base
    ${CMAKE_CURRENT_SOURCE_DIR}/base/system_wrappers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing
    ${CMAKE_CURRENT_SOURCE_DIR}/base/abseil
    ${CMAKE_CURRENT_SOURCE_DIR}/base/jsoncpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/aec3
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/resampler
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/logging
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/include
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_processing/utility
)

if(WIN32)
    target_link_libraries(webrtc_aec3 PUBLIC winmm)
endif()
