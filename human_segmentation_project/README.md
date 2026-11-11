# Human Segmentation Demo

English | [简体中文](README.zh.md)

Qt5 desktop demo for the Quectel Pi M2 (RK3576, Debian aarch64). It runs the human segmentation SDK (`HumanSegmentation`) to extract human foreground contours from a USB camera, a local video file, or a selected image. The UI provides contour, white-background, and original-image views. Results can be exported as PNG images, TXT, JSON, and LabelMe polygon annotations.

![Human Segmentation](assets/human_segmentation.png)

## Requirements

- Quectel Pi M2 / RK3576 running Debian aarch64 with a desktop session
- C++17 compiler, `make`, `pkg-config`, and Qt5 Widgets development files
- The vendor-provided RK3576 human segmentation SDK package `RK_human_segmentation_NPU_v1.2`
- USB camera for live segmentation (optional for image and video modes)

The SDK, model files, and proprietary runtime libraries are not included in this project. Obtain the SDK through the authorized vendor channel. Do not commit SDK binaries, model files, device-bound licenses, or authorization codes to a public repository.

## Build

Place the SDK package at `$HOME/RK_human_segmentation_NPU_v1.2`, then run on the device as the desktop user:

```bash
cd "$HOME/human_segmentation_project"
sh setup_from_sdk.sh "$HOME/RK_human_segmentation_NPU_v1.2"
sh build.sh
```

`setup_from_sdk.sh` locates `RKNN-2.3.2-linux/rk3576/human_segmentation` in the SDK package and copies the header, runtime libraries, RK3576 model, and sample images into this project. It also accepts that SDK subdirectory directly. `build.sh` builds `human_segmentation_cam_demo` using the system Qt5 and the SDK's bundled OpenCV library; do not mix it with the system OpenCV.

Required Debian packages, if missing:

```bash
sudo apt update
sudo apt install -y build-essential pkg-config qtbase5-dev
```

## Run

Start the app from the device's graphical desktop session:

```bash
cd "$HOME/human_segmentation_project"
sh run.sh                                      # Launch the UI
sh run.sh --camera /dev/videoN                 # Use a specific camera node
sh run.sh --video /path/to/video.mp4            # Segment a local video file
sh run.sh --headless --image images/input.jpg  # Segment an image without a window
sh run.sh --headless --video /path/to/video.mp4 # Process every decoded video frame; export the last frame
sh run.sh --help                               # Show all options
```

The UI provides **Open Image**, **Open Video**, **Start Camera**, **Detect Again**, **Export Result**, and a full-screen control. Adjust confidence and rotation in the sidebar, and use the tabs to switch views. Videos play at their own frame rate while segmentation runs on the latest frame; click **Stop Video** to stop. Changing confidence or rotation restarts video playback from the beginning. Images and videos can also be dragged onto the window. Keyboard shortcuts: `Ctrl+O` opens an image, `Space` starts/stops the camera, `Ctrl+S` exports results, `F11` toggles full screen, and `Esc` leaves full screen. The app automatically scans for USB video-capture devices. Results are exported to `outputs/` by default.

## Authorization

Set a valid authorization code through `HUMAN_SEG_AUTH_CODE` or replace the `REPLACE_WITH_ALGORITHM_AUTH_CODE` placeholder in `run.sh`. `HUMAN_SEG_LICENSE_FILE` specifies the license path, which defaults to `license/human_segmentation.lic` in the project directory. Retain the device-bound license between runs; do not copy it to another device or commit it. The first authorization requires network access, a correct system time, and a writable license directory.

The UI shows **Licensed** when formal authorization is active; otherwise it displays the authorization status code.

## Reporting Issues

If you encounter any issues during use, please submit technical inquiries on the [Quectel Official Forum](https://forumschinese.quectel.com/c/quectel-pi/58). Our technical support team will respond promptly. Feel free to submit Issues and Pull Requests to improve this project.