# 人脸识别 Demo

[English](README.md) | 简体中文

面向 Quectel Pi M2（RK3576、Debian/Ubuntu aarch64）的 Qt5 桌面 Demo，支持 USB 摄像头与本地图片人脸识别、关键点、头部姿态和注册持久化。界面为英文，显示底库数量及 RAM、存储和 CPU 使用情况。

![人脸识别效果](assets/face-demo.png)

## 环境要求

- RK3576 Debian/Ubuntu aarch64，带图形桌面会话
- C++17 编译器、Make、pkg-config、Qt5 Widgets 开发包
- 厂商提供的 Face SDK Linux 包，保留目录名 `rknn-2.3.2-linux`
- USB 摄像头（仅实时识别需要）

SDK 二进制、专有运行库和模型不包含在仓库中，请通过授权渠道获取。不要公开授权码、授权文件、人脸底图或注册的生物特征数据。

## 编译

在设备端安装缺少的编译依赖：

```bash
sudo apt update
sudo apt install -y build-essential pkg-config qtbase5-dev
```

将 SDK 平台目录放在 `$HOME/rknn-2.3.2-linux`，工程放在 `$HOME/face_project`。在设备桌面终端，以普通用户执行：

```bash
cd "$HOME/face_project"
sh setup_from_sdk.sh "$HOME/rknn-2.3.2-linux"
sh build.sh
chmod +x run.sh
```

复制脚本将 SDK 头文件、库、模型、样例图和人脸底图复制到工程内；编译使用系统 Qt5 和随 SDK 提供的 OpenCV world 库。

## 运行

提供有效授权码后，在设备桌面终端启动：

```bash
cd "$HOME/face_project"
FACE_AUTH_CODE='your-authorized-code' ./run.sh
```

将 `your-authorized-code` 替换为自己的授权码。指定摄像头时在命令后追加 `/dev/videoN`；不传参数则自动探测 USB 采集节点。`v4l2-ctl` 是可选排查工具，不是编译依赖。

界面按钮为 **Open Camera**、**Local Image**、**Register Face**、**Landmarks**、**Full Screen** 和 **Quit**。按 `F` 切换全屏，按 `Esc` 或 `Q` 退出；关闭摄像头会清屏。

## 注册人脸

**Register Face** 从暂停的摄像头帧或当前图片中选择面积最大的可用人脸，保存带少量周边留白的裁剪图到 `facedb/<姓名>.jpg`；下次启动从图片重新提取特征。姓名/ID 必须唯一且不含路径字符，不覆盖已存在的同名 JPEG；添加另一张样本时使用不同名称。

启动时加载 `facedb/` 下的底图，以文件名去扩展名作为姓名。底图应只包含一个人，OpenCV 画面标注建议使用英文姓名或 ID。请使用同一普通用户运行，确保工程目录可写，并保护、备份人脸图片。旧的 `facedb/registered/` 特征 JSON 仍兼容加载，但不再生成新 JSON；若只有旧特征记录，更换识别模型后应重新注册。

## 授权

`run.sh` 中的 `XXXX` 仅为占位符，不是有效授权码。请设置 `FACE_AUTH_CODE`，或在本地替换占位符；不要提交真实授权码。默认配置为 `FACE_MODULE=3`（RK3576）、`FACE_APP_ID=145`、`FACE_LICENSE_FILE=$ROOT/face_license.ini`，其中 `ROOT` 是工程目录。授权码、应用 ID 和 `FaceRecognition` 资源许可必须匹配。

检查启动 `[init]` 行是否同时满足 `alg_status=0`、`auth_status=0`。模型初始化或窗口打开不代表授权成功。后续启动优先验证有效的设备绑定本地授权件，因此复用成功不代表新提供的授权码可以在线激活。请保留授权文件，不要复制到其他设备。

## 报告问题

如在使用过程中遇到问题，欢迎在[移远官方论坛](https://forumschinese.quectel.com/c/quectel-pi/58) 提交技术咨询，我们的技术支持团队将及时为您解答。 欢迎提交Issue和Pull Request来改进此项目。