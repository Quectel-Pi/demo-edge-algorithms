#include "tts_engine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <atomic>
#include <vector>

extern "C" {
#include "aivox_slas.h"
#include "aivox_tts_api.h"
#include "aivox_tts_config.h"
}

extern const unsigned char g_aivox_resource_blob[];

namespace tts {

namespace {

PcmSink g_sink;
std::mutex g_bridge_mutex;

const char* kAuthCodeEnv = "TTS_AUTH_CODE";
const char* kLicenseDirEnv = "TTS_LICENSE_DIR";
const char* kDefaultLicenseDir = "/data";

int bridgeCallback(void*, int, int ds, int,
                   int dSize, const void* dBuffer) {
  if (dSize <= 0 || dBuffer == nullptr) return 0;

  static std::atomic<int> diag_blocks{0};
  const int idx = diag_blocks.fetch_add(1);
  if (idx < 3) {
    std::fprintf(stderr, "[tts] pcm block #%d: dSize=%d ds=%d\n", idx, dSize,
                 ds);
  }

  PcmSink sink;
  {
    std::lock_guard<std::mutex> lock(g_bridge_mutex);
    sink = g_sink;
  }
  if (!sink) return 0;
  const auto* samples = static_cast<const int16_t*>(dBuffer);
  const std::size_t count = static_cast<std::size_t>(dSize) / sizeof(int16_t);
  return sink(samples, count, ds == 1);
}

}

class AivoxTtsEngine final : public ITtsEngine {
 public:
  AivoxTtsEngine() {
    char buf[64] = {0};
    aivox_tts_getVersion(buf);
    version_ = buf;
  }

  ~AivoxTtsEngine() override { shutdown(); }

  bool initialize(std::string* error) override {
    if (ready_) return true;

    const char* dir_env = std::getenv(kLicenseDirEnv);
    const char* dir = (dir_env && *dir_env) ? dir_env : kDefaultLicenseDir;
    license_dir_ = dir;

    license_dir_buf_ = license_dir_;
    int rc = aivox_slas_set_license_path_dir(license_dir_buf_.data());
    if (rc != AIVOX_SLAS_OK) {
      logLine("set_license_path_dir failed", rc);
    }

    rc = aivox_slas_set_dev(AIVOX_SLAS_HAL, DEV_RK3576, 3, nullptr);
    if (rc != AIVOX_SLAS_OK) logLine("set_dev failed", rc);

    const char* code_env = std::getenv(kAuthCodeEnv);
    if (code_env && *code_env) {
      auth_code_ = code_env;
      rc = aivox_slas_func_http_request_license(nullptr, nullptr, TTS,
                                                auth_code_.data());
      logLine("http_request_license(TTS)", rc);
    }

    rc = aivox_slas_func_licenses_verify(nullptr, nullptr, TTS);
    license_code_ = rc;
    logLine("licenses_verify(TTS)", rc);

    {
      std::lock_guard<std::mutex> lock(g_bridge_mutex);
      g_sink = nullptr;
    }
    rc = aivox_tts_create(nullptr, nullptr, &bridgeCallback,
                          const_cast<unsigned char*>(g_aivox_resource_blob));
    if (rc != 0) {
      if (error) *error = "aivox_tts_create failed, code " + std::to_string(rc);
      logLine("aivox_tts_create", rc);
      return false;
    }

    aivox_tts_SetParam(AIVOX_TTS_PARAM_VOLUME, 0);
    aivox_tts_SetParam(AIVOX_TTS_PARAM_PITCH, 0);
    aivox_tts_SetParam(AIVOX_TTS_PARAM_SPEED, 0);
    aivox_tts_SetParam(AIVOX_TTS_PARAM_TEXT_MODE, AIVOX_TTS_READDIGIT_AUTO);
    aivox_tts_SetParam(AIVOX_TTS_PARAM_NUM1_MODE, AIVOX_TTS_CHNUM1_READ_YI);
    aivox_tts_SetParam(AIVOX_TTS_PARAM_CHUNK_MS, 30);
    aivox_tts_SetParam(AIVOX_TTS_PARAM_INPUT_CODEPAGE, AIVOX_TTS_CODEPAGE_UTF8);

    created_ = true;
    ready_ = true;
    if (error) error->clear();
    return true;
  }

  bool isReady() const override { return ready_; }

  bool setParam(unsigned int key, int value) override {
    if (!created_) return false;

    return aivox_tts_SetParam(key, static_cast<unsigned int>(value)) == 0;
  }

  bool speak(const std::string& text, const PcmSink& sink,
             std::string* error) override {
    if (!created_) {
      if (error) *error = "engine not initialized";
      return false;
    }
    if (text.empty()) {
      if (error) *error = "text is empty";
      return false;
    }

    std::string clean;
    clean.reserve(text.size());
    bool last_space = false;
    for (char c : text) {
      const bool is_space = (c == '\n' || c == '\r' || c == '\t' || c == ' ');
      if (is_space) {
        if (!last_space) clean.push_back(' ');
        last_space = true;
      } else {
        clean.push_back(c);
        last_space = false;
      }
    }
    while (!clean.empty() && clean.front() == ' ') clean.erase(clean.begin());
    while (!clean.empty() && clean.back() == ' ') clean.pop_back();
    if (clean.empty()) {
      if (error) *error = "text is empty after sanitizing";
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(g_bridge_mutex);
      g_sink = sink;
    }

    std::vector<std::string> segments;
    std::size_t begin = 0;
    static const char* const kMarks[] = {"。", "！", "？", "；"};
    while (begin < clean.size()) {
      std::size_t next = clean.size();
      for (const char* mark : kMarks) {
        const std::size_t p = clean.find(mark, begin);
        if (p != std::string::npos && p + std::strlen(mark) < next) {
          next = p + std::strlen(mark);
        }
      }
      std::string seg = clean.substr(begin, next - begin);
      while (!seg.empty() && seg.front() == ' ') seg.erase(seg.begin());
      while (!seg.empty() && seg.back() == ' ') seg.pop_back();
      if (!seg.empty()) {
        if (seg.size() <= 600) {
          segments.push_back(seg);
        } else {

          std::size_t sub_begin = 0;
          while (sub_begin < seg.size()) {
            std::size_t sub_end = seg.find("，", sub_begin);
            if (sub_end == std::string::npos) {
              sub_end = seg.size();
            } else {
              sub_end += 3;
            }
            std::string sub = seg.substr(sub_begin, sub_end - sub_begin);
            while (!sub.empty() && sub.front() == ' ') sub.erase(sub.begin());
            while (!sub.empty() && sub.back() == ' ') sub.pop_back();
            if (!sub.empty()) segments.push_back(sub);
            sub_begin = sub_end;
          }
        }
      }
      begin = next;
    }

    int failed = 0;
    stopped_ = false;
    for (const auto& seg : segments) {
      if (stopped_.load()) break;
      const int rc = aivox_tts_TextPut(seg.c_str(),
                                       static_cast<unsigned int>(seg.size()));
      if (rc != 0) ++failed;
    }

    {
      std::lock_guard<std::mutex> lock(g_bridge_mutex);
      g_sink = nullptr;
    }

    if (segments.empty()) {
      if (error) *error = "text is empty after splitting";
      return false;
    }
    if (failed == static_cast<int>(segments.size())) {
      if (error) *error = "all segments failed";
      return false;
    }
    return true;
  }

  void stop() override {
    stopped_ = true;
    if (created_) aivox_tts_Stop();
  }

  void shutdown() override {
    if (created_) {
      aivox_tts_Destroy();
      created_ = false;
    }
    ready_ = false;
    std::lock_guard<std::mutex> lock(g_bridge_mutex);
    g_sink = nullptr;
  }

  std::string version() const override { return version_; }
  int licenseCode() const override { return license_code_; }

 private:
  static void logLine(const char* what, int code) {
    std::fprintf(stderr, "[slas] %s -> %d\n", what, code);
  }

  bool ready_ = false;
  bool created_ = false;
  std::atomic<bool> stopped_{false};
  int license_code_ = -1;
  std::string version_;
  std::string license_dir_;
  std::string license_dir_buf_;
  std::string auth_code_;
};

std::shared_ptr<ITtsEngine> createEngine() {
  return std::make_shared<AivoxTtsEngine>();
}

std::string licenseCodeText(int code) {
  switch (code) {
    case 0: return "OK";
    case 100001: return "Not authorized";
    case 100002: return "Trial timeout";
    case 100005: return "Get device info failed";
    case 100010: return "License server unreachable";
    case 100012: return "License file missing";
    case 100014: return "License mismatch";
    case 100016: return "Platform mismatch";
    default: return "Error " + std::to_string(code);
  }
}

}
