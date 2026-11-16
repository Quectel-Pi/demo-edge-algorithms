#include "kws_engine.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <QDateTime>
#include <QFileInfo>

#include "aivox_ovkws.h"
#include "aivox_slas.h"
#include "qlspeech_types.h"

namespace {

constexpr int kLicenseFuncVadKws = 1;
constexpr int kHalApp           = 0;
constexpr long kStreamMode      = 0;

QString sdkErrorText(int code) {
    return QStringLiteral("SDK error code %1").arg(code);
}

}

struct AivoxKwsEngineImpl {
    void* handle = nullptr;
    std::atomic<kws::IKwsResultSink*> sink{nullptr};
    std::mutex mutex;
    float threshold = 0.30f;
};

extern "C" int aivox_kws_event_trampoline(void* user, qlKWSParam param) {
    auto* impl = static_cast<AivoxKwsEngineImpl*>(user);
    if (!impl) return 0;

    kws::WakeEvent event;
    const std::size_t maxLen = sizeof(param.keyword) / sizeof(param.keyword[0]);
    event.keyword = QString::fromUtf8(param.keyword, static_cast<int>(strnlen(param.keyword, maxLen)));
    event.score = static_cast<double>(param.hit_score);
    event.threshold = static_cast<double>(param.hit_scoreThr);
    event.startT = static_cast<qint64>(param.startT);
    event.endT = static_cast<qint64>(param.endT);
    event.detectedAt = QDateTime::currentDateTime();

    if (kws::IKwsResultSink* sink = impl->sink.load()) {
        sink->onWakeEvent(event);
    }
    return 0;
}

AivoxKwsEngine::AivoxKwsEngine() : impl_(new AivoxKwsEngineImpl) {}

AivoxKwsEngine::~AivoxKwsEngine() {
    auto* impl = static_cast<AivoxKwsEngineImpl*>(impl_);
    if (impl && impl->handle) {

        aivox_ovkws_reset(impl->handle);
        aivox_ovkws_destroy(impl->handle);
        impl->handle = nullptr;
    }
    delete impl;
    impl_ = nullptr;
}

bool AivoxKwsEngine::init(const kws::EngineConfig& config, QString* error) {
    config_ = config;
    auto* impl = static_cast<AivoxKwsEngineImpl*>(impl_);

    if (!QFileInfo::exists(config.tokensFile)) {
        if (error) *error = QStringLiteral("tokens file not found: ") + config.tokensFile;
        return false;
    }
    if (!QFileInfo::exists(config.keywordsFile)) {
        if (error) *error = QStringLiteral("keywords file not found: ") + config.keywordsFile;
        return false;
    }

    std::string licenseDir = config.licenseDir.toStdString();
    fprintf(stderr, "[INIT] license dir = %s\n", licenseDir.c_str());
    int rc = aivox_slas_set_license_path_dir(const_cast<char*>(licenseDir.c_str()));
    if (rc != 0) {
        if (error) *error = QStringLiteral("license path rejected (") + sdkErrorText(rc) + QStringLiteral("): ") +
                            config.licenseDir;
        return false;
    }

    if (config.boardModule == 0 && config.deviceNode.isEmpty()) {
        if (error) *error = QStringLiteral(
            "board module 0 (non-Quectel) requires --dev-node, e.g. --dev-node /dev/ttyGS0");
        return false;
    }
    std::string devNode = config.deviceNode.toStdString();
    char* custNode = (config.boardModule == 0) ? devNode.data() : nullptr;

    fprintf(stderr, "[INIT] set_dev module=%d at=%d node=%s\n", config.boardModule,
            config.atIndex, config.deviceNode.isEmpty() ? "<none>" : devNode.c_str());
    rc = aivox_slas_set_dev(kHalApp, config.boardModule, config.atIndex, custNode);
    if (rc != 0) {
        if (error) *error = QStringLiteral("aivox_slas_set_dev failed: ") + sdkErrorText(rc);
        return false;
    }

    std::string authCode = config.authCode.toStdString();
    fprintf(stderr, "[INIT] request license ...\n");
    const int requestRc = aivox_slas_func_http_request_license(nullptr, nullptr,
                                                               kLicenseFuncVadKws,
                                                               const_cast<char*>(authCode.c_str()));
    if (requestRc != 0) {

        qWarning("AIVOX license request returned %s; trying cached license",
                 qPrintable(sdkErrorText(requestRc)));
    }

    fprintf(stderr, "[INIT] verify license ...\n");
    rc = aivox_slas_func_licenses_verify(nullptr, nullptr, kLicenseFuncVadKws);
    if (rc != 0) {
        if (error) *error = QStringLiteral("license verification failed: ") + sdkErrorText(rc) +
                            QStringLiteral(". Check network on first activation and a writable license directory.");
        return false;
    }

    std::string tokens = config.tokensFile.toStdString();
    std::string keywords = config.keywordsFile.toStdString();
    fprintf(stderr, "[INIT] create engine\n  tokens   = %s\n  keywords = %s\n", tokens.c_str(),
            keywords.c_str());
    impl->handle = aivox_ovkws_create(nullptr, nullptr, nullptr, tokens.c_str(), keywords.c_str());
    if (!impl->handle) {
        if (error) *error = QStringLiteral("aivox_ovkws_create returned null (check tokens/keywords/model match)");
        return false;
    }

    rc = aivox_ovkws_callbacks(impl->handle, "ovkws", aivox_kws_event_trampoline, impl);
    if (rc != 0) {
        if (error) *error = QStringLiteral("aivox_ovkws_callbacks failed: ") + sdkErrorText(rc);
        if (impl->handle) {
            aivox_ovkws_destroy(impl->handle);
            impl->handle = nullptr;
        }
        return false;
    }

    impl->threshold = config.threshold;
    if (impl->threshold > 0.0f) {
        rc = aivox_ovkws_set_threshold(impl->handle, impl->threshold);
        if (rc != 0) {
            if (error) *error = QStringLiteral("aivox_ovkws_set_threshold failed: ") + sdkErrorText(rc);
            aivox_ovkws_destroy(impl->handle);
            impl->handle = nullptr;
            return false;
        }
    }

    char info[512] = {0};
    if (aivox_ovkws_get_build_info(info) == 0) {
        version_ = QString::fromUtf8(info).trimmed();
    } else {
        version_ = QStringLiteral("AIVOX open-vocabulary KWS (version unavailable)");
    }

    if (error) error->clear();
    return true;
}

bool AivoxKwsEngine::feed(const int16_t* samples, std::size_t count, QString* error) {
    auto* impl = static_cast<AivoxKwsEngineImpl*>(impl_);
    if (!impl || !impl->handle) {
        if (error) *error = QStringLiteral("engine is not initialized");
        return false;
    }
    if (!samples || count == 0) {
        if (error) *error = QStringLiteral("empty audio frame");
        return false;
    }

    std::lock_guard<std::mutex> lock(impl->mutex);
    int rc = aivox_ovkws_process(impl->handle, const_cast<short*>(samples),
                                 static_cast<unsigned long>(count), kStreamMode);
    if (rc != 0) {
        if (error) *error = QStringLiteral("aivox_ovkws_process failed: ") + sdkErrorText(rc);
        return false;
    }
    if (error) error->clear();
    return true;
}

void AivoxKwsEngine::reset() {
    auto* impl = static_cast<AivoxKwsEngineImpl*>(impl_);
    if (!impl || !impl->handle) return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    aivox_ovkws_reset(impl->handle);
}

bool AivoxKwsEngine::setThreshold(float value, QString* error) {
    if (value < 0.0f || value > 1.0f) {
        if (error) *error = QStringLiteral("threshold must be in [0, 1]");
        return false;
    }
    auto* impl = static_cast<AivoxKwsEngineImpl*>(impl_);
    if (!impl || !impl->handle) {
        if (error) *error = QStringLiteral("engine is not initialized");
        return false;
    }
    std::lock_guard<std::mutex> lock(impl->mutex);
    int rc = aivox_ovkws_set_threshold(impl->handle, value);
    if (rc != 0) {
        if (error) *error = QStringLiteral("aivox_ovkws_set_threshold failed: ") + sdkErrorText(rc);
        return false;
    }
    impl->threshold = value;
    config_.threshold = value;
    if (error) error->clear();
    return true;
}

void AivoxKwsEngine::setResultSink(kws::IKwsResultSink* sink) {
    auto* impl = static_cast<AivoxKwsEngineImpl*>(impl_);
    if (impl) impl->sink.store(sink);
}

QString AivoxKwsEngine::version() const { return version_; }
