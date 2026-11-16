// asr_engine.h - 算法接入模块
//
// IAsrEngine      - 流式 ASR 引擎抽象接口（工程其余部分只依赖它）
// SherpaAsrEngine - 唯一接触第三方算法包 sherpa-onnx 的实现
//                  （通过 C API 调用 libsherpa-onnx-c-api.so）
//
// 关键：SherpaOnnxOnlineRecognizerConfig 里的 const char* 只保存指针不拷贝，
// 因此 encoder/decoder/joiner/tokens 必须先存为成员变量再赋值。

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sherpa-onnx-c-api-fwd.h"

namespace asr {

class IAsrEngine {
 public:
  virtual ~IAsrEngine() = default;
  virtual int sampleRate() const = 0;
  virtual void feed(const std::vector<float>& samples) = 0;
  virtual void decode() = 0;
  virtual void reset() = 0;
  virtual bool isEndpoint() const = 0;
  virtual std::string partialText() const = 0;
};

class SherpaAsrEngine final : public IAsrEngine {
 public:
  SherpaAsrEngine(const std::string& tokens, const std::string& encoder,
                  const std::string& decoder, const std::string& joiner,
                  int num_threads = 2);
  ~SherpaAsrEngine() override;

  int sampleRate() const override;
  void feed(const std::vector<float>& samples) override;
  void decode() override;
  void reset() override;
  bool isEndpoint() const override;
  std::string partialText() const override;

 private:
  int sample_rate_ = 16000;
  // 必须存为成员！C API config 里的 const char* 指向这些内部缓冲区，
  // SherpaOnnxCreateOnlineRecognizer 只保存指针不拷贝字符串。
  std::string tokens_;
  std::string encoder_;
  std::string decoder_;
  std::string joiner_;
  int num_threads_;
  // C API v1.12.10 的句柄都是 const 指针
  const SherpaOnnxOnlineRecognizer* recognizer_ = nullptr;
  const SherpaOnnxOnlineStream* stream_ = nullptr;
};

}  // namespace asr
