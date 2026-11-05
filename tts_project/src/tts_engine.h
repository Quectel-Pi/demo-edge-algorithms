#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace tts {

using PcmSink = std::function<int(const int16_t* samples, std::size_t count,
                                  bool is_last)>;

class ITtsEngine {
 public:
  virtual ~ITtsEngine() = default;

  virtual bool initialize(std::string* error) = 0;
  virtual bool isReady() const = 0;

  virtual bool setParam(unsigned int key, int value) = 0;

  virtual bool speak(const std::string& text, const PcmSink& sink,
                     std::string* error) = 0;

  virtual void stop() = 0;

  virtual void shutdown() = 0;

  virtual std::string version() const = 0;

  virtual int licenseCode() const = 0;
};

std::shared_ptr<ITtsEngine> createEngine();

std::string licenseCodeText(int code);

}
