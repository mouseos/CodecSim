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

# Embed font resources in VST3 DLL (iPlug2 CMake only adds .rc to APP target)
if(WIN32 AND TARGET ${PROJECT_NAME}-vst3)
  set(_rc_file "${CMAKE_CURRENT_SOURCE_DIR}/resources/main.rc")
  if(EXISTS "${_rc_file}")
    target_sources(${PROJECT_NAME}-vst3 PRIVATE "${_rc_file}")
    set_source_files_properties("${_rc_file}" PROPERTIES
      COMPILE_FLAGS "/I\"${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts\" /I\"${CMAKE_CURRENT_SOURCE_DIR}/resources/img\" /I\"${CMAKE_CURRENT_SOURCE_DIR}/resources\""
    )
  endif()
endif()

# Add FFmpeg include path
set(FFMPEG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ffmpeg/include")
if(TARGET ${PROJECT_NAME}-app)
  target_include_directories(${PROJECT_NAME}-app PRIVATE ${FFMPEG_INCLUDE_DIR})
endif()
if(TARGET ${PROJECT_NAME}-vst3)
  target_include_directories(${PROJECT_NAME}-vst3 PRIVATE ${FFMPEG_INCLUDE_DIR})
endif()

# Copy ffmpeg.exe to build output directories
# Priority: nonfree (static, developer override) > LGPL (shared, default for distribution)
set(FFMPEG_NONFREE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ffmpeg-nonfree")
set(FFMPEG_LGPL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ffmpeg/bin")

if(EXISTS "${FFMPEG_NONFREE_DIR}/ffmpeg.exe")
  # Nonfree static build (developer only, not for redistribution)
  set(FFMPEG_SRC_DIR "${FFMPEG_NONFREE_DIR}")
  set(FFMPEG_IS_SHARED FALSE)
  message(STATUS "Using nonfree ffmpeg (static) from ${FFMPEG_SRC_DIR}")
elseif(EXISTS "${FFMPEG_LGPL_DIR}/ffmpeg.exe")
  # LGPL shared build (safe for redistribution)
  set(FFMPEG_SRC_DIR "${FFMPEG_LGPL_DIR}")
  set(FFMPEG_IS_SHARED TRUE)
  message(STATUS "Using LGPL ffmpeg (shared) from ${FFMPEG_SRC_DIR}")
endif()

if(FFMPEG_SRC_DIR)
  foreach(_target app vst3)
    if(TARGET ${PROJECT_NAME}-${_target})
      # Copy ffmpeg.exe
      add_custom_command(TARGET ${PROJECT_NAME}-${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${FFMPEG_SRC_DIR}/ffmpeg.exe"
          "$<TARGET_FILE_DIR:${PROJECT_NAME}-${_target}>/ffmpeg.exe"
        COMMENT "Copying ffmpeg.exe to ${_target} output directory"
      )
      # Copy shared libraries for LGPL build
      if(FFMPEG_IS_SHARED)
        file(GLOB _ffmpeg_dlls "${FFMPEG_SRC_DIR}/*.dll")
        foreach(_dll ${_ffmpeg_dlls})
          add_custom_command(TARGET ${PROJECT_NAME}-${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_dll}"
              "$<TARGET_FILE_DIR:${PROJECT_NAME}-${_target}>/"
            COMMENT "Copying ffmpeg DLL: ${_dll}"
          )
        endforeach()
      endif()
      # Copy LICENSE
      if(EXISTS "${FFMPEG_SRC_DIR}/../LICENSE.txt")
        add_custom_command(TARGET ${PROJECT_NAME}-${_target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FFMPEG_SRC_DIR}/../LICENSE.txt"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}-${_target}>/LICENSE-ffmpeg.txt"
          COMMENT "Copying ffmpeg LICENSE to ${_target} output directory"
        )
      elseif(EXISTS "${FFMPEG_SRC_DIR}/LICENSE")
        add_custom_command(TARGET ${PROJECT_NAME}-${_target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FFMPEG_SRC_DIR}/LICENSE"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}-${_target}>/LICENSE-ffmpeg.txt"
          COMMENT "Copying ffmpeg LICENSE to ${_target} output directory"
        )
      endif()
    endif()
  endforeach()
else()
  message(WARNING "No ffmpeg.exe found. Looked in:\n  ${FFMPEG_NONFREE_DIR}\n  ${FFMPEG_LGPL_DIR}")
endif()
