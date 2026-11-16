#pragma once

#include <atomic>
#include <string>
#include <thread>

#include <QObject>
#include <QString>

#include "kws_interfaces.h"

class AlsaAudioSource : public QObject, public kws::IAudioSource {
    Q_OBJECT
public:
    explicit AlsaAudioSource(QObject* parent = nullptr);
    ~AlsaAudioSource() override;

    bool open(const QString& device, QString* error) override;
    bool start(kws::FrameSink sink, QString* error) override;
    void stop() override;
    bool isRunning() const override;

    QString device() const { return device_; }

signals:
    void deviceOpened(const QString& device);
    void errorOccurred(const QString& message);

private:
    void runLoop();

    QString device_;
    kws::FrameSink sink_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
