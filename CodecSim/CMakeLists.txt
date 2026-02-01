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
    CodecRegistry.cpp
    CodecRegistry.h
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

# Copy ffmpeg.exe to build output directories
set(FFMPEG_NONFREE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ffmpeg-nonfree")
if(EXISTS "${FFMPEG_NONFREE_DIR}/ffmpeg.exe")
  if(TARGET ${PROJECT_NAME}-app)
    add_custom_command(TARGET ${PROJECT_NAME}-app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FFMPEG_NONFREE_DIR}/ffmpeg.exe"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}-app>/ffmpeg.exe"
      COMMENT "Copying ffmpeg.exe (nonfree) to app output directory"
    )
  endif()
  if(TARGET ${PROJECT_NAME}-vst3)
    add_custom_command(TARGET ${PROJECT_NAME}-vst3 POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FFMPEG_NONFREE_DIR}/ffmpeg.exe"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}-vst3>/ffmpeg.exe"
      COMMENT "Copying ffmpeg.exe (nonfree) to VST3 output directory"
    )
  endif()
endif()
