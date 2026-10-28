#include "posture_detector.h"

/* 算法 SDK 公开头（唯一一处 include；若换算法包，只改本文件即可） */
#include "PeopleKptDet.h"

#include <QFile>
#include <QImage>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <utility>

struct PostureDetectorImpl {
    PeopleKptDet kpt;
    bool ok = false;
};

namespace {

/* 姿态枚举 → 英文文案（与 SDK 枚举对齐，便于界面直接显示） */
QString poseText(int pose) {
    switch (pose) {
        case NORMAL:                    return QStringLiteral("Normal");
        case CHEEK_SUPPORT:             return QStringLiteral("Cheek Support");
        case HEAD_BOWING_BEND_ON_DESK:  return QStringLiteral("Head Bowing");
        case UNBALANCED_SHOULDER:       return QStringLiteral("Unbalanced Shoulder");
        case ERROR:                     return QStringLiteral("Detection Error");
        default:                        return QStringLiteral("Unknown");
    }
}

}  // namespace

PostureDetector::PostureDetector(std::shared_ptr<FrameHub> hub, QObject* parent)
    : QObject(parent), hub_(std::move(hub)), impl_(new PostureDetectorImpl) {
    /* 初始参数用默认值快照 */
    cfg_ = std::make_shared<const AlgorithmConfig>(AlgorithmConfig::defaults());
}

int AlgorithmConfig::inferIntervalMs() const {
    const int fps = std::max(1, std::min(60, inferFps));
    return std::max(1, 1000 / fps);
}

void PostureDetector::setConfig(std::shared_ptr<const AlgorithmConfig> cfg) {
    if (!cfg) return;
    {
        std::lock_guard<std::mutex> lock(cfgMtx_);
        cfg_ = std::move(cfg);
    }
    cv_.notify_all();   /* 唤醒算法线程，及时按新参数工作 */
}

std::shared_ptr<const AlgorithmConfig> PostureDetector::config() const {
    std::lock_guard<std::mutex> lock(cfgMtx_);
    return cfg_;
}

PostureDetector::~PostureDetector() {
    shutdown();
    if (impl_) delete impl_;
}

bool PostureDetector::isInitialized() const { return impl_->ok; }
bool PostureDetector::isDetectionEnabled() const { return enabled_.load(); }
QString PostureDetector::lastError() const { return lastError_; }

/* 提交初始化请求；真正的 init 在 worker 线程执行，不阻塞调用方 */
void PostureDetector::initialize(QString model, QString license, QString auth, int module) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        initModel_   = std::move(model);
        initLicense_ = std::move(license);
        initAuth_    = std::move(auth);
        initModule_  = module;
        initRequested_ = true;
    }
    cv_.notify_all();
}

void PostureDetector::setDetectionEnabled(bool on) {
    enabled_.store(on);
    cv_.notify_all();
    emit detectionEnabledChanged(on);
}

void PostureDetector::start() {
    if (thread_.joinable()) return;   /* 已在运行 */
    running_.store(true);
    thread_ = std::thread([this] { runLoop(); });
}

void PostureDetector::shutdown() {
    enabled_.store(false);
    running_.store(false);
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void PostureDetector::runLoop() {
    /* 推理限频：两次推理间隔至少 minInferInterval(fps 由"高级设置"可调) */
    auto minInferInterval = std::chrono::milliseconds(
        config() ? config()->inferIntervalMs() : 66);
    auto lastInfer = std::chrono::steady_clock::now() - minInferInterval;

    while (running_.load()) {
        std::shared_ptr<cv::Mat> latest;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            /* 醒来条件：退出 / 收到初始化请求；否则最多等 20ms 后轮询最新帧 */
            cv_.wait_for(lock, std::chrono::milliseconds(20),
                         [this] { return !running_.load() || initRequested_; });
            if (!running_.load()) break;

            if (initRequested_) {
                doInitialize();
                initRequested_ = false;
                initDone_ = true;
            }
            if (!enabled_.load() || !impl_->ok) continue;   /* 未开启/未就绪：本轮不推理 */
            latest = hub_ ? hub_->takeLatest() : nullptr;
        }

        /* 每次醒来同步该时刻的参数快照，保证帧率/阈值及时生效 */
        if (auto cfg = config()) {
            const auto cfgInterval = std::chrono::milliseconds(cfg->inferIntervalMs());
            if (cfgInterval != minInferInterval) {
                minInferInterval = cfgInterval;
                lastInfer -= cfgInterval;   /* 变大时提前到点，给后续推理留出窗口 */
            }
        }

        /* 只处理"新一帧"；帧未更新则跳过，正好做到"按推理节奏取最新帧"。
         * 受限于限频，抽到的新帧若未到推理窗口，本轮直接跳过，下轮再取最新帧。 */
        if (latest && latest.get() != lastProcessed_) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastInfer >= minInferInterval) {
                lastInfer = now;
                lastProcessed_ = latest.get();
                runInference(*latest);
            }
        }
    }

    /* 线程退出前释放算法资源 */
    if (impl_ && impl_->ok) {
        impl_->kpt.uninit_model();
        impl_->ok = false;
    }
}

void PostureDetector::doInitialize() {
    lastError_.clear();
    if (impl_->ok) { emit initializedChanged(true, QStringLiteral("ready")); return; }

    /* 让 OpenCV 出错时抛 cv::Exception 而非默认 abort()，这样 runInference 的
     * try/catch 能兜住单个坏帧，保证 demo 不整体退出。 */
    cv::redirectError([](int status, const char* func_name, const char* err_msg,
                         const char* file_name, int line, void* /*userdata*/) -> int {
        throw cv::Exception(status, err_msg, func_name, file_name, line);
    });

    /* init 签名带非 const char*；QByteArray.data() 提供可写缓冲，生命周期覆盖整个同步调用 */
    const QByteArray m = QFile::encodeName(initModel_);
    const QByteArray l = initLicense_.isEmpty() ? QByteArray() : QFile::encodeName(initLicense_);
    const QByteArray a = initAuth_.toUtf8();

    const PEOPLE_POSTURE_RES_CODE rc = impl_->kpt.init(
        const_cast<char*>(m.constData()),
        l.isEmpty() ? nullptr : const_cast<char*>(l.constData()),
        initModule_,
        a.isEmpty() ? nullptr : const_cast<char*>(a.constData()));

    const int statusInit = impl_->kpt.get_status(0);
    const int statusAuth = impl_->kpt.get_status(1);
    std::fprintf(stderr, "[det] init rc=%d status(init=%d, auth=%d)\n",
                 static_cast<int>(rc), statusInit, statusAuth);

    /* 根据 init/auth 两个状态码判断整体成功或失败：两者都为 0 才算成功，任一非 0 即失败。
     * 界面算法状态栏据此显示单个英文单词，不再暴露裸数字。 */
    auto statusWord = [](int i, int a) -> QString {
        return (i == 0 && a == 0) ? QStringLiteral("Success") : QStringLiteral("Fail");
    };

    if (rc == PEOPLE_POSTURE_PROCESS_OK) {
        impl_->ok = true;
        emit initializedChanged(true, QStringLiteral("algorithm initialized: %1")
                                     .arg(statusWord(statusInit, statusAuth)));
    } else {
        lastError_ = QStringLiteral("algorithm init failed ret=%1: %2")
                         .arg(static_cast<int>(rc))
                         .arg(statusWord(statusInit, statusAuth));
        emit initializedChanged(false, lastError_);
    }
}

void PostureDetector::runInference(const cv::Mat& bgr) {
    if (bgr.empty()) return;

    try {
        /* 记录原始帧尺寸：推理在 640x640 上做，结果坐标由本函数统一映射回
         * "原始帧"空间（与摄像头实时预览同坐标系），方便界面直接叠加标注。 */
        const int origW = bgr.cols, origH = bgr.rows;

        /* letterbox：按比例缩放到适配 640x640，保持人体长宽比不变
         * （避免把 4:3 直接拉伸成正方形导致形变、降低识别率），缺失区域补黑边。 */
        const double scale = std::min(640.0 / origW, 640.0 / origH);
        const int cw = static_cast<int>(std::lround(origW * scale));
        const int ch = static_cast<int>(std::lround(origH * scale));
        const int xoff = (640 - cw) / 2;
        const int yoff = (640 - ch) / 2;

        cv::Mat content;
        cv::resize(bgr, content, cv::Size(cw, ch), 0, 0, cv::INTER_LINEAR);
        cv::Mat square;
        cv::copyMakeBorder(content, square, yoff, 640 - ch - yoff,
                           xoff, 640 - cw - xoff,
                           cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        cv::Mat* src = &square;

        cv::Mat continuous;
        if (!src->isContinuous()) {
            continuous = src->clone();
            src = &continuous;
        }

        /* 读取当前参数快照：单/多目标、两个置信度阈值均在"高级设置"中可调 */
        const auto cfg = config() ? config() : nullptr;
        const bool onlyMaxBox = cfg ? cfg->onlyMaxBox : true;
        const float detTh = cfg ? cfg->detConfTh : 0.5f;
        const float kptTh = cfg ? cfg->kptConfTh : 0.3f;

        PoseFrameResult sdk;
        const PEOPLE_POSTURE_RES_CODE rc =
            impl_->kpt.inference(src->data, src->rows, src->cols,
                                 src->channels(), sdk, onlyMaxBox);

        PostureFrameResult r;
        if (rc == PEOPLE_POSTURE_PROCESS_OK) {
            const bool aligned = sdk.kpt_landmarks.size() == sdk.det_landmarks.size();
            if (!aligned) r.keypoints.clear();

            /* 按检测置信度阈值过滤：框与对应关键点"整组"取舍，保持两者索引对齐 */
            r.boxes.reserve(sdk.det_landmarks.size());
            for (size_t i = 0; i < sdk.det_landmarks.size(); ++i) {
                const DetInf& b = sdk.det_landmarks[i];
                if (b.score < detTh) continue;   /* 低于阈值的检测丢弃 */

                PostureBox box;
                box.x1 = static_cast<int>((b.x1 - xoff) / scale);
                box.y1 = static_cast<int>((b.y1 - yoff) / scale);
                box.x2 = static_cast<int>((b.x2 - xoff) / scale);
                box.y2 = static_cast<int>((b.y2 - yoff) / scale);
                box.score = b.score;
                r.boxes.push_back(box);

                if (aligned) {
                    std::vector<PostureKeyPoint> kps;
                    kps.reserve(sdk.kpt_landmarks[i].size());
                    for (const KptInf& k : sdk.kpt_landmarks[i]) {
                        if (k.score < kptTh) continue;   /* 关键点置信度阈值过滤 */
                        PostureKeyPoint kp;
                        kp.x     = (k.x - xoff) / static_cast<float>(scale);
                        kp.y     = (k.y - yoff) / static_cast<float>(scale);
                        kp.score = k.score;
                        kps.push_back(kp);
                    }
                    r.keypoints.push_back(std::move(kps));
                }
            }

            r.has_person  = !r.boxes.empty();
            r.pose        = static_cast<int>(sdk.pose_result.pose);
            r.is_side_face = sdk.pose_result.is_side_face;
            r.pose_text   = poseText(r.pose);
            r.detail      = QStringLiteral("%1 person(s) detected")
                                .arg(static_cast<int>(r.boxes.size()));
        } else {
            /* 无人/推理失败：仍发出空结果，让界面更新为"No person detected"，
             * 避免人离开镜头后识别信息停留在上一帧。 */
            std::fprintf(stderr, "[det] inference rc=%d (no person)\n", static_cast<int>(rc));
            r.has_person = false;
            r.pose       = ERROR;
            r.confidence = 0.f;
            r.pose_text  = QStringLiteral("No person detected");
            r.detail     = QStringLiteral("None");
        }

        /* 置信度：取最高检测框分数（0~1） */
        for (const PostureBox& b : r.boxes)
            if (b.score > r.confidence) r.confidence = b.score;

        /* 只发轻量结果；不绘制大图、不转 QImage，算法线程尽快返回 */
        emit resultReady(r);
    } catch (const cv::Exception& e) {
        /* OpenCV 分配失败(如垃圾尺寸)若不捕获会直接 terminate 整个进程。
         * 这里只跳过本帧，demo 不退出，并把错误透出给界面。 */
        std::fprintf(stderr, "[det] cv::Exception: %s\n", e.what());
        lastError_ = QString::fromLocal8Bit(e.what());
        emit detectorError(lastError_);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[det] std::exception: %s\n", e.what());
        lastError_ = QString::fromLocal8Bit(e.what());
        emit detectorError(lastError_);
    }
}