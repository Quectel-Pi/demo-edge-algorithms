#include "kws_pipeline.h"

#include <algorithm>
#include <chrono>

KwsPipeline::KwsPipeline(kws::SpotterFactory factory, QObject* parent)
    : QObject(parent), factory_(std::move(factory)) {}

KwsPipeline::~KwsPipeline() { stop(); }

void KwsPipeline::start(const kws::EngineConfig& config) {
    stop();
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        config_ = config;
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_.clear();
    }
    ready_ = false;
    running_ = true;
    worker_ = std::thread([this] { workerLoop(); });
}

void KwsPipeline::stop() {
    running_ = false;
    queueCv_.notify_all();
    if (worker_.joinable()) worker_.join();
    ready_ = false;
}

void KwsPipeline::submit(const kws::AudioFrame& frame) {
    if (!ready_.load()) return;
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (queue_.size() >= 50) queue_.pop_front();
    queue_.push_back(frame);
    queueCv_.notify_one();
}

void KwsPipeline::requestReset() {

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.clear();
    }
    pendingReset_ = true;
    queueCv_.notify_all();
}

bool KwsPipeline::setThreshold(float value, QString* error) {
    std::lock_guard<std::mutex> lock(spotterMutex_);
    if (!spotter_) {
        if (error) *error = QStringLiteral("engine is not ready");
        return false;
    }
    bool ok = spotter_->setThreshold(value, error);
    if (ok) {
        std::lock_guard<std::mutex> cfgLock(configMutex_);
        config_.threshold = value;
    }
    return ok;
}

kws::EngineStats KwsPipeline::stats() const {
    kws::EngineStats s;
    s.frames = frames_.load();
    s.audioSamples = samples_.load();
    s.totalMs = static_cast<double>(totalMicros_.load()) / 1000.0;
    s.maxMs = static_cast<double>(maxMicros_.load()) / 1000.0;
    return s;
}

QString KwsPipeline::engineVersion() const {
    std::lock_guard<std::mutex> lock(versionMutex_);
    return version_;
}

QString KwsPipeline::lastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

void KwsPipeline::onWakeEvent(const kws::WakeEvent& event) {

    emit wakeDetected(event);
}

void KwsPipeline::workerLoop() {

    std::unique_ptr<kws::IKeywordSpotter> spotter = factory_ ? factory_() : nullptr;
    if (!spotter) {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = QStringLiteral("failed to create KWS engine");
        emit failed(lastError_);
        running_ = false;
        return;
    }

    spotter->setResultSink(this);
    kws::EngineConfig cfg;
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        cfg = config_;
    }

    QString error;
    if (!spotter->init(cfg, &error)) {
        {
            std::lock_guard<std::mutex> lock(errorMutex_);
            lastError_ = error;
        }
        emit failed(error);
        running_ = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(spotterMutex_);
        spotter_ = std::move(spotter);
    }
    {
        std::lock_guard<std::mutex> lock(versionMutex_);
        version_ = spotter_->version();
    }
    ready_ = true;
    emit ready(engineVersion());

    while (running_.load()) {
        bool doReset = pendingReset_.exchange(false);
        kws::AudioFrame frame;
        bool hasFrame = false;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait_for(lock, std::chrono::milliseconds(20), [this] {
                return !running_.load() || !queue_.empty() || pendingReset_.load();
            });
            if (!running_.load()) break;
            doReset = doReset || pendingReset_.exchange(false);
            if (!queue_.empty()) {
                frame = std::move(queue_.front());
                queue_.pop_front();
                hasFrame = true;
            }
        }

        std::lock_guard<std::mutex> lock(spotterMutex_);
        if (!spotter_) break;
        if (doReset) spotter_->reset();
        if (!hasFrame) continue;

        const auto t0 = std::chrono::steady_clock::now();
        QString feedErr;
        const bool ok = spotter_->feed(frame.samples.data(), frame.samples.size(), &feedErr);
        const auto t1 = std::chrono::steady_clock::now();
        const long long us =
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        frames_.fetch_add(1);
        samples_.fetch_add(static_cast<long long>(frame.samples.size()));
        totalMicros_.fetch_add(us);
        long long prev = maxMicros_.load();
        while (us > prev && !maxMicros_.compare_exchange_weak(prev, us)) {
        }

        if (!ok && !feedErr.isEmpty()) {
            {
                std::lock_guard<std::mutex> errLock(errorMutex_);
                lastError_ = feedErr;
            }
            emit feedError(feedErr);
        }
    }

    {
        std::lock_guard<std::mutex> lock(spotterMutex_);
        spotter_.reset();
    }
    ready_ = false;
}
