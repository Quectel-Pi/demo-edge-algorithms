#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QMainWindow>

class QComboBox;
class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QTimer;
class QVBoxLayout;

namespace tts {
class IAudioSink;
class ITtsEngine;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  MainWindow(std::shared_ptr<tts::IAudioSink> sink,
             std::function<std::shared_ptr<tts::ITtsEngine>()> engine_factory,
             QString default_text, QWidget* parent = nullptr);
  ~MainWindow() override;

 signals:
  void engineStateChanged(const QString& state);
  void speakStateChanged(const QString& state);
  void errorOccurred(const QString& message);
  void synthTimeChanged(const QString& text);

 protected:
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void onSpeakClicked();
  void onStopClicked();
  void onSaveClicked();
  void onExitClicked();
  void onProgressTick();
  void onParamChanged();
  void onSpeakerVolumeChanged(int value);

 private:
  void buildUi();
  void preload();
  void speak(const std::string& text);
  void joinWorker();
  void requestEngineStopAsync();
  void updateEngineState(const QString& state);
  void updateSpeakState(const QString& state);
  void updateResourceStats();
  void applyParamsToEngine();
  void appendPcm(const int16_t* samples, std::size_t count);
  bool saveWav(const QString& path, QString* error);

  std::shared_ptr<tts::IAudioSink> sink_;
  std::function<std::shared_ptr<tts::ITtsEngine>()> engine_factory_;
  std::shared_ptr<tts::ITtsEngine> engine_;
  std::mutex engine_mutex_;

  std::thread loader_;
  std::thread worker_;
  std::thread stop_thread_;
  std::future<void> stop_future_;
  std::atomic<bool> engine_ready_{false};
  std::atomic<bool> engine_failed_{false};
  std::atomic<bool> speaking_{false};
  std::atomic<bool> stop_requested_{false};

  std::vector<int16_t> pcm_;
  std::mutex pcm_mutex_;
  std::atomic<std::size_t> played_samples_{0};

  QLabel* status_pill_ = nullptr;

  QPlainTextEdit* text_edit_ = nullptr;
  QLabel* char_count_ = nullptr;
  QLabel* playback_time_ = nullptr;
  QLabel* playback_state_ = nullptr;
  QLabel* synth_time_ = nullptr;

  QSlider* volume_slider_ = nullptr;
  QSlider* pitch_slider_ = nullptr;
  QSlider* speed_slider_ = nullptr;
  QSlider* speaker_slider_ = nullptr;
  QLabel* volume_value_ = nullptr;
  QLabel* pitch_value_ = nullptr;
  QLabel* speed_value_ = nullptr;
  QLabel* speaker_value_ = nullptr;
  QComboBox* text_mode_ = nullptr;
  QComboBox* num1_mode_ = nullptr;

  QLabel* license_label_ = nullptr;
  QLabel* version_label_ = nullptr;
  QLabel* mem_label_ = nullptr;
  QLabel* cpu_label_ = nullptr;
  QTimer* stats_timer_ = nullptr;
  unsigned long long cpu_last_total_ = 0;
  unsigned long long cpu_last_idle_ = 0;
  bool cpu_has_last_ = false;

  QPushButton* speak_button_ = nullptr;
  QPushButton* stop_button_ = nullptr;
  QPushButton* save_button_ = nullptr;
  QPushButton* exit_button_ = nullptr;
};
