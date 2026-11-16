#pragma once
/* main.h — 主逻辑模块：应用配置 + 入口 + WAV 写出
 *
 * 定义两类内容：
 *   - appconfig：从环境变量/命令行读取算法模型与授权配置。
 *   - wav      ：16k/单声道/16bit PCM 的 WAV 写出（录制结果落盘用）。
 */

#include <QString>
#include <cstdint>
#include <string>
#include <vector>

/* ---------------- 应用配置 ---------------- */
namespace appconfig {

QString readEnv(const char* name, const QString& fallback);

/* 默认值（run.sh 注入的环境变量优先，此处为兜底） */
extern const QString kDefaultModel;
extern const QString kDefaultLicenseDir;
extern const QString kDefaultCaptureDev;
extern const QString kDefaultAuthCode; /* 授权码（环境变量 ANS_AUTH_CODE 可覆盖） */
extern const int     kDefaultModule;   /* RK3576 */

}  // namespace appconfig

/* ---------------- WAV 写出 ---------------- */
namespace wav {

/* 把单声道 int16 采样写入 16bit PCM WAV。 */
bool writePcmFile(const std::string& path, const int16_t* data,
                  uint64_t samples, uint32_t sampleRate);

}  // namespace wav
