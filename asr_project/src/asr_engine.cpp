// asr_engine.cpp - sherpa-onnx C API 流式识别实现
//
// 使用 dewatermark 裁剪包提供的 libsherpa-onnx-c-api.so（4.5MB，完整实现）。
// 关键：config 里的 const char* 只保存指针不拷贝，路径必须存为成员变量。

#include "asr_engine.h"
#include "sherpa-onnx-c-api-fwd.h"

namespace asr {

SherpaAsrEngine::SherpaAsrEngine(const std::string& tokens,
                                 const std::string& encoder,
                                 const std::string& decoder,
                                 const std::string& joiner, int num_threads)
    : tokens_(tokens), encoder_(encoder), decoder_(decoder),
      joiner_(joiner), num_threads_(num_threads) {
  SherpaOnnxOnlineRecognizerConfig config{};
  // 特征提取：模型要求 16 kHz / 80 维
  config.feat_config.sample_rate   = 16000;
  config.feat_config.feature_dim   = 80;
  // 模型文件（const char* 只存指针，路径存在成员变量里）
  config.model_config.transducer.encoder = encoder_.c_str();
  config.model_config.transducer.decoder = decoder_.c_str();
  config.model_config.transducer.joiner  = joiner_.c_str();
  config.model_config.tokens        = tokens_.c_str();
  config.model_config.num_threads   = num_threads_;
  config.model_config.provider      = "cpu";
  config.model_config.model_type    = "";
  config.model_config.modeling_unit = "";
  config.model_config.bpe_vocab     = "";
  // 解码
  config.decoding_method        = "greedy_search";
  config.max_active_paths       = 4;
  config.blank_penalty          = 0.0f;
  // 端点检测：与包内预编译二进制默认值保持一致
  config.enable_endpoint            = 1;
  config.rule1_min_trailing_silence = 2.4f;
  config.rule2_min_trailing_silence = 1.2f;
  config.rule3_min_utterance_length = 20.0f;
  // 未用到的可选字段显式置空字符串，避免实现层拿 NULL 构造 std::string
  config.hotwords_file = "";
  config.hotwords_score = 1.5f;
  config.ctc_fst_decoder_config.graph = "";
  config.ctc_fst_decoder_config.max_active = 3000;
  config.rule_fsts = "";
  config.rule_fars = "";
  config.hr.dict_dir = "";
  config.hr.lexicon = "";
  config.hr.rule_fsts = "";

  recognizer_ = SherpaOnnxCreateOnlineRecognizer(&config);
  if (recognizer_) {
    stream_ = SherpaOnnxCreateOnlineStream(recognizer_);
  }
}

SherpaAsrEngine::~SherpaAsrEngine() {
  if (stream_) {
    SherpaOnnxDestroyOnlineStream(stream_);
    stream_ = nullptr;
  }
  if (recognizer_) {
    SherpaOnnxDestroyOnlineRecognizer(recognizer_);
    recognizer_ = nullptr;
  }
}

int SherpaAsrEngine::sampleRate() const { return sample_rate_; }

void SherpaAsrEngine::feed(const std::vector<float>& samples) {
  if (!stream_ || samples.empty()) return;
  SherpaOnnxOnlineStreamAcceptWaveform(
      stream_, sample_rate_, samples.data(),
      static_cast<int32_t>(samples.size()));
}

void SherpaAsrEngine::decode() {
  if (!recognizer_ || !stream_) return;
  // 有足够帧就连续解码，直到需要更多音频
  while (SherpaOnnxIsOnlineStreamReady(recognizer_, stream_)) {
    SherpaOnnxDecodeOnlineStream(recognizer_, stream_);
  }
}

void SherpaAsrEngine::reset() {
  if (!recognizer_) return;
  if (stream_) {
    SherpaOnnxDestroyOnlineStream(stream_);
    stream_ = nullptr;
  }
  stream_ = SherpaOnnxCreateOnlineStream(recognizer_);
}

bool SherpaAsrEngine::isEndpoint() const {
  return recognizer_ && stream_ &&
         SherpaOnnxOnlineStreamIsEndpoint(recognizer_, stream_);
}

std::string SherpaAsrEngine::partialText() const {
  if (!recognizer_ || !stream_) return "";
  // 直接取结果结构体的 text 字段（比解析 json 可靠，不依赖空格/转义格式）
  const SherpaOnnxOnlineRecognizerResult* r =
      SherpaOnnxGetOnlineStreamResult(recognizer_, stream_);
  if (!r) return "";
  std::string text = r->text ? r->text : "";
  SherpaOnnxDestroyOnlineRecognizerResult(r);
  return text;
}

}  // namespace asr
