# Real-Time Speech Recognition (ASR)

[English](README.md) | [中文](README_zh.md)

This project is a real-time streaming speech recognition demo built on the Quectel Pi M2.

It captures speech in real time from the on-board microphone and runs the sherpa-onnx streaming Conformer algorithm, recognizing while capturing. The GUI displays the recognized Chinese text instantly and also provides recognition history and on-board resource monitoring.

## Features

- Real-time streaming recognition (text appears while you speak), no need to wait until a whole sentence is recorded
- Audio input toggle (Start / Stop): capture and recognition start when enabled and stop when disabled
- Large live Chinese text in a dark stage area, with an audio waveform along the bottom that reacts to the volume
- Recognition history saved sentence by sentence (index + text + time), auto-scrolled to the latest entry
- Background model preloading, so you can speak right after pressing Start; audio spoken during loading is buffered and recognized afterwards
- Live on-board memory and CPU usage
- Local processing, low latency, no cloud dependency

**Recognition capability**

> A relatively quiet environment, normal speaking pace, and a distance of 0.5–1 m from the microphone are recommended.

| Item | Description |
| --- | --- |
| Model | sherpa-onnx streaming-conformer-zh (INT8, encoder about 160 MB) |
| Language | Chinese |
| Audio format | 16 kHz / mono, fed in 100 ms chunks |
| Auto segmentation | 1.2 s of silence after speech ends a sentence; 20 s of continuous speech forces a split, so recognition can run continuously |

## Hardware Requirements

- Quectel Pi M2 single-board computer
- On-board microphone (ES8389, ALSA device plughw:0,0)
- Display device (HDMI monitor)
- Keyboard and mouse

## Software Setup

> The project is compiled locally on the board. Make sure the package repositories are reachable before the first build.

- OS: Debian 13 (the default system on Quectel Pi M2, aarch64 with desktop environment)
- Build tools: g++ (C++17), CMake, Ninja, pkg-config
- Display: Qt 6 Base (falls back to Qt5 automatically when only Qt5 is available)
- Audio: ALSA (libasound2-dev)

### Implementation Steps

1. Open a terminal on the board and clone the project with git.

```bash
sudo apt update
sudo apt install -y git
git clone https://github.com/Quectel-Pi/demo-edge-algorithms.git
cd demo-edge-algorithms/asr_project
```

2. Extract the algorithm bundle to /home/q/ans_project/.

3. Install the build environment and dependencies.

```bash
sudo chmod 755 scripts/*.sh
sudo bash scripts/setup_toolchain.sh
```

The script installs build-essential, cmake, ninja-build, pkg-config, libasound2-dev, qt6-base-dev and libgl1-mesa-dev. If you only need command-line debugging, run the same script with SKIP_GUI=1 to skip Qt.

4. Build the project. The terminal prints [OK] 编译完成 (build finished) when it succeeds.

```bash
chmod +x scripts/*.sh
bash scripts/build.sh
```

5. Verify the toolchain (optional).

```bash
g++ --version; cmake --version; ninja --version
```

## Project Structure

```
asr_project/
├── CMakeLists.txt                     # build configuration
├── README.md                          # English documentation
├── README_zh.md                       # Chinese documentation
├── src/                               # source code
│   ├── main.cpp                       # entry point and module wiring
│   ├── main_window.h/.cpp             # GUI, capture/recognition thread, resource monitor
│   ├── audio_input.h/.cpp             # audio capture (ALSA + preprocessing-chain interface)
│   └── asr_engine.h/.cpp              # algorithm wrapper (the only module touching the bundle)
├── scripts/                           # helper scripts
│   ├── setup_toolchain.sh             # install the build environment
│   ├── build.sh                       # one-shot build
│   └── run.sh                         # one-shot run
├── sherpa-onnx-asr-conformer-dewatermark/   # algorithm bundle (embedded, models and shared libraries)
```

## Running

Run the launcher from the board desktop terminal (a normal user is recommended; do not run GUI programs as root).

```bash
cd ~/asr_project
bash scripts/run.sh
```

If Could not load the Qt platform plugin "xcb" appears, run sudo apt install -y libxcb-cursor0 first and make sure DISPLAY=:0 is set.

**UI operations**

| Action | Description |
| --- | --- |
| Launch | The window opens automatically; the top-right corner shows Model Loading… and turns green Model Ready when loading finishes |
| Start | Starts capture and recognition, the status becomes Listening; speak to the microphone and text appears live |
| Stop | Stops capture and recognition, the status returns to Idle |
| History | Each sentence is appended to the recognition history on the right after auto segmentation |
| Exit | Stops the threads, releases the model and exits the program |

## Reporting Issues

If you run into any problem, feel free to post a technical question on the [Quectel official forum](https://forumschinese.quectel.com/c/quectel-pi/58). Our technical support team will respond as soon as possible.

Issues and Pull Requests are welcome to help improve this project.
