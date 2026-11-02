# 音频降噪（ANS）

[English](README_EN.md) | 中文

本项目是一个基于 Quectel Pi M2 实现的单通道语音降噪（ANS）示例。
程序通过板载 ES8389 Codec 实时采集麦克风，边采边调用 AIVOX AEC_ANS 算法逐帧降噪，界面实时显示原始与降噪后波形，录制结束可保存两份 WAV 。

## 功能特性

- 实时音频采集与逐帧降噪（16000 Hz / 16 bit / 单声道，160 采样 = 10 ms 一帧）
- 平稳噪声抑制 + 非平稳噪声抑制（SDK 内置单一强度参数，默认 0.6）
- 三路波形实时显示（Captured Input / Processed Result / Live Mic Input）
- 波形按峰值自动归一化铺满，录完可回放原声与降噪结果
- 麦克风软件预增益（0 ~ 30 dB）与降噪强度在线调节，下一帧生效
- 原始音频与降噪结果同时落盘，便于对比与复核
- 纯本地处理，低延迟（约 10~30 ms），无需云端依赖

**界面组成**

|界面区域|内容|
|----------|---------|
|左侧 Waveforms|Captured Input（原始，可回放）、Processed Result（降噪后，可回放）、Live Mic Input（Mic Test）|
|右侧 Record & Process|录制开关、采集设备选择、电平条、实时帧数与耗时、输出 WAV 路径、处理结果|
|右侧 Function Switches|降噪强度、麦克风增益、两条降噪状态（Stationary / Non-stationary）|

> 本示例只做降噪（ANS），不包含回声消除（AEC），无需参考信号。

## 硬件要求

- Quectel Pi M2 单板电脑（RK3576）
- 板载 ES8389 Codec（card0 / device0，麦克风 + 喇叭）
- 显示设备（HDMI / DSI 屏）
- 键鼠

## 软件环境搭建

- 操作系统：Debian 13（Quectel Pi M2 默认系统，aarch64，带桌面环境）
- 显示框架：Qt 6 Widgets（C++17）
- 音频：ALSA（libasound2）
- 算法：AIVOX AEC_ANS SDK（Linux aarch64 动态库 + `.mnn` 降噪模型，不随本仓库分发）
- 依赖组件：
  - Qt6 Base（Widgets）
  - libasound2 / libasound2-dev / alsa-utils
  - CMake + Ninja + g++（C++17）
  - libgl1-mesa-dev、qt6-qpa-plugins、libxcb-cursor0 等运行期库

### 实现步骤

1. 在智能主控板上打开终端，使用 git 克隆项目代码。

```bash
sudo apt update
sudo apt install -y git
git clone https://github.com/Quectel-Pi/demo-edge-algorithms.git
cd demo-edge-algorithms/ans_project
```

执行完成后，当前目录下应生成 `ans_project` 文件夹。

2. 将算法包解压到 `/home/q/ans_project/`。

3. 在智能主控板的终端下依次执行下面命令，终端显示 `[OK] 工具链安装完成` 则说明部署完成。

```bash
sudo chmod 755 scripts/*.sh
sudo bash scripts/setup_toolchain.sh
```

如果执行 `setup_toolchain.sh` 过程中报错，请先不要继续执行后续步骤。建议先确认网络连接正常，再返回项目根目录重新执行一次。

4. 编译并运行（请使用普通用户，例如 `q`，不要用 `sudo`，否则 `build/`、`output/` 会变成 root 属主，保存 WAV 时报 `errno=13`）。

```bash
bash scripts/build.sh
bash scripts/run.sh
```

启动日志会打印当前生效的采集/回放设备：

```
[MIXER] configuring ES8389 playback routes...
  numid=57 spk switch      on
CAP=default  PLAY=default  OUTPUT=/home/q/ans_demo/output
```

## 项目结构

```
ans_project/
├── CMakeLists.txt                    # 构建配置
├── README.md                         # 项目中文说明文档
├── README_EN.md                      # 项目英文说明文档
├── AIVOX-TTS-ZH-LITE-V2.0-30k-vc/    # 算法包
├── src/                              # 源代码目录
│   ├── main.cpp                      # 主程序入口（配置注入 + WAV 写出）
│   ├── main.h                        # appconfig / wav 声明
│   ├── audio_types.h                 # AudioFrame / ProcessorConfig / ProcessStats
│   ├── audio_capture.*               # ALSA 采集线程与回放（AudioPlayback）
│   ├── ans_processor.*               # 算法封装（唯一接入算法包）+ 流式处理线程
│   └── main_window.*                 # 界面（固定 1240×762 画布）
└── scripts/
    ├── setup_toolchain.sh            # 部署环境脚本
    ├── build.sh                      # 编译脚本（CMake + Ninja）
    └── run.sh                        # 运行脚本（注入算法路径/授权/混音器）
```

`include/`、`lib/`、`models/` 由算法包提供，与工程解耦；工程仅通过 `src/ans_processor.*` 接入算法。

## 运行程序

在项目根目录执行 `run.sh` 启动程序。

```bash
cd ~/ans_project
bash scripts/run.sh                 # 使用默认采集/回放设备
bash scripts/run.sh --cap hw:0,0    # 指定采集设备
```

启动后：

1. 在 Record & Process 卡片选择采集设备，点击 **Start Recording** 开始录制并降噪；
2. 实时观察三条波形：原始（蓝）、降噪后（绿）、Mic Test（橙）；
3. 点击 **Stop** 结束录制，结果保存为 `output/recorded_input.wav` 与 `output/recorded_processed.wav`；
4. 点击 **▶ Play Input** / **▶ Play Output** 通过喇叭回放对比；
5. 点击 **Exit** 退出。

## 常见问题

|现象|排查方法|
|---|---|
|回放没有声音|`amixer -c 0 cget numid=57` 确认 `spk switch` 为 `on`（本版驱动不支持按名字设置 Speaker Switch），并确认 `numid=51/52` 音量、`numid=59` Headphone Switch 为 off|
|提示 `主机已关闭`（error -112）|`default` 被 PipeWire / PulseAudio 抢占，改用 `ANS_PLAYBACK_DEV=plughw:0,0`，或停掉 PipeWire 后重试|
|回放声音变快 / 变调|Codec 时钟与请求采样率不匹配，先 `aplay -D plughw:0,0 -c1 -r16000 -f S16_LE output/recorded_input.wav` 对比，再按 `aplay -D hw:0,0 --dump-hw-params` 显示的实际支持速率处理|
|保存 WAV 报 `errno=13`|`output/` 或旧 WAV 为 root 属主：`sudo chown -R $(id -u):$(id -g) output`，并以普通用户运行|
|授权失败|确认 `ANS_AUTH_CODE`、网络可访问授权服务器、`/data` 可写，以及 `/dev/ttyGS0` 存在|

更多细节见 [ANS_DEMO_GUIDE.md](ANS_DEMO_GUIDE.md)。

## 报告问题

如在使用过程中遇到问题，欢迎在[移远官方论坛](https://forumschinese.quectel.com/c/quectel-pi/58) 提交技术咨询，我们的技术支持团队将及时为您解答。

欢迎提交 Issue 和 Pull Request 来改进此项目。
