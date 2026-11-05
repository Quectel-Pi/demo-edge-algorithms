#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include <QObject>
#include <QString>

#include "kws_interfaces.h"

class KwsPipeline : public QObject, public kws::IKwsResultSink {
    Q_OBJECT
public:
    explicit KwsPipeline(kws::SpotterFactory factory, QObject* parent = nullptr);
    ~KwsPipeline() override;

    void start(const kws::EngineConfig& config);
    void stop();

    void submit(const kws::AudioFrame& frame);

    void requestReset();

    bool setThreshold(float value, QString* error = nullptr);
    bool isReady() const { return ready_.load(); }

    kws::EngineStats stats() const;
    QString engineVersion() const;
    QString lastError() const;

signals:
    void ready(const QString& engineVersion);
    void failed(const QString& error);
    void wakeDetected(const kws::WakeEvent& event);
    void feedError(const QString& error);

private:
    void onWakeEvent(const kws::WakeEvent& event) override;
    void workerLoop();

    kws::SpotterFactory factory_;
    std::unique_ptr<kws::IKeywordSpotter> spotter_;

    kws::EngineConfig config_;
    std::mutex configMutex_;

    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> pendingReset_{false};

    std::thread worker_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<kws::AudioFrame> queue_;

    std::mutex spotterMutex_;

    std::atomic<long long> frames_{0};
    std::atomic<long long> samples_{0};
    std::atomic<long long> totalMicros_{0};
    std::atomic<long long> maxMicros_{0};

    mutable std::mutex errorMutex_;
    QString lastError_;
    mutable std::mutex versionMutex_;
    QString version_;
};
