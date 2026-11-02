// main_window.cpp - UI 模块实现
//
// 界面按 1280x720 蓝白设计稿实现：
//   顶栏（Logo + 标题 + 状态胶囊）
//   ├─ 左：Live Transcript  深色舞台 + 38px 白色大字（实时汉字）
//   └─ 右：Status / Recognized History / Actions(Start + Exit)
// 布局全部用伸缩因子，窗口尺寸按屏幕可用区域自适应（上限 1280x720）。

#include "main_window.h"

#include <QCloseEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/resource.h>

#include "asr_engine.h"
#include "audio_input.h"

namespace {

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
              padding:6px 13px; font-size:12px; font-weight:600; }
QFrame#card { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #FFFFFF, stop:1 #F3F7FE);
              border:1px solid #DFE8F6; border-bottom:2px solid #CFDAEC; border-radius:14px; }
QLabel#secTitle { font-size:12px; letter-spacing:1px; color:#8FA5CE; font-weight:700; }
QLabel#stageText { background:transparent; color:#FFFFFF; font-size:46px;
                   font-weight:700; }
QLabel#hint { font-size:12px; color:#58709B; }
QLabel#chip { background:#F0F5FF; border:1px solid #DBE6F7; border-radius:12px;
              padding:4px 12px; font-size:13px; color:#58709B; }
QLabel#statePill { border-radius:13px; padding:5px 12px; font-size:13px; font-weight:700; }
QPushButton#btnPrimary { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,
                            stop:0 #4F80FF, stop:1 #2A54E8);
                         color:#FFFFFF; border:none; border-bottom:3px solid #1E44C4;
                         border-radius:12px; font-size:15px; font-weight:700; }
QPushButton#btnPrimary:hover { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,
                                  stop:0 #3B6DFF, stop:1 #2450DD); }
QPushButton#btnPrimary:pressed { background:#2450DD; border-bottom:1px solid #1E44C4; }
QPushButton#btnGhost { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,
                           stop:0 #FFFFFF, stop:1 #E8EFFF);
                       color:#2F5BFF; border:1.5px solid #BCD0FB;
                       border-bottom:2px solid #A9C0F4; border-radius:12px;
                       font-size:15px; font-weight:700; }
QPushButton#btnGhost:hover { background:#EEF3FF; }
QPushButton#btnGhost:pressed { background:#DBE6FF; border-bottom:1px solid #A9C0F4; }
QFrame#historyItem { background:#FFFFFF; border:1px solid #D3DDF0;
                     border-left:3px solid #2F5BFF; border-radius:10px; }
QLabel#idx { background:#E9EFFF; color:#2F5BFF; border-radius:6px; padding:2px 6px;
             font-size:11px; font-weight:800; }
QLabel#historyText { font-size:14px; color:#132A4A; font-weight:600; }
QLabel#historyTime { font-size:11px; color:#8FA5CE; }
QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; border:none; }
)QSS";

// 用 QChar(0x25CF) 构造圆点，避免源码里出现依赖编码的字符
QString dot(const QString& text) {
  return QString(QChar(0x25CF)) + " " + text;
}

// 深色舞台为空时的三种占位提示（浅色，与实时白字区分）
const char* kPlaceholder =
    "<span style=\"color:#8FA5CE\">Press Start and speak...</span>";
const char* kWaiting =
    "<span style=\"color:#8FA5CE\">Waiting for the model to load...</span>";
const char* kModelFailedText =
    "<span style=\"color:#F08A8A\">Model failed to load</span>";

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

QLabel* makeChip(const QString& text, QWidget* parent) {
  auto* chip = new QLabel(text, parent);
  chip->setObjectName("chip");
  return chip;
}

// 读取 /proc/meminfo：实时已用 / 总内存（MB）
// 已用 = MemTotal - MemFree - Buffers - Cached - SReclaimable
// （与 top/htop 的 used 口径一致，反映进程真实占用，随运行实时变化）
// 注意：/proc 是大小为 0 的虚拟文件，QFile::atEnd() 会直接返回 true，
// 因此这里用 fopen/fgets（等价于 cat）逐行读取。
bool readMemInfo(int* used_mb, int* total_mb) {
  std::FILE* fp = std::fopen("/proc/meminfo", "r");
  if (!fp) return false;

  long total_kb = 0;
  long free_kb = 0;
  long buffers_kb = 0;
  long cached_kb = 0;
  long reclaim_kb = 0;
  long avail_kb = 0;
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
    used_kb = total_kb - avail_kb;  // 老内核兜底
  }
  if (used_kb < 0) used_kb = 0;

  *total_mb = int(total_kb / 1024);
  *used_mb = int(used_kb / 1024);
  return true;
}

// 读取 /proc/stat：每个核累计的 idle / total 时间（jiffies）
void readCpuTimes(std::vector<unsigned long long>* idle,
                  std::vector<unsigned long long>* total) {
  idle->clear();
  total->clear();

  std::FILE* fp = std::fopen("/proc/stat", "r");
  if (!fp) return;

  char line[512];
  while (std::fgets(line, sizeof(line), fp)) {
    if (std::strncmp(line, "cpu", 3) != 0) break;  // cpu 行连续排在开头
    if (line[3] == ' ') continue;                  // 跳过 "cpu " 汇总行
    unsigned long long f[10] = {0};
    const int n = std::sscanf(
        line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &f[0],
        &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7], &f[8], &f[9]);
    if (n < 4) continue;
    unsigned long long sum = 0;
    for (int i = 0; i < n && i < 8; ++i) sum += f[i];  // user..steal
    idle->push_back(f[3] + (n > 4 ? f[4] : 0));        // idle + iowait
    total->push_back(sum);
  }
  std::fclose(fp);
}

}  // namespace

// ---------------------------------------------------------------------------
// StageWidget：左侧深色舞台
// 自绘内容：深色渐变底 + 淡网格 + 两条漂移的语音曲线 + 底部镜像波形条。
// 波形幅度由实时音量（levelChanged）驱动，静音时保留轻微呼吸动画。
// 不声明 Q_OBJECT：只做绘制，不需要自定义信号槽。
// ---------------------------------------------------------------------------
class StageWidget : public QWidget {
 public:
  explicit StageWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setMinimumHeight(140);
    timer_.setInterval(50);  // 20 fps，兼顾流畅与板端 CPU 占用
    connect(&timer_, &QTimer::timeout, this, [this] {
      phase_ += 1.0f;
      smooth_ += (level_ - smooth_) * 0.25f;  // 平滑，避免波形跳变
      update();
    });
    timer_.start();
  }

  void setLevel(float level) { level_ = qBound(0.0f, level, 1.0f); }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(r, 12, 12);
    p.setClipPath(clip);

    QLinearGradient bg(r.topLeft(), r.bottomRight());
    bg.setColorAt(0.00, QColor(0x11, 0x1A, 0x2B));
    bg.setColorAt(0.55, QColor(0x16, 0x23, 0x3D));
    bg.setColorAt(1.00, QColor(0x1B, 0x2A, 0x49));
    p.fillRect(rect(), bg);

    // 淡网格（42px，与设计稿保持一致）
    p.setPen(QPen(QColor(255, 255, 255, 10), 1));
    for (int x = 42; x < width(); x += 42) p.drawLine(x, 0, x, height());
    for (int y = 42; y < height(); y += 42) p.drawLine(0, y, width(), y);

    drawVoiceLines(p);
    drawWaveBars(p);
  }

 private:
  // 两条缓慢漂移的语音包络线（线条装饰）
  void drawVoiceLines(QPainter& p) {
    const double mid_y = height() * 0.60;
    for (int k = 0; k < 2; ++k) {
      const double amp = 8.0 + 24.0 * smooth_;
      const double freq = 0.011 + 0.004 * k;
      const double ph = phase_ * (0.05 + 0.02 * k) + k * 1.7;
      QPainterPath path;
      for (int x = 0; x <= width(); x += 4) {
        const double y =
            mid_y + amp * std::sin(x * freq + ph) * std::cos(x * freq * 0.6 + ph);
        if (x == 0) {
          path.moveTo(x, y);
        } else {
          path.lineTo(x, y);
        }
      }
      p.setPen(QPen(QColor(110, 165, 255, k == 0 ? 62 : 34), 1.4));
      p.drawPath(path);
    }
  }

  // 底部镜像波形条：静音时轻微呼吸，说话时随音量起伏
  void drawWaveBars(QPainter& p) {
    const int n = 64;
    const double base_y = height() - 52;
    const double max_amp = qMin(38.0, height() * 0.20);
    const double step = double(width()) / n;

    QLinearGradient g(0, base_y - max_amp, 0, base_y + max_amp);
    g.setColorAt(0.0, QColor(120, 170, 255, 200));
    g.setColorAt(0.5, QColor(58, 108, 224, 140));
    g.setColorAt(1.0, QColor(120, 170, 255, 200));
    p.setPen(Qt::NoPen);
    p.setBrush(g);

    for (int i = 0; i < n; ++i) {
      double s = std::sin(phase_ * 0.09 + i * 0.55) * 0.5 +
                 std::sin(phase_ * 0.17 + i * 1.31) * 0.3 +
                 std::sin(phase_ * 0.05 + i * 0.23) * 0.2;
      s = std::fabs(s);
      const double amp = max_amp * (0.30 + 0.70 * s) * (0.18 + 0.82 * smooth_);
      const double w = qMax(2.0, step * 0.42);
      const double cx = i * step + step * 0.5;
      p.drawRoundedRect(QRectF(cx - w / 2, base_y - amp, w, amp * 2), w / 2,
                        w / 2);
    }
  }

  QTimer timer_;
  float level_ = 0.0f;   // 目标音量（由工作线程信号更新）
  float smooth_ = 0.0f;  // 平滑后的音量
  float phase_ = 0.0f;   // 动画相位
};

MainWindow::MainWindow(
    std::shared_ptr<asr::IAudioSource> source,
    std::function<std::shared_ptr<asr::IAsrEngine>()> engine_factory,
    std::vector<std::shared_ptr<asr::IAudioProcessor>> processors,
    QWidget* parent)
    : QMainWindow(parent),
      source_(std::move(source)),
      engine_factory_(std::move(engine_factory)),
      processors_(std::move(processors)) {
  buildUi();
  setStyleSheet(kStyleSheet);

  // 窗口尺寸按屏幕可用区域自适应，最大 1280x720（设计稿尺寸）
  if (const QScreen* screen = QGuiApplication::primaryScreen()) {
    const QRect avail = screen->availableGeometry();
    const int w = qMin(1280, qMax(800, avail.width() * 92 / 100));
    const int h = qMin(720, qMax(480, avail.height() * 92 / 100));
    resize(w, h);
  } else {
    resize(1280, 720);
  }

  // 工作线程 -> GUI：跨线程信号自动走队列，线程安全
  connect(this, &MainWindow::partialChanged, this,
          [this](const QString& text) {
            // 富文本 + 显式白色，避免 HTML 特殊字符破坏样式
            has_live_text_ = true;
            live_label_->setText(
                QString("<span style=\"color:#FFFFFF\">%1</span>")
                    .arg(text.toHtmlEscaped()));
          });
  connect(this, &MainWindow::segmentFinalized, this,
          [this](const QString& sentence) {
            appendHistory(sentence);
            resetStageText();
          });
  connect(this, &MainWindow::statusChanged, this, &MainWindow::updateStatus);
  connect(this, &MainWindow::modelStateChanged, this,
          &MainWindow::updateModelStatus);
  connect(this, &MainWindow::levelChanged, this,
          [this](float level) { stage_->setLevel(level); });

  updateStatus("Idle");
  updateModelStatus("Model Loading...");  // 同时把舞台文案切到“等待模型”

  // 每秒刷新内存 / CPU（4 核）占用
  stats_timer_ = new QTimer(this);
  stats_timer_->setInterval(1000);
  connect(stats_timer_, &QTimer::timeout, this,
          &MainWindow::updateResourceStats);
  updateResourceStats();  // 首次：建立 CPU 采样基线
  // 400 ms 后再采一次，让 CPU 百分比尽快出现在界面上
  QTimer::singleShot(400, this, &MainWindow::updateResourceStats);
  stats_timer_->start();

  // 窗口先画出来，随后在后台低优先级线程里预加载模型：
  // 这样点 Start 时模型通常已就绪，可以立刻说话；加载期间界面也不卡。
  QTimer::singleShot(200, this, [this] { preload(); });
}

MainWindow::~MainWindow() {
  stop();
  if (loader_.joinable()) loader_.join();
}

void MainWindow::preload() {
  if (engine_ready_.load() || engine_loading_.exchange(true)) return;

  emit modelStateChanged("Model Loading...");
  loader_ = std::thread([this] {
    // 降低加载线程优先级，避免抢 CPU 导致主线程长时间拿不到时间片
    setpriority(PRIO_PROCESS, 0, 10);

    auto engine = engine_factory_();
    {
      std::lock_guard<std::mutex> lock(engine_mutex_);
      engine_ = engine;
    }
    engine_loading_.store(false);
    if (engine) {
      engine_ready_.store(true, std::memory_order_release);
      emit modelStateChanged("Model Ready");
    } else {
      preload_failed_.store(true);
      emit modelStateChanged("Model Failed");
    }
  });
}

void MainWindow::buildUi() {
  setWindowTitle("Speech Recognition");

  // 最外层：14px 边距 + 页面圆角卡片
  auto* root = new QFrame(this);
  root->setObjectName("root");
  auto* root_layout = new QVBoxLayout(root);
  root_layout->setContentsMargins(14, 14, 14, 14);
  root_layout->setSpacing(0);

  auto* page = new QFrame(root);
  page->setObjectName("page");
  auto* page_layout = new QVBoxLayout(page);
  page_layout->setContentsMargins(0, 0, 0, 0);
  page_layout->setSpacing(0);
  root_layout->addWidget(page);
  setCentralWidget(root);

  // ---------- 顶栏 ----------
  auto* topbar = new QFrame(page);
  topbar->setObjectName("topbar");
  topbar->setFixedHeight(58);
  auto* top_layout = new QHBoxLayout(topbar);
  top_layout->setContentsMargins(16, 0, 16, 0);
  top_layout->setSpacing(12);

  auto* logo = new QLabel("A", topbar);
  logo->setObjectName("logo");
  logo->setAlignment(Qt::AlignCenter);
  logo->setFixedSize(34, 34);

  auto* brand = new QVBoxLayout();
  brand->setSpacing(1);
  auto* brand_title = new QLabel("Speech Recognition", topbar);
  brand_title->setObjectName("brandTitle");
  auto* brand_sub =
      new QLabel("Quectel PI · RK3576 · sherpa-onnx streaming", topbar);
  brand_sub->setObjectName("brandSub");
  brand->addWidget(brand_title);
  brand->addWidget(brand_sub);

  status_pill_ = new QLabel(topbar);
  status_pill_->setObjectName("pill");

  top_layout->addWidget(logo);
  top_layout->addLayout(brand);
  top_layout->addStretch();
  top_layout->addWidget(status_pill_);
  page_layout->addWidget(topbar);

  // ---------- 主体 ----------
  auto* body = new QHBoxLayout();
  body->setContentsMargins(16, 14, 16, 14);
  body->setSpacing(18);
  page_layout->addLayout(body, 1);

  // 左：实时汉字
  auto* live_card = makeCard(page);
  auto* live_layout = new QVBoxLayout(live_card);
  live_layout->setContentsMargins(16, 12, 16, 12);
  live_layout->setSpacing(10);
  live_layout->addWidget(makeSectionTitle("Live Transcript", live_card));

  // 深色舞台：StageWidget 自绘背景 + 动态波形，文字用透明 QLabel 叠在上面
  stage_ = new StageWidget(live_card);
  auto* stage_layout = new QVBoxLayout(stage_);
  stage_layout->setContentsMargins(32, 18, 32, 18);  // 居中显示，不再为波形留边
  stage_layout->setSpacing(0);

  live_label_ = new QLabel(kPlaceholder, stage_);
  live_label_->setObjectName("stageText");
  live_label_->setTextFormat(Qt::RichText);
  live_label_->setWordWrap(true);
  live_label_->setAlignment(Qt::AlignCenter);  // 水平 + 垂直居中
  live_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  stage_layout->addWidget(live_label_, 1);

  live_layout->addWidget(stage_, 1);

  auto* hint_row = new QHBoxLayout();
  hint_row->setSpacing(8);
  auto* hint = new QLabel(
      "Streaming partial result · updated in real time", live_card);
  hint->setObjectName("hint");
  hint_row->addWidget(hint);
  hint_row->addStretch();
  hint_row->addWidget(makeChip("16 kHz", live_card));
  hint_row->addWidget(makeChip("Mono", live_card));
  hint_row->addWidget(makeChip("100 ms chunk", live_card));
  live_layout->addLayout(hint_row);

  // 右列容器
  auto* right_box = new QWidget(page);
  right_box->setMaximumWidth(380);
  right_box->setMinimumWidth(280);
  auto* right = new QVBoxLayout(right_box);
  right->setContentsMargins(0, 0, 0, 0);
  right->setSpacing(14);

  // 右：状态卡（运行状态 + 内存 + CPU 总占用）
  auto* status_card = makeCard(right_box);
  status_card->setMinimumHeight(108);
  auto* status_layout = new QVBoxLayout(status_card);
  status_layout->setContentsMargins(16, 12, 16, 12);
  status_layout->setSpacing(8);
  status_layout->addWidget(makeSectionTitle("Status", status_card));

  auto* state_row = new QHBoxLayout();
  state_row->setSpacing(10);
  state_label_ = new QLabel(status_card);
  state_label_->setObjectName("statePill");
  state_row->addWidget(state_label_);
  state_row->addWidget(makeChip("Conformer · INT8", status_card));
  state_row->addStretch();
  status_layout->addLayout(state_row);

  auto* res_row = new QHBoxLayout();
  res_row->setSpacing(8);
  mem_label_ = makeChip("Memory ...", status_card);
  cpu_label_ = makeChip("CPU ...", status_card);
  res_row->addWidget(mem_label_);
  res_row->addWidget(cpu_label_);
  res_row->addStretch();
  status_layout->addLayout(res_row);
  right->addWidget(status_card);

  // 右：识别历史
  auto* history_card = makeCard(right_box);
  auto* history_layout = new QVBoxLayout(history_card);
  history_layout->setContentsMargins(16, 12, 16, 12);
  history_layout->setSpacing(10);
  history_layout->addWidget(makeSectionTitle("Recognized History", history_card));

  history_scroll_ = new QScrollArea(history_card);
  history_scroll_->setWidgetResizable(true);
  history_scroll_->setFrameShape(QFrame::NoFrame);
  history_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* list_host = new QWidget();
  history_list_ = new QVBoxLayout(list_host);
  history_list_->setContentsMargins(0, 0, 0, 0);
  history_list_->setSpacing(8);
  history_list_->addStretch();  // 新条目插入到该 stretch 之前 -> 顶部对齐
  history_scroll_->setWidget(list_host);
  history_layout->addWidget(history_scroll_, 1);
  right->addWidget(history_card, 1);

  // 右：操作区
  auto* action_card = makeCard(right_box);
  action_card->setMinimumHeight(104);
  auto* action_layout = new QVBoxLayout(action_card);
  action_layout->setContentsMargins(16, 12, 16, 12);
  action_layout->setSpacing(10);
  action_layout->addWidget(makeSectionTitle("Actions", action_card));

  auto* buttons = new QHBoxLayout();
  buttons->setSpacing(10);
  toggle_button_ = new QPushButton("Start", action_card);
  toggle_button_->setObjectName("btnPrimary");
  toggle_button_->setCursor(Qt::PointingHandCursor);
  toggle_button_->setMinimumHeight(52);
  connect(toggle_button_, &QPushButton::clicked, this,
          &MainWindow::onToggleClicked);

  exit_button_ = new QPushButton("Exit", action_card);
  exit_button_->setObjectName("btnGhost");
  exit_button_->setCursor(Qt::PointingHandCursor);
  exit_button_->setMinimumHeight(52);
  connect(exit_button_, &QPushButton::clicked, this,
          &MainWindow::onExitClicked);

  buttons->addWidget(toggle_button_);
  buttons->addWidget(exit_button_);
  action_layout->addLayout(buttons);
  right->addWidget(action_card);

  body->addWidget(live_card, 7);
  body->addWidget(right_box, 3);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  stop();
  QMainWindow::closeEvent(event);
}

void MainWindow::onExitClicked() { close(); }

void MainWindow::onToggleClicked() {
  if (running_.load()) {
    stop();
    toggle_button_->setText("Start");
  } else {
    resetStageText();
    start();
    // 启动失败（如音频设备打不开）时 running_ 会回落为 false
    toggle_button_->setText(running_.load() ? "Stop" : "Start");
  }
}

void MainWindow::start() {
  if (running_.load()) return;
  // 上一次工作线程可能已自行结束（如音频设备打开失败），先回收再新建
  if (worker_.joinable()) worker_.join();
  running_.store(true);
  worker_ = std::thread(&MainWindow::run, this);
}

void MainWindow::stop() {
  running_.store(false);
  if (worker_.joinable()) worker_.join();
  resetStageText();
}

void MainWindow::resetStageText() {
  has_live_text_ = false;
  if (!live_label_) return;
  if (preload_failed_.load()) {
    live_label_->setText(kModelFailedText);
  } else if (engine_ready_.load()) {
    live_label_->setText(kPlaceholder);
  } else {
    live_label_->setText(kWaiting);
  }
}

void MainWindow::run() {
  // 先开麦克风再等模型：点 Start 就能立刻说话，模型没就绪时先把音频缓存下来
  if (!source_->open()) {
    emit statusChanged("Failed to open audio input device");
    running_.store(false);
    return;
  }

  std::shared_ptr<asr::IAsrEngine> engine;
  {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    if (engine_ready_.load(std::memory_order_acquire)) engine = engine_;
  }
  if (!engine && preload_failed_.load()) {
    emit statusChanged("Failed to load model");
    source_->close();
    running_.store(false);
    return;
  }
  if (engine) engine->reset();
  emit statusChanged(engine ? "Listening" : "Preparing...");

  const std::size_t kMaxPending = static_cast<std::size_t>(16000) * 20;  // 20 s
  std::vector<float> pending;

  // 喂一块音频 -> 解码 -> 处理端点/刷新实时文字
  auto feed_block = [&](const float* data, std::size_t count) {
    engine->feed(std::vector<float>(data, data + count));
    engine->decode();

    if (engine->isEndpoint()) {
      const std::string sentence = engine->partialText();
      if (!sentence.empty()) {
        emit segmentFinalized(QString::fromUtf8(sentence.c_str()));
      }
      engine->reset();
    }

    const std::string partial = engine->partialText();
    if (!partial.empty()) {
      emit partialChanged(QString::fromUtf8(partial.c_str()));
    }
  };

  while (running_.load()) {
    auto samples = source_->read(chunk_samples_);
    if (!running_.load()) break;
    if (samples.empty()) continue;

    // 解耦处理链：降噪 / AEC 后续在此插入
    for (auto& proc : processors_) {
      if (!running_.load()) break;
      samples = proc->process(std::move(samples));
    }
    if (samples.empty()) continue;

    // 计算音量驱动左侧波形（rms 归一到 0~1，0.10 约等于正常说话）
    double sum_sq = 0.0;
    for (float s : samples) sum_sq += double(s) * s;
    const float rms =
        float(std::sqrt(sum_sq / double(samples.size())));
    emit levelChanged(qMin(1.0f, rms / 0.10f));

    if (!engine) {
      // 模型还在加载：缓存这段语音，加载完成后立刻补识别
      pending.insert(pending.end(), samples.begin(), samples.end());
      if (pending.size() > kMaxPending) {
        pending.erase(pending.begin(),
                      pending.begin() + (pending.size() - kMaxPending));
      }

      {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        if (engine_ready_.load(std::memory_order_acquire)) engine = engine_;
      }
      if (!engine) {
        if (preload_failed_.load()) break;  // 加载失败，别再空转缓冲
        continue;
      }

      emit statusChanged("Listening");
      engine->reset();
      for (std::size_t off = 0; off + chunk_samples_ <= pending.size();
           off += chunk_samples_) {
        feed_block(pending.data() + off, chunk_samples_);
      }
      pending.clear();
      continue;
    }

    feed_block(samples.data(), samples.size());
  }

  // 清空识别流，下次 Start 是全新的识别上下文
  if (engine) engine->reset();
  source_->close();
  emit levelChanged(0.0f);
  emit statusChanged("Stopped");
  running_.store(false);
}

void MainWindow::appendHistory(const QString& sentence) {
  if (sentence.isEmpty()) return;
  ++history_count_;

  auto* item = new QFrame();
  item->setObjectName("historyItem");
  auto* row = new QHBoxLayout(item);
  row->setContentsMargins(11, 9, 11, 9);
  row->setSpacing(9);

  auto* index = new QLabel(QString("%1").arg(history_count_, 2, 10, QChar('0')),
                           item);
  index->setObjectName("idx");
  auto* text = new QLabel(sentence, item);
  text->setObjectName("historyText");
  text->setWordWrap(true);
  auto* time = new QLabel(QTime::currentTime().toString("HH:mm"), item);
  time->setObjectName("historyTime");

  row->addWidget(index, 0, Qt::AlignTop);
  row->addWidget(text, 1);
  row->addWidget(time, 0, Qt::AlignTop);

  // 插到末尾 stretch 之前，保持列表顶部对齐
  history_list_->insertWidget(history_list_->count() - 1, item);

  // 始终滚动到最新一条
  if (history_scroll_) {
    QScrollBar* bar = history_scroll_->verticalScrollBar();
    bar->setValue(bar->maximum());
  }
}

void MainWindow::updateStatus(const QString& status) {
  // 只反映运行状态（Idle / Preparing / Listening / Stopped ...），
  // 模型是否就绪由右上角胶囊单独显示，避免重复。
  state_label_->setText(dot(status));

  QString color = "#58709B";
  QString bg = "#F0F5FF";
  QString border = "#DBE6F7";
  if (status == "Listening") {
    color = "#138A4E";
    bg = "#E7F8EE";
    border = "#BFE8CF";
  } else if (status.startsWith("Preparing")) {
    color = "#B45309";
    bg = "#FDEFCB";
    border = "#F2DCA0";
  } else if (status.startsWith("Failed")) {
    color = "#B42318";
    bg = "#FDECEC";
    border = "#F5C6C2";
  }

  state_label_->setStyleSheet(
      QString("color:%1; background:%2; border:1px solid %3; border-radius:13px;"
              " padding:5px 12px; font-size:13px; font-weight:700;")
          .arg(color, bg, border));
}

void MainWindow::updateModelStatus(const QString& state) {
  status_pill_->setText(dot(state));

  QString color = "#58709B";
  if (state.startsWith("Model Ready")) {
    color = "#138A4E";
  } else if (state.startsWith("Model Loading")) {
    color = "#B45309";
  } else if (state.startsWith("Model Failed")) {
    color = "#B42318";
  }
  status_pill_->setStyleSheet(
      QString("color:%1; background:#FFFFFF; border:1px solid #E3EAF7;"
              " border-radius:13px; padding:6px 13px; font-size:12px;"
              " font-weight:600;")
          .arg(color));

  // 舞台没在显示识别文字时，占位文案跟随模型状态：
  // 加载中 -> Waiting for the model to load...，就绪 -> Press Start and speak...
  if (!has_live_text_) resetStageText();
}

void MainWindow::updateResourceStats() {
  // ---- 内存：/proc/meminfo（每秒刷新，随运行实时变化）----
  int used_mb = 0;
  int total_mb = 0;
  if (readMemInfo(&used_mb, &total_mb)) {
    const int pct =
        total_mb > 0 ? int(100.0 * double(used_mb) / double(total_mb) + 0.5) : 0;
    mem_label_->setText(QString("Memory %1 MB (%2%)").arg(used_mb).arg(pct));
  } else {
    mem_label_->setText("Memory n/a");
  }

  // ---- CPU：/proc/stat 相邻两次采样求差，算总体占用 ----
  std::vector<unsigned long long> idle;
  std::vector<unsigned long long> total;
  readCpuTimes(&idle, &total);

  // 首次打印一次解析结果，方便在板端确认 /proc 读取是否正常
  static bool logged_once = false;
  if (!logged_once) {
    logged_once = true;
    std::fprintf(stderr, "[stats] mem=%d/%d MB, cpu cores=%d\n", used_mb,
                 total_mb, int(idle.size()));
  }

  if (idle.empty()) {
    cpu_label_->setText("CPU n/a");
    return;
  }
  if (idle.size() != cpu_last_idle_.size()) {  // 首次：只建立基线
    cpu_last_idle_ = idle;
    cpu_last_total_ = total;
    return;
  }

  // 只显示总体 CPU 占用（各核平均）
  unsigned long long sum_dt = 0;
  unsigned long long sum_di = 0;
  for (std::size_t i = 0; i < idle.size(); ++i) {
    sum_dt += total[i] - cpu_last_total_[i];
    sum_di += idle[i] - cpu_last_idle_[i];
  }
  const int usage =
      sum_dt > 0 ? int(100.0 * double(sum_dt - sum_di) / double(sum_dt) + 0.5) : 0;
  cpu_label_->setText(QString("CPU %1%").arg(usage));

  cpu_last_idle_ = idle;
  cpu_last_total_ = total;
}
