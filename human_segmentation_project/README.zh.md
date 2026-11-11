# 人像分割 Demo

[English](README.md) | 简体中文

面向 Quectel Pi M2（RK3576、Debian aarch64）的 Qt5 桌面 Demo，调用人像分割 SDK（`HumanSegmentation`）提取人像前景轮廓。支持 USB 摄像头实时分割、本地视频分割与本地图片分割，提供轮廓、白底和原图三种视图，可导出 PNG 图片、TXT、JSON 和 LabelMe 多边形标注。

![人像分割](assets/human_segmentation.png)

## 环境要求

- RK3576 Debian aarch64，带桌面会话
- C++17、Make、pkg-config、Qt5 Widgets 开发包
- RK3576 人像分割 SDK 包 `RK_human_segmentation_NPU_v1.2`
- USB 摄像头（仅实时分割需要）

SDK、模型及专有运行库不包含在本工程中，请通过授权渠道获取。不要将 SDK 二进制、模型、设备授权文件或授权码提交到公开仓库。

## 编译

将 SDK 包放在设备用户目录 `$HOME/RK_human_segmentation_NPU_v1.2`，然后在设备桌面终端执行：

```bash
cd "$HOME/human_segmentation_project"
sh setup_from_sdk.sh "$HOME/RK_human_segmentation_NPU_v1.2"
sh build.sh
```

`setup_from_sdk.sh` 会自动定位 SDK 中的 `RKNN-2.3.2-linux/rk3576/human_segmentation` 目录，并复制头文件、运行库、RK3576 模型和示例图片，也可直接传入该 SDK 子目录。`build.sh` 编译生成 `human_segmentation_cam_demo`，界面使用系统 Qt5，图像处理使用 SDK 自带的 OpenCV，不要混用系统 OpenCV。

缺少编译依赖时安装：

```bash
sudo apt update
sudo apt install -y build-essential pkg-config qtbase5-dev
```

## 运行

在设备桌面会话启动：

```bash
cd "$HOME/human_segmentation_project"
sh run.sh                                      # 启动界面
sh run.sh --camera /dev/videoN                 # 指定摄像头节点
sh run.sh --video /path/to/video.mp4            # 分割本地视频
sh run.sh --headless --image images/input.jpg  # 无窗口分割图片
sh run.sh --headless --video /path/to/video.mp4 # 无窗口处理全部解码帧，导出最后一帧
sh run.sh --help                               # 查看全部参数
```

界面提供 **Open Image**、**Open Video**、**Start Camera**、**Detect Again**、**Export Result** 和全屏按钮，可在左侧调整置信度与旋转角度，通过标签页切换显示方式。视频按原帧率播放，分割对最新帧进行；点击 **Stop Video** 停止。修改置信度或旋转角度会让视频从头播放。图片和视频也可直接拖入窗口。快捷键：`Ctrl+O` 打开图片，`Space` 开始/停止摄像头，`Ctrl+S` 导出结果，`F11` 切换全屏，`Esc` 退出全屏。程序自动扫描 USB 视频采集设备。结果默认导出到 `outputs/`。

## 授权

请通过环境变量 `HUMAN_SEG_AUTH_CODE` 提供有效授权码，或替换 `run.sh` 中的 `REPLACE_WITH_ALGORITHM_AUTH_CODE` 占位符。`HUMAN_SEG_LICENSE_FILE` 指定许可证路径，默认为工程目录下的 `license/human_segmentation.lic`。请保留该设备绑定许可证，不要复制到其他设备或提交到仓库。首次授权需要网络、正确的系统时间以及可写的许可证目录。

正式授权生效时界面显示 **Licensed**，否则显示授权状态码。

## 报告问题

如在使用过程中遇到问题，欢迎在[移远官方论坛](https://forumschinese.quectel.com/c/quectel-pi/58) 提交技术咨询，我们的技术支持团队将及时为您解答。欢迎提交 Issue 和 Pull Request 来改进此项目。