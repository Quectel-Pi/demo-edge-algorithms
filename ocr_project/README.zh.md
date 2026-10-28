# Quectel Pi M2 OCR 文字识别 Demo

[English](README.md) | 简体中文

基于瑞芯微 RK3576 NPU OCR SDK（DeepOCR）的 Qt5 桌面示例，运行于 Quectel Pi M2 开发板的 Debian aarch64 系统。支持本地图片与 USB 摄像头两种输入，显示文字检测框与识别文本，并可导出多种格式。

使用 C++17、Qt 5 Widgets 和 SDK 自带 OpenCV，在开发板本机编译，以普通用户 `pi` 运行。

## 功能特性

- 本地图片输入：PNG、JPEG、BMP 等 OpenCV 可解码格式。
- USB 摄像头输入：自动枚举具备采集能力的 USB 设备，或手动指定 `/dev/videoN`。
- 图片顺时针旋转 0 / 90 / 180 / 270 度，识别坐标对应旋转后的图像。
- 可调检测阈值（0.01–0.99，默认 0.20）。
- 多边形文字区域绘制，文字标签可开关。
- 结果表格、完整文本视图与一键复制。
- 导出标注图、推理原图、UTF-8 文本、结构化 JSON 和 LabelMe JSON。
- 状态栏显示初始化/授权状态、推理返回码、区域数与推理耗时。
- 采集与推理双线程：预览按摄像头帧率刷新，OCR 对最新帧异步进行，直播流畅不卡顿。
- 无窗口（headless）模式，便于脚本化与远程调用。

界面文本、表格和导出均来自 SDK 真实返回值。推理失败时清空结果、停止连续识别并禁用导出，不会把失败伪装成“无文字”。`rc=0` 且结果为空表示正常未检测到文字。

## 运行环境

| 项目 | 要求 |
| --- | --- |
| 开发板 | Quectel Pi M2 / RK3576 |
| 系统 | Debian GNU/Linux 13，aarch64 |
| 运行用户 | 普通用户 `pi`（UID 1000），属于 `video`、`render` 组 |
| 编译器 | 系统 g++，C++17 |
| 图形库 | Qt 5 Widgets（实测 5.15） |
| OpenCV | 使用 SDK 自带 4.2.0，禁止混入系统 OpenCV |
| NPU 运行时 | RKNN runtime 2.3.2 |
| 字体 | Noto CJK（中文显示） |

开发包（如缺失，由管理员安装）：

```sh
sudo apt-get update
sudo apt-get install -y build-essential pkg-config qtbase5-dev fonts-noto-cjk
```

不需要系统 `libopencv-dev`，工程只使用 SDK 自带的 `libopencv_world.so`。

## SDK 依赖

本工程依赖 OCR 算法 SDK 包 `RK_OCR_NPU_1.2`（DeepOCR），不包含 SDK 本体、模型和授权文件。`setup_from_sdk.sh` 会从整包中自动定位 RK3576 Linux 的 `deep_ocr` 目录：

```text
RK_OCR_NPU_1.2/
└── RK_OCR_NPU/RKNN-2.3.2-linux/rk3576/deep_ocr/   # 本工程使用的 RK3576 Linux 包
    ├── include/deep_ocr.h
    ├── lib/aarch64_Linux/                        # libdeep_ocr.so / libdeepocr.so
    ├── 3rd_party/{opencv_4.2.0,jsoncpp_1.9.5,license,rknn_2.3.2}/
    ├── models/sim.engine, model_ocr.json
    └── images/*.png
```

要点：

- 本工程使用 RK3576 **Linux** 包（`RKNN-2.3.2-linux/rk3576/deep_ocr`），`setup_from_sdk.sh` 自动定位，不要用 Android 或 rk3562 / rk3568 子目录。
- 库实际类名为 `DeepOcr`，模组参数 `module=3`。
- 工程调用 `init()` 加载 `models/sim.engine`。
- 禁止混用系统 OpenCV 与 SDK OpenCV，二者 ABI 不同。

## 项目结构

```text
ocr_project/
├── src/ocr_cam_demo.cpp    # SDK 封装、采集、工作线程、Qt 界面、导出
├── Makefile                # g++ / Qt5Widgets 构建
├── build.sh                # 构建入口
├── setup_from_sdk.sh       # 从 SDK 复制依赖
├── run.sh                  # 运行时环境入口
├── README.md               # 英文说明
└── README.zh.md            # 中文说明
```

`include/`、`lib/`、`models/`、`images/` 由 `setup_from_sdk.sh` 生成；`license/`、`outputs/` 在板端运行时生成。这些目录与编译产物均为生成/运行期产物，不应纳入版本库。

## 快速开始

本仓库不含 SDK 本体、模型与授权文件。先从算法供应商获取 RK3576 Linux OCR SDK 包 `deep_ocr`。

将整个 SDK 包与本工程上传到开发板（示例用 adb）：

```sh
adb push RK_OCR_NPU_1.2 /home/pi/RK_OCR_NPU_1.2
adb push ocr_project /home/pi/ocr_project
```

在 adb root shell 中把所有权交给普通用户 `pi`：

```sh
chown -R pi:pi /home/pi/RK_OCR_NPU_1.2 /home/pi/ocr_project
```

以 `pi` 用户编译并运行：

```sh
cd ~/ocr_project
sh setup_from_sdk.sh ~/RK_OCR_NPU_1.2      # 自动定位 RK3576 Linux deep_ocr 并复制依赖
sh build.sh                               # 本机编译
sh run.sh                                 # 启动图形界面
```

`setup_from_sdk.sh` 只复制依赖，不下载、不激活授权、不修改 SDK 源文件。默认 SDK 目录为 `$HOME/RK_OCR_NPU_1.2`（也可直接传入包内的 `deep_ocr` 目录）。

## 授权配置

RK3576 模组参数固定 `module=3`。授权码通过环境变量 `OCR_AUTH_CODE` 下发；`run.sh` 已 `export` 一个占位符 `REPLACE_WITH_ALGORITHM_AUTH_CODE`，集成方替换为自己的算法授权码即可。

工程源码不内嵌授权码。两种设置方式（任选其一）：

```sh
# 方式一：编辑 run.sh，将 REPLACE_WITH_ALGORITHM_AUTH_CODE 改为真实授权码
# 方式二：运行前用环境变量下发（不写入命令历史）
cd ~/ocr_project
read -r -s -p 'OCR authorization code: ' OCR_AUTH_CODE; printf '\n'
export OCR_AUTH_CODE
sh run.sh
```

- `OCR_LICENSE_FILE` 可覆盖许可证路径，默认 `license/ocr.lic`，首次运行由 SDK 生成。
- 初始化后应同时检查 `init()` 返回值、`get_status(0)`（模型）、`get_status(1)`（授权）。
- 首次授权需要网络、正确系统时间、匹配设备指纹和可写许可证路径。
- 不要提交 `license/`，不要公开 SDK 原始日志中的序列号/设备指纹/签名，不要复制其他设备的许可证。

## 图形界面

```sh
cd ~/ocr_project
sh run.sh
```

| 控件 | 行为 |
| --- | --- |
| Image | 选择图片并识别 |
| Camera | 用当前设备开始连续识别 |
| ■ 停止 | 停止采集并释放摄像头 |
| ⟳ 刷新 | 对当前来源重试，应用阈值/旋转设置 |
| Camera 下拉 | `auto` 自动枚举，或填 `/dev/videoN` |
| Detection threshold | 下次识别/刷新生效（会重建 SDK 实例） |
| Rotation | 下次识别/刷新应用的顺时针旋转 |
| Text labels | 开关图上文字标签 |
| Copy text | 复制全部识别文本 |
| Export | 导出当前成功帧的结果文件 |

只有授权正常且 `rc=0` 的完整帧才能导出；复制还需结果非空。摄像头采用双线程：采集线程按摄像头帧率持续刷新预览，推理线程对最新帧异步做 OCR 并叠加结果框，因此预览流畅，不随单次推理耗时卡顿；文字框按 OCR 完成节奏更新，可能比画面滞后一帧。

### 桌面与显示

`run.sh` 自动设置 `XDG_RUNTIME_DIR`，在 Qt Wayland 插件可用时使用 Wayland，否则回退 xcb/XWayland。无桌面环境（SSH/ADB）时使用 `--headless`。

## 命令行

```sh
sh run.sh --headless --help
sh run.sh --headless --image images/111.png
sh run.sh --headless --image images/temp.png --confidence 0.2 --rotate 90
sh run.sh --headless --camera auto --frames 3
sh run.sh --headless --camera /dev/video0 --output outputs/camera
sh run.sh --image images/111.png --quit-after 5 --screenshot outputs/gui.png
```

| 参数 | 说明 |
| --- | --- |
| `--headless` | 无窗口运行 |
| `--image <path>` | 输入图片（默认 `images/111.png`） |
| `--camera <device>` | USB 摄像头设备或 `auto` |
| `--confidence <0.01-0.99>` | 检测阈值 |
| `--rotate <0/90/180/270>` | 顺时针旋转 |
| `--frames <1-10000>` | headless 摄像头帧数 |
| `--repeat <1-10000>` | 图片重复次数 |
| `--output <path>` | 结果目录（默认 `outputs`） |
| `--quit-after <秒>` | GUI 自动关闭 |
| `--screenshot <path>` | 处理后保存界面截图 |
| `--self-test` | 用合成数据校验导出链路（不调用 SDK） |

`--image` 与 `--camera` 不能同时指定。耗时统计仅覆盖 `inference()` 调用。

进程退出码：

| 码 | 含义 |
| --- | --- |
| 0 | headless 成功，或 GUI 正常关闭 |
| 2 | 参数/图片/摄像头输入无效 |
| 3 | SDK 初始化或授权失败 |
| 4 | SDK 推理失败 |
| 5 | 导出失败 |
| 6 | 其他运行异常 |
| 130 | 被信号中断 |

## SDK 接口

```cpp
DeepOcr sdk;
sdk.set_param(confidence);                                  // 初始化前设置阈值
int rc = sdk.init(model, license, 3, auth);                 // module=3
int model_status = sdk.get_status(0);                       // 0 = 模型就绪
int auth_status  = sdk.get_status(1);                       // 0 = 授权通过
std::vector<OCR_Rec_Result> results;
rc = sdk.inference(bgr.data, bgr.rows, bgr.cols, bgr.channels(), results);
sdk.uinit();
```

输入与生命周期：

- 输入为连续内存的三通道 BGR（必要时 `clone()`），高度传 `rows`、宽度传 `cols`。
- 图像内存在 SDK 调用期间须保持有效。
- 工作线程串行持有 SDK，界面线程不并发调用。
- 阈值通过初始化生效，工程改变阈值时重建实例。

输出结构：

```cpp
typedef struct Ocr_Rec_Result {
    std::vector<std::vector<int>> box;   // 多边形点坐标
    std::string text;                    // UTF-8 文本
} OCR_Rec_Result;
```

SDK 返回码：

| 值 | 常量 |
| --- | --- |
| 0 | OCR_PROCESS_OK |
| -1 | OCR_INIT_ERROR |
| -2 | OCR_UNINIT_ERROR |
| -3 | OCR_ERR_INFERENCE_ERROR |
| -4 | OCR_ERR_IMAGE_EMPTY |
| -5 | OCR_ERR_JSON_NULL |
| -6 | OCR_ERR_JSON_PARSE |

SDK 返回码与进程退出码是两套编号。授权问题读 `get_status(1)`，不要把 `-3` 直接当成授权失效。

## 导出格式

一次成功导出生成同前缀五个文件：

```text
ocr_YYYYMMDD_HHMMSS_mmm_N.png            # 多边形与文字标注图
ocr_YYYYMMDD_HHMMSS_mmm_N_source.png     # 实际送入 SDK 的（旋转后）原图
ocr_YYYYMMDD_HHMMSS_mmm_N.txt            # UTF-8 文本，按结果顺序换行
ocr_YYYYMMDD_HHMMSS_mmm_N.json           # 结构化结果
ocr_YYYYMMDD_HHMMSS_mmm_N.labelme.json   # LabelMe 多边形标注
```

结构化 JSON 字段：

```json
{
  "source": "images/example.png",
  "result_code": 0,
  "auth_status": 0,
  "width": 640,
  "height": 480,
  "rotation": 0,
  "detection_threshold": 0.2,
  "inference_ms": 0.0,
  "results": [
    {"index": 1, "text": "示例文本", "box": [[20,30],[220,30],[220,80],[20,80]]}
  ]
}
```

LabelMe 的 `imagePath` 指向 `_source.png`，坐标与旋转后原图一致。写入失败会清理本次不完整文件组。

## 能力边界

- SDK 返回文本与区域坐标，不含逐条置信度；界面阈值是检测参数。
- 文本按 SDK 返回顺序给出，不保证自然阅读顺序。
- 不提供翻译、表格结构还原、手写识别保证或二维码解码；样图中的二维码图案不代表具备扫码能力。
- 识别精度与长期稳定性取决于 SDK 模型，需按实际场景评估。

## 常见问题

| 现象 | 排查 |
| --- | --- |
| 找不到 `.so` | 用 `run.sh` 启动，重跑 `setup_from_sdk.sh`，用 `ldd` 检查 |
| `inference` 返回 -3 | 先 `ldd` 确认只加载 SDK `libopencv_world.so`，禁止系统 OpenCV 混入 |
| `wayland` 插件缺失 | run.sh 自动回退 xcb，确认 pi 桌面已登录 |
| 无法连接显示 | 核对运行用户、`XDG_RUNTIME_DIR`、`DISPLAY`，或用 `--headless` |
| 没有摄像头 | 检查 USB 连接与 pi 的 video 权限，手动填可采集节点 |
| 选到板载 ISP 节点 | 不要固定 `/dev/video0`，`auto` 只选 USB 采集设备 |
| `auth` 非零 | 检查授权码、许可证、设备指纹、网络与时间 |
| 中文方框 | 安装 Noto CJK 字体 |

## 报告问题

如在使用过程中遇到问题，欢迎在[移远官方论坛](https://forumschinese.quectel.com/c/quectel-pi/58) 提交技术咨询，我们的技术支持团队将及时为您解答。 欢迎提交 Issue 和 Pull Request 来改进此项目。
