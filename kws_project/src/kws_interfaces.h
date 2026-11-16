#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include <QString>

#include "kws_types.h"

namespace kws {

using FrameSink = std::function<void(const AudioFrame&)>;

class IAudioSource {
public:
    virtual ~IAudioSource() = default;

    virtual bool open(const QString& device, QString* error) = 0;

    virtual bool start(FrameSink sink, QString* error) = 0;

    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};

class IKwsResultSink {
public:
    virtual ~IKwsResultSink() = default;

    virtual void onWakeEvent(const WakeEvent& event) = 0;
};

class IKeywordSpotter {
public:
    virtual ~IKeywordSpotter() = default;

    virtual bool init(const EngineConfig& config, QString* error) = 0;

    virtual bool feed(const int16_t* samples, std::size_t count, QString* error) = 0;

    virtual void reset() = 0;

    virtual bool setThreshold(float value, QString* error) = 0;

    virtual void setResultSink(IKwsResultSink* sink) = 0;

    virtual QString version() const = 0;
};

using SpotterFactory = std::function<std::unique_ptr<IKeywordSpotter>()>;

}
