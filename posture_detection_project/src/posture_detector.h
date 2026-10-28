#pragma once
/* posture_detector.h — 姿态检测算法封装模块
 *
 * 使用 PIMPL 隐藏算法 SDK(private/PeopleKptDet)细节，并提供与 SDK 解耦的
 * 轻量结果类型 PostureFrameResult。上层(界面)只依赖本文件定义的类型，不 include SDK。
 */
#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <opencv2/core.hpp>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "camera.h"   /* FrameHub：算法线程通过它拉取"最新帧"，不排队 */

struct PostureBox {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float score = 0.f;
};

struct PostureKeyPoint {
    float x = 0.f, y = 0.f, score = 0.f;
};

/* 与具体算法解耦的单帧检测结果 */
struct PostureFrameResult {
    bool has_person = false;
    int  pose = -1;                        /* 0正常 1托腮 2低头/趴桌 3高低肩 4ERROR */
    bool is_side_face = false;             /* 是否侧脸 */
    float confidence = 0.f;                /* 置信度(取最高检测框分数, 0~1) */
    QString pose_text;                     /* 姿态文本，供界面直接显示 */
    QString detail;                        /* 附加信息（人数等） */
    std::vector<PostureBox> boxes;         /* 检测框（坐标已映射到原图） */
    std::vector<std::vector<PostureKeyPoint>> keypoints; /* 每人 17 关键点 */
};

Q_DECLARE_METATYPE(PostureFrameResult)

/* 与具体算法解耦、可在运行时通过"高级设置"调整的识别参数。
 * 纯工程层结构，不在界面引入任何 SDK 类型。 */
struct AlgorithmConfig {
    float detConfTh  = 0.5f;   /* 检测置信度阈值：低于此分的检测框(及对应关键点)丢弃 */
    float kptConfTh  = 0.3f;   /* 关键点置信度阈值：低于此分的关键点不输出/不绘制 */
    bool  onlyMaxBox = true;   /* 单目标(true)/多目标(false)，传给 SDK inference 参数 */
    int   inferFps   = 15;     /* 推理限频(帧/秒)，用于推导两次推理最小间隔 */

    static AlgorithmConfig defaults() { return AlgorithmConfig{}; }
    int inferIntervalMs() const;   /* 由 inferFps 推导的最小推理间隔(毫秒) */
};

class PostureDetectorImpl;

class PostureDetector : public QObject {
    Q_OBJECT
public:
    /* hub 与摄像头共享同一帧缓存；算法线程自管理，无需外部 moveToThread */
    explicit PostureDetector(std::shared_ptr<FrameHub> hub, QObject* parent = nullptr);
    ~PostureDetector() override;

    void start();                /* 自起算法线程 */
    void shutdown();             /* 停线程并释放算法资源 */
    void initialize(QString model, QString license, QString auth, int module = 3);
    void setDetectionEnabled(bool on);

    /* 运行时替换识别参数（线程安全：用快照 + 加锁原子替换） */
    void setConfig(std::shared_ptr<const AlgorithmConfig> cfg);
    /* 返回当前参数快照（GUI 打开"高级设置"对话框时读取） */
    std::shared_ptr<const AlgorithmConfig> config() const;

    bool isInitialized() const;
    bool isDetectionEnabled() const;
    QString lastError() const;

signals:
    void initializedChanged(bool ok, const QString& info);
    void detectionEnabledChanged(bool on);
    /* 推理成功：仅携带轻量结果结构体（坐标在"原始帧"空间）。
     * 不再在算法线程里绘制大图/转 QImage，省 CPU；标注由界面线程叠加到实时预览上。 */
    void resultReady(const PostureFrameResult& result);
    void detectorError(const QString& message);

private:
    void runLoop();
    void doInitialize();
    void runInference(const cv::Mat& bgr);

    PostureDetectorImpl* impl_;
    std::shared_ptr<FrameHub> hub_;

    std::atomic<bool> running_{false};
    std::atomic<bool> enabled_{false};
    std::thread thread_;

    std::mutex mtx_;
    std::condition_variable cv_;
    bool initRequested_ = false;    /* 收到初始化请求 */
    bool initDone_ = false;         /* 初始化已完成 */
    QString initModel_, initLicense_, initAuth_;
    int  initModule_ = 3;

    /* 识别参数快照：GUI 线程写入、算法线程读取，用 cfgMtx_ 保护 */
    mutable std::mutex cfgMtx_;
    std::shared_ptr<const AlgorithmConfig> cfg_;

    const cv::Mat* lastProcessed_ = nullptr; /* 仅算法线程访问：最近一次已处理的帧 */

    QString lastError_;
};