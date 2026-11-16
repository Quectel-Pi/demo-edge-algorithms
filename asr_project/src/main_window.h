// main_window.h - UI 模块
//
// 布局按 1280x720 设计稿实现（蓝白风格），并用布局伸缩 + 屏幕尺寸自适应，
// 支持其他分辨率。两个文本框：实时汉字（深色舞台大字）、识别历史（可滚动列表）；
// 两个按钮：Start（起停音频输入）、Exit（退出程序）。
// 只依赖 IAudioSource / IAudioProcessor / IAsrEngine 接口，不依赖具体实现。

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <QMainWindow>

class QLabel;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;
class StageWidget;

namespace asr {
class IAudioSource;
class IAudioProcessor;
class IAsrEngine;
}  // namespace asr

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  // engine_factory 在窗口显示后由后台线程调用（预加载模型）：
  // 模型（encoder 167MB）加载慢，放在后台低优先级线程里做，
  // 保证点 Start 立刻就能开始说话，同时窗口不会卡住。
  MainWindow(std::shared_ptr<asr::IAudioSource> source,
             std::function<std::shared_ptr<asr::IAsrEngine>()> engine_factory,
             std::vector<std::shared_ptr<asr::IAudioProcessor>> processors,
             QWidget* parent = nullptr);
  ~MainWindow() override;

 signals:
  void partialChanged(const QString& text);
  void segmentFinalized(const QString& sentence);
  void statusChanged(const QString& status);   // 运行状态（Idle/Listening/...）
  void modelStateChanged(const QString& state);  // 模型就绪状态（顶栏胶囊）
  void levelChanged(float level);              // 实时音量 0~1，驱动波形

 protected:
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void onToggleClicked();
  void onExitClicked();

 private:
  void buildUi();
  void preload();  // 后台线程：提前把模型加载好
  void start();
  void stop();
  void run();  // 工作线程：采集 -> 处理链 -> 识别 循环
  void appendHistory(const QString& sentence);
  void updateStatus(const QString& status);         // 右侧 Status 卡
  void updateModelStatus(const QString& state);     // 顶栏胶囊 + 舞台占位文案
  void updateResourceStats();                       // 内存 / CPU
  void resetStageText();  // 恢复占位提示（按模型是否就绪选文案）

  std::shared_ptr<asr::IAudioSource> source_;
  std::function<std::shared_ptr<asr::IAsrEngine>()> engine_factory_;
  std::shared_ptr<asr::IAsrEngine> engine_;  // 由后台预加载线程创建
  std::vector<std::shared_ptr<asr::IAudioProcessor>> processors_;

  std::thread worker_;
  std::atomic<bool> running_{false};

  std::thread loader_;                 // 模型预加载线程
  std::mutex engine_mutex_;            // 保护 engine_ 的赋值/读取
  std::atomic<bool> engine_loading_{false};
  std::atomic<bool> engine_ready_{false};
  std::atomic<bool> preload_failed_{false};

  std::size_t chunk_samples_ = 1600;  // 100 ms @ 16 kHz

  // 顶栏
  QLabel* status_pill_ = nullptr;
  // 左：实时汉字（StageWidget 自绘深色舞台 + 动态波形）
  StageWidget* stage_ = nullptr;
  QLabel* live_label_ = nullptr;
  bool has_live_text_ = false;  // 舞台当前是否在显示识别文字
  // 右：状态卡
  QLabel* state_label_ = nullptr;
  QLabel* mem_label_ = nullptr;
  QLabel* cpu_label_ = nullptr;
  QTimer* stats_timer_ = nullptr;
  // 上一次 /proc/stat 采样，用于算 CPU 占用
  std::vector<unsigned long long> cpu_last_total_;
  std::vector<unsigned long long> cpu_last_idle_;
  // 右：识别历史
  QScrollArea* history_scroll_ = nullptr;
  QVBoxLayout* history_list_ = nullptr;
  int history_count_ = 0;
  // 右：操作区
  QPushButton* toggle_button_ = nullptr;
  QPushButton* exit_button_ = nullptr;
};
