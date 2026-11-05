#include "main_window.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "audio_output.h"
#include "tts_engine.h"

namespace {

constexpr unsigned int kVolumeKey = 1;
constexpr unsigned int kPitchKey = 2;
constexpr unsigned int kSpeedKey = 3;
constexpr unsigned int kTextModeKey = 4;
constexpr unsigned int kNum1ModeKey = 5;

constexpr int kSampleRate = 16000;

int algoParamValue(int slider_value) { return slider_value * 327; }

const char* kStyleSheet = R"QSS(
QFrame#root { background:#E6EDF9; }
QFrame#page { background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #F5F8FE, stop:1 #E9EFFC);
              border:1px solid #DCE5F5; border-radius:18px; }
QFrame#topbar { background:#F4F8FF; border-bottom:1px solid #E3EAF7;
                border-top-left-radius:18px; border-top-right-radius:18px; }
QLabel#logo { background:qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #2F5BFF, stop:1 #5A84FF);
              color:#FFFFFF; border-radius:10px; font-size:15px; font-weight:800; }
QLabel#brandTitle { font-size:15px; font-weight:700; color:#132A4A; }
QLabel#brandSub { font-size:12px; color:#58709B; }
QLabel#pill { background:#FFFFFF; border:1px solid #E3EAF7; border-radius:13px;
              padding:6px 13px; font-size:12px; font-weight:600; color:#58709B; }
QFrame#card { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #FFFFFF, stop:1 #F3F7FE);
              border:1px solid #DFE8F6; border-bottom:2px solid #CFDAEC; border-radius:14px; }
QLabel#secTitle { font-size:12px; letter-spacing:1px; color:#8FA5CE; font-weight:700; }
QLabel#hint { font-size:12px; color:#58709B; }
QLabel#value { font-size:13px; color:#132A4A; font-weight:700; }
QLabel#playback { font-size:15px; color:#132A4A; font-weight:700; }
QLabel#mono { font-size:12px; color:#58709B; }
QLabel#statePill { border-radius:13px; padding:5px 12px; font-size:13px; font-weight:700; }
QPlainTextEdit#input { background:#FFFFFF; color:#132A4A; border:1px solid #DCE5F5;
                       border-radius:12px; padding:16px; font-size:17px;
                       selection-background-color:#2F5BFF; selection-color:#FFFFFF;
                       placeholder-text-color:#A5B4CE; }
QPlainTextEdit#input:focus { border:1px solid #3F6FE0; }
QPushButton#btnPrimary { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,
                            stop:0 #4F80FF, stop:1 #2A54E8);
                         color:#FFFFFF; border:none; border-bottom:3px solid #1E44C4;
                         border-radius:12px; font-size:15px; font-weight:700; }
QPushButton#btnPrimary:hover { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,
                                  stop:0 #3B6DFF, stop:1 #2450DD); }
QPushButton#btnPrimary:pressed { background:#2450DD; border-bottom:1px solid #1E44C4; }
QPushButton#btnPrimary:disabled { background:#C7D4EE; border-bottom:2px solid #B4C3E2;
                                  color:#F5F8FF; }
QPushButton#btnGhost { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,
                           stop:0 #FFFFFF, stop:1 #E8EFFF);
                       color:#2F5BFF; border:1.5px solid #BCD0FB;
                       border-bottom:2px solid #A9C0F4; border-radius:12px;
                       font-size:14px; font-weight:700; }
QPushButton#btnGhost:hover { background:#EEF3FF; }
QPushButton#btnGhost:pressed { background:#DBE6FF; border-bottom:1px solid #A9C0F4; }
QPushButton#btnGhost:disabled { color:#A8BBDA; border-color:#E1E9F7; }
QSlider::groove:horizontal { height:6px; background:#E4EBF8; border-radius:3px; }
QSlider::sub-page:horizontal { background:#4F80FF; border-radius:3px; }
QSlider::handle:horizontal { width:16px; height:16px; margin:-6px 0;
                             border-radius:8px; background:#FFFFFF;
                             border:2px solid #4F80FF; }
QComboBox { background:#FFFFFF; border:1px solid #D3DDF0; border-radius:8px;
            padding:5px 10px; font-size:13px; color:#132A4A; }
QComboBox::drop-down { border:none; width:18px; }
QComboBox QAbstractItemView { background:#FFFFFF; border:1px solid #D3DDF0;
                              selection-background-color:#E9EFFF; color:#132A4A; }
)QSS";

QFrame* makeCard(QWidget* parent) {
  auto* card = new QFrame(parent);
  card->setObjectName("card");
  return card;
}

QLabel* makeSectionTitle(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text.toUpper(), parent);
  label->setObjectName("secTitle");
  return label;
}

QLabel* makeHint(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setObjectName("hint");
  return label;
}

QLabel* makeValueLabel(QWidget* parent) {
  auto* label = new QLabel("0", parent);
  label->setObjectName("value");
  label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  label->setFixedWidth(60);
  return label;
}

QSlider* makeSlider(QWidget* parent) {
  auto* slider = new QSlider(Qt::Horizontal, parent);

  slider->setRange(-100, 100);
  slider->setSingleStep(10);
  slider->setPageStep(100);
  slider->setValue(0);
  slider->setFocusPolicy(Qt::NoFocus);
  return slider;
}

bool readMemInfo(int* used_mb, int* total_mb) {
  std::FILE* fp = std::fopen("/proc/meminfo", "r");
  if (!fp) return false;

  long total_kb = 0, free_kb = 0, buffers_kb = 0, cached_kb = 0;
  long reclaim_kb = 0, avail_kb = 0;
  char line[256];
  while (std::fgets(line, sizeof(line), fp)) {
    long value = 0;
    if (std::sscanf(line, "MemTotal: %ld", &value) == 1) {
      total_kb = value;
    } else if (std::sscanf(line, "MemFree: %ld", &value) == 1) {
      free_kb = value;
    } else if (std::sscanf(line, "Buffers: %ld", &value) == 1) {
      buffers_kb = value;
    } else if (std::sscanf(line, "Cached: %ld", &value) == 1) {
      cached_kb = value;
    } else if (std::sscanf(line, "SReclaimable: %ld", &value) == 1) {
      reclaim_kb = value;
    } else if (std::sscanf(line, "MemAvailable: %ld", &value) == 1) {
      avail_kb = value;
    }
  }
  std::fclose(fp);
  if (total_kb <= 0) return false;

  long used_kb = 0;
  if (buffers_kb > 0 || cached_kb > 0 || reclaim_kb > 0) {
    used_kb = total_kb - free_kb - buffers_kb - cached_kb - reclaim_kb;
  } else {
    used_kb = total_kb - avail_kb;
  }
  if (used_kb < 0) used_kb = 0;

  *total_mb = int(total_kb / 1024);
  *used_mb = int(used_kb / 1024);
  return true;
}

bool readCpuTotal(unsigned long long* total, unsigned long long* idle) {
  std::FILE* fp = std::fopen("/proc/stat", "r");
  if (!fp) return false;

  char line[512];
  bool ok = false;
  if (std::fgets(line, sizeof(line), fp)) {
    unsigned long long f[10] = {0};
    const int n = std::sscanf(
        line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &f[0],
        &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7], &f[8], &f[9]);
    if (n >= 4) {
      unsigned long long sum = 0;
      for (int i = 0; i < n; ++i) sum += f[i];
      *total = sum;
      *idle = f[3] + (n > 4 ? f[4] : 0);
      ok = true;
    }
  }
  std::fclose(fp);
  return ok;
}

}

MainWindow::MainWindow(
    std::shared_ptr<tts::IAudioSink> sink,
    std::function<std::shared_ptr<tts::ITtsEngine>()> engine_factory,
    QString default_text, QWidget* parent)
    : QMainWindow(parent),
      sink_(std::move(sink)),
      engine_factory_(std::move(engine_factory)) {
  setWindowTitle("Quectel Pi M2 - TTS Demo");
  setFixedSize(1280, 720);

  setStyleSheet(QString::fromUtf8(kStyleSheet));
  buildUi();

  text_edit_->setPlainText(default_text);

  loader_ = std::thread(&MainWindow::preload, this);
}

MainWindow::~MainWindow() {
  joinWorker();
  if (stop_thread_.joinable()) stop_thread_.join();
  loader_.join();
}

void MainWindow::buildUi() {
  auto* root = new QFrame(this);
  root->setObjectName("root");
  setCentralWidget(root);

  auto* root_layout = new QVBoxLayout(root);
  root_layout->setContentsMargins(0, 0, 0, 0);
  root_layout->setSpacing(0);

  auto* page = new QFrame(root);
  page->setObjectName("page");
  root_layout->addWidget(page);

  auto place = [](QWidget* w, int x, int y, int width, int height) {
    w->setGeometry(x, y, width, height);
  };

  auto* topbar = new QFrame(page);
  topbar->setObjectName("topbar");
  place(topbar, 0, 0, 1280, 66);

  auto* logo = new QLabel("PI", topbar);
  logo->setObjectName("logo");
  logo->setAlignment(Qt::AlignCenter);
  place(logo, 20, 15, 36, 36);

  auto* title = new QLabel("Quectel Pi M2 - TTS Demo", topbar);
  title->setObjectName("brandTitle");
  place(title, 68, 20, 520, 26);

  status_pill_ = new QLabel("ENGINE: LOADING", topbar);
  status_pill_->setObjectName("statePill");
  status_pill_->setAlignment(Qt::AlignCenter);
  status_pill_->setStyleSheet("color:#C07A00; background:#FFF3D6;");
  place(status_pill_, 1090, 18, 170, 30);

  auto* text_card = makeCard(page);
  place(text_card, 18, 82, 782, 506);
  auto* text_title = makeSectionTitle("Text to Synthesize", text_card);
  place(text_title, 18, 14, 300, 22);

  text_edit_ = new QPlainTextEdit(text_card);
  text_edit_->setObjectName("input");
  text_edit_->setPlaceholderText("Type Chinese text to synthesize...");
  place(text_edit_, 18, 44, 746, 410);

  auto* text_hint = makeHint("UTF-8 input, Chinese supported", text_card);
  place(text_hint, 18, 466, 300, 20);
  char_count_ = makeHint("0 characters", text_card);
  char_count_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  place(char_count_, 644, 466, 120, 20);

  auto* playback_card = makeCard(page);
  place(playback_card, 18, 604, 782, 100);
  auto* playback_title = makeSectionTitle("Playback", playback_card);
  place(playback_title, 18, 14, 200, 22);

  playback_time_ = new QLabel("Audio: 0.0 s", playback_card);
  playback_time_->setObjectName("playback");
  place(playback_time_, 18, 42, 300, 26);

  playback_state_ = new QLabel("Idle", playback_card);
  playback_state_->setObjectName("statePill");
  playback_state_->setAlignment(Qt::AlignCenter);
  playback_state_->setStyleSheet("color:#2F5BFF; background:#E9EFFF;");
  place(playback_state_, 654, 34, 110, 32);

  synth_time_ = new QLabel("Synth time: -", playback_card);
  synth_time_->setObjectName("mono");
  place(synth_time_, 18, 68, 300, 20);

  auto* param_card = makeCard(page);
  place(param_card, 816, 82, 446, 268);
  auto* param_title = makeSectionTitle("Parameters", param_card);
  place(param_title, 18, 12, 200, 22);

  auto placeSliderRow = [&](int y, const QString& name, const QString& hint,
                            QSlider** slider, QLabel** value) {
    auto* name_label = new QLabel(name, param_card);
    place(name_label, 18, y, 90, 18);
    auto* hint_label = makeHint(hint, param_card);
    place(hint_label, 108, y, 150, 18);
    *value = makeValueLabel(param_card);
    place(*value, 350, y, 60, 18);
    *slider = makeSlider(param_card);
    place(*slider, 18, y + 22, 410, 20);
  };
  placeSliderRow(38, "Volume", "-100% ~ +100%", &volume_slider_, &volume_value_);
  placeSliderRow(82, "Pitch", "-100% ~ +100%", &pitch_slider_, &pitch_value_);
  placeSliderRow(126, "Speed", "0.50x ~ 1.50x", &speed_slider_, &speed_value_);

  auto* speaker_label = new QLabel("Speaker", param_card);
  place(speaker_label, 18, 170, 90, 18);
  auto* speaker_hint = makeHint("hardware 0-100", param_card);
  place(speaker_hint, 108, 170, 150, 18);
  speaker_value_ = makeValueLabel(param_card);
  speaker_value_->setText("70");
  place(speaker_value_, 350, 170, 60, 18);
  speaker_slider_ = new QSlider(Qt::Horizontal, param_card);
  speaker_slider_->setRange(0, 100);
  speaker_slider_->setValue(70);
  speaker_slider_->setFocusPolicy(Qt::NoFocus);
  place(speaker_slider_, 18, 192, 410, 20);

  auto* mode_label = new QLabel("Number reading", param_card);
  place(mode_label, 18, 216, 150, 18);
  text_mode_ = new QComboBox(param_card);
  text_mode_->addItem("Auto", 0);
  text_mode_->addItem("Digit by digit", 1);
  text_mode_->addItem("As value", 2);
  place(text_mode_, 18, 236, 195, 28);

  auto* num1_label = new QLabel("Read 1 as", param_card);
  place(num1_label, 233, 216, 150, 18);
  num1_mode_ = new QComboBox(param_card);
  num1_mode_->addItem("yi (1)", 1);
  num1_mode_->addItem("yao (yao)", 0);
  place(num1_mode_, 233, 236, 195, 28);

  auto* status_card = makeCard(page);
  place(status_card, 816, 364, 446, 140);
  auto* status_title = makeSectionTitle("Status", status_card);
  place(status_title, 18, 12, 200, 20);

  license_label_ = new QLabel("License: checking...", status_card);
  license_label_->setObjectName("mono");
  place(license_label_, 18, 36, 410, 18);

  version_label_ = new QLabel("TTS version: -", status_card);
  version_label_->setObjectName("mono");
  place(version_label_, 18, 56, 410, 18);

  mem_label_ = new QLabel("Memory: -", status_card);
  mem_label_->setObjectName("mono");
  place(mem_label_, 18, 76, 410, 18);

  cpu_label_ = new QLabel("CPU: -", status_card);
  cpu_label_->setObjectName("mono");
  place(cpu_label_, 18, 96, 410, 18);

  auto* action_card = makeCard(page);
  place(action_card, 816, 518, 446, 186);
  auto* action_title = makeSectionTitle("Actions", action_card);
  place(action_title, 18, 12, 200, 20);

  speak_button_ = new QPushButton("SPEAK", action_card);
  speak_button_->setObjectName("btnPrimary");
  speak_button_->setEnabled(false);
  place(speak_button_, 18, 36, 410, 46);

  stop_button_ = new QPushButton("STOP", action_card);
  stop_button_->setObjectName("btnGhost");
  stop_button_->setEnabled(false);
  place(stop_button_, 18, 90, 200, 40);

  save_button_ = new QPushButton("SAVE WAV", action_card);
  save_button_->setObjectName("btnGhost");
  save_button_->setEnabled(false);
  place(save_button_, 228, 90, 200, 40);

  exit_button_ = new QPushButton("EXIT", action_card);
  exit_button_->setObjectName("btnGhost");
  place(exit_button_, 18, 138, 410, 40);

  connect(text_edit_, &QPlainTextEdit::textChanged, this, [this]() {
    char_count_->setText(QString::number(text_edit_->toPlainText().size()) +
                         " characters");
  });
  connect(speak_button_, &QPushButton::clicked, this,
          &MainWindow::onSpeakClicked);
  connect(stop_button_, &QPushButton::clicked, this, &MainWindow::onStopClicked);
  connect(save_button_, &QPushButton::clicked, this, &MainWindow::onSaveClicked);
  connect(exit_button_, &QPushButton::clicked, this, &MainWindow::onExitClicked);

  connect(volume_slider_, &QSlider::valueChanged, this,
          &MainWindow::onParamChanged);
  connect(pitch_slider_, &QSlider::valueChanged, this,
          &MainWindow::onParamChanged);
  connect(speed_slider_, &QSlider::valueChanged, this,
          &MainWindow::onParamChanged);
  connect(text_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::onParamChanged);
  connect(num1_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::onParamChanged);
  connect(speaker_slider_, &QSlider::valueChanged, this,
          &MainWindow::onSpeakerVolumeChanged);

  connect(this, &MainWindow::synthTimeChanged, this,
          [this](const QString& text) { synth_time_->setText(text); });

  connect(this, &MainWindow::engineStateChanged, this,
          [this](const QString& state) { updateEngineState(state); });
  connect(this, &MainWindow::speakStateChanged, this,
          [this](const QString& state) { updateSpeakState(state); });
  connect(this, &MainWindow::errorOccurred, this, [this](const QString& msg) {
    playback_state_->setText("Error");
    playback_state_->setStyleSheet("color:#B23A3A; background:#FDE8E8;");
    playback_time_->setText(msg);
  });

  char_count_->setText("0 characters");

  stats_timer_ = new QTimer(this);
  stats_timer_->setInterval(1000);
  connect(stats_timer_, &QTimer::timeout, this, &MainWindow::onProgressTick);
  stats_timer_->start();
}

void MainWindow::preload() {
  auto engine = engine_factory_();
  std::string error;
  const bool ok = engine->initialize(&error);
  {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    engine_ = engine;
  }
  if (!ok) {
    engine_failed_ = true;
    emit engineStateChanged("FAILED");
    emit errorOccurred(QString::fromStdString(error));
    return;
  }
  engine_ready_ = true;
  emit engineStateChanged("READY");
}

void MainWindow::onSpeakClicked() {
  if (speaking_) return;
  if (!engine_ready_) {
    emit errorOccurred(engine_failed_ ? "Engine failed to initialize"
                                     : "Engine is still initializing, please wait");
    return;
  }
  const QString text = text_edit_->toPlainText().trimmed();
  if (text.isEmpty()) {
    emit errorOccurred("Please enter text to synthesize");
    return;
  }

  joinWorker();

  {
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    pcm_.clear();
  }
  played_samples_ = 0;
  stop_requested_ = false;
  speaking_ = true;

  sink_->resetStop();
  sink_->setVolume(speaker_slider_->value());

  speak_button_->setEnabled(false);
  stop_button_->setEnabled(true);
  playback_state_->setText("Synthesizing");
  playback_state_->setStyleSheet("color:#C07A00; background:#FFF3D6;");
  playback_time_->setText("Audio: 0.0 s");
  synth_time_->setText("Synth time: -");

  worker_ = std::thread(&MainWindow::speak, this, text.toStdString());
}

void MainWindow::speak(const std::string& text) {
  std::shared_ptr<tts::ITtsEngine> engine;
  {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    engine = engine_;
  }
  if (!engine) {
    speaking_ = false;
    emit speakStateChanged("IDLE");
    return;
  }

  if (!sink_->isOpen() && !sink_->open()) {
    speaking_ = false;
    emit errorOccurred("Audio output unavailable (check ALSA device)");
    emit speakStateChanged("IDLE");
    return;
  }

  auto sink_cb = [this](const int16_t* samples, std::size_t count,
                        bool ) -> int {
    if (stop_requested_) return -1;
    {
      std::lock_guard<std::mutex> lock(pcm_mutex_);
      pcm_.insert(pcm_.end(), samples, samples + count);
    }
    played_samples_ += count;
    if (!sink_->write(samples, count)) return -1;
    return 0;
  };

  std::string error;

  const auto synth_start = std::chrono::steady_clock::now();
  const bool ok = engine->speak(text, sink_cb, &error);
  const double synth_s = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - synth_start)
                             .count();
  emit synthTimeChanged(QString("Synth time: %1 s").arg(synth_s, 0, 'f', 2));
  speaking_ = false;

  {
    std::size_t total = 0;
    {
      std::lock_guard<std::mutex> lock(pcm_mutex_);
      total = pcm_.size();
    }
    std::fprintf(stderr, "[tts] synthesized %.2f s (%zu samples, assumed 16 kHz)\n",
                 static_cast<double>(total) / kSampleRate, total);
  }

  if (!ok) {
    if (stop_requested_) {
      emit speakStateChanged("STOPPED");
    } else {
      emit errorOccurred(QString::fromStdString(error));
      emit speakStateChanged("IDLE");
    }
    return;
  }

  sink_->flush();
  emit speakStateChanged(stop_requested_ ? "STOPPED" : "DONE");
}

void MainWindow::onStopClicked() {
  stop_requested_ = true;

  sink_->requestStop();
  playback_state_->setText("STOPPED");
  playback_state_->setStyleSheet("color:#58709B; background:#EEF3FB;");

  requestEngineStopAsync();
}

void MainWindow::requestEngineStopAsync() {
  std::shared_ptr<tts::ITtsEngine> engine;
  {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    engine = engine_;
  }
  if (!engine) return;
  if (stop_thread_.joinable()) stop_thread_.join();
  std::promise<void> done;
  stop_future_ = done.get_future();
  stop_thread_ = std::thread([engine, done = std::move(done)]() mutable {
    engine->stop();
    done.set_value();
  });
}

void MainWindow::onSpeakerVolumeChanged(int value) {
  speaker_value_->setText(QString::number(value));
  sink_->setVolume(value);
}

void MainWindow::onSaveClicked() {
  QString error;
  if (!saveWav(QString(), &error)) {
    emit errorOccurred(error.isEmpty() ? "Save WAV failed" : error);
  }
}

bool MainWindow::saveWav(const QString& path, QString* error) {
  std::vector<int16_t> samples;
  {
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    samples = pcm_;
  }
  if (samples.empty()) {
    if (error) *error = "No PCM data yet, press SPEAK first";
    return false;
  }

  QString target = path;
  if (target.isEmpty()) {
    const QString dir = QCoreApplication::applicationDirPath() + "/output";
    QDir().mkpath(dir);
    target = dir + "/tts_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".wav";
  }

  std::FILE* fp = std::fopen(target.toUtf8().constData(), "wb");
  if (!fp) {
    const QString fallback = "/tmp/tts_" +
        QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".wav";
    fp = std::fopen(fallback.toUtf8().constData(), "wb");
    if (!fp) {
      if (error) *error = "Cannot open file for writing";
      return false;
    }
    target = fallback;
  }

  const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * 2);
  const uint32_t rate = kSampleRate;
  const uint16_t channels = 1;
  const uint16_t bits = 16;
  const uint32_t byte_rate = rate * channels * bits / 8;
  const uint16_t block_align = channels * bits / 8;
  const uint32_t riff_size = 36 + data_bytes;

  std::fwrite("RIFF", 1, 4, fp);
  std::fwrite(&riff_size, 4, 1, fp);
  std::fwrite("WAVEfmt ", 1, 8, fp);
  const uint32_t fmt_size = 16;
  const uint16_t fmt_pcm = 1;
  std::fwrite(&fmt_size, 4, 1, fp);
  std::fwrite(&fmt_pcm, 2, 1, fp);
  std::fwrite(&channels, 2, 1, fp);
  std::fwrite(&rate, 4, 1, fp);
  std::fwrite(&byte_rate, 4, 1, fp);
  std::fwrite(&block_align, 2, 1, fp);
  std::fwrite(&bits, 2, 1, fp);
  std::fwrite("data", 1, 4, fp);
  std::fwrite(&data_bytes, 4, 1, fp);
  std::fwrite(samples.data(), 2, samples.size(), fp);
  std::fclose(fp);

  playback_time_->setText("Saved: " + target);
  return true;
}

void MainWindow::onExitClicked() {
  close();
}

void MainWindow::onProgressTick() {
  updateResourceStats();

  if (speaking_) {

    const double seconds =
        static_cast<double>(sink_->playedSamples()) / kSampleRate;
    playback_time_->setText(QString("Audio: %1 s").arg(seconds, 0, 'f', 1));
  } else {
    const std::size_t total = [this]() {
      std::lock_guard<std::mutex> lock(pcm_mutex_);
      return pcm_.size();
    }();
    if (total > 0) {
      const double seconds = static_cast<double>(total) / kSampleRate;
      if (!playback_time_->text().startsWith("Saved:")) {
        playback_time_->setText(QString("Audio: %1 s").arg(seconds, 0, 'f', 1));
      }
    }
  }
}

void MainWindow::onParamChanged() {

  volume_value_->setText(QString::number(volume_slider_->value()) + "%");
  pitch_value_->setText(QString::number(pitch_slider_->value()) + "%");
  const int speed_pct = speed_slider_->value();
  speed_value_->setText(QString::number(1.0 + speed_pct / 200.0, 'f', 2) + "x");
  applyParamsToEngine();
}

void MainWindow::applyParamsToEngine() {
  std::shared_ptr<tts::ITtsEngine> engine;
  {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    engine = engine_;
  }
  if (!engine || !engine->isReady()) return;

  engine->setParam(kVolumeKey, algoParamValue(volume_slider_->value()));
  engine->setParam(kPitchKey, algoParamValue(pitch_slider_->value()));
  engine->setParam(kSpeedKey, algoParamValue(speed_slider_->value()));
  engine->setParam(kTextModeKey, text_mode_->currentData().toInt());
  engine->setParam(kNum1ModeKey, num1_mode_->currentData().toInt());
}

void MainWindow::updateEngineState(const QString& state) {
  if (state == "READY") {
    status_pill_->setText("ENGINE: READY");
    status_pill_->setStyleSheet("color:#1E7A46; background:#E2F6EA;");
    speak_button_->setEnabled(true);
    license_label_->setText("License: " + QString::fromStdString(
        tts::licenseCodeText(engine_ ? engine_->licenseCode() : -1)));
    version_label_->setText("TTS version: " + QString::fromStdString(
        engine_ ? engine_->version() : std::string("-")));
    applyParamsToEngine();
  } else if (state == "FAILED") {
    status_pill_->setText("ENGINE: FAILED");
    status_pill_->setStyleSheet("color:#B23A3A; background:#FDE8E8;");
  }
}

void MainWindow::updateSpeakState(const QString& state) {
  const bool idle = (state == "DONE" || state == "STOPPED" || state == "IDLE");
  speak_button_->setEnabled(engine_ready_ && idle);

  stop_button_->setEnabled(engine_ready_);

  if (state == "DONE") {
    playback_state_->setText("Done");
    playback_state_->setStyleSheet("color:#1E7A46; background:#E2F6EA;");
    save_button_->setEnabled(true);
  } else if (state == "STOPPED") {
    playback_state_->setText("Stopped");
    playback_state_->setStyleSheet("color:#58709B; background:#EDF2FA;");
    save_button_->setEnabled(true);
  } else if (state == "IDLE") {
    playback_state_->setText("Idle");
    playback_state_->setStyleSheet("color:#2F5BFF; background:#E9EFFF;");
  }

  const std::size_t total = [this]() {
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    return pcm_.size();
  }();
  if (total > 0) {
    playback_time_->setText(QString("Audio: %1 s")
                                .arg(static_cast<double>(total) / kSampleRate,
                                     0, 'f', 1));
  }
}

void MainWindow::updateResourceStats() {
  int used_mb = 0, total_mb = 0;
  if (readMemInfo(&used_mb, &total_mb)) {
    mem_label_->setText(QString("Memory: %1 / %2 MB").arg(used_mb).arg(total_mb));
  }

  unsigned long long total = 0, idle = 0;
  if (!readCpuTotal(&total, &idle)) return;
  if (cpu_has_last_) {
    const unsigned long long d_total = total - cpu_last_total_;
    const unsigned long long d_idle = idle - cpu_last_idle_;
    if (d_total > 0) {
      const double usage = 100.0 * (1.0 - static_cast<double>(d_idle) /
                                        static_cast<double>(d_total));
      cpu_label_->setText(QString("CPU: %1 %").arg(usage, 0, 'f', 1));
    }
  }
  cpu_last_total_ = total;
  cpu_last_idle_ = idle;
  cpu_has_last_ = true;
}

void MainWindow::joinWorker() {
  if (worker_.joinable()) worker_.join();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  stop_requested_ = true;
  sink_->requestStop();
  requestEngineStopAsync();
  joinWorker();

  if (stop_future_.valid() &&
      stop_future_.wait_for(std::chrono::milliseconds(800)) !=
          std::future_status::ready) {
    if (stop_thread_.joinable()) stop_thread_.detach();
  } else if (stop_thread_.joinable()) {
    stop_thread_.join();
  }
  sink_->close();
  std::shared_ptr<tts::ITtsEngine> engine;
  {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    engine = engine_;
  }
  if (engine) engine->shutdown();
  event->accept();
}
