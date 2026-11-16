#include "audio_output.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace tts {

namespace {

constexpr unsigned int kInputRate = 16000;

const char* const kVolumeElemNames[] = {"DACL",     "DACR",     "PCM",
                                        "Playback", "Headphone", "Speaker",
                                        "Master",   "DAC",       "Digital"};
}

AlsaAudioSink::AlsaAudioSink(std::string device, unsigned int hw_rate_override)
    : device_(std::move(device)), override_rate_(hw_rate_override) {}

AlsaAudioSink::~AlsaAudioSink() { close(); }

bool AlsaAudioSink::open() {
  if (opened_) return true;

  snd_pcm_t* handle = nullptr;
  std::vector<const char*> candidates;
  if (!device_.empty()) candidates.push_back(device_.c_str());
  candidates.push_back("plughw:0,0");
  candidates.push_back("default");
  candidates.push_back("hw:0,0");

  int rc = -1;
  const char* used_dev = nullptr;
  for (const char* dev : candidates) {
    rc = snd_pcm_open(&handle, dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (rc >= 0) {
      used_dev = dev;
      break;
    }
    handle = nullptr;
  }
  if (!handle) {
    std::fprintf(stderr, "[alsa-out] open failed: %s\n", snd_strerror(rc));
    return false;
  }
  opened_device_ = used_dev ? used_dev : "";

  snd_pcm_hw_params_t* params;
  snd_pcm_hw_params_alloca(&params);
  snd_pcm_hw_params_any(handle, params);
  snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
  unsigned int channels = 1;
  snd_pcm_hw_params_set_channels_near(handle, params, &channels);
  unsigned int rate = kInputRate;
  snd_pcm_hw_params_set_rate_near(handle, params, &rate, nullptr);
  snd_pcm_uframes_t period = 1024;
  snd_pcm_hw_params_set_period_size_near(handle, params, &period, nullptr);

  snd_pcm_uframes_t buffer = 8192;
  snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer);
  if (snd_pcm_hw_params(handle, params) < 0) {
    std::fprintf(stderr, "[alsa-out] hw_params failed\n");
    snd_pcm_close(handle);
    return false;
  }

  unsigned int got_rate = 0, got_channels = 0;
  snd_pcm_hw_params_get_rate(params, &got_rate, nullptr);
  snd_pcm_hw_params_get_channels(params, &got_channels);
  std::fprintf(stderr, "[alsa-out] opened %s, rate=%u channels=%u\n",
               opened_device_.c_str(), got_rate, got_channels);

  actual_rate_ = got_rate > 0 ? got_rate : kInputRate;
  if (override_rate_ > 0) {

    std::fprintf(stderr,
                 "[alsa-out] forced hardware rate %u (ALSA reports %u), "
                 "software resampling enabled\n",
                 override_rate_, actual_rate_);
    actual_rate_ = override_rate_;
  } else if (actual_rate_ != kInputRate) {
    std::fprintf(stderr,
                 "[alsa-out] hardware rate %u != %u, software resampling enabled\n",
                 actual_rate_, kInputRate);
  }

  snd_pcm_prepare(handle);
  resample_phase_ = 0.0;
  handle_ = handle;
  opened_ = true;
  openMixer();
  startPlayer();
  return true;
}

void AlsaAudioSink::close() {
  stopped_ = true;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.clear();
  }
  player_run_ = false;
  queue_cv_.notify_all();
  if (player_.joinable()) player_.join();

  {
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    if (handle_) {
      snd_pcm_drop(handle_);
      snd_pcm_close(handle_);
      handle_ = nullptr;
    }
  }
  closeMixer();

  if (timing_started_ && input_samples_ > 0) {
    std::fprintf(stderr, "[alsa-out] audio %.2f s queued/played\n",
                 static_cast<double>(input_samples_) / kInputRate);
  }
  timing_started_ = false;
  input_samples_ = 0;
  last_write_s_ = 0.0;
  opened_ = false;
}

bool AlsaAudioSink::write(const int16_t* samples, std::size_t count) {
  if (!opened_ || !handle_ || samples == nullptr || count == 0) return false;
  if (stopped_.load()) return false;

  if (!timing_started_) {
    timing_started_ = true;
    play_start_ = std::chrono::steady_clock::now();
  }
  input_samples_ += count;

  std::vector<int16_t> out;
  if (actual_rate_ == kInputRate) {
    out.assign(samples, samples + count);
  } else {

    const double step = static_cast<double>(kInputRate) / actual_rate_;
    out.reserve(static_cast<std::size_t>(count / step) + 2);
    double pos = resample_phase_;
    while (pos < static_cast<double>(count)) {
      const std::size_t i0 = static_cast<std::size_t>(pos);
      const double frac = pos - static_cast<double>(i0);
      const std::size_t i1 = std::min(i0 + 1, count - 1);
      const double value = samples[i0] * (1.0 - frac) + samples[i1] * frac;
      const long rounded = std::lrint(value);
      out.push_back(static_cast<int16_t>(
          std::max(-32768L, std::min(32767L, rounded))));
      pos += step;
    }
    resample_phase_ = pos - static_cast<double>(count);
  }

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!player_run_.load()) return false;
    queue_.push_back(std::move(out));
  }
  queue_cv_.notify_one();
  updateTiming();
  return true;
}

void AlsaAudioSink::requestStop() {
  stopped_ = true;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.clear();
  }
  queue_cv_.notify_all();

  std::unique_lock<std::mutex> lock(pcm_mutex_, std::try_to_lock);
  if (lock.owns_lock() && handle_) {
    snd_pcm_drop(handle_);
    std::fprintf(stderr, "[alsa-out] requestStop: hw buffer dropped\n");
  } else {
    std::fprintf(stderr, "[alsa-out] requestStop: deferred to player thread\n");
  }
}

void AlsaAudioSink::resetStop() {
  stopped_ = false;
  resample_phase_ = 0.0;
  input_samples_ = 0;
  played_frames_ = 0;
  timing_started_ = false;
  last_write_s_ = 0.0;
  last_report_samples_ = 0;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    queue_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    if (handle_) snd_pcm_prepare(handle_);
  }
  startPlayer();
}

void AlsaAudioSink::flush() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    queue_cv_.wait(lock, [this] {
      return queue_.empty() || stopped_.load() || !player_run_.load();
    });
  }
  if (stopped_.load() || !handle_) return;
  std::lock_guard<std::mutex> lock(pcm_mutex_);
  if (handle_) {
    snd_pcm_drain(handle_);
    snd_pcm_prepare(handle_);
  }
}

bool AlsaAudioSink::isOpen() const { return opened_; }
int AlsaAudioSink::sampleRate() const { return static_cast<int>(kInputRate); }

std::size_t AlsaAudioSink::playedSamples() const {
  const std::size_t frames = played_frames_.load();
  if (actual_rate_ == 0) return frames;

  return static_cast<std::size_t>(static_cast<double>(frames) * kInputRate /
                                  actual_rate_);
}

void AlsaAudioSink::startPlayer() {
  if (player_run_.exchange(true)) return;
  player_ = std::thread(&AlsaAudioSink::playerLoop, this);
}

void AlsaAudioSink::playerLoop() {
  std::vector<int16_t> chunk;
  while (player_run_.load()) {
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait_for(lock, std::chrono::milliseconds(20), [this] {
        return !queue_.empty() || !player_run_.load();
      });
      if (!player_run_.load()) break;
      if (queue_.empty()) continue;
      chunk = std::move(queue_.front());
      queue_.pop_front();
    }
    queue_cv_.notify_all();
    if (stopped_.load()) {

      std::lock_guard<std::mutex> lock(pcm_mutex_);
      if (handle_) snd_pcm_drop(handle_);
      std::fprintf(stderr, "[alsa-out] player stopped, hw buffer dropped\n");
      break;
    }
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    if (handle_) writeRaw(chunk.data(), chunk.size());
  }
}

bool AlsaAudioSink::writeRaw(const int16_t* samples, std::size_t count) {
  std::size_t written = 0;
  while (written < count) {
    if (stopped_.load()) return false;
    const snd_pcm_sframes_t rc = snd_pcm_writei(
        handle_, samples + written,
        static_cast<snd_pcm_uframes_t>(count - written));
    if (rc < 0) {
      if (snd_pcm_recover(handle_, static_cast<int>(rc), 1) < 0) {
        std::fprintf(stderr, "[alsa-out] write error: %s\n", snd_strerror(rc));
        return false;
      }
      continue;
    }
    if (rc == 0) continue;
    written += static_cast<std::size_t>(rc);
  }
  played_frames_.fetch_add(count);
  return true;
}

void AlsaAudioSink::updateTiming() {
  last_write_s_ = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                play_start_)
                      .count();
}

void AlsaAudioSink::openMixer() {
  closeMixer();

  if (snd_mixer_open(&mixer_, 0) < 0) {
    std::fprintf(stderr, "[mixer] snd_mixer_open failed\n");
    mixer_ = nullptr;
    return;
  }

  const char* attached = nullptr;
  char card_name[16];
  for (int card = 0; card < 5; ++card) {
    std::snprintf(card_name, sizeof(card_name), "hw:%d", card);
    if (snd_mixer_attach(mixer_, card_name) >= 0) {
      attached = card_name;
      break;
    }
    snd_mixer_detach(mixer_, card_name);
  }
  if (!attached) {
    std::fprintf(stderr, "[mixer] attach hw:0~4 failed\n");
    closeMixer();
    return;
  }
  snd_mixer_selem_register(mixer_, nullptr, nullptr);
  if (snd_mixer_load(mixer_) < 0) {
    std::fprintf(stderr, "[mixer] snd_mixer_load failed\n");
    closeMixer();
    return;
  }

  for (const char* wanted : kVolumeElemNames) {
    for (snd_mixer_elem_t* e = snd_mixer_first_elem(mixer_); e;
         e = snd_mixer_elem_next(e)) {
      if (!snd_mixer_selem_has_playback_volume(e)) continue;
      if (std::strcmp(snd_mixer_selem_get_name(e), wanted) == 0) {
        vol_elem_ = e;
        break;
      }
    }
    if (vol_elem_) break;
  }

  if (!vol_elem_) {
    for (snd_mixer_elem_t* e = snd_mixer_first_elem(mixer_); e;
         e = snd_mixer_elem_next(e)) {
      if (!snd_mixer_selem_has_playback_volume(e)) continue;
      const char* name = snd_mixer_selem_get_name(e);
      const bool fuzzy = std::strstr(name, "Playback") ||
                         std::strstr(name, "PLAYBACK") ||
                         std::strstr(name, "Volume") ||
                         std::strstr(name, "HP") ||
                         std::strstr(name, "SPK");
      if (fuzzy && snd_mixer_selem_has_playback_switch(e)) {
        vol_elem_ = e;
        break;
      }
    }
  }

  if (!vol_elem_) {
    for (snd_mixer_elem_t* e = snd_mixer_first_elem(mixer_); e;
         e = snd_mixer_elem_next(e)) {
      if (!snd_mixer_selem_has_playback_volume(e)) continue;
      const char* name = snd_mixer_selem_get_name(e);
      if (std::strstr(name, "ADC") || std::strstr(name, "Capture")) continue;
      vol_elem_ = e;
      break;
    }
  }

  if (!vol_elem_) {
    std::fprintf(stderr,
                 "[mixer] no playback volume control on %s, available:", attached);
    for (snd_mixer_elem_t* e = snd_mixer_first_elem(mixer_); e;
         e = snd_mixer_elem_next(e)) {
      if (snd_mixer_selem_has_playback_volume(e)) {
        std::fprintf(stderr, " %s", snd_mixer_selem_get_name(e));
      }
    }
    std::fprintf(stderr, "\n");
    return;
  }

  std::fprintf(stderr, "[mixer] volume control: %s on %s\n",
               snd_mixer_selem_get_name(vol_elem_), attached);

  if (std::strcmp(snd_mixer_selem_get_name(vol_elem_), "DACL") == 0 ||
      std::strcmp(snd_mixer_selem_get_name(vol_elem_), "DACR") == 0) {
    const char* other = std::strcmp(snd_mixer_selem_get_name(vol_elem_), "DACL") == 0
                            ? "DACR"
                            : "DACL";
    for (snd_mixer_elem_t* e = snd_mixer_first_elem(mixer_); e;
         e = snd_mixer_elem_next(e)) {
      if (!snd_mixer_selem_has_playback_volume(e)) continue;
      if (std::strcmp(snd_mixer_selem_get_name(e), other) == 0) {
        vol_elem2_ = e;
        break;
      }
    }
  }
  setVolume(volume_percent_);
}

void AlsaAudioSink::closeMixer() {
  vol_elem_ = nullptr;
  vol_elem2_ = nullptr;
  if (mixer_) {
    snd_mixer_close(mixer_);
    mixer_ = nullptr;
  }
}

bool AlsaAudioSink::setVolume(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  volume_percent_ = percent;
  if (!vol_elem_) return false;

  long minv = 0, maxv = 0;
  if (snd_mixer_selem_get_playback_volume_range(vol_elem_, &minv, &maxv) < 0) {
    return false;
  }
  auto apply = [](snd_mixer_elem_t* e, int pct) -> bool {
    long lo = 0, hi = 0;
    if (snd_mixer_selem_get_playback_volume_range(e, &lo, &hi) < 0) return false;
    const long v = lo + (hi - lo) * pct / 100;
    if (snd_mixer_selem_set_playback_volume_all(e, v) < 0) return false;
    if (snd_mixer_selem_has_playback_switch(e)) {
      snd_mixer_selem_set_playback_switch_all(e, pct > 0 ? 1 : 0);
    }
    return true;
  };
  bool ok = apply(vol_elem_, percent);
  if (vol_elem2_) ok = apply(vol_elem2_, percent) && ok;
  return ok;
}

}
