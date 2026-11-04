# Quectel Pi 算法 Demo

[English](README.md) | [简体中文](README_zh.md)

本仓库专门用于存放和展示 Quectel Pi 上的 AI 边缘算法 Demo，为开发者提供可运行的示例及应用集成参考。每个子目录对应一个独立 Demo，包含其源码、构建脚本、运行脚本和详细说明。

## Demo 列表

| 目录 | 内容 | 说明 |
| --- | --- | --- |
| [`face_project`](face_project/README.zh.md) | 人脸识别 | 摄像头与本地图片识别示例，包含人脸注册功能。 |
| [`facial_attributes_project`](facial_attributes_project/README.zh.md) | 人脸属性检测 | 多人脸表情、年龄与性别预测示例，支持标注图与 JSON 导出。 |
| [`gesture_project`](gesture_project/README.zh.md) | 手势识别 | 摄像头与本地图片手部检测和手势分类示例。 |
| [`maskdet_project`](maskdet_project/README.zh.md) | 口罩检测 | 摄像头与本地图片口罩佩戴状态检测（正确佩戴 / 未佩戴 / 不正确佩戴）示例，支持标注图、TXT、JSON 与 LabelMe 导出。 |
| [`ocr_project`](ocr_project/README.zh.md) | OCR 文字识别 | 摄像头与本地图片文字检测和识别示例，支持标注图、TXT、JSON 与 LabelMe 导出。 |
| [`posture_detection_project`](posture_detection_project/README_zh.md) | 坐姿检测 | 基于人体关键点的坐姿检测（托腮 / 低头趴桌 / 高低肩）示例，支持实时预览。 |

## 使用说明

请进入对应 Demo 目录，按照其中的 README 准备设备环境、获取所需 SDK，并执行编译和运行步骤。各算法 Demo 的 SDK、依赖和授权要求可能不同，请以各自文档为准。

本仓库用于提供示例代码，不包含厂商 SDK、专有运行库、模型或设备授权信息。请通过授权渠道获取相关资源，不要将授权码、授权文件或人脸等敏感数据提交到仓库。

## 报告问题

如在使用过程中遇到问题，欢迎在[移远官方论坛](https://forumschinese.quectel.com/c/quectel-pi/58) 提交技术咨询，我们的技术支持团队将及时为您解答。 欢迎提交Issue和Pull Request来改进此项目。