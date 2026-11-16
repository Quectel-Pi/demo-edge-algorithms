// audio_input.h - 音频获取模块
//
// 两个解耦接口 + 板载麦克风实现：
//   IAudioSource    - 音频来源抽象（麦克风/文件/网络可互换）
//   IAudioProcessor - 音频预处理链抽象（降噪 / AEC 的插入点）
//   AlsaAudioSource - IAudioSource 的板载麦克风实现（ALSA C API），
//                     输出 16kHz 单声道归一化 float
//                     不依赖 sherpa-onnx 头文件，直接调系统 ALSA

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <alsa/asoundlib.h>  // snd_pcm_t

namespace asr {

class IAudioSource {
 public:
  virtual ~IAudioSource() = default;

  virtual bool open() = 0;
  virtual void close() = 0;

  // 阻塞读取，返回单声道、归一化到 [-1,1] 的 PCM 采样
  virtual std::vector<float> read(std::size_t max_samples) = 0;

  virtual int sampleRate() const = 0;
  virtual int channels() const = 0;
};

class IAudioProcessor {
 public:
  virtual ~IAudioProcessor() = default;

  // 在采集线程上被调用，可对音频块做修改（如降噪）
  virtual std::vector<float> process(std::vector<float> samples) = 0;
};

// 默认直通处理器：音频原样通过（降噪算法后续在此插入）
class NoopAudioProcessor final : public IAudioProcessor {
 public:
  std::vector<float> process(std::vector<float> samples) override;
};

class AlsaAudioSource final : public IAudioSource {
 public:
  // device: ALSA PCM 设备名；空字符串 = 先试 "default" 再试 "hw:0,0"
  explicit AlsaAudioSource(std::string device = "");
  ~AlsaAudioSource() override;

  bool open() override;
  void close() override;
  std::vector<float> read(std::size_t max_samples) override;
  int sampleRate() const override;
  int channels() const override;

 private:
  std::string device_;
  snd_pcm_t* handle_ = nullptr;
  bool opened_ = false;
};

}  // namespace asr
