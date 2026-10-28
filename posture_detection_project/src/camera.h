#pragma once
/* camera.h — USB 摄像头采集模块
 *
 * 职责：在独立线程循环打开 /dev/videoN 取帧，并广播给上层。
 *  - BGR 原始帧（供算法分析）
 *  - 缩放后的预览帧（供界面显示）
 * 抽象出该模块后，界面与算法都不关心具体采集设备。
 */
#include <QImage>
#include <QObject>
#include <opencv2/core.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

/* 枚举所有可用摄像头设备号（可实际打开并读到帧），已排序，USB 靠前；无则空。
 * 用于界面"摄像头选择"下拉框列出全部候选。 */
std::vector<int> listAvailableCameras();

/* 跨线程"最新帧缓存"：取帧线程只覆盖写入最新一帧，算法线程按需拉取当前最新帧。
 * 采用"只保留最新"模式，杜绝逐帧排队造成的积压，保证算法看到的始终是最新姿态。 */
class FrameHub {
public:
    /* 写入最新一帧（覆盖旧帧） */
    void publish(std::shared_ptr<cv::Mat> frame) {
        std::lock_guard<std::mutex> lock(mtx_);
        latest_ = std::move(frame);
    }
    /* 取出当前最新一帧；尚无帧时返回空 */
    std::shared_ptr<cv::Mat> takeLatest() {
        std::lock_guard<std::mutex> lock(mtx_);
        return latest_;
    }

private:
    std::mutex mtx_;
    std::shared_ptr<cv::Mat> latest_;
};

class CameraWorker : public QObject {
    Q_OBJECT
public:
    /* hub 由上层(界面)创建，与算法模块共享同一帧缓存 */
    explicit CameraWorker(int deviceIndex, std::shared_ptr<FrameHub> hub,
                          QObject* parent = nullptr);
    ~CameraWorker() override;

    void setDeviceIndex(int index);
    int deviceIndex() const;
    bool isRunning() const;

public slots:
    void start();     // 开始采集（自起线程）
    void stop();      // 停止并回收线程

signals:
    /* 已缩放并转为 RGB 的预览帧 */
    void previewReady(const QImage& image);
    void captureError(const QString& message);
    /* 成功打开并开始取帧的设备号（自动检索时用于提示） */
    void deviceOpened(int index);

private:
    void runLoop();

    int  deviceIndex_;
    std::shared_ptr<FrameHub> hub_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};