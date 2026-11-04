# 手势识别 Demo

面向 Quectel Pi M2（RK3576、Debian/Ubuntu aarch64）的 Qt5 桌面 Demo，调用 Gesture SDK 完成手部检测和手势分类。支持 USB 摄像头实时识别与本地图片识别，界面显示 RAM、ROM 和 CPU 使用情况。

![手势识别效果](assets/gesture-demo.png)

## 环境要求

- RK3576 Debian/Ubuntu aarch64，带桌面会话
- C++17、Make、pkg-config、Qt5 Widgets 开发包
-  RK3576 Gesture SDK 包
- USB 摄像头（仅实时识别需要）

SDK、模型及专有运行库不包含在本仓库中，请通过授权渠道获取。不要将 SDK 二进制、模型、设备授权文件或授权码提交到公开仓库。

## 编译

将解压后的 SDK 平台目录放在设备用户目录 `$HOME/gesture_RK3576`，然后在设备桌面终端执行：

```bash
cd "$HOME/gesture_project"
sh setup_from_sdk.sh "$HOME/gesture_RK3576"
sh build.sh
```

缺少编译依赖时安装：

```bash
sudo apt update
sudo apt install -y build-essential pkg-config qtbase5-dev
```

`v4l2-ctl` 为可选工具，仅用于检查摄像头节点。

## 运行

在设备桌面会话启动：

```bash
cd "$HOME/gesture_project"
./run.sh                 # 自动探测 USB 摄像头
./run.sh /dev/videoN     # 指定摄像头节点
```

界面按钮为 **Open Camera**、**Local Image**、**Full Screen** 和 **Quit**。按 `F` 切换全屏，按 `Esc` 或 `Q` 退出。程序通过 V4L2 扫描 USB 视频采集节点，不假设摄像头是 `/dev/video0`。

## 授权

请通过环境变量 `GESTURE_AUTH_CODE` 提供有效授权码，或将 `run.sh` 中的 `XXXX` 替换为有效码。`XXXX` 只是占位符，不能用于授权。脚本将 `GESTURE_LICENSE_FILE` 指向工程目录下的 `authorization.lic`。授权成功后 SDK 可能生成 `authorization.lic` 和 `ql_ans_license.txt`；请保留这两个设备绑定文件，确保运行用户可写工程目录，不要复制到其他设备或提交到仓库。

SDK 仅在授权校验返回 `100010` 且确认授权服务器不可达时提供 30 分钟算法试用。空授权码或在线无效码在默认严格配置下会导致初始化失败。

## 报告问题

如在使用过程中遇到问题，欢迎在[移远官方论坛](https://forumschinese.quectel.com/c/quectel-pi/58) 提交技术咨询，我们的技术支持团队将及时为您解答。 欢迎提交Issue和Pull Request来改进此项目。