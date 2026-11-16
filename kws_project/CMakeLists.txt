cmake_minimum_required(VERSION 3.16)
project(kws_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# 算法包：AIVOX 开放词表 KWS（zipformer-960k-zh，ARM v8 授权物料）
# 默认放在工程根目录下；可用 -DKWS_SDK_ROOT=... 或环境变量 KWS_SDK_ROOT 覆盖。
# 工程只依赖 aivox_ovkws.h 暴露的 8 个 C 接口，不直接触碰核心算法库。
# ---------------------------------------------------------------------------
set(KWS_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/aivox-scr-xiaoyuantongxue-zipformer-960k-zh/SDK"
    CACHE PATH "AIVOX KWS SDK 根目录（含 include / lib / tokens.txt）")
set(KWS_SDK_LIB "${KWS_SDK_ROOT}/lib/linux_arm_v8")

if(NOT EXISTS "${KWS_SDK_ROOT}/include/aivox_ovkws.h")
    message(FATAL_ERROR
        "未找到 AIVOX KWS SDK：${KWS_SDK_ROOT}\n"
        "请把算法包放到工程根目录，或执行：\n"
        "  cmake -S . -B build -DKWS_SDK_ROOT=/path/to/SDK")
endif()

# ---------------------------------------------------------------------------
# 依赖：Qt6 Widgets（界面）+ ALSA（采集）
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(ALSA REQUIRED)

add_executable(kws_demo
    src/main.h
    src/main.cpp
    src/kws_types.h
    src/kws_interfaces.h
    src/audio_capture.h
    src/audio_capture.cpp
    src/kws_engine.h
    src/kws_engine.cpp
    src/kws_pipeline.h
    src/kws_pipeline.cpp
    src/main_window.h
    src/main_window.cpp
)

target_include_directories(kws_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${KWS_SDK_ROOT}/include
)

target_link_directories(kws_demo PRIVATE
    ${KWS_SDK_LIB}
)

target_link_libraries(kws_demo PRIVATE
    Qt6::Widgets
    ALSA::ALSA
    aivox_ovkws
    _slas
    kissfft-float
    m
    pthread
    dl
)

# 板端直接运行 build/kws_demo 时也能找到算法包里的 .so
set_target_properties(kws_demo PROPERTIES
    BUILD_RPATH "${KWS_SDK_LIB}"
    INSTALL_RPATH "${KWS_SDK_LIB}"
)
