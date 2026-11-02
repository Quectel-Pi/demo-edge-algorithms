// audio_input.cpp - 音频获取模块实现
//
// AlsaAudioSource 直接用 ALSA C API（alsa/asoundlib.h）实现，
// 不依赖 sherpa-onnx 头文件或 .cc 源码。

#include "audio_input.h"

#include <utility>
#include <cstring>
#include <cstdio>
#include <algorithm>

#include <alsa/asoundlib.h>

namespace asr {

std::vector<float> NoopAudioProcessor::process(std::vector<float> samples) {
  return samples;
}

AlsaAudioSource::AlsaAudioSource(std::string device)
    : device_(std::move(device)) {}

AlsaAudioSource::~AlsaAudioSource() { close(); }

bool AlsaAudioSource::open() {
  if (opened_) return true;

  snd_pcm_t* handle = nullptr;

  // 依次尝试：用户指定 -> plughw:0,0（板载 codec 走 plug 层可自动做
  // 声道/采样率转换）-> default -> hw:0,0
  std::vector<const char*> candidates;
  if (!device_.empty()) candidates.push_back(device_.c_str());
  candidates.push_back("plughw:0,0");
  candidates.push_back("default");
  candidates.push_back("hw:0,0");

  int rc = -1;
  for (const char* dev : candidates) {
    rc = snd_pcm_open(&handle, dev, SND_PCM_STREAM_CAPTURE, 0);
    if (rc >= 0) break;
    handle = nullptr;
  }
  if (!handle) return false;

  // 配置参数：16kHz / 16bit / 单声道
  snd_pcm_hw_params_t* params;
  snd_pcm_hw_params_alloca(&params);
  snd_pcm_hw_params_any(handle, params);
  snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
  unsigned int channels = 1;
  snd_pcm_hw_params_set_channels_near(handle, params, &channels);
  unsigned int rate = 16000;
  snd_pcm_hw_params_set_rate_near(handle, params, &rate, nullptr);
  snd_pcm_uframes_t period = 1024;
  snd_pcm_hw_params_set_period_size_near(handle, params, &period, nullptr);
  if (snd_pcm_hw_params(handle, params) < 0) {
    snd_pcm_close(handle);
    return false;
  }

  // 打印实际协商结果，便于排查（硬件可能只支持 48k / 立体声）
  unsigned int got_rate = 0, got_channels = 0;
  snd_pcm_hw_params_get_rate(params, &got_rate, nullptr);
  snd_pcm_hw_params_get_channels(params, &got_channels);
  fprintf(stderr, "[alsa] opened, rate=%u channels=%u\n", got_rate, got_channels);
  if (got_rate != 16000) {
    fprintf(stderr, "[alsa] WARNING: negotiated rate %u != 16000, "
                    "ASR input will be wrong\n", got_rate);
  }

  handle_ = handle;
  opened_ = true;
  return true;
}

void AlsaAudioSource::close() {
  if (handle_) {
    snd_pcm_close(handle_);
    handle_ = nullptr;
  }
  opened_ = false;
}

std::vector<float> AlsaAudioSource::read(std::size_t max_samples) {
  if (!opened_ || !handle_) return {};

  // 要读的帧数（16kHz 单声道，每帧 2 字节 = int16）
  auto frames = static_cast<snd_pcm_uframes_t>(max_samples);

  std::vector<int16_t> raw(frames);
  int rc = snd_pcm_readi(handle_, raw.data(), frames);
  if (rc < 0) {
    // 打印真实错误并尝试恢复，避免静默丢帧
    fprintf(stderr, "[alsa] read error: %s\n", snd_strerror(rc));
    snd_pcm_recover(handle_, rc, 1);
    return {};
  }
  if (rc != static_cast<int>(frames)) {
    raw.resize(rc);
  }

  // int16 -> float32 归一化 [-1, 1]
  std::vector<float> out(raw.size());
  const float inv = 1.0f / 32768.0f;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    out[i] = raw[i] * inv;
  }
  return out;
}

int AlsaAudioSource::sampleRate() const { return 16000; }
int AlsaAudioSource::channels() const { return 1; }

}  // namespace asr
