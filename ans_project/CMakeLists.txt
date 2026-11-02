cmake_minimum_required(VERSION 3.16)
project(ans_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 元对象编译：为含 Q_OBJECT 的头生成 moc_*.cpp，必须开启
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# 算法包 SDK（AIVOX AEC_ANS，本工程只用其中的 ANS 降噪接口）根目录
# —— 固定为板端实际解包目录，可被环境变量覆盖
# ---------------------------------------------------------------------------
set(AECANS_SDK_ROOT "/home/q/aec_ans_project/AIVOXAEC_ANS_V1.1.0.2.1_SDK_16k_10ms_Exp009_dewatermark_encrypt" CACHE PATH "AEC_ANS SDK 解压根目录")
# Linux aarch64 平台库目录
set(AECANS_SDK_LIB "${AECANS_SDK_ROOT}/lib/Linux-aarch64_v8")

# ---------------------------------------------------------------------------
# 依赖：Qt6 Widgets（界面）+ ALSA(libasound2)（采集/回放）
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(ALSA REQUIRED)

add_executable(ans_demo
    src/main.h          src/main.cpp
    src/audio_types.h
    src/ans_processor.h src/ans_processor.cpp
    src/audio_capture.h     src/audio_capture.cpp
    src/main_window.h       src/main_window.cpp
)

target_include_directories(ans_demo PRIVATE
    ${Qt6Widgets_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${AECANS_SDK_ROOT}/include   # aivox_aec_ans.h / aivox_slas.h
)

target_link_directories(ans_demo PRIVATE ${AECANS_SDK_LIB})

target_link_libraries(ans_demo PRIVATE
    Qt6::Widgets
    ${ALSA_LIBRARIES}
    aivox_aec_ans                 # 算法 SDK（依赖 libMNN.so/lib_slas.so，运行期由 run.sh 注入路径）
    ${AECANS_SDK_LIB}/lib_slas.so # SLAS 授权库，提供 api 层调用的 aivox_slas_* 授权接口
    dl
    pthread
)

# 链接期允许 SDK 传递依赖暂缺，运行期由 run.sh 设置的 LD_LIBRARY_PATH 解析
target_link_options(ans_demo PRIVATE
    "-Wl,--allow-shlib-undefined"
)

message(STATUS "AEC_ANS SDK root: ${AECANS_SDK_ROOT}")
message(STATUS "AEC_ANS SDK lib : ${AECANS_SDK_LIB}")