#include "main.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMetaType>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "audio_types.h"
#include "main_window.h"

namespace appconfig {

QString readEnv(const char* name, const QString& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? QString::fromLocal8Bit(v) : fallback;
}

const QString kDefaultModel =
    QStringLiteral("/home/q/aec_ans_project/AIVOXAEC_ANS_V1.1.0.2.1_SDK_16k_10ms_Exp009_dewatermark_encrypt/models/"
                   "Exp009M060_16k_mdl-v1.0_10ms_32ms_simple.mnn");
const QString kDefaultLicenseDir = QStringLiteral("/data");
const QString kDefaultCaptureDev  = QStringLiteral("default");
/* 播放设备默认 default（让 PulseAudio/ALSA dmix 管重采样，
 * 避免 ES8389 codec PLL 时钟导致的倍速播放问题；允许 ANS_PLAYBACK_DEV 覆盖 */
const QString kDefaultPlaybackDev = QStringLiteral("default");
const int     kDefaultModule      = 3;   /* RK3576 */
/* 正式授权码（RK3576/M2）—— 内置默认，环境变量可覆盖；首次联网激活后 license
 * 落盘到 /data，后续优先走本地校验，不依赖网络。 */
const QString kDefaultAuthCode =
    QStringLiteral("F388795C-BD59-42A2-BF84-C3690E8509A6");

}  // namespace appconfig

namespace wav {

bool writePcmFile(const std::string& path, const int16_t* data, uint64_t samples,
                  uint32_t sampleRate) {
    constexpr uint16_t kChannels = 1;
    const uint32_t dataBytes = uint32_t(samples * sizeof(int16_t) * kChannels);
    const uint32_t riffSize = 36 + dataBytes;

    unsigned char hdr[44];
    std::memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'R'; hdr[1] = 'I'; hdr[2] = 'F'; hdr[3] = 'F';
    hdr[4] = uint8_t(riffSize); hdr[5] = uint8_t(riffSize >> 8);
    hdr[6] = uint8_t(riffSize >> 16); hdr[7] = uint8_t(riffSize >> 24);
    hdr[8] = 'W'; hdr[9] = 'A'; hdr[10] = 'V'; hdr[11] = 'E';
    hdr[12] = 'f'; hdr[13] = 'm'; hdr[14] = 't'; hdr[15] = ' ';
    hdr[16] = 16; hdr[17] = 0; hdr[18] = 0; hdr[19] = 0;      /* fmt chunk 16 */
    hdr[20] = 1; hdr[21] = 0;                                   /* PCM */
    hdr[22] = 1; hdr[23] = 0;                                   /* channels=1 */
    hdr[24] = uint8_t(sampleRate); hdr[25] = uint8_t(sampleRate >> 8);
    hdr[26] = uint8_t(sampleRate >> 16); hdr[27] = uint8_t(sampleRate >> 24);
    const uint32_t byteRate = sampleRate * 2;
    hdr[28] = uint8_t(byteRate); hdr[29] = uint8_t(byteRate >> 8);
    hdr[30] = uint8_t(byteRate >> 16); hdr[31] = uint8_t(byteRate >> 24);
    hdr[32] = 2; hdr[33] = 0;                                   /* block align */
    hdr[34] = 16; hdr[35] = 0;                                  /* bits */
    hdr[36] = 'd'; hdr[37] = 'a'; hdr[38] = 't'; hdr[39] = 'a';
    hdr[40] = uint8_t(dataBytes); hdr[41] = uint8_t(dataBytes >> 8);
    hdr[42] = uint8_t(dataBytes >> 16); hdr[43] = uint8_t(dataBytes >> 24);

    fprintf(stderr, "[WAV] write: samples=%llu dataBytes=%u path=[%s]\n",
            (unsigned long long)samples, dataBytes, path.c_str());
    /* 目标文件可能由 root 创建（权限 644）：普通用户无法截断写入，open 会报 errno=13。
     * 删除文件只需要目录写权限，因此先 unlink 旧文件再重新创建。 */
    std::remove(path.c_str());
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        fprintf(stderr, "[WAV] open failed errno=%d (%s) path=[%s]\n",
                errno, strerror(errno), path.c_str());
        return false;
    }
    f.write(reinterpret_cast<const char*>(hdr), 44);
    f.write(reinterpret_cast<const char*>(data), dataBytes);
    const bool ok = f.good();
    fprintf(stderr, "[WAV] write done ok=%d bad=%d fail=%d eof=%d\n",
            int(ok), int(f.bad()), int(f.fail()), int(f.eof()));
    return ok;
}

}  // namespace wav

int main(int argc, char** argv) {
    qRegisterMetaType<audio::AudioFrame>("audio::AudioFrame");
    qRegisterMetaType<audio::ProcessStats>("audio::ProcessStats");
    qRegisterMetaType<std::vector<int16_t>>("std::vector<int16_t>");

    QApplication app(argc, argv);

    /* 配置来源：环境变量(run.sh 注入) > 默认值；命令行参数可覆盖采集设备名 */
    const QString model     = appconfig::readEnv("ANS_MODEL", appconfig::kDefaultModel);
    const QString license   = appconfig::readEnv("ANS_LICENSE_DIR", appconfig::kDefaultLicenseDir);
    const QString auth      = appconfig::readEnv("ANS_AUTH_CODE", appconfig::kDefaultAuthCode);
    const int     module    = appconfig::kDefaultModule;
    QString capDev = appconfig::readEnv("ANS_CAPTURE_DEV", appconfig::kDefaultCaptureDev);
    QString playDev = appconfig::readEnv("ANS_PLAYBACK_DEV", appconfig::kDefaultPlaybackDev);

    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--cap") == 0) capDev = QString::fromLocal8Bit(argv[i + 1]);
    }

    /* 结果音频目录：run.sh 注入 ANS_OUTPUT_DIR（工程内 output/ 的绝对路径）；
     * 未注入时取可执行文件同级（build/）的上级目录下的 output/，保证不写到工程外。 */
    QString outDir = appconfig::readEnv("ANS_OUTPUT_DIR", QString());
    if (outDir.isEmpty())
        outDir = QDir(QCoreApplication::applicationDirPath())
                     .absoluteFilePath(QStringLiteral("../output"));

    MainWindow win;
    win.setConfig(model, license, auth, module, capDev, playDev, outDir);
    win.buildDevices();      /* 扫描 ALSA 设备填充下拉 */
    win.show();              /* 固定尺寸窗口（尺寸在 buildUi 中按卡片画布算出） */

    return app.exec();
}