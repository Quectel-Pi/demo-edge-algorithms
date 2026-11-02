#include "audio_capture.h"

#include <alsa/asoundlib.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

/* ======================= AudioCapture ======================= */

AudioCapture::AudioCapture(QObject* parent) : QObject(parent) {}

AudioCapture::~AudioCapture() { stop(); }

void AudioCapture::openDevice(const QString& device) { device_ = device; }

QString AudioCapture::device() const { return device_; }
bool AudioCapture::isRunning() const { return running_.load(); }

void AudioCapture::start() {
    if (thread_.joinable()) return;
    running_ = true;
    thread_ = std::thread([this] { runLoop(); });
}

void AudioCapture::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void AudioCapture::runLoop() {
    const std::string devName = device_.isEmpty() ? "default" : device_.toStdString();

    fprintf(stderr, "[CAPT] opening pcm='%s' channels=%d rate=%d\n",
            devName.c_str(), audio::kChannels, audio::kSampleRate);

    snd_pcm_t* pcm = nullptr;
    int rc = snd_pcm_open(&pcm, devName.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        fprintf(stderr, "[CAPT] snd_pcm_open FAILED: %s\n", snd_strerror(rc));
        emit captureError(QStringLiteral("snd_pcm_open(capture %1) failed: %2")
                              .arg(QString::fromStdString(devName), snd_strerror(rc)));
        running_ = false;
        return;
    }

    /* soft_resample=1：让 ALSA 把硬件实际采样率自动重采样到我们要的 16k，
     * 否则如果硬件只支持 8k/48k 等 closest rate，我们拿到的数据就是错采样率的 */
    rc = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                            audio::kChannels, audio::kSampleRate, 1 /* 允许软件重采样 */,
                            500000 /* 0.5s */);
    if (rc < 0) {
        fprintf(stderr, "[CAPT] snd_pcm_set_params FAILED: %s\n", snd_strerror(rc));
        emit captureError(QStringLiteral("snd_pcm_set_params(capture) failed: %1")
                              .arg(snd_strerror(rc)));
        snd_pcm_close(pcm);
        running_ = false;
        return;
    }

    /* 回读 ALSA capture 实际硬件采样率 */
    unsigned int actRate = 0;
    snd_pcm_hw_params_t* hwp = nullptr;
    snd_pcm_hw_params_alloca(&hwp);
    snd_pcm_hw_params_current(pcm, hwp);
    snd_pcm_hw_params_get_rate(hwp, &actRate, nullptr);
    fprintf(stderr, "[CAPT] ALSA actual rate=%u (requested %d) soft_resample=1\n",
            actRate, audio::kSampleRate);

    emit deviceOpened(QString::fromStdString(devName));

    char* raw = new char[audio::kFrameBytes];
    audio::AudioFrame frame;
    while (running_) {
        rc = snd_pcm_readi(pcm, raw, audio::kFrameSamples);
        if (rc == -EPIPE) {
            snd_pcm_prepare(pcm);   /* 过载/欠载后恢复 */
            continue;
        }
        if (rc < 0) {
            if (rc == -EAGAIN) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
            emit captureError(QStringLiteral("capture readi failed: %1").arg(snd_strerror(rc)));
            break;
        }
        if (rc != audio::kFrameSamples) {
            /* 读取不足一帧则丢弃该次，等下次凑齐 */
            continue;
        }
        std::memcpy(frame.samples.data(), raw, audio::kFrameBytes);
        emit frameCaptured(frame);
    }
    delete[] raw;
    snd_pcm_close(pcm);
    running_ = false;
}

/* ======================= AudioPlayback ======================= */

AudioPlayback::AudioPlayback(QObject* parent) : QObject(parent) {}

AudioPlayback::~AudioPlayback() { stop(); }

void AudioPlayback::play(const std::vector<int16_t>& samples, const QString& device) {
    stop();                       /* 单实例单流：先停掉上一段 */
    if (samples.empty()) return;
    device_ = device;
    playing_ = true;
    thread_ = std::thread([this, data = samples] { runLoop(data); });
}

void AudioPlayback::stop() {
    playing_ = false;
    if (thread_.joinable()) thread_.join();
}

void AudioPlayback::runLoop(std::vector<int16_t> data) {
    const std::string devName = device_.isEmpty() ? "default" : device_.toStdString();

    fprintf(stderr, "[PLAY] opening pcm='%s' ch=%d rate=%d frames=%zu\n",
            devName.c_str(), audio::kChannels, audio::kSampleRate, data.size());

    snd_pcm_t* pcm = nullptr;
    /* 加 SND_PCM_NONBLOCK 避免 open 被其他进程短暂占用时卡死；
     * writei 循环里再显式 poll 等待可写，节奏由我们控制 */
    int rc = snd_pcm_open(&pcm, devName.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (rc < 0) {
        fprintf(stderr, "[PLAY] snd_pcm_open FAILED: %s\n", snd_strerror(rc));
        emit playbackError(QStringLiteral("snd_pcm_open(playback %1) failed: %2")
                               .arg(QString::fromStdString(devName), snd_strerror(rc)));
        playing_ = false;
        emit finished();
        return;
    }

    /* 显式 hw_params 配置（等价于 aplay 底层的完整序列）：
     * 1) 安装 hw_params
     * 2) 设定 format / channels / rate（容忍 hardware 重采样）
     * 3) 设定 access / buffer_size / period_size
     * 4) apply */
    snd_pcm_hw_params_t* hwp = nullptr;
    snd_pcm_hw_params_alloca(&hwp);
    snd_pcm_hw_params_any(pcm, hwp);
    snd_pcm_hw_params_set_format(pcm, hwp, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, hwp, audio::kChannels);
    unsigned int r = audio::kSampleRate;
    int dir = 0;
    snd_pcm_hw_params_set_rate_near(pcm, hwp, &r, &dir);
    /* 周期 = 20ms（320 samples @16k），缓冲 = 10个周期 = 200ms */
    snd_pcm_uframes_t period_frames = audio::kSampleRate / 50;   /* 16k/50 = 320 */
    snd_pcm_uframes_t buffer_frames = period_frames * 10;        /* 3200 frames = 200ms */
    dir = 0;
    snd_pcm_hw_params_set_period_size_near(pcm, hwp, &period_frames, &dir);
    snd_pcm_hw_params_set_buffer_size_near(pcm, hwp, &buffer_frames);
    rc = snd_pcm_hw_params(pcm, hwp);
    if (rc < 0) {
        fprintf(stderr, "[PLAY] snd_pcm_hw_params FAILED: %s\n", snd_strerror(rc));
        emit playbackError(QStringLiteral("snd_pcm_hw_params(playback) failed: %1")
                               .arg(snd_strerror(rc)));
        snd_pcm_close(pcm);
        playing_ = false;
        emit finished();
        return;
    }

    /* 显式设置 sw_params：start threshold = 1 周期，避免 buffer 填太慢导致
     * DAC 欠载；又避免填到全满才开始，播放节奏由硬件时钟决定 */
    snd_pcm_sw_params_t* swp = nullptr;
    snd_pcm_sw_params_alloca(&swp);
    snd_pcm_sw_params_current(pcm, swp);
    snd_pcm_uframes_t start_thresh = period_frames;
    snd_pcm_sw_params_set_start_threshold(pcm, swp, start_thresh);
    snd_pcm_sw_params_set_avail_min(pcm, swp, period_frames / 2);
    snd_pcm_sw_params(pcm, swp);

    /* 显式 prepare，确保状态干净 */
    rc = snd_pcm_prepare(pcm);
    if (rc < 0) {
        fprintf(stderr, "[PLAY] snd_pcm_prepare FAILED: %s\n", snd_strerror(rc));
        emit playbackError(QStringLiteral("snd_pcm_prepare failed: %1").arg(snd_strerror(rc)));
        snd_pcm_close(pcm);
        playing_ = false;
        emit finished();
        return;
    }

    /* 回读最终参数（诊断用） */
    unsigned int actRate = 0;
    snd_pcm_hw_params_t* final = nullptr;
    snd_pcm_hw_params_alloca(&final);
    snd_pcm_hw_params_current(pcm, final);
    snd_pcm_hw_params_get_rate(final, &actRate, nullptr);
    snd_pcm_uframes_t actBuf = 0, actPer = 0;
    snd_pcm_hw_params_get_buffer_size(final, &actBuf);
    snd_pcm_hw_params_get_period_size(final, &actPer, nullptr);
    fprintf(stderr, "[PLAY] OK  rate=%u buf=%lu per=%lu start_thresh=%lu\n",
            actRate, (unsigned long)actBuf, (unsigned long)actPer, (unsigned long)start_thresh);

    /* writei 循环：用 poll 等待可写，每次写一个 period 量，
     * 节奏完全由硬件时钟（16k DAC）驱动，不会快也不会慢 */
    size_t offset = 0;
    while (playing_ && offset < data.size()) {
        const snd_pcm_uframes_t n = snd_pcm_uframes_t(
            std::min(size_t(period_frames), data.size() - offset));

        /* poll 等待 PCM 变得可写（有足够空槽），超时 100ms */
        while (true) {
            rc = snd_pcm_writei(pcm, data.data() + offset, n);
            if (rc == -EAGAIN) {
                struct pollfd pfd;
                snd_pcm_poll_descriptors(pcm, &pfd, 1);
                poll(&pfd, 1, 100);
                continue;
            }
            if (rc == -EPIPE) {   /* 欠载：重 prepare + 重启 */
                fprintf(stderr, "[PLAY] xrun (EPIPE) — preparing\n");
                snd_pcm_prepare(pcm);
                continue;
            }
            break;
        }
        if (rc <= 0) {
            fprintf(stderr, "[PLAY] writei failed: %s (rc=%d)\n", snd_strerror(int(rc)), rc);
            emit playbackError(QStringLiteral("playback writei failed: %1").arg(snd_strerror(int(rc))));
            break;
        }
        offset += size_t(rc);
        emit progress(qint64(offset));
    }

    if (offset >= data.size()) {
        snd_pcm_drain(pcm);
        fprintf(stderr, "[PLAY] drain ok, wrote %zu frames (expected %zu)\n", offset, data.size());
    } else {
        snd_pcm_drop(pcm);
    }
    snd_pcm_close(pcm);
    playing_ = false;
    emit finished();
}