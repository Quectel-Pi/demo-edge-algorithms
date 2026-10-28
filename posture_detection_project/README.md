# Posture Detection

This project is developed based on the **Quectel PI M2 (RK3576)**, integrating the algorithm package `PeopleKptDet`. It uses a USB camera to capture frames in real time for posture detection (Cheek Support / Head Bowing on Desk / Unbalanced Shoulder). The UI is built with **Qt 6 Widgets and C++ 17**, delivering low-latency, higher-accuracy posture recognition with real-time preview.

## 🎯 Features

* **Real-time preview**: launches in full screen to show the camera feed; components scale proportionally with the screen resolution
* **Accurate posture recognition**: runs NPU inference on human keypoints to recognize posture
* **Multi-camera support**: the dropdown auto-scans all available USB devices and switches seamlessly at runtime
* **Detection overlay**: draws detection boxes and keypoints in real time, and shows posture and confidence at the top-left of the detection box
* **Result debounce filtering**: filters the algorithm results to suppress high-frequency jitter between postures
* **System status monitoring**: shows memory usage and CPU utilization in real time
* **Advanced settings**: adjustable algorithm parameters with reset-to-default support

## ⚙️ How It Works

1. **Capture**: a camera thread continuously grabs frames and previews them on the UI (keeps the original aspect ratio, no stretching)
2. **Inference**: after detection is enabled, the current frame is fed to the algorithm for human keypoint and posture recognition; the preview frame rate is decoupled from the inference frame rate
3. **Display**: detection boxes and keypoints are overlaid on the preview, with posture and confidence shown at the top-left of the detection box

## 📋 System Requirements

* Hardware: **Quectel PI M2 (RK3576)** + USB UVC camera + display
* OS: **Debian 13 (aarch64)**
* Dependencies: **Qt 6 (Widgets)**, **OpenCV**, algorithm package **PeopleKptDet**

## 🚀 Installation & Running

**1. Install the toolchain**

The toolchain is installed in one step via a script; no manual download or compilation is required:

```bash
sudo apt update
sudo bash scripts/setup_toolchain.sh              # install the Qt6/OpenCV build chain and runtime dependencies
sudo SKIP_GUI=1 bash scripts/setup_toolchain.sh   # skip GUI dependencies, for command-line debugging only
```

**2. Build**

```bash
mkdir -p build                    # build output directory (CMake / Ninja output)
chmod +x scripts/*.sh
sudo bash scripts/build.sh        # output build/posture_demo
```

**3. Run**

Set your authorization code, then launch:

```bash
export PEOPLEKPT_AUTH_CODE=<your-auth-code>
bash scripts/run.sh               # inject the algorithm path/auth code/model parameters and launch, full screen by default
```

**Camera permission** (when prompted for no permission): add the current user to the `video` group and log in again.

```bash
sudo usermod -aG video "$USER"
```

## 🎛️ Usage Instructions

1. **Launch**: `bash scripts/run.sh`, automatically opens the first available camera and previews
2. **Select camera**: switch via the `CAMERA` card dropdown
3. **Start detection**: click **Start Detection**, overlays the preview and updates the result card
4. **Stop detection**: click again to clear the results and restore the bare preview
5. **Adjust parameters**: click **Advanced Settings**, changes apply after confirmation, with reset-to-default support
6. **Exit**: click **Exit**

## 🔧 Configuration Parameters

* **Advanced settings (adjustable in the UI)**: inference frame rate `inferFps` (default 15), detection confidence `detConfTh` (0.50),
  keypoint confidence `kptConfTh` (0.30), single/multi target `onlyMaxBox` (true)
* **Environment variables**: `POSTURE_SDK_ROOT` (algorithm package path), `PEOPLEKPT_AUTH_CODE` (auth code),
  `CAMERA_DEVICE` / `--device N` (specify a camera)

## 📁 Project Structure

```
posture_detection_project/          # project root
├── CMakeLists.txt                 # build configuration
├── README.md
├── src/
│   ├── main.cpp                   # entry point
│   ├── camera.*                   # camera capture
│   ├── posture_detector.*         # algorithm wrapper (the only module that integrates the algorithm package)
│   ├── settings_dialog.*          # advanced settings panel
│   └── main_window.*              # UI
└── scripts/
    ├── setup_toolchain.sh         # toolchain installation
    ├── build.sh                   # build
    └── run.sh                     # run
```

## ❓ FAQ

* **No image**: confirm the camera is connected and the system can see `/dev/video*`; for permission issues, join the `video` group
* **Initialization / authorization failure**: check whether the `PEOPLEKPT_AUTH_CODE` auth code is correct
* **Exits after detection starts**: mostly an inference-stage crash; first check authorization status and memory usage

## 📬 Report Issues

If you encounter any problem while using this project, feel free to submit a technical inquiry on the Quectel official forum, and our technical support team will respond promptly. Issues and Pull Requests are welcome to improve this project.