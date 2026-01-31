cmake_minimum_required(VERSION 3.25)
project(CodecSim VERSION 1.0.0)

set(IPLUG2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../iPlug2 CACHE PATH "iPlug2 root directory")
include(${IPLUG2_DIR}/iPlug2.cmake)

find_package(iPlug2 REQUIRED)

iplug_add_plugin(${PROJECT_NAME}
  SOURCES
    CodecSim.cpp
    CodecSim.h
    CodecProcessor.cpp
    CodecProcessor.h
    FFmpegPipeManager.cpp
    FFmpegPipeManager.h
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
  FORMATS
    VST3 APP
)

# Add FFmpeg include path
set(FFMPEG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ffmpeg/include")
if(TARGET ${PROJECT_NAME}-app)
  target_include_directories(${PROJECT_NAME}-app PRIVATE ${FFMPEG_INCLUDE_DIR})
endif()
if(TARGET ${PROJECT_NAME}-vst3)
  target_include_directories(${PROJECT_NAME}-vst3 PRIVATE ${FFMPEG_INCLUDE_DIR})
endif()
