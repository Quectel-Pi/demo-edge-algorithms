#pragma once

#include <memory>

#include <QString>

#include "kws_interfaces.h"

class AivoxKwsEngine : public kws::IKeywordSpotter {
public:
    AivoxKwsEngine();
    ~AivoxKwsEngine() override;

    bool init(const kws::EngineConfig& config, QString* error) override;
    bool feed(const int16_t* samples, std::size_t count, QString* error) override;
    void reset() override;
    bool setThreshold(float value, QString* error) override;
    void setResultSink(kws::IKwsResultSink* sink) override;
    QString version() const override;

private:
    void* impl_ = nullptr;
    kws::EngineConfig config_;
    mutable QString version_;
};
