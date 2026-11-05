cmake_minimum_required(VERSION 3.16)

project(tts_qt_demo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
  set(QT_LIBS Qt6::Widgets)
else()
  find_package(Qt5 REQUIRED COMPONENTS Widgets)
  set(QT_LIBS Qt5::Widgets)
endif()

set(AIVOX_TTS_HOME "${CMAKE_CURRENT_SOURCE_DIR}/AIVOX-TTS-ZH-LITE-V2.0-30k-vc"
    CACHE PATH "AIVOX TTS bundle root")

add_library(aivox_tts_resource STATIC
  ${AIVOX_TTS_HOME}/resource/aivox_resource_blob_data.c
)

add_executable(tts_qt_demo
  src/main.cpp
  src/main_window.cpp
  src/audio_output.cpp
  src/tts_engine.cpp
)

target_include_directories(tts_qt_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${AIVOX_TTS_HOME}/include
)

target_link_directories(tts_qt_demo PRIVATE
  ${AIVOX_TTS_HOME}/lib
)

target_link_libraries(tts_qt_demo PRIVATE
  ${QT_LIBS}
  aivox_tts_shared
  _slas
  aivox_tts_resource
  asound
  dl
  pthread
  m
)
