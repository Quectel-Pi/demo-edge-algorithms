#pragma once
/* audio_capture.h — 音频采集/回放模块（ALSA）
 *
 * AudioCapture  : 从 ALSA capture 设备持续采集 16k/单声道/16bit PCM，
 *                 按固定帧长(160 采样=10ms)切帧后通过 frameCaptured 信号发布，
 *                 由界面层决定如何累积/处理（本 demo 用于录制-流式降噪处理）。
 * AudioPlayback : 把一段 PCM 缓冲异步写入 ALSA playback 设备（用于播放输入/输出波形）。
 * 界面/算法不关心具体硬件设备名。采集/回放与算法完全解耦（不 include 算法 SDK）。
 */

#include <QObject>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include "audio_types.h"

class AudioCapture : public QObject {
    Q_OBJECT
public:
    explicit AudioCapture(QObject* parent = nullptr);
    ~AudioCapture() override;

    void openDevice(const QString& device);   /* ALSA PCM 设备名，如 "hw:0,0" / "default" */
    QString device() const;
    bool isRunning() const;

public slots:
    void start();   /* 自起采集线程 */
    void stop();    /* 停止并回收线程 */

signals:
    void frameCaptured(const audio::AudioFrame& f);   /* 每采到一帧(10ms)触发一次 */
    void captureError(const QString& message);
    void deviceOpened(const QString& device);

private:
    void runLoop();

    QString       device_;
    std::atomic<bool> running_{false};
    std::thread   thread_;
};

/* 异步回放：play() 起后台线程写 ALSA，stop() 立即中止并回收线程。
 * 同一实例同一时刻只播一段音频（再次 play 会先停掉上一段）。 */
class AudioPlayback : public QObject {
    Q_OBJECT
public:
    explicit AudioPlayback(QObject* parent = nullptr);
    ~AudioPlayback() override;

    /* 播放 16k/单声道/16bit PCM 缓冲；device 为空时用 ALSA "default" */
    void play(const std::vector<int16_t>& samples, const QString& device = QString());
    void stop();
    bool isPlaying() const { return playing_.load(); }

signals:
    void finished();                        /* 正常播完或被打断，线程已退出 */
    void playbackError(const QString& message);
    void progress(qint64 samplesPlayed);    /* 播放进度反馈（已写样本数，供波形同步滚动） */

private:
    void runLoop(std::vector<int16_t> data);

    QString device_;
    std::atomic<bool> playing_{false};
    std::thread thread_;
};