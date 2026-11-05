#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <alsa/asoundlib.h>

namespace tts {

class IAudioSink {
 public:
  virtual ~IAudioSink() = default;

  virtual bool open() = 0;
  virtual void close() = 0;

  virtual bool write(const int16_t* samples, std::size_t count) = 0;

  virtual bool isOpen() const = 0;
  virtual int sampleRate() const = 0;

  virtual std::size_t playedSamples() const = 0;

  virtual void requestStop() = 0;

  virtual void resetStop() = 0;

  virtual void flush() = 0;

  virtual bool setVolume(int percent) = 0;
};

class AlsaAudioSink final : public IAudioSink {
 public:

  explicit AlsaAudioSink(std::string device = "",
                         unsigned int hw_rate_override = 0);
  ~AlsaAudioSink() override;

  bool open() override;
  void close() override;
  bool write(const int16_t* samples, std::size_t count) override;
  bool isOpen() const override;
  int sampleRate() const override;
  std::size_t playedSamples() const override;
  void requestStop() override;
  void resetStop() override;
  void flush() override;
  bool setVolume(int percent) override;

 private:
  bool writeRaw(const int16_t* samples, std::size_t count);
  void playerLoop();
  void startPlayer();
  void openMixer();
  void closeMixer();
  void updateTiming();

  std::string device_;
  std::string opened_device_;
  unsigned int override_rate_ = 0;
  snd_pcm_t* handle_ = nullptr;
  bool opened_ = false;

  unsigned int actual_rate_ = 16000;
  double resample_phase_ = 0.0;

  std::deque<std::vector<int16_t>> queue_;
  mutable std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::thread player_;
  std::atomic<bool> player_run_{false};
  std::atomic<bool> stopped_{false};
  std::atomic<std::size_t> played_frames_{0};
  std::mutex pcm_mutex_;

  snd_mixer_t* mixer_ = nullptr;
  snd_mixer_elem_t* vol_elem_ = nullptr;

  snd_mixer_elem_t* vol_elem2_ = nullptr;
  int volume_percent_ = 70;

  std::size_t input_samples_ = 0;
  bool timing_started_ = false;
  double last_write_s_ = 0.0;
  std::size_t last_report_samples_ = 0;
  std::chrono::steady_clock::time_point play_start_{};
};

}
