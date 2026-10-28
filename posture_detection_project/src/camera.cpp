#include "camera.h"

#include <QThread>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>

namespace {
/* 尝试打开设备并抓一帧确认是可用摄像头；成功返回 true。
 * 注意：OpenCV 的整数索引并不等于 /dev/videoN 的 N（它会枚举并跳过非 capture 节点，
 * 造成索引与设备号错位），因此这里必须用设备路径字符串 "/dev/videoN" 精确打开。 */
bool tryOpenCamera(cv::VideoCapture& cap, int i) {
    const std::string dev = "/dev/video" + std::to_string(i);
    if (!cap.open(dev, cv::CAP_V4L2) || !cap.isOpened()) return false;
    cv::Mat probe;
    if (!cap.read(probe) || probe.empty()) {   // 能取到帧才算可用，跳过元数据节点
        cap.release();
        return false;
    }
    return true;
}

/* 枚举 /sys/class/video4linux/videoN/name 中名字含 "USB"/"UVC" 的节点号。
 * 真正的 USB 摄像头是 UVC 设备（如 /dev/video73），而 Rockchip ISP 节点
 * （rkisp_mainpath / rkcif-mipi-lvds / rkvpss_scale0）名字不同，且是 Multiplanar，
 * OpenCV V4L2 无法直接读。以 /sys name 排查能稳定跳过 ISP 节点。 */
std::vector<int> usbCameraIndices() {
    std::vector<int> out;
    for (int i = 0; i < 128; ++i) {
        std::ifstream f("/sys/class/video4linux/video" + std::to_string(i) + "/name");
        std::string name;
        if (!(f && std::getline(f, name))) continue;
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name.find("usb") != std::string::npos ||
            name.find("uvc") != std::string::npos)
            out.push_back(i);
    }
    return out;
}

/* 自动检索可用摄像头：
 *   1) 优先尝试 USB/UVC 节点（/sys name 识别），逐个路径打开并抓帧确认；
 *   2) 兜底全量扫描（跳过 rkisp/rkcif/rkvpss 的 33..72 节点区间——这些是
 *      Multiplanar ISP 节点，OpenCV 打开再 read 会直接段错误，必须跳过）。 */
int scanFirstCamera(int maxIndex = 127) {
    for (const int idx : usbCameraIndices()) {
        cv::VideoCapture cap;
        if (tryOpenCamera(cap, idx)) {
            std::fprintf(stderr, "[cam] usb 检索命中 /dev/video%d\n", idx);
            return idx;
        }
    }
    for (int i = 0; i <= maxIndex; ++i) {
        if (i >= 33 && i <= 72) continue;   /* 跳过 ISP 节点区间，防崩溃 */
        cv::VideoCapture cap;
        if (tryOpenCamera(cap, i)) {
            std::fprintf(stderr, "[cam] 全量检索命中 /dev/video%d\n", i);
            return i;
        }
    }
    return -1;
}

/* 预览帧固定为主边 640px(按纵横比缩放)，BGR→RGB 后包成 QImage */
QImage matToQImage(const cv::Mat& src) {
    if (src.empty()) return {};
    cv::Mat rgb;
    const double scale = 640.0 / static_cast<double>(src.cols);
    cv::resize(src, rgb,
               cv::Size(640, static_cast<int>(std::lround(src.rows * scale))),
               0, 0, cv::INTER_LINEAR);
    if (rgb.channels() == 3)
        cv::cvtColor(rgb, rgb, cv::COLOR_BGR2RGB);
    else if (rgb.channels() == 1)
        cv::cvtColor(rgb, rgb, cv::COLOR_GRAY2RGB);
    /* .copy(): 深拷贝，跨线程传递安全 */
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}
}  // namespace

/* 返回所有可用摄像头设备号（供界面下拉框）。放在匿名命名空间之外，全局可见，
 * 才能被 main_window.cpp 通过头文件声明链接到。USB 优先且保序，再补全量扫描，
 * 逐个实测可读帧才纳入，去重。 */
std::vector<int> listAvailableCameras() {
    std::vector<int> out;
    auto appendIfOpenable = [&out](int idx) {
        cv::VideoCapture cap;
        if (tryOpenCamera(cap, idx)) out.push_back(idx);
    };

    for (const int idx : usbCameraIndices()) appendIfOpenable(idx);
    for (int i = 0; i <= 127; ++i) {
        if (i >= 33 && i <= 72) continue;          /* 跳过 ISP 节点区间 */
        if (std::find(out.begin(), out.end(), i) != out.end()) continue; /* 去重 */
        appendIfOpenable(i);
    }
    std::fprintf(stderr, "[cam] listAvailableCameras found %zu:",
                 static_cast<size_t>(out.size()));
    for (const int idx : out) std::fprintf(stderr, " %d", idx);
    std::fprintf(stderr, "\n");
    return out;
}

CameraWorker::CameraWorker(int deviceIndex, std::shared_ptr<FrameHub> hub, QObject* parent)
    : QObject(parent), deviceIndex_(deviceIndex), hub_(std::move(hub)) {}

CameraWorker::~CameraWorker() { stop(); }

void CameraWorker::setDeviceIndex(int index) { deviceIndex_ = index; }
int  CameraWorker::deviceIndex() const { return deviceIndex_; }
bool CameraWorker::isRunning() const { return running_.load(); }

void CameraWorker::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void CameraWorker::start() {
    if (thread_.joinable()) return;  // 已在运行
    running_.store(true);
    thread_ = std::thread([this] { runLoop(); });
}

void CameraWorker::runLoop() {
    cv::VideoCapture cap;
    bool haveCap = false;

    while (running_.load()) {
        if (!haveCap) {
            /* deviceIndex_ >= 0 用指定设备号；< 0 表示自动检索第一个可用摄像头 */
            if (deviceIndex_ >= 0) {
                /* 用设备路径打开，避免整数索引与 /dev/videoN 设备号错位 */
                if (tryOpenCamera(cap, deviceIndex_)) {
                    haveCap = true;
                    emit deviceOpened(deviceIndex_);
                } else {
                    std::fprintf(stderr, "[cam] 显式设备 /dev/video%d 打开失败\n", deviceIndex_);
                }
            } else {
                const int found = scanFirstCamera();
                if (found >= 0) {
                    haveCap = tryOpenCamera(cap, found);
                    if (haveCap) emit deviceOpened(found);
                }
            }

            if (!haveCap) {
                emit captureError(QStringLiteral("No camera available (auto-scan failed)"));
                QThread::msleep(1000);
                continue;
            }
            cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        }

        cv::Mat frame;                    // 每次迭代全新缓冲
        if (!cap.read(frame) || frame.empty()) {
            emit captureError(QStringLiteral("Failed to read camera frame"));
            QThread::msleep(50);
            continue;
        }

        /* BGR 帧：将单帧缓冲拷到独立内存再发布，避免下一轮 cap.read() 重用
         * 同一缓冲时，把算法线程仍在引用的帧覆写（跨线程共享 buffer 的数据竞争）。 */
        auto bgr = std::make_shared<cv::Mat>(frame.clone());
        if (hub_) hub_->publish(bgr);

        /* 裸预览始终发送，跑满摄像头帧率（30fps） */
        emit previewReady(matToQImage(frame));
    }
}