# Quectel Pi Algorithm Demos

[English](README.md) | [简体中文](README_zh.md)

This repository is dedicated to AI edge-algorithm demos for Quectel Pi. It provides runnable examples and integration references for developers. Each subdirectory is an independent demo with its own source code, build and run scripts, and documentation.

### Demos

| Directory | Demo | Documentation |
| --- | --- | --- |
| [`face_project`](face_project/README.md) | Face recognition | Camera and local-image recognition, including face registration. |
| [`facial_attributes_project`](facial_attributes_project/README.md) | Facial attributes | Multi-face emotion, age and gender prediction with annotated JPG and JSON export. |
| [`gesture_project`](gesture_project/README.md) | Gesture recognition | Hand detection and gesture classification from a camera or local image. |
| [`maskdet_project`](maskdet_project/README.md) | Mask detection | Mask-wearing detection (correct / no mask / incorrect) from a camera or local image, with annotated, TXT, JSON and LabelMe export. |
| [`ocr_project`](ocr_project/README.md) | OCR text recognition | Text detection and recognition from a camera or local image, with annotated, TXT, JSON and LabelMe export. |
| [`posture_detection_project`](posture_detection_project/README.md) | Posture detection | Human keypoint based sitting-posture detection (cheek support, head bowing, unbalanced shoulders) with real-time preview. |

Follow the README in each demo directory for its device setup, SDK requirements, build, and run instructions. SDKs, proprietary runtime libraries, models, and device authorization materials are not included in this repository. Obtain them through authorized channels, and do not commit authorization data or sensitive biometric data.

## Reporting Issues

If you encounter any issues during use, please submit technical inquiries on the [Quectel Official Forum](https://forumschinese.quectel.com/c/quectel-pi/58). Our technical support team will respond promptly. Feel free to submit Issues and Pull Requests to improve this project.