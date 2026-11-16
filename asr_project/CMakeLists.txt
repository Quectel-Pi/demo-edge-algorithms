cmake_minimum_required(VERSION 3.16)

project(asr_qt_demo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ---------------------------------------------------------------------------
# Qt (Widgets) - 优先 Qt6，找不到退 Qt5
# ---------------------------------------------------------------------------
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
  set(QT_LIBS Qt6::Widgets)
else()
  find_package(Qt5 REQUIRED COMPONENTS Widgets)
  set(QT_LIBS Qt5::Widgets)
endif()

# ---------------------------------------------------------------------------
# sherpa-onnx 算法包（C API 模式）
# dewatermark 裁剪包只提供：
#   libsherpa-onnx-c-api.so  — 完整 C API 实现（4.5MB）
#   libsherpa-onnx-cxx-api.so — 薄 C++ wrapper（我们不用）
#   libonnxruntime.so        — ONNX Runtime
# 我们用 C API，不需要编任何 .cc，也不需要 C++ 头文件。
# ---------------------------------------------------------------------------
set(SHERPA_ONNX_HOME "${CMAKE_CURRENT_SOURCE_DIR}/sherpa-onnx-asr-conformer-dewatermark"
    CACHE PATH "sherpa-onnx bundle root")
set(SHERPA_ONNX_ARCH "linux_arm_v8" CACHE STRING
    "which prebuilt subfolder under <SHERPA_ONNX_HOME>/lib to use")

# ---------------------------------------------------------------------------
# Sources - src/ 平铺，四模块：main / UI / 音频获取 / 算法接入
# 不 include 任何 sherpa-onnx C++ 头文件，只 include 手写的 C API 前向声明
# ---------------------------------------------------------------------------
add_executable(asr_qt_demo
  src/main.cpp
  src/main_window.cpp
  src/audio_input.cpp
  src/asr_engine.cpp
)

target_include_directories(asr_qt_demo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${SHERPA_ONNX_HOME}/header  # 只用到 header/sherpa-onnx-c-api-fwd.h
)

target_link_directories(asr_qt_demo PRIVATE
  ${SHERPA_ONNX_HOME}/lib/${SHERPA_ONNX_ARCH}
)

target_link_libraries(asr_qt_demo PRIVATE
  ${QT_LIBS}
  sherpa-onnx-c-api   # C API 实现（4.5MB，完整）
  onnxruntime          # ONNX Runtime
  asound               # ALSA（系统）
  dl
  pthread
  m
)
