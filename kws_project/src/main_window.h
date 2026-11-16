#pragma once

#include <QDateTime>
#include <QHash>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <deque>
#include <vector>

#include "kws_interfaces.h"
#include "kws_types.h"
#include "main.h"

class QCloseEvent;
class QFrame;
class QLabel;
class QPushButton;
class QScrollArea;
class QSlider;
class QTimer;
class QVBoxLayout;
class QWidget;
class AlsaAudioSource;
class KwsPipeline;
class StageWidget;
class WaveformWidget;
class LevelBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(kws::SpotterFactory factory, appconfig::AppConfig config, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onToggleListening(bool on);
    void onFrame(const kws::AudioFrame& frame);
    void onPipelineReady(const QString& engineVersion);
    void onPipelineFailed(const QString& error);
    void onFeedError(const QString& error);
    void onCaptureError(const QString& error);
    void onWake(const kws::WakeEvent& event);
    void onThresholdChanged(int value);
    void onGainChanged(int value);
    void onEditWakeWord();
    void onUiTimer();
    void onWaveTimer();
    void onWakeUiTimeout();

private:
    void buildUi();
    void applyLayout(double scale);
    void preloadEngine();
    void startListening();
    void stopListening();
    void resetStage();
    void refreshWakeLabelText();
    void showWakeLabel(const QStringList& words, const QString& color);
    QString wakeLabelStyle(const QString& color) const;
    bool applyWakeWords(const QString& text, QString* error);
    void updateHeaderState(const QString& text, const QString& color);
    void updateModelPill(const QString& text, const QString& color);
    void updateStageChip(const QString& text, const QString& bg, const QString& fg, const QString& border);
    void appendHistory(const kws::WakeEvent& event);
    void updateSessionStats();
    void updateResources();
    void setControlEnabled(bool on);

    appconfig::AppConfig cfg_;
    kws::SpotterFactory factory_;

    QWidget* page_ = nullptr;
    double   uiScale_ = 1.0;
    KwsPipeline* pipeline_ = nullptr;
    AlsaAudioSource* capture_ = nullptr;
    bool listening_ = false;
    bool pipelineReady_ = false;
    bool resumeAfterReload_ = false;

    QFrame* header_ = nullptr;
    QLabel* brandIcon_ = nullptr;
    QLabel* modelPill_ = nullptr;
    QLabel* statePill_ = nullptr;

    QFrame* wakeCard_ = nullptr;
    StageWidget* stage_ = nullptr;
    QLabel* liveChip_ = nullptr;
    QLabel* scoreChip_ = nullptr;
    QLabel* wakeLabelText_ = nullptr;
    QStringList wakeLabels_;
    QStringList wakeWordsChinese_;
    QHash<QString, QString> wakeLabelByChinese_;
    int wakeLabelFontPx_ = 34;
    QLabel* stageHint_ = nullptr;
    QLabel* hintText_ = nullptr;

    QFrame* waveCard_ = nullptr;
    QLabel* waveTag_ = nullptr;
    WaveformWidget* inputWave_ = nullptr;
    LevelBar* levelBar_ = nullptr;
    QLabel* levelValue_ = nullptr;
    QLabel* waveNote_ = nullptr;

    QFrame* statsCard_ = nullptr;
    QLabel* framesValue_ = nullptr;
    QLabel* audioValue_ = nullptr;
    QLabel* latencyValue_ = nullptr;
    QLabel* rtfValue_ = nullptr;
    QLabel* engineLabel_ = nullptr;

    QFrame* controlCard_ = nullptr;
    QPushButton* listenButton_ = nullptr;
    QLabel* thresholdValue_ = nullptr;
    QSlider* thresholdSlider_ = nullptr;
    QLabel* gainValue_ = nullptr;
    QSlider* gainSlider_ = nullptr;
    QLabel* keywordValue_ = nullptr;
    QPushButton* editKeywordButton_ = nullptr;
    QLabel* cpuChip_ = nullptr;
    QLabel* memChip_ = nullptr;
    QLabel* licenseLabel_ = nullptr;

    QFrame* historyCard_ = nullptr;
    QScrollArea* historyScroll_ = nullptr;
    QWidget* historyBody_ = nullptr;
    QVBoxLayout* historyList_ = nullptr;
    std::deque<QFrame*> historyItems_;
    int historyCount_ = 0;

    QPushButton* exitButton_ = nullptr;

    std::vector<int16_t> inputView_;
    float  levelDisplay_ = 0.0f;
    double micGainDb_ = 12.0;
    float  threshold_ = 0.55f;
    QTimer* uiTimer_ = nullptr;
    QTimer* waveTimer_ = nullptr;
    QTimer* wakeUiTimer_ = nullptr;
    unsigned long long cpuLastIdle_ = 0;
    unsigned long long cpuLastTotal_ = 0;

    static constexpr int kPageW = 1240;
    static constexpr int kPageH = 762;
};
