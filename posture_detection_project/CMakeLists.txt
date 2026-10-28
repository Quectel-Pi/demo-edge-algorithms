cmake_minimum_required(VERSION 3.16)
project(posture_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 元对象编译：为含 Q_OBJECT 的头生成 moc_*.cpp，必须开启，否则链接报
# undefined reference to vtable/staticMetaObject/信号
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# 算法包 SDK（PeopleKptDet）路径 —— 默认取仓库根目录下的 PeoplePostureDet_RK3576_Ubuntu
# 子目录；若设了环境变量 POSTURE_SDK_ROOT 则以其为准
set(POSTURE_SDK_ROOT "${CMAKE_SOURCE_DIR}/PeoplePostureDet_RK3576_Ubuntu" CACHE PATH "PeopleKptDet SDK 解压根目录")
if(DEFINED ENV{POSTURE_SDK_ROOT})
    set(POSTURE_SDK_ROOT "$ENV{POSTURE_SDK_ROOT}" CACHE PATH "PeopleKptDet SDK 解压根目录" FORCE)
endif()

# 运行时第三方依赖目录（SDK 自带 OpenCV/RNN/ModelInfer/JsonCpp），主要供 run.sh 拼 LD_LIBRARY_PATH 用
set(POSTURE_SDK_LIB   "${POSTURE_SDK_ROOT}/lib")

# ---------------------------------------------------------------------------
# 依赖：Qt6 Widgets（界面）+ 系统 OpenCV（USB 摄像头采集）
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(OpenCV REQUIRED)

add_executable(posture_demo
    src/main.cpp    src/main.h
    src/camera.h    src/camera.cpp
    src/posture_detector.h src/posture_detector.cpp
    src/main_window.h      src/main_window.cpp
    src/settings_dialog.h  src/settings_dialog.cpp
)

target_include_directories(posture_demo PRIVATE
    ${Qt6Widgets_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${POSTURE_SDK_ROOT}/include          # PeopleKptDet.h
)

target_link_directories(posture_demo PRIVATE ${POSTURE_SDK_LIB})

target_link_libraries(posture_demo PRIVATE
    Qt6::Widgets
    ${OpenCV_LIBS}                        # 仅用于摄像头采集（videoio）
    PeopleKptDet                          # 算法 SDK
    dl
    pthread
)

# 链接期允许 SDK 传递依赖暂缺，运行期由 run.sh 设置的 LD_LIBRARY_PATH 解析
target_link_options(posture_demo PRIVATE
    "-Wl,--allow-shlib-undefined"
)