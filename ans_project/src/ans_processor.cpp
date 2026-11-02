#include "ans_processor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>

/* ---- 算法 SDK 头：仅在本地接入 ---- */
#include "aivox_aec_ans.h"
#include "aivox_slas.h"

namespace {
using Clock = std::chrono::steady_clock;
}

/* ======================= AnsProcessor ======================= */

class AnsProcessorImpl {
public:
    void* pObj = nullptr;
    bool  initOk = false;
    QString ver;
};

AnsProcessor::AnsProcessor() : impl_(new AnsProcessorImpl) {}
AnsProcessor::~AnsProcessor() { cleanup(); }

QString AnsProcessor::initialize(const QString& model, const QString& licenseDir,
                                 const QString& authCode, int module) {
    if (impl_->pObj) cleanup();

    const QByteArray dirBuf   = licenseDir.isEmpty() ? QByteArray("/data")
                                                     : licenseDir.toUtf8();
    const QByteArray authBuf  = authCode.toUtf8();
    const QByteArray modelBuf = model.toUtf8();

    /* 1) 授权 SLAS */
    int err = aivox_slas_set_license_path_dir(const_cast<char*>(dirBuf.constData()));
    if (err != 0) return QStringLiteral("SLAS set_license_path_dir failed: %1").arg(err);


    err = aivox_slas_set_dev(0, module, 3, nullptr);
    if (err != 0) return QStringLiteral("SLAS set_dev failed: %1").arg(err);

    if (!authCode.isEmpty())
        aivox_slas_func_http_request_license(nullptr, nullptr, 0,
                                             const_cast<char*>(authBuf.constData()));
    int verifyErr = aivox_slas_func_licenses_verify(nullptr, nullptr, 0);
    if (verifyErr != 0)
        return QStringLiteral("SLAS license verify failed: %1").arg(verifyErr);

    /* 2) 创建引擎：只用降噪模型（model 为空串/NULL 时走 SDK 内置默认） */
    char* modelPath = model.isEmpty() ? nullptr : const_cast<char*>(modelBuf.constData());
    impl_->pObj = aivox_aec_ans_init(nullptr, nullptr, modelPath);
    if (!impl_->pObj)
        return QStringLiteral("aivox_aec_ans_init returned NULL (engine create failed)");

    char version[128] = {0};
    aivox_aec_ans_get_version(version);
    impl_->ver = QString::fromUtf8(version);

    /* 参数对齐 demo 的 denoise_aec_ans_ans（纯 ANS 示例）：set_agc(0) + set_agc_config(-3,1)。
     * 纯 ANS 管线下 AGC 不参与运算，显式关闭可避免音量被自动抬升；
     * 调用返回非 0（该管线不支持 AGC）时忽略即可。 */
    (void)aivox_aec_ans_set_agc(impl_->pObj, 0);
    (void)aivox_aec_ans_set_agc_config(impl_->pObj, -3, 1);

    impl_->initOk = true;
    return QString();
}

bool AnsProcessor::isInitialized() const { return impl_->initOk; }

QString AnsProcessor::process(const int16_t* in, int16_t* out) {
    if (!impl_->pObj || !in || !out)
        return QStringLiteral("invalid processor/args");

    /* 纯降噪：单麦无参考信号，SDK 允许参考传 NULL */
    const int ret = aivox_aec_ans_process_ANS(impl_->pObj, const_cast<int16_t*>(in), nullptr, out);
    return ret == 0 ? QString() : QStringLiteral("aivox_aec_ans_process_ANS ret=%1").arg(ret);
}

void AnsProcessor::setDenoiseStrength(float value) {
    if (impl_->pObj) aivox_aec_ans_set_denoise_strength(impl_->pObj, value);
}

void AnsProcessor::reset() {
    if (impl_->pObj) aivox_aec_ans_reset(impl_->pObj);
}

void AnsProcessor::cleanup() {
    if (impl_->pObj) { aivox_aec_ans_cleanup(impl_->pObj); impl_->pObj = nullptr; }
    impl_->initOk = false;
}

QString AnsProcessor::version() const { return impl_->ver; }

/* ======================= StreamProcessWorker ======================= */

StreamProcessWorker::StreamProcessWorker(const QString& model, const QString& licenseDir,
                                         const QString& authCode, int module, QObject* parent)
    : QObject(parent), model_(model), license_(licenseDir), auth_(authCode), module_(module) {}

StreamProcessWorker::~StreamProcessWorker() {
    stop();
    proc_.cleanup();
}

QString StreamProcessWorker::start(const audio::ProcessorConfig& cfg) {
    stop();   /* 先收掉上一轮线程（引擎保留，避免每次开录都重新走授权/联网） */

    if (!proc_.isInitialized()) {
        const QString err = proc_.initialize(model_, license_, auth_, module_);
        if (!err.isEmpty()) return err;
    } else {
        proc_.reset();   /* 新一轮录制：清掉上一轮的降噪自适应状态 */
    }
    proc_.setDenoiseStrength(cfg.denoiseStrength);

    {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.clear();
        cfg_ = cfg;
        stats_ = audio::ProcessStats{};
        outAcc_.clear();
        dropped_ = 0;
    }
    applied_ = cfg;
    /* 引擎收敛期：丢掉开头 100ms 输出（自适应参数尚未稳定，首段常有大冲击） */
    skipOut_ = 10;

    running_ = true;
    thread_ = std::thread([this] { runLoop(); });
    return QString();
}

void StreamProcessWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        running_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();   /* 线程会先把积压队列排空再退出 */
}

void StreamProcessWorker::setConfig(const audio::ProcessorConfig& cfg) {
    std::lock_guard<std::mutex> lock(mtx_);
    cfg_ = cfg;
}

void StreamProcessWorker::submitFrame(const int16_t* in, size_t n) {
    if (!in || n == 0) return;
    Item it;
    it.in.assign(in, in + n);

    std::lock_guard<std::mutex> lock(mtx_);
    if (!running_) return;
    if (queue_.size() >= kMaxQueue) {   /* 积压过多：丢最旧帧，保证实时性 */
        queue_.pop_front();
        ++dropped_;
    }
    queue_.push_back(std::move(it));
    cv_.notify_one();
}

audio::ProcessStats StreamProcessWorker::stats() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return stats_;
}

std::vector<int16_t> StreamProcessWorker::takeOutput() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return outAcc_;
}

int StreamProcessWorker::droppedFrames() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return dropped_;
}

void StreamProcessWorker::runLoop() {
    std::vector<int16_t> out(audio::kFrameSamples, 0);
    for (;;) {
        Item it;
        audio::ProcessorConfig cfg;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_.load(); });
            if (queue_.empty()) return;     /* 已停止且积压排空 */
            it = std::move(queue_.front());
            queue_.pop_front();
            cfg = cfg_;
        }

        /* 参数只对后续帧生效：引擎有状态，已处理的数据不能回改 */
        if (cfg.denoiseStrength != applied_.denoiseStrength) {
            proc_.setDenoiseStrength(cfg.denoiseStrength);
            applied_ = cfg;
        }

        const size_t n = std::min(it.in.size(), size_t(audio::kFrameSamples));
        auto t0 = Clock::now();
        const QString err = proc_.process(it.in.data(), out.data());
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

        if (!err.isEmpty()) {
            running_ = false;
            emit failed(err);
            return;
        }

        /* 引擎收敛期丢弃：仍喂算法（让它收敛），但不入账、不显示 */
        if (skipOut_ > 0) {
            --skipOut_;
            continue;
        }

        int peak = 0;
        for (size_t k = 0; k < n; ++k) {
            int a = int(it.in[k]); if (a < 0) a = -a;
            if (a > peak) peak = a;
        }
        {
            std::lock_guard<std::mutex> lock(mtx_);
            ++stats_.frames;
            stats_.totalMs += ms;
            if (ms > stats_.maxMs) stats_.maxMs = ms;
            stats_.audioSec += double(n) / audio::kSampleRate;
            stats_.peakLevel = peak;
            outAcc_.insert(outAcc_.end(), out.begin(), out.begin() + n);   /* 累计完整结果 */
        }
        emit frameProcessed(std::vector<int16_t>(out.begin(), out.begin() + n));
    }
}
