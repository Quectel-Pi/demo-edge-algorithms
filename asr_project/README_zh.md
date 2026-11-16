# 实时语音识别（ASR）

[English](README_en.md) | 中文

本项目是一个基于 Quectel Pi M2 实现的实时流式语音识别示例。

系统通过板载麦克风实时采集语音，结合 sherpa-onnx 流式 Conformer 算法，边收边识别，界面即时显示识别出的中文文字，并支持识别历史与板端资源监控。

## 功能特性

- 实时流式识别（边说边出字），无需等待整句录完
- 音频输入开关（Start / Stop），开启即采集识别，关闭即停止
- 深色舞台区大字实时显示汉字，底部音频波形随音量动态起伏
- 识别历史逐句保存（序号 + 文本 + 时间），自动滚动到最新
- 模型后台预加载，点击 Start 可立即说话；加载期间说的话会缓存后补识别
- 实时显示板端内存与 CPU 占用
- 本地处理，低延迟、无需云端依赖

**识别能力**

> 建议在相对安静的环境、距离麦克风 0.5~1 m 内正常语速说话。

| 项目 | 说明 |
| --- | --- |
| 识别模型 | sherpa-onnx streaming-conformer-zh（INT8，encoder 约 160 MB） |
| 识别语种 | 中文 |
| 音频格式 | 16 kHz / 单声道，100 ms 分块送入 |
| 自动切句 | 说话后静音 1.2 s 切句；连续说话 20 s 强制切句，可连续不断识别 |

## 硬件要求

- Quectel Pi M2 单板电脑
- 板载麦克风（ES8389，ALSA 设备 plughw:0,0）
- 显示设备（HDMI 显示器）
- 键盘鼠标

## 软件环境搭建

> 项目需在板端本地编译，首次编译前请确认网络可访问软件源。

- 操作系统：Debian 13（Quectel Pi M2 默认系统，aarch64 带桌面环境）
- 编译工具：g++（C++17）、CMake、Ninja、pkg-config
- 显示：Qt 6 Base（若系统只有 Qt5 会自动回退）
- 音频：ALSA（libasound2-dev）

### 实现步骤

1. 在智能主控板上打开终端，使用 git 克隆项目代码。


```bash
sudo apt update
sudo apt install -y git
git clone https://github.com/Quectel-Pi/demo-edge-algorithms.git
cd demo-edge-algorithms/asr_project
```

2. 将算法包解压到 `/home/q/ans_project/`

3. 安装编译环境与依赖。

```bash
sudo chmod 755 scripts/*.sh
sudo bash scripts/setup_toolchain.sh
```

安装脚本会安装 build-essential、cmake、ninja-build、pkg-config、libasound2-dev、qt6-base-dev、libgl1-mesa-dev；若只需命令行调试可设置 SKIP_GUI=1 后执行同一脚本以跳过 Qt。

4. 编译工程，终端显示 [OK] 编译完成 即表示成功。

```bash
chmod +x scripts/*.sh
bash scripts/build.sh
```

5. 验证工具链（可选）。

```bash
g++ --version; cmake --version; ninja --version
```

## 项目结构

```
asr_project/
├── CMakeLists.txt                     # 构建配置
├── README.md                          # 项目英文说明文档
├── README_zh.md                       # 项目中文说明文档
├── src/                               # 源代码目录
│   ├── main.cpp                       # 主程序入口与模块组装
│   ├── main_window.h/.cpp             # 界面、采集识别线程、资源监控
│   ├── audio_input.h/.cpp             # 音频获取（ALSA + 预处理链接口）
│   └── asr_engine.h/.cpp              # 算法封装（唯一接入算法包的模块）
├── scripts/                           # 工具脚本
│   ├── setup_toolchain.sh             # 部署编译环境
│   ├── build.sh                       # 一键编译
│   └── run.sh                         # 一键运行
├── sherpa-onnx-asr-conformer-dewatermark/   # 算法包（内嵌，含模型与动态库）
```

## 运行程序

在板端桌面终端（建议普通用户，不要用 root 运行图形程序）执行启动脚本。

```bash
cd ~/asr_project
bash scripts/run.sh
```

若提示 Could not load the Qt platform plugin "xcb"，先执行 sudo apt install -y libxcb-cursor0 并确认 DISPLAY=:0。

**界面操作**

| 操作 | 说明 |
| --- | --- |
| 启动 | 窗口自动打开，右上角显示 Model Loading…，加载完成后变为绿色 Model Ready |
| Start | 开始采集与识别，状态变为 Listening，对着麦克风说话即可实时出字 |
| Stop | 停止采集与识别，状态回到 Idle |
| 历史 | 每句话在自动切句后追加到右侧识别历史 |
| Exit | 停止线程、释放模型并退出程序 |

## 报告问题

如在使用过程中遇到问题，欢迎在[移远官方论坛](https://forumschinese.quectel.com/c/quectel-pi/58) 提交技术咨询，我们的技术支持团队将及时为您解答。

欢迎提交 Issue 和 Pull Request 来改进此项目。
