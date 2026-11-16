

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaType>

#include <cstdlib>
#include <memory>

#include "kws_engine.h"
#include "kws_pipeline.h"
#include "kws_types.h"
#include "main.h"
#include "main_window.h"

namespace appconfig {

QString readEnv(const char* name, const QString& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? QString::fromLocal8Bit(v) : fallback;
}

}

int main(int argc, char** argv) {
    qRegisterMetaType<kws::AudioFrame>("kws::AudioFrame");
    qRegisterMetaType<kws::WakeEvent>("kws::WakeEvent");
    qRegisterMetaType<kws::EngineStats>("kws::EngineStats");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kws_demo"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Quectel PI M2 keyword spotting (wake word) demo."));
    parser.addOptions({
        {"cap", "ALSA capture device.", "device"},
        {"tokens", "Path to tokens.txt (must match the model vocabulary).", "path"},
        {"keywords", "Path to the keyword file (pinyin + @text).", "path"},
        {"license", "Directory for the license cache.", "dir"},
        {"threshold", "Global decision threshold [0,1].", "value"},
        {"gain", "Microphone software pre-gain in dB.", "db"},
        {"label", "English display label of the wake word.", "text"},
        {"module", "Board module id for aivox_slas_set_dev.", "id"},
        {"at", "Device-id source for aivox_slas_set_dev: 0/1 IMEI, 2 SPIMEI, 3 SN.", "index"},
        {"dev-node", "Serial node for a non-Quectel board (module 0).", "node"},
    });
    parser.addHelpOption();
    parser.process(app);

    const QString appRoot =
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral(".."));

    appconfig::AppConfig cfg;
    cfg.sdkRoot = appconfig::readEnv(
        "KWS_SDK_ROOT", appRoot + QLatin1Char('/') + appconfig::kSdkPackagePath);
    cfg.tokensFile = parser.isSet("tokens")
        ? parser.value("tokens")
        : appconfig::readEnv("KWS_TOKENS", appRoot + QStringLiteral("/models/tokens.txt"));
    cfg.keywordsFile = parser.isSet("keywords")
        ? parser.value("keywords")
        : appconfig::readEnv("KWS_KEYWORDS", appRoot + QStringLiteral("/models/keywords.txt"));

    cfg.pinyinFile = QDir(QFileInfo(cfg.tokensFile).absolutePath())
                         .absoluteFilePath(QStringLiteral("pinyin.txt"));
    cfg.licenseDir = parser.isSet("license")
        ? parser.value("license")
        : appconfig::readEnv("KWS_LICENSE_DIR", appconfig::kDefaultLicenseDir);
    cfg.authCode = appconfig::readEnv("KWS_AUTH_CODE", appconfig::kDefaultAuthCode);
    cfg.captureDevice = parser.isSet("cap")
        ? parser.value("cap")
        : appconfig::readEnv("KWS_CAPTURE_DEV", appconfig::kDefaultCaptureDev);
    cfg.wakeLabel = parser.isSet("label")
        ? parser.value("label")
        : appconfig::readEnv("KWS_WAKE_LABEL", QString());

    bool ok = false;
    cfg.threshold = parser.isSet("threshold")
        ? parser.value("threshold").toFloat(&ok)
        : appconfig::readEnv("KWS_THRESHOLD", QString::number(appconfig::kDefaultThreshold)).toFloat(&ok);
    if (!ok) cfg.threshold = appconfig::kDefaultThreshold;
    cfg.micGainDb = parser.isSet("gain")
        ? parser.value("gain").toDouble(&ok)
        : appconfig::readEnv("KWS_GAIN_DB", QString::number(appconfig::kDefaultMicGainDb)).toDouble(&ok);
    if (!ok) cfg.micGainDb = appconfig::kDefaultMicGainDb;
    cfg.boardModule = parser.isSet("module")
        ? parser.value("module").toInt(&ok)
        : appconfig::readEnv("KWS_BOARD_MODULE", QString::number(appconfig::kDefaultBoardModule)).toInt(&ok);
    if (!ok) cfg.boardModule = appconfig::kDefaultBoardModule;
    cfg.atIndex = parser.isSet("at")
        ? parser.value("at").toInt(&ok)
        : appconfig::readEnv("KWS_AT", QString::number(appconfig::kDefaultAt)).toInt(&ok);
    if (!ok) cfg.atIndex = appconfig::kDefaultAt;
    cfg.deviceNode = parser.isSet("dev-node")
        ? parser.value("dev-node")
        : appconfig::readEnv("KWS_DEV_NODE", appconfig::kDefaultDevNode);

    kws::SpotterFactory factory = []() -> std::unique_ptr<kws::IKeywordSpotter> {
        return std::make_unique<AivoxKwsEngine>();
    };

    MainWindow win(std::move(factory), cfg);
    win.show();
    return app.exec();
}
