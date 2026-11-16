#pragma once

#include <QString>

namespace appconfig {

struct AppConfig {
    QString sdkRoot;
    QString tokensFile;
    QString keywordsFile;
    QString pinyinFile;
    QString licenseDir;
    QString authCode;
    QString captureDevice;
    QString wakeLabel;
    QString wakeTokens;
    QString deviceNode;
    float   threshold  = 0.30f;
    double  micGainDb  = 30.0;
    int     boardModule = 3;
    int     atIndex     = 3;
};

QString readEnv(const char* name, const QString& fallback);

const QString kSdkPackagePath =
    QStringLiteral("aivox-scr-xiaoyuantongxue-zipformer-960k-zh/SDK");
const QString kDefaultAuthCode =
    QStringLiteral("DD07D3A7-8BD0-41CB-8ADE-21BB2421E5AD");
const QString kDefaultLicenseDir   = QStringLiteral("/data");
const QString kDefaultCaptureDev   = QStringLiteral("default");
const QString kDefaultWakeLabel    = QStringLiteral("XIAO YUAN TONG XUE");
const float   kDefaultThreshold   = 0.30f;
const double  kDefaultMicGainDb   = 30.0;

const int     kDefaultBoardModule  = 3;
const int     kDefaultAt          = 3;
const QString kDefaultDevNode     = QString();

}
