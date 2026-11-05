#include "audio_capture.h"

#include <alsa/asoundlib.h>

#include <chrono>
#include <cstring>

AlsaAudioSource::AlsaAudioSource(QObject* parent) : QObject(parent) {}

AlsaAudioSource::~AlsaAudioSource() { stop(); }

bool AlsaAudioSource::open(const QString& device, QString* error) {
    if (running_.load()) {
        if (error) *error = QStringLiteral("capture is already running");
        return false;
    }
    device_ = device;
    if (error) error->clear();
    return true;
}

bool AlsaAudioSource::start(kws::FrameSink sink, QString* error) {
    if (thread_.joinable()) {
        if (error) *error = QStringLiteral("capture thread already exists");
        return false;
    }
    if (!sink) {
        if (error) *error = QStringLiteral("audio sink is empty");
        return false;
    }
    sink_ = std::move(sink);
    running_ = true;
    thread_ = std::thread([this] { runLoop(); });
    if (error) error->clear();
    return true;
}

void AlsaAudioSource::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

bool AlsaAudioSource::isRunning() const { return running_.load(); }

void AlsaAudioSource::runLoop() {
    const std::string devName = device_.isEmpty() ? "default" : device_.toStdString();

    snd_pcm_t* pcm = nullptr;
    int rc = snd_pcm_open(&pcm, devName.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        emit errorOccurred(QStringLiteral("snd_pcm_open(%1) failed: %2")
                               .arg(QString::fromStdString(devName), QString::fromUtf8(snd_strerror(rc))));
        running_ = false;
        return;
    }

    rc = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                            kws::kChannels, kws::kSampleRate, 1 , 100000 );
    if (rc < 0) {
        emit errorOccurred(QStringLiteral("snd_pcm_set_params failed: %1")
                               .arg(QString::fromUtf8(snd_strerror(rc))));
        snd_pcm_close(pcm);
        running_ = false;
        return;
    }

    emit deviceOpened(QString::fromStdString(devName));

    kws::AudioFrame frame;
    std::size_t have = 0;
    while (running_.load()) {
        const std::size_t need = static_cast<std::size_t>(kws::kFrameSamples) - have;
        int got = static_cast<int>(snd_pcm_readi(pcm, frame.samples.data() + have, need));
        if (got < 0) {
            if (got == -EPIPE || got == -ESTRPIPE || got == -EIO) {
                snd_pcm_recover(pcm, got, 1);
                have = 0;
                continue;
            }
            if (got == -EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            emit errorOccurred(QStringLiteral("snd_pcm_readi failed: %1")
                                   .arg(QString::fromUtf8(snd_strerror(got))));
            break;
        }
        have += static_cast<std::size_t>(got);
        if (have < static_cast<std::size_t>(kws::kFrameSamples)) continue;

        have = 0;
        if (sink_) sink_(frame);
    }

    snd_pcm_close(pcm);
    running_ = false;
}
