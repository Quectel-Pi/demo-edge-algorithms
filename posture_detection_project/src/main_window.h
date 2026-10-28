#pragma once
/* main_window.h — 主界面窗口（QtWidgets）
 *
 * 界面只依赖 CameraWorker / PostureDetector / PostureFrameResult 的高层接口，
 * 不依赖任何算法 SDK 类型，便于后续独立调整布局与交互。
 */
#include <QElapsedTimer>
#include <QImage>
#include <QMainWindow>
#include <QString>
#include <deque>
#include <memory>
#include <utility>
#include <vector>
#include "posture_detector.h"

class QCloseEvent;
class QComboBox;
class QLabel;
class QTimer;
class QPushButton;
class CameraWorker;
class PostureDetector;
class FrameHub;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /* 设置算法配置（在启动摄像头前调用） */
    void setAlgorithmConfig(QString model, QString license, QString auth, int module = 3);
    /* 打开摄像头并启动流水线 */
    void startPipe(int deviceIndex);

private slots:
    void onDetectionToggled(bool on);
    void onPreview(const QImage& image);
    void onDetectorResult(const PostureFrameResult& r);
    void onDetectorStatus(bool ok, const QString& info);
    void onCameraError(const QString& msg);
    void onUiTimer();
    void onOpenSettings();
    void onCameraChanged(int index);

private:
    void buildUi();
    void stopPipeline();
    /* 把采集目标任务切换到指定设备号（停旧→换号→重启，复用同一 CameraWorker） */
    void applyCamera(int deviceIndex);
    /* 把检测框/关键点叠加到当前预览帧上；返回新图供显示 */
    QImage drawOverlay(const QImage& base, const PostureFrameResult& r);
    /* 输出结果按"每 kResultWindow 次算法输出汇总一次"的方式决策并保持显示，
     * 避免逐帧刷新导致 unknown/动作 高频互抢。 */
    void updateFilteredResult(bool hasPerson);
    /* 清空当前批次累计计数 */
    void resetFilterBatch();

protected:
    /* 关闭窗口时先停线程、释放算法资源，保证进程干净退出 */
    void closeEvent(QCloseEvent* event) override;

private:
    QLabel* previewLabel_;
    QLabel* fpsLabel_ = nullptr;   /* 叠加在预览左上角的小字 fps */
    QPushButton* detectButton_ = nullptr;  /* 检测开关（按键式） */
    QLabel* resultDetect_ = nullptr;   /* 输出结果-是否检测到人 */
    QLabel* resultPose_ = nullptr;     /* 输出结果-姿态 */
    QLabel* resultConf_ = nullptr;     /* 输出结果-置信度 */
    QLabel* statsLabel_;           /* 仅显示内存占用 */
    QLabel* cpuLabel_ = nullptr;   /* CPU 占用率 */
    QComboBox* cameraCombo_ = nullptr;
    QPushButton* settingsButton_;
    QTimer* uiTimer_ = nullptr;
    int     uiFrameCount_ = 0;   /* 预览帧计数，用于换算 fps */
    long    cpuPrevUtime_ = -1, cpuPrevStime_ = -1;   /* 上次进程 CPU 时间(ticks)，用于差分算占用率 */
    long    cpuPrevSysTotal_ = -1;                   /* 上次系统总 CPU 时间(ticks)，用于多核占比 */
    bool    isSwitchingCameraCombo_ = false; /* 填充下拉时屏蔽误触发 */
    int     activeCamera_ = -1;              /* 当前生效的设备号 */

    CameraWorker* camera_ = nullptr;
    PostureDetector* detector_ = nullptr;
    std::shared_ptr<FrameHub> hub_;   /* 摄像头写 / 算法读 的共享最新帧缓存 */

    QString model_, license_, auth_;
    int     module_ = 3;
    bool    cameraRunning_ = false;
    bool    detectionOn_ = false;   /* 检测是否处于开启状态，用于丢弃关闭后迟到的检测结果信号 */

    /* 最近一次摄像头预览帧 与 最近一次检测结果（仅 GUI 线程访问）。
     * onPreview 把结果叠加到最新预览帧上，保证预览始终流畅、不依赖推理速度。 */
    QImage lastPreview_;
    PostureFrameResult lastResult_;
    bool   hasResult_ = false;

    /* 输出结果"滚动 + 去抖"滤波：维护最近 kResultWindow 个算法输出，每新来
     * kSlideStep 个就滑动判定一次（丢最老、接最新，窗口持续重叠）；判定规则为
     * "三类不良坐姿(1托腮/2低头/3高低肩)中计数最多且 >1 次"，否则 unknown；
     * 结果变化后保持 kDebounce 步再允许再次变化，避免阈值边沿来回翻转。 */
    static constexpr int kResultWindow = 20;   /* 滚动窗口容量 */
    static constexpr int kSlideStep    = 10;   /* 每满该数量新输出滑动判定一次 */
    static constexpr int kDebounce     = 1;    /* 判定结果变化后保持的步数 */
    static constexpr int kNoPersonClearMs = 1000; /* 镜头无人持续该时长后才清空滚动窗口 */
    std::deque<std::pair<int, float>> window_; /* 最近 kResultWindow 个 (姿态,置信度) */
    int   batchN_       = 0;                    /* 距上次判定的新输出数 */
    int   holdRemain_   = 0;                     /* 去抖保持剩余步数（调试用亦含计时） */
    int   shownPose_    = 0;                    /* 当前显示姿态（0=unknown） */
    QElapsedTimer batchTimer_;              /* 当前滑动周期耗时计时（调试用） */
    QElapsedTimer noPersonTimer_;           /* 镜头持续无人的计时（去抖清空用） */
    bool  noPersonOngoing_ = false;          /* 当前是否正处于"无人计时"中 */
};