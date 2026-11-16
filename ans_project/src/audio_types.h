#pragma once
/* audio_types.h — 与算法 SDK 解耦的轻量音频数据结构
 *
 * 上层(界面/采集/回放)只依赖本文件定义的类型与常量，不 include 任何算法 SDK 头。
 * 单通道 16bit 小端 PCM，16000Hz，帧长 160 采样(10ms)。
 */

#include <cstdint>
#include <vector>

#include <QMetaType>

namespace audio {

/* 数据格式约束（与 SDK 手册/示例一致） */
constexpr int kSampleRate    = 16000;   /* Hz */
/* 算法要求每帧 10ms：160 采样 / 320 字节（手册「编码格式」与 6.1.9~6.1.12 各接口说明）。
 * 传 256 采样时引擎每次只写 160 个样本，剩余 96 个样本保持为 0，
 * 结果是「160 样本音频 + 96 样本静音」循环，听感严重发闷/断续。 */
constexpr int kFrameSamples  = 160;     /* 帧长(采样点/帧)，160 = 10ms @16k */
constexpr int kFrameBytes    = kFrameSamples * sizeof(int16_t);
constexpr int kChannels      = 1;       /* 单声道 */

/* 一帧音频数据：单通道 16bit 小端 */
struct AudioFrame {
    std::vector<int16_t> samples;   /* 长度固定为 kFrameSamples */

    AudioFrame() { samples.resize(kFrameSamples, 0); }
};

/* 与具体算法解耦的处理参数。默认值与算法包 demo 的 ANS 模式
 * （example_aivox_aec_ans.cpp 的降噪示例）一致：denoise=0.6。 */
struct ProcessorConfig {
    /* 降噪强度 [0,1]：SDK 的强度接口(aivox_aec_ans_set_denoise_strength)，
     * 平稳降噪与非平稳降噪共用该参数，无法单独调节。 */
    float denoiseStrength = 0.6f;
    int   module          = 3;      /* SLAS 板型：3=RK3576（M2） */

    static ProcessorConfig defaults() { return ProcessorConfig{}; }
};

/* 单次处理计时结果（供界面展示 RTF / 均耗时） */
struct ProcessStats {
    long long frames      = 0;    /* 已处理帧数 */
    double    totalMs     = 0.0;  /* 累计处理时长(ms) */
    double    maxMs       = 0.0;  /* 单帧最大耗时(ms) */
    double    audioSec    = 0.0;  /* 对应音频时长(s) */
    int       peakLevel   = 0;    /* 最近一帧输入峰值(满量程 0..32767) */

    double avgMs() const { return frames > 0 ? totalMs / frames : 0.0; }
    double rtf() const  { return (frames > 0 && audioSec > 0.0) ? totalMs / 1000.0 / audioSec : 0.0; }
    int    levelPercent() const { return peakLevel * 100 / 32768; }
};

}  // namespace audio

Q_DECLARE_METATYPE(audio::AudioFrame)
Q_DECLARE_METATYPE(audio::ProcessStats)