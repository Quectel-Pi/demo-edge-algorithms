# 坐姿检测

本项目基于 **Quectel PI M2（RK3576）** 开发，集成算法包 `PeopleKptDet`，用 USB 摄像头实时采集画面进行坐姿检测（托腮 / 低头趴桌 / 高低肩）。界面基于 **Qt 6 Widgets 与 C++ 17** 构建，实现低延迟、较高准确率的坐姿识别与实时预览。

## 🎯 功能特点

* **实时预览**：启动即以全屏展示摄像头采集画面，组件随屏幕分辨率等比例放大
* **精准姿态识别**：通过 NPU 推理人体关键点，识别坐姿状态
* **多摄像头支持**：下拉列表自动扫描所有可用 USB 设备，运行时无障碍切换
* **检测标注叠加**：实时绘制检测框、关键点，并在检测框左上角显示姿态与置信度
* **结果去抖滤波**：对算法结果做滤波，抑制姿态之间的高频抖动
* **系统状态监控**：实时显示内存占用与 CPU 占用率
* **高级设置**：可调算法参数，支持恢复默认

## ⚙️ 工作原理

1. **采集**：摄像头线程持续抓取画面，界面实时预览（保持原始比例，不拉伸）
2. **推理**：开启检测后，把当前帧送入算法进行人体关键点与姿态识别；预览帧率与推理帧率解耦
3. **显示**：在预览画面上叠加检测框与关键点，检测框左上角显示姿态与置信度

## 📋 系统要求

* 硬件：**Quectel PI M2（RK3576）** + USB UVC 摄像头 + 显示器
* 系统：**Debian 13（aarch64）**
* 依赖：**Qt 6（Widgets）**、**OpenCV**、算法包 **PeopleKptDet**

## 🚀 安装与运行

**1. 安装工具链**

工具链通过脚本一键安装，无需手动下载编译：

```bash
sudo apt update
sudo bash scripts/setup_toolchain.sh              # 安装编译链 Qt6/OpenCV 及运行期依赖
sudo SKIP_GUI=1 bash scripts/setup_toolchain.sh   # 跳过 GUI 依赖，仅命令行调试
```

**2. 构建**

```bash
mkdir -p build                    # 构建产物目录（CMake / Ninja 输出）
chmod +x scripts/*.sh
sudo bash scripts/build.sh        # 产物 build/posture_demo
```

**3. 运行**

先设置授权码，再启动：

```bash
export PEOPLEKPT_AUTH_CODE=<您的授权码>
bash scripts/run.sh               # 注入算法路径/授权码/模型参数并启动，默认全屏
```

**摄像头权限**（提示无权限时）：将当前用户加入 `video` 组并重新登录。

```bash
sudo usermod -aG video "$USER"
```

## 🎛️ 使用说明

1. **启动**：`bash scripts/run.sh`，自动打开第一个可用摄像头并预览
2. **选择摄像头**：在 `CAMERA` 卡下拉框切换
3. **开启检测**：点击 **Start Detection**，预览叠加标注并更新结果卡
4. **停止检测**：再点一次，清空结果恢复裸预览
5. **调整参数**：点击 **Advanced Settings**，修改后确认生效，可恢复默认
6. **退出**：点击 **Exit**

## 🔧 配置参数

* **高级设置（界面内可调）**：推理帧率 `inferFps`（默认 15）、检测置信度 `detConfTh`（0.50）、
  关键点置信度 `kptConfTh`（0.30）、单/多目标 `onlyMaxBox`（true）
* **环境变量**：`POSTURE_SDK_ROOT`（算法包路径）、`PEOPLEKPT_AUTH_CODE`（授权码）、
  `CAMERA_DEVICE` / `--device N`（指定摄像头）

## 📁 项目结构

```
posture_detection_project/         # 项目根目录
├── CMakeLists.txt                 # 构建配置
├── README.md
├── src/
│   ├── main.cpp                   # 入口
│   ├── camera.*                   # 摄像头采集
│   ├── posture_detector.*         # 算法封装（唯一接入算法包模块）
│   ├── settings_dialog.*          # 高级设置面板
│   └── main_window.*              # 界面
└── scripts/
    ├── setup_toolchain.sh         # 工具链安装
    ├── build.sh                   # 编译
    └── run.sh                     # 运行
```

## ❓ 常见问题

* **没有画面**：确认摄像头连接正常、系统能看到 `/dev/video*`；权限问题请加入 `video` 组
* **初始化 / 授权失败**：检查 `PEOPLEKPT_AUTH_CODE` 授权码是否正确
* **开启检测后退出**：多为推理阶段崩溃，优先排查授权状态与内存占用

## 📬 报告问题
如在使用过程中遇到问题，欢迎在移远官方论坛 提交技术咨询，我们的技术支持团队将及时为您解答。 欢迎提交Issue和Pull Request来改进此项目。