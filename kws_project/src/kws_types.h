#pragma once

#include <cstdint>
#include <vector>

#include <QDateTime>
#include <QMetaType>
#include <QString>

namespace kws {

constexpr int kSampleRate   = 16000;
constexpr int kFrameSamples = 160;
constexpr int kChannels     = 1;
constexpr int kFrameBytes   = kFrameSamples * sizeof(int16_t);

struct AudioFrame {
    std::vector<int16_t> samples;

    AudioFrame() { samples.resize(kFrameSamples, 0); }
};

struct EngineConfig {
    QString tokensFile;
    QString keywordsFile;
    QString licenseDir;
    QString authCode;
    int     boardModule = 0;
    QString deviceNode;
    int     atIndex     = 3;
    float   threshold   = 0.30f;
};

struct WakeEvent {
    QString   keyword;
    QString   displayName;
    double    score      = 0.0;
    double    threshold  = 0.0;
    qint64    startT     = 0;
    qint64    endT       = 0;
    QDateTime detectedAt;

    bool isValid() const { return !keyword.isEmpty(); }
};

struct EngineStats {
    qint64 frames     = 0;
    qint64 audioSamples = 0;
    double totalMs    = 0.0;
    double maxMs      = 0.0;

    double avgMs() const { return frames > 0 ? totalMs / static_cast<double>(frames) : 0.0; }
    double audioSec() const { return static_cast<double>(audioSamples) / kSampleRate; }
    double rtf() const {
        const double a = audioSec();
        return a > 0.0 ? (totalMs / 1000.0) / a : 0.0;
    }
};

}

Q_DECLARE_METATYPE(kws::AudioFrame)
Q_DECLARE_METATYPE(kws::WakeEvent)
Q_DECLARE_METATYPE(kws::EngineStats)
