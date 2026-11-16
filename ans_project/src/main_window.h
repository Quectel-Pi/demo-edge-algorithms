#pragma once
/* main_window.h — 主界面窗口（QtWidgets，英文界面）
 *
 * 只有一页「Record & Process」：
 *  主开关开始采集，同时启动流式处理线程，每 10ms 一帧边采边降噪并实时刷新波形；
 *  功能开关（降噪强度）变更从下一帧生效；停止时保存原始与处理两份 WAV。
 * 界面只依赖 StreamProcessWorker / AudioCapture / audio::* 高层接口，不依赖算法 SDK。
 */

#include <QElapsedTimer>
#include <QMainWindow>
#include <cstdint>
#include <vector>
#include "audio_types.h"

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QTimer;
class QWidget;
class AudioCapture;
class AudioPlayback;
class StreamProcessWorker;
class WaveformWidget;

/* 界面结构：绝对定位画布
 * 预览为固定像素设计稿（900 宽），buildUi() 用 setGeometry 按像素表摆放，
 * page_ 为整页画布，窗口按画布定尺寸（固定大小、不全屏），resizeEvent 只负责居中。 */
class QResizeEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void setConfig(QString model, QString license, QString auth, int module, QString capDev,
                   QString playDev, QString outDir);
    void buildDevices();      /* 扫描 ALSA 设备填充下拉 */

private slots:
    void onRecordToggled(bool on);
    void onFrame(const audio::AudioFrame& f);
    void onProcessedFrame(const std::vector<int16_t>& out);   /* 流式处理输出（实时） */
    void onConfigChanged();
    void onUiTimer();
    void onMicTestToggled(bool on);
    void onLiveFrame(const audio::AudioFrame& f);
    void onPlayInput();      /* 播放 Captured Input 对应音频 */
    void onPlayOutput();     /* 播放 Processed Result 对应音频 */
    void onPlayProgress(qint64 samplesPlayed);  /* 播放进度 → 滚动对应波形 */

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void applyLayout(double s);        /* 按缩放系数重算所有绝对坐标/字号 */
    void stopAll();
    void stopRecord(bool process);     /* 结束录制；process=true 时保存原始/处理后两份 WAV */
    void stopMicTest();
    void stopPlayback();
    void updatePlayButtons();          /* 按当前播放状态刷新两个播放按钮 */
    void setSwitchesEnabled(bool en);
    audio::ProcessorConfig readConfigFromUi() const;

    QString model_, license_, auth_;
    int     module_ = 3;
    QString capDev_;
    QString playDev_;
    QString outDir_ = QStringLiteral("output");   /* 结果音频目录（工程内 output/） */

    /* Record & Process 控件 */
    QComboBox* capCombo_ = nullptr;
    QPushButton* recordButton_ = nullptr;
    QProgressBar* levelBar_ = nullptr;
    QLabel* levelLabel_ = nullptr;
    QLabel* recInfoLabel_ = nullptr;   /* elapsed / frames 实时信息 */
    QLineEdit* outEdit_ = nullptr;
    QLabel* resultLabel_ = nullptr;

    /* 功能开关（内联，变更即时生效） */
    QSlider*  denoiseSlider_ = nullptr;
    QLabel*   denoiseValue_ = nullptr;
    QSlider*  micGainSlider_ = nullptr;      /* 麦克风软件预增益 0~30 dB */
    QLabel*   micGainValue_ = nullptr;
    double    micGain_ = 1.0;                /* 当前线性增益（由 dB 换算） */
    QLabel*   stationaryInfo_ = nullptr;     /* 平稳降噪状态（与强度共用同一参数） */
    QLabel*   nonStationaryInfo_ = nullptr;  /* 非平稳降噪状态（与强度共用同一参数） */
    QWidget*  switchesPanel_ = nullptr;

    /* 音频起始瞬态抑制：ALSA 起振 / 编解码器上电 / 算法收敛的开头几百毫秒
     * 会出现与真实语音无关的巨大冲击。做法是先丢弃开头若干帧，再线性淡入。 */
    static constexpr int kStartupSkipFrames = 20;   /* 丢弃 200 ms（20 x 10ms） */
    static constexpr int kFadeInFrames      = 16;   /* 再淡入 160 ms */
    int startupSkipLeft_ = 0;
    int fadeLeft_ = 0;
    int liveSkipLeft_ = 0;                          /* Mic Test 起始同样处理 */

    /* 算法状态 */
    QLabel* authLabel_ = nullptr;

    /* 全屏退出按钮 */
    QPushButton* exitButton_ = nullptr;

    /* 绝对定位画布（固定像素尺寸，窗口内居中） */
    QWidget* page_ = nullptr;
    double   uiScale_ = 1.0;   /* 当前生效的等比缩放系数 */

    QTimer* uiTimer_ = nullptr;

    /* 页面与卡片 */
    QWidget* recordWidget_ = nullptr;
    QWidget* waveformsWidget_ = nullptr;

    /* 波形图（预览布局：输入 / 处理结果 / 实时麦克风） */
    WaveformWidget* inputWave_ = nullptr;       /* Captured Input（蓝） */
    WaveformWidget* processedWave_ = nullptr;   /* Processed Result（绿） */
    WaveformWidget* liveWave_ = nullptr;        /* Live Mic Input（橙） */
    QPushButton* micTestButton_ = nullptr;
    QLabel* liveInfo_ = nullptr;

    /* 波形播放：分别对应 Captured Input(recBuf_) 与 Processed Result(lastOut_) */
    QPushButton* playInputButton_ = nullptr;
    QPushButton* playOutputButton_ = nullptr;
    AudioPlayback* player_ = nullptr;
    int playTarget_ = 0;               /* 0=none, 1=input, 2=output */
    std::vector<int16_t> lastOut_;     /* 最近一次流式处理输出缓冲（供播放/波形） */

    /* 录制-流式处理链路 */
    AudioCapture* capture_ = nullptr;
    StreamProcessWorker* stream_ = nullptr;    /* 流式处理线程：录制中逐帧处理 */
    std::vector<int16_t> recBuf_;      /* 本次录制原始采样（停止时落盘/回放） */
    bool  recording_ = false;
    int   recFrames_ = 0;
    int   recPeak_ = 0;
    QElapsedTimer recTimer_;
    std::vector<int16_t> inputView_;   /* Captured Input 波形显示窗口（最近约 1s） */
    std::vector<int16_t> outView_;     /* Processed Result 波形显示窗口（最近约 1s） */

    /* 麦克风测试：仅采集展示，不进算法 */
    bool  micTestOn_ = false;
    AudioCapture* micTestCapture_ = nullptr;
    std::vector<int16_t> liveBuf_;     /* Live Mic 环形缓冲（最近约 1s） */

    audio::ProcessorConfig currentCfg_;
};