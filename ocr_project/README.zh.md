# OCR 文字识别 Demo

面向 Quectel Pi M2（RK3576、Debian aarch64）的 Qt5 桌面 Demo，调用 OCR SDK（DeepOCR）完成文字检测和识别。支持 USB 摄像头实时识别与本地图片识别，可导出标注图、TXT、JSON 和 LabelMe JSON。

![OCR 文字识别效果](assets/ocr.png)

## 环境要求

- RK3576 Debian aarch64，带桌面会话
- C++17、Make、pkg-config、Qt5 Widgets 开发包、Noto CJK 字体
- RK3576 OCR SDK 包 `RK_OCR_NPU_1.2`
- USB 摄像头（仅实时识别需要）

SDK、模型及专有运行库不包含在本仓库中，请通过授权渠道获取。不要将 SDK 二进制、模型、设备授权文件或授权码提交到公开仓库。

## 编译

将 SDK 包放在设备用户目录 `$HOME/RK_OCR_NPU_1.2`，然后在设备桌面终端执行：

```bash
cd "$HOME/ocr_project"
sh setup_from_sdk.sh "$HOME/RK_OCR_NPU_1.2"
sh build.sh
```

`setup_from_sdk.sh` 会自动定位 SDK 中 RK3576 Linux 的 `deep_ocr` 目录，并复制头文件、运行库、模型和示例图片。程序使用 SDK 自带的 OpenCV，不要混用系统 OpenCV。

缺少编译依赖时安装：

```bash
sudo apt update
sudo apt install -y build-essential pkg-config qtbase5-dev fonts-noto-cjk
```

## 运行

在设备桌面会话启动：

```bash
cd "$HOME/ocr_project"
./run.sh                                     # 启动界面
./run.sh --camera /dev/videoN                # 指定摄像头节点
./run.sh --headless --image images/111.png   # 无窗口识别图片
./run.sh --headless --help                   # 查看全部参数
```

界面按钮为 **Image**、**Camera**、**Copy text** 和 **Export**，检测阈值与旋转角度可在界面中调整。程序自动扫描 USB 视频采集节点，不假设摄像头是 `/dev/video0`。结果默认导出到 `outputs/`。

## 授权

请通过环境变量 `OCR_AUTH_CODE` 提供有效授权码，或将 `run.sh` 中的 `REPLACE_WITH_ALGORITHM_AUTH_CODE` 替换为有效码，该值只是占位符，不能用于授权。脚本将 `OCR_LICENSE_FILE` 指向工程目录下的 `license/ocr.lic`，首次授权成功后由 SDK 生成；请保留该设备绑定文件，不要复制到其他设备或提交到仓库。首次授权需要网络和正确的系统时间。

## 报告问题

如在使用过程中遇到问题，欢迎在[移远官方论坛](https://forumschinese.quectel.com/c/quectel-pi/58) 提交技术咨询，我们的技术支持团队将及时为您解答。 欢迎提交Issue和Pull Request来改进此项目。
