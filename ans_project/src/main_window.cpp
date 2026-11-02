#include "main_window.h"

#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPaintEvent>
#include <QPolygon>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QShortcut>
#include <QSlider>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QtMath>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <alsa/asoundlib.h>

#include "ans_processor.h"
#include "audio_capture.h"
#include "main.h"   /* wav::writePcmFile */

/* 全局缩放系数：buildUi() 按主屏尺寸与设计基准(1240x762)的较小比例算出，
 * 仅用于整体等比适配屏幕，再用 P(v)=qRound(v*gUiScale) 换算所有像素值。 */
double gUiScale = 1.0;

/* 整页设计尺寸：左右两栏布局（左栏波形、右栏控制），绝对像素画布 */
static constexpr int kPageW = 1240;
static constexpr int kPageH = 762;
static double fitScale(int w, int h);

/* ================= 波形图控件 =================
 * 纯 QPainter 自绘：把 int16 采样降采样映射到像素，垂直居中为 0 电平，
 * 可设颜色。数据 setData() 传入后 repaint()。 */
class WaveformWidget : public QWidget {
public:
    explicit WaveformWidget(QColor color, QWidget* parent = nullptr)
        : QWidget(parent), color_(std::move(color)) {
        /* 背景/圆角由全局样式表 QWidget#wave 提供，高度由 applyLayout 绝对定位控制 */
        setObjectName(QStringLiteral("wave"));
        setAttribute(Qt::WA_StyledBackground, true);
    }
    void setData(const std::vector<int16_t>& s) {
        data_ = s;
        update();
    }
    void setColor(QColor c) { color_ = std::move(c); update(); }
    /* 播放进度游标：progress 取值 [0,1]；<0 表示不画游标（非播放态） */
    void setPlaybackProgress(double p) { progress_ = p; update(); }
    double playbackProgress() const { return progress_; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int w = width(), h = height();
        const int mid = h / 2;

        /* 中线 */
        p.setPen(QPen(QColor("#e3eaf7"), 1));
        p.drawLine(0, mid, w, mid);

        if (data_.empty()) return;
        /* 降采样：水平每像素一格，取该区间绝对值最大采样的有符号值 */
        const int bins = std::max(1, (int)data_.size() / std::max(1, w));
        const int nPts = std::min(w, (int)data_.size());
        p.setPen(QPen(color_, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPolygonF poly;
        /* 自动幅度归一化：按当前数据峰值铺满 90% 高度（最多放大 16 倍）。
         * 板上麦克风电平偏低时，固定倍数仍会画成一条直线，因此按峰值自适应。 */
        double peak = 0.0;
        for (int16_t v : data_) {
            const double a = std::abs(double(v));
            if (a > peak) peak = a;
        }
        double k = peak > 1.0 ? (0.9 * 32768.0) / peak : 1.0;
        if (k < 1.0) k = 1.0;
        if (k > 16.0) k = 16.0;
        const double half = (h / 2.0 - 4.0);
        for (int x = 0; x < nPts; ++x) {
            int16_t v = data_[x];
            const int seg = std::min((int)data_.size() - 1, x * bins + bins - 1);
            for (int i = x * bins; i <= seg; ++i) {
                const int a = data_[i];
                if (std::abs(a) > std::abs(v)) v = a;
            }
            double y = mid - (v / (double)32768.0) * half * k;
            if (y < 1.0) y = 1.0;            /* 削波保护，边界各留 1px */
            if (y > h - 2.0) y = h - 2.0;
            poly << QPointF(double(x) + 0.5, y);
        }
        p.drawPolyline(poly);

        /* 播放进度游标：竖线随进度移动 */
        if (progress_ >= 0.0 && progress_ <= 1.0) {
            const double x = progress_ * (w - 1.0);
            p.setPen(QPen(QColor("#c81e1e"), 2));
            p.drawLine(QPointF(x, 2), QPointF(x, h - 2));
        }
    }
private:
    QColor color_;
    double progress_ = -1.0;        /* 播放进度 [0,1]；<0 不画游标 */
    std::vector<int16_t> data_;
};

namespace {
/* 列出 ALSA 设备名（供下拉选择）。较通用：返回 hw:N,C 与 default。 */
QStringList listAlsaDevices() {
    QStringList out;
    out << QStringLiteral("default");

    snd_ctl_t* ctl = nullptr;
    int card = -1;
    if (snd_card_next(&card) != 0) return out;
    while (card >= 0) {
        QString name = QStringLiteral("hw:%1").arg(card);
        if (snd_ctl_open(&ctl, name.toUtf8().constData(), 0) == 0) {
            out << name;   /* 卡片级；具体子设备可由用户填 hw:N,M */
            snd_ctl_close(ctl);
        }
        if (snd_card_next(&card) != 0) break;
    }
    return out;
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    currentCfg_ = audio::ProcessorConfig::defaults();
    buildUi();
}

MainWindow::~MainWindow() { stopAll(); }

void MainWindow::setConfig(QString model, QString license, QString auth, int module,
                           QString capDev, QString playDev, QString outDir) {
    model_ = std::move(model);
    license_ = std::move(license);
    auth_ = std::move(auth);
    module_ = module;
    capDev_ = std::move(capDev);
    playDev_ = std::move(playDev);
    outDir_ = std::move(outDir);
    /* 输出框默认指向工程内 output/ 下的处理结果 */
    if (outEdit_) outEdit_->setText(outDir_ + QStringLiteral("/recorded_processed.wav"));
}

void MainWindow::buildDevices() {
    const QStringList devs = listAlsaDevices();
    capCombo_->clear();
    for (const QString& d : devs) capCombo_->addItem(d, d);
    int ci = capCombo_->findData(capDev_);
    capCombo_->setCurrentIndex(ci >= 0 ? ci : 0);
}

void MainWindow::closeEvent(QCloseEvent* event) { stopAll(); event->accept(); }

/* 窗口尺寸固定（= 卡片画布 + 外框留白），缩放系数只在 buildUi 时算一次，
 * 这里仅把画布居中，避免窗口尺寸回馈导致比例反复变化 */
void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (!page_) return;
    const int x = (width() - page_->width()) / 2;
    const int y = (height() - page_->height()) / 2;
    page_->move(x < 0 ? 0 : x, y < 0 ? 0 : y);
}

void MainWindow::stopAll() {
    stopPlayback();
    stopMicTest();
    stopRecord(false);
    if (stream_) {
        stream_->stop();
        delete stream_;
        stream_ = nullptr;
    }
    recBuf_.clear();
    lastOut_.clear();
    outView_.clear();
    recFrames_ = 0;
}

void MainWindow::stopRecord(bool process) {
    stopMicTest();   /* 录制与麦克风测试互斥：开录制即停测试 */
    recording_ = false;
    if (capture_) {
        capture_->stop();
        delete capture_;
        capture_ = nullptr;
    }
    /* 停流式线程：它会先把队列里剩余的帧处理完再退出，尾部音频不会丢。
     * 完整结果从 worker 内部累计缓冲取（信号是异步投递的，不能靠 lastOut_ 累计）。 */
    audio::ProcessStats st;
    if (stream_) {
        stream_->stop();
        st = stream_->stats();
        lastOut_ = stream_->takeOutput();
    }
    if (recordButton_->isChecked()) {
        recordButton_->blockSignals(true);
        recordButton_->setChecked(false);
        recordButton_->blockSignals(false);
    }
    recordButton_->setText(QStringLiteral("▶  Start Recording"));
    setSwitchesEnabled(true);
    updatePlayButtons();

    if (!process || recBuf_.empty()) return;

    /* 落盘目录固定为工程内 output/（run.sh 通过 ANS_OUTPUT_DIR 传入绝对路径），
     * 不做任何工程外回退；不可写时界面会明确报出实际尝试的路径与原因。 */
    QDir().mkpath(outDir_);

    /* 原始输入落盘（便于与处理结果对比） */
    const QString inPath = outDir_ + QStringLiteral("/recorded_input.wav");
    QFileInfo ifi(inPath);
    QString inSaved;
    if (ifi.dir().mkpath(QStringLiteral(".")) &&
        wav::writePcmFile(ifi.absoluteFilePath().toStdString(), recBuf_.data(),
                          static_cast<uint64_t>(recBuf_.size()), audio::kSampleRate)) {
        inSaved = ifi.absoluteFilePath();
    }

    /* 处理后音频落盘（流式输出的累计结果）。界面填的路径不可写时，
     * 回退到工程内 output/recorded_processed.wav，并回写实际路径。 */
    QString outSaved;
    QString outFailPath;
    if (!lastOut_.empty()) {
        const QString want = outEdit_->text().trimmed();
        QFileInfo ofi(want.isEmpty() ? outDir_ + QStringLiteral("/recorded_processed.wav") : want);
        if (ofi.dir().mkpath(QStringLiteral(".")) &&
            wav::writePcmFile(ofi.absoluteFilePath().toStdString(), lastOut_.data(),
                              static_cast<uint64_t>(lastOut_.size()), audio::kSampleRate)) {
            outSaved = ofi.absoluteFilePath();
        } else {
            outFailPath = ofi.absoluteFilePath();
            const QString fb = outDir_ + QStringLiteral("/recorded_processed.wav");
            if (wav::writePcmFile(fb.toStdString(), lastOut_.data(),
                                  static_cast<uint64_t>(lastOut_.size()), audio::kSampleRate)) {
                outSaved = fb;
                outEdit_->setText(fb);
            }
        }
    }

    QString msg;
    if (!inSaved.isEmpty()) msg += QStringLiteral("Input: %1\n").arg(inSaved);
    if (!outSaved.isEmpty()) msg += QStringLiteral("Output: %1").arg(outSaved);
    else if (!outFailPath.isEmpty())
        msg += QStringLiteral("Failed to save processed WAV (permission denied): %1").arg(outFailPath);
    else msg += QStringLiteral("Failed to save processed WAV.");
    msg += QStringLiteral("\nFrames=%1, avg=%2 ms, max=%3 ms, RTF=%4, dropped=%5")
               .arg(st.frames)
               .arg(st.avgMs(), 0, 'f', 2)
               .arg(st.maxMs, 0, 'f', 2)
               .arg(st.rtf(), 0, 'f', 3)
               .arg(stream_ ? stream_->droppedFrames() : 0);
    resultLabel_->setText(msg);
    authLabel_->setText(QStringLiteral("Ready"));
    statusBar()->showMessage(inSaved.isEmpty() ? QStringLiteral("Input WAV not saved")
                                               : QStringLiteral("Saved: %1").arg(inSaved), 6000);
}

void MainWindow::setSwitchesEnabled(bool en) {
    switchesPanel_->setEnabled(en);
}

audio::ProcessorConfig MainWindow::readConfigFromUi() const {
    audio::ProcessorConfig c;
    c.denoiseStrength = denoiseSlider_ ? denoiseSlider_->value() / 100.0f : 0.6f;
    c.module          = module_;
    if (denoiseValue_) denoiseValue_->setText(QStringLiteral("%1").arg(c.denoiseStrength, 0, 'f', 2));
    /* 平稳/非平稳降噪由引擎内部常开，强度与上面的滑块是同一个参数 */
    if (stationaryInfo_)
        stationaryInfo_->setText(QStringLiteral("Stationary noise suppression: ON (strength %1)")
                                     .arg(c.denoiseStrength, 0, 'f', 2));
    if (nonStationaryInfo_)
        nonStationaryInfo_->setText(QStringLiteral("Non-stationary noise suppression: ON (strength %1)")
                                        .arg(c.denoiseStrength, 0, 'f', 2));
    return c;
}

/* 依据窗口实际可用尺寸求等比系数：整页 1240x762 必须完整落在可视区内 */
static double fitScale(int w, int h) {
    if (w <= 0 || h <= 0) return 1.0;
    double s = std::min(double(w) / kPageW, double(h) / kPageH);
    if (s > 1.0) s = 1.0;   /* 不放大，最大 1:1 原始像素 */
    if (s < 0.2) s = 0.2;   /* 保护下限，避免极端小窗导致零尺寸 */
    return s;
}

/* Exit 图标：沿用坐姿检测 UI 的线性 log-out 画法（左侧开口框 + 向右穿出的箭头），
 * 24x24 逻辑坐标、2px 圆头描边，用 QPainter 自绘，不依赖 SVG/字体，避免缺字形。 */
static QIcon makeExitIcon(const QColor& color, int px) {
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(px / 24.0, px / 24.0);
    p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(5, 4), QPointF(11, 4));      /* 框顶 */
    p.drawLine(QPointF(5, 20), QPointF(11, 20));    /* 框底 */
    p.drawLine(QPointF(5, 4), QPointF(5, 20));      /* 框左 */
    p.drawLine(QPointF(20, 12), QPointF(9, 12));    /* 箭头杆 */
    p.drawLine(QPointF(15, 8), QPointF(20, 12));    /* 箭头上翼 */
    p.drawLine(QPointF(20, 12), QPointF(15, 16));   /* 箭头下翼 */
    p.end();
    return QIcon(pm);
}

/* 生成随缩放系数 s 联动的样式表（字号/圆角/间距全部按比例） */
static QString uiStyleSheet(double s) {
    auto P = [s](double v) { return qRound(v * s); };
    return QStringLiteral(
        /* 窗口背景作为「总外框」：画布四周留一圈深色描边带，形成外框效果 */
        "QMainWindow { background:#c2d1ea; }"
        "QWidget#page { background:#e6edf9; }"
        "QFrame#header { background:#f4f8ff; border:1px solid #e3eaf7; border-radius:%1px; }"
        "QFrame#card { background:#ffffff; border:1px solid #e3eaf7; border-radius:%1px; }"
        "QLabel { color:#132a4a; background:transparent; font-size:%2px; }"
        "QLabel#muted { color:#58709b; font-size:%3px; }"
        "QLabel#title { color:#132a4a; font-size:%4px; font-weight:bold; }"
        "QLabel#h3 { color:#132a4a; font-size:%5px; font-weight:bold; }"
        "QLabel#cap { color:#2f5bff; font-size:%6px; font-weight:bold; background:#eef3ff;"
                   " border:1px solid #d4e0ff; padding:0 %7px; border-radius:%8px; }"
        "QLabel#result { color:#0a6; font-size:%9px; font-weight:bold; }"
        "QLineEdit, QComboBox, QSpinBox { border:1px solid #dbe4f2; border-radius:%10px;"
                   " padding:0 %11px; background:#fbfcff; color:#132a4a; font-size:%9px; outline:none; }"
        "QComboBox::drop-down { border:none; width:%12px; }"
        "QComboBox:focus, QSpinBox:focus, QLineEdit:focus { outline:none; border:1px solid #c3d2ef; }"
        "QPushButton { background:#2f5bff; color:#ffffff; border:none; border-radius:%13px;"
                   " font-size:%14px; font-weight:bold; outline:none; }"
        "QPushButton#small { border-radius:%10px; font-size:%9px; }"
        "QPushButton#exit { background:#fff1f1; color:#c81e1e; border:1px solid #f3c7c7;"
                   " border-radius:%13px; font-size:%14px; font-weight:bold; }"
        "QPushButton#exit:hover { background:#ffe3e3; }"
        "QPushButton#mic { background:#16a34a; color:#ffffff; border:none;"
                   " border-radius:%23px; font-size:%24px; font-weight:bold; }"
        "QPushButton#mic:checked { background:#dc2626; }"
        "QPushButton:hover { background:#2456c4; }"
        "QPushButton:checked { background:#16a34a; }"
        "QPushButton:disabled { background:#a8c0ee; color:#eef3fd; }"
        "QCheckBox { spacing:%15px; color:#132a4a; font-size:%2px; }"
        "QSlider::groove:horizontal { height:%16px; background:#e3eaf7; border-radius:%17px; }"
        "QSlider::handle:horizontal { width:%18px; height:%18px; margin:-%19px 0;"
                   " background:#2f5bff; border-radius:%20px; }"
        "QProgressBar { border:1px solid #dbe4f2; border-radius:%10px; background:#f1f5fc;"
                   " text-align:center; font-size:%9px; color:#132a4a; }"
        "QProgressBar::chunk { background:#2f5bff; border-radius:%21px; }"
        "QWidget#wave { background:#fbfcff; border:1px solid #e3eaf7; border-radius:%22px; }")
        .arg(P(10)).arg(P(13)).arg(P(12)).arg(P(19)).arg(P(15))
        .arg(P(12)).arg(P(12)).arg(P(12)).arg(P(13)).arg(P(6))
        .arg(P(8)).arg(P(22)).arg(P(8)).arg(P(16)).arg(P(8))
        .arg(P(4)).arg(P(2)).arg(P(16)).arg(P(6)).arg(P(8))
        .arg(P(5)).arg(P(8)).arg(P(6)).arg(P(13));
}

/* 应用绝对定位：所有子控件按其 dx/dy/dw/dh 设计像素属性重新 setGeometry，
 * 只带 dw 或 dh 的做固定宽/高。窗口尺寸变化时重新调用即可等比适配。 */
void MainWindow::applyLayout(double s) {
    if (!page_) return;
    uiScale_ = s;
    gUiScale = s;
    auto P = [s](double v) { return qRound(v * s); };
    setStyleSheet(uiStyleSheet(s));
    page_->setFixedSize(P(kPageW), P(kPageH));

    const QList<QWidget*> ws = page_->findChildren<QWidget*>();
    for (QWidget* w : ws) {
        const QVariant dx = w->property("dx");
        const QVariant dw = w->property("dw");
        const QVariant dh = w->property("dh");
        if (dx.isValid() && dw.isValid()) {
            w->setGeometry(P(dx.toInt()), P(w->property("dy").toInt()),
                           P(dw.toInt()), P(dh.toInt()));
        } else if (dw.isValid()) {
            if (dh.isValid()) w->setFixedSize(P(dw.toInt()), P(dh.toInt()));
            else              w->setFixedWidth(P(dw.toInt()));
        } else if (dh.isValid()) {
            w->setFixedHeight(P(dh.toInt()));
        }
    }
}

void MainWindow::buildUi() {
    /* 设计基准 = 固定像素画布（宽 1240 / 高 762），所有坐标均为该基准下的
       绝对像素；place() 只记录设计值，applyLayout() 再按当前窗口尺寸换算落地。 */

    /* ================= 绝对定位画布（左右两栏）=================
       预览像素表（单位 px，1240 宽基准）：
         header        0,0    1240 x 52
         左栏 Waveforms 0,64   720 x 682   （三条波形纵向排列）
         右栏 Record   736,64  488 x 328
         右栏 Switches 736,404 488 x 192   （总高 762）        */
    /* 记录设计像素坐标；实际 setGeometry 由 applyLayout() 统一执行 */
    auto place = [](QWidget* w, int x, int y, int ww, int hh) {
        w->setProperty("dx", x);
        w->setProperty("dy", y);
        w->setProperty("dw", ww);
        w->setProperty("dh", hh);
    };
    /* 固定宽（行内标签/输入框等），高度交给所在行 */
    auto fixedW = [](QWidget* w, int ww) { w->setProperty("dw", ww); };
    /* 固定高（进度条等） */
    auto fixedH = [](QWidget* w, int hh) { w->setProperty("dh", hh); };
    /* 卡片内行容器：固定 x=16，宽度按所在栏的卡片内宽（默认 868） */
    auto mkRow = [](QWidget* parent, int y, int h, int ww = 868) {
        auto* r = new QWidget(parent);
        r->setProperty("dx", 16);
        r->setProperty("dy", y);
        r->setProperty("dw", ww);
        r->setProperty("dh", h);
        return r;
    };
    auto rowLay = [](QWidget* r) {
        auto* lay = new QHBoxLayout(r);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(8);
        return lay;
    };
    auto mkBrowse = [](QWidget* parent) {
        auto* b = new QPushButton(QStringLiteral("Browse"), parent);
        b->setObjectName(QStringLiteral("small"));
        b->setProperty("dw", 70);
        b->setProperty("dh", 30);
        return b;
    };
    auto mkCardTitle = [](QWidget* card, const QString& t, int ww = 868) {
        auto* l = new QLabel(t, card);
        l->setObjectName(QStringLiteral("h3"));
        l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        l->setProperty("dx", 16);
        l->setProperty("dy", 16);
        l->setProperty("dw", ww);
        l->setProperty("dh", 18);
        return l;
    };

    auto* central = new QWidget;
    setCentralWidget(central);

    page_ = new QWidget(central);
    page_->setObjectName(QStringLiteral("page"));

    /* ---------- 顶部条：标题 + 算法状态 + Exit ---------- */
    auto* header = new QFrame(page_);
    header->setObjectName(QStringLiteral("header"));
    place(header, 0, 0, 1240, 52);

    auto* titleLabel = new QLabel(QStringLiteral("Audio Noise Reduction Demo"), header);
    titleLabel->setObjectName(QStringLiteral("title"));
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    place(titleLabel, 16, 0, 470, 52);

    authLabel_ = new QLabel(QStringLiteral("Algorithm status: not initialized"), header);
    authLabel_->setObjectName(QStringLiteral("cap"));
    authLabel_->setAlignment(Qt::AlignCenter);
    place(authLabel_, 840, 13, 280, 26);

    /* Exit 按钮不再单独占位：放进 Function Switches 卡片右下角（见下方该卡片内） */

    /* ---------- 右栏 Record & Process 卡片 (736,64,488x366) ---------- */
    auto* recWidget = new QFrame(page_);
    recWidget->setObjectName(QStringLiteral("card"));
    place(recWidget, 736, 64, 488, 366);
    mkCardTitle(recWidget, QStringLiteral("◉  Record & Process"), 456);

    recordButton_ = new QPushButton(QStringLiteral("▶  Start Recording"), recWidget);
    recordButton_->setCheckable(true);
    place(recordButton_, 16, 46, 456, 52);

    /* Capture device 行 (y=100) */
    auto* devRow = mkRow(recWidget, 116, 32, 456);
    auto* devLay = rowLay(devRow);
    auto* devLbl = new QLabel(QStringLiteral("Capture device:"), devRow);
    fixedW(devLbl, 120);
    capCombo_ = new QComboBox(devRow);
    devLay->addWidget(devLbl);
    devLay->addWidget(capCombo_, 1);

    /* Level 行 (y=140)：Label + 进度条 + 百分比 */
    auto* lvRow = mkRow(recWidget, 160, 22, 456);
    auto* lvLay = rowLay(lvRow);
    auto* lvLbl = new QLabel(QStringLiteral("Level:"), lvRow);
    fixedW(lvLbl, 60);
    levelBar_ = new QProgressBar(lvRow);
    levelBar_->setRange(0, 100);
    levelBar_->setValue(0);
    levelBar_->setTextVisible(false);
    fixedH(levelBar_, 16);
    levelLabel_ = new QLabel(QStringLiteral("0%"), lvRow);
    levelLabel_->setObjectName(QStringLiteral("muted"));
    fixedW(levelLabel_, 60);
    levelLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lvLay->addWidget(lvLbl);
    lvLay->addWidget(levelBar_, 1);
    lvLay->addWidget(levelLabel_);

    /* 实时信息 (y=170) */
    recInfoLabel_ = new QLabel(QStringLiteral("Elapsed: 0.0 s  |  Frames: 0"), recWidget);
    recInfoLabel_->setObjectName(QStringLiteral("muted"));
    recInfoLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    place(recInfoLabel_, 16, 192, 456, 16);

    /* Output WAV 行 (y=218) */
    auto* outPathRow = mkRow(recWidget, 218, 32, 456);
    auto* outPathLay = rowLay(outPathRow);
    auto* outPathLbl = new QLabel(QStringLiteral("Output WAV:"), outPathRow);
    fixedW(outPathLbl, 110);
    outEdit_ = new QLineEdit(outDir_ + QStringLiteral("/recorded_processed.wav"), outPathRow);
    auto* outBrowse = mkBrowse(outPathRow);
    connect(outBrowse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save output WAV"),
            outEdit_->text().trimmed().isEmpty() ? outDir_ + QStringLiteral("/recorded_processed.wav")
                                                 : outEdit_->text().trimmed(),
            QStringLiteral("WAV (*.wav)"));
        if (!f.isEmpty()) outEdit_->setText(f);
    });
    outPathLay->addWidget(outPathLbl);
    outPathLay->addWidget(outEdit_, 1);
    outPathLay->addWidget(outBrowse);

    /* 处理结果 (y=232)：绿色文本最多 3 行（Input / Output / Frames 统计），
       高度 76 保证最后一行完整显示，不被卡片底边裁掉 */
    resultLabel_ = new QLabel(QStringLiteral("No recording yet."), recWidget);
    resultLabel_->setObjectName(QStringLiteral("result"));
    resultLabel_->setWordWrap(true);
    resultLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    place(resultLabel_, 16, 262, 456, 88);

    /* ---------- Function Switches 卡片 (736,442,488x304)：填满右栏下方空白 ---------- */
    switchesPanel_ = new QFrame(page_);
    switchesPanel_->setObjectName(QStringLiteral("card"));
    place(switchesPanel_, 736, 442, 488, 304);
    mkCardTitle(switchesPanel_, QStringLiteral("⚙  Function Switches (apply immediately)"), 456);

    /* 强度行 (y=56)：SDK 只有一个强度参数，平稳降噪/非平稳降噪共用 */
    auto* denoiseRow = mkRow(switchesPanel_, 56, 28, 456);
    auto* denoiseLay = rowLay(denoiseRow);
    auto* denoiseLbl = new QLabel(QStringLiteral("Denoise strength:"), denoiseRow);
    fixedW(denoiseLbl, 230);
    denoiseSlider_ = new QSlider(Qt::Horizontal, denoiseRow);
    denoiseSlider_->setRange(0, 100);
    denoiseSlider_->setValue(qRound(currentCfg_.denoiseStrength * 100));
    denoiseValue_ = new QLabel(denoiseRow);
    denoiseValue_->setObjectName(QStringLiteral("muted"));
    fixedW(denoiseValue_, 40);
    denoiseValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    denoiseLay->addWidget(denoiseLbl);
    denoiseLay->addWidget(denoiseSlider_, 1);
    denoiseLay->addWidget(denoiseValue_);

    /* 麦克风增益行 (y=74)：软件预增益，0~30 dB，作用在采集帧进入算法之前
     * （带削波保护），调整后从下一帧生效 */
    auto* gainRow = mkRow(switchesPanel_, 98, 28, 456);
    auto* gainLay = rowLay(gainRow);
    auto* gainLbl = new QLabel(QStringLiteral("Mic gain:"), gainRow);
    fixedW(gainLbl, 230);
    micGainSlider_ = new QSlider(Qt::Horizontal, gainRow);
    micGainSlider_->setRange(0, 30);
    micGainSlider_->setValue(0);
    micGainValue_ = new QLabel(QStringLiteral("0 dB"), gainRow);
    micGainValue_->setObjectName(QStringLiteral("muted"));
    fixedW(micGainValue_, 60);
    micGainValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    gainLay->addWidget(gainLbl);
    gainLay->addWidget(micGainSlider_, 1);
    gainLay->addWidget(micGainValue_);

    /* 平稳降噪 / 非平稳降噪状态（y=154 / y=176）：引擎内部常开，无独立开关 */
    stationaryInfo_ = new QLabel(switchesPanel_);
    stationaryInfo_->setObjectName(QStringLiteral("muted"));
    place(stationaryInfo_, 16, 154, 456, 18);
    nonStationaryInfo_ = new QLabel(switchesPanel_);
    nonStationaryInfo_->setObjectName(QStringLiteral("muted"));
    place(nonStationaryInfo_, 16, 176, 456, 18);

    /* Exit：与 Start Recording 同尺寸（456x52），位于卡片底部（y=236，下留 16px），
     * 字号沿用 #exit 样式，与 Start 一致 */
    exitButton_ = new QPushButton(QStringLiteral("Exit"), switchesPanel_);
    exitButton_->setObjectName(QStringLiteral("exit"));
    exitButton_->setIcon(makeExitIcon(QColor("#c81e1e"), 20));
    exitButton_->setIconSize(QSize(20, 20));
    place(exitButton_, 16, 236, 456, 52);
    connect(exitButton_, &QPushButton::clicked, this, &QWidget::close);

    readConfigFromUi();   /* 初始化强度数值与两条降噪状态文本 */

    /* ---------- 左栏 Waveforms 卡片 (0,64,720x682) ---------- */
    auto* waveCard = new QFrame(page_);
    waveCard->setObjectName(QStringLiteral("card"));
    place(waveCard, 0, 64, 720, 682);
    mkCardTitle(waveCard, QStringLiteral("▤  Waveforms"), 688);

    /* Captured Input (head y=46 / wave y=68) */
    auto* inHead = new QLabel(QStringLiteral("Captured Input"), waveCard);
    inHead->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    place(inHead, 16, 46, 300, 16);
    playInputButton_ = new QPushButton(QStringLiteral("▶  Play Input"), waveCard);
    playInputButton_->setObjectName(QStringLiteral("small"));
    place(playInputButton_, 584, 41, 120, 26);
    playInputButton_->setEnabled(false);
    inputWave_ = new WaveformWidget(QColor("#2f5bff"), waveCard);
    place(inputWave_, 16, 68, 688, 150);

    /* Processed Result (head y=250 / wave y=272) */
    auto* procHead = new QLabel(QStringLiteral("Processed Result"), waveCard);
    procHead->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    place(procHead, 16, 250, 300, 16);
    playOutputButton_ = new QPushButton(QStringLiteral("▶  Play Output"), waveCard);
    playOutputButton_->setObjectName(QStringLiteral("small"));
    place(playOutputButton_, 584, 245, 120, 26);
    playOutputButton_->setEnabled(false);
    processedWave_ = new WaveformWidget(QColor("#16a34a"), waveCard);
    place(processedWave_, 16, 272, 688, 150);

    /* Live Mic Input (head y=454 / wave y=476 / button 右侧) */
    auto* liveHead = new QLabel(QStringLiteral("Live Mic Input"), waveCard);
    liveHead->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    place(liveHead, 16, 454, 200, 16);

    liveInfo_ = new QLabel(QStringLiteral("monitor ~1 s"), waveCard);
    liveInfo_->setObjectName(QStringLiteral("muted"));
    liveInfo_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    place(liveInfo_, 240, 454, 160, 16);

    micTestButton_ = new QPushButton(QStringLiteral("●  Mic Test"), waveCard);
    micTestButton_->setCheckable(true);
    micTestButton_->setObjectName(QStringLiteral("mic"));
    place(micTestButton_, 584, 449, 120, 26);

    liveWave_ = new WaveformWidget(QColor("#f59e0b"), waveCard);
    place(liveWave_, 16, 476, 688, 150);

    /* 说明文字 (y=636) */
    auto* waveNote = new QLabel(
        QStringLiteral("Mic Test captures microphone audio only (not sent to the algorithm) "
                       "and scrolls about 1 s of input to verify the mic path."),
        waveCard);
    waveNote->setObjectName(QStringLiteral("muted"));
    waveNote->setWordWrap(true);
    waveNote->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    place(waveNote, 16, 636, 688, 30);

    waveformsWidget_ = waveCard;

    /* 只有一页：Record & Process 卡片 + 功能开关 + 波形 */
    recordWidget_ = recWidget;
    recordWidget_->setVisible(true);
    switchesPanel_->setVisible(true);
    waveformsWidget_->setVisible(true);

    setWindowTitle(QStringLiteral("Audio Noise Reduction Demo (ANS)"));
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setVisible(false);

    readConfigFromUi();   /* 初次同步 denoiseValue_ 显示 */
    onConfigChanged();    /* 初始化麦克风增益（dB → 线性） */

    /* 固定窗口（不全屏）：在屏幕上按整页画布 1240x762 定尺寸，四周留外框留白。
     * 预留标题栏/窗口装饰高度，保证整窗完整落在可用屏幕内。 */
    double s0 = 1.0;
    QScreen* scr = QGuiApplication::primaryScreen();
    if (scr) s0 = fitScale(scr->availableGeometry().width() - 48,
                           scr->availableGeometry().height() - 96);
    applyLayout(s0);
    const int pad = std::max(4, qRound(8 * s0));   /* 外框留白 */
    setFixedSize(page_->width() + pad * 2, page_->height() + pad * 2);
    if (scr) {
        const QRect av = scr->availableGeometry();
        move(av.x() + (av.width() - width()) / 2, av.y() + (av.height() - height()) / 2);
    }

    /* Esc 退出（固定窗口，也可用标题栏关闭按钮） */
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, this, &QWidget::close);

    /* 连接 */
    connect(recordButton_, &QPushButton::toggled, this, &MainWindow::onRecordToggled);
    connect(denoiseSlider_, &QSlider::valueChanged, this, &MainWindow::onConfigChanged);
    connect(micGainSlider_, &QSlider::valueChanged, this, &MainWindow::onConfigChanged);
    connect(micTestButton_, &QPushButton::toggled, this, &MainWindow::onMicTestToggled);
    connect(playInputButton_, &QPushButton::clicked, this, &MainWindow::onPlayInput);
    connect(playOutputButton_, &QPushButton::clicked, this, &MainWindow::onPlayOutput);

    /* 回放：单实例单流，播完/出错回到未播放状态 */
    player_ = new AudioPlayback(this);
    connect(player_, &AudioPlayback::finished, this, [this] {
        if (player_->isPlaying()) return;   /* 已被新的播放接管，忽略旧线程的结束通知 */
        const int target = playTarget_;
        playTarget_ = 0;
        updatePlayButtons();
        /* 播放结束：波形恢复为整段窗口显示，并隐藏游标 */
        if (target == 1 && inputWave_) { inputWave_->setData(recBuf_); inputWave_->setPlaybackProgress(-1.0); }
        else if (target == 2 && processedWave_) { processedWave_->setData(lastOut_); processedWave_->setPlaybackProgress(-1.0); }
    });
    connect(player_, &AudioPlayback::playbackError, this, [this](const QString& m) {
        statusBar()->showMessage(m, 6000);
    });
    connect(player_, &AudioPlayback::progress, this, &MainWindow::onPlayProgress);

    uiTimer_ = new QTimer(this);
    connect(uiTimer_, &QTimer::timeout, this, &MainWindow::onUiTimer);
    uiTimer_->start(200);
}

void MainWindow::onUiTimer() {
    if (!recording_) return;
    const double sec = recTimer_.elapsed() / 1000.0;
    levelBar_->setValue(qBound(0, recFrames_ > 0 && recPeak_ > 0 ? recPeak_ * 100 / 32768 : 0, 100));
    levelLabel_->setText(QStringLiteral("%1%").arg(levelBar_->value()));
    recInfoLabel_->setText(QStringLiteral("Elapsed: %1 s | Frames: %2")
                               .arg(sec, 0, 'f', 1)
                               .arg(recFrames_));

    /* 流式处理的实时状态：已处理帧数 / 均耗时 / RTF / 丢弃帧数 */
    if (stream_ && stream_->isRunning()) {
        const audio::ProcessStats st = stream_->stats();
        resultLabel_->setText(QStringLiteral("Streaming... out frames=%1, avg=%2 ms, RTF=%3, dropped=%4")
                                  .arg(st.frames)
                                  .arg(st.avgMs(), 0, 'f', 2)
                                  .arg(st.rtf(), 0, 'f', 3)
                                  .arg(stream_->droppedFrames()));
    }
}

/* ---------------- 录制-流式处理 ---------------- */

void MainWindow::onRecordToggled(bool on) {
    if (!on) { stopRecord(true); return; }

    /* 惰性创建流式 worker（引擎常驻，start 时才做授权+建引擎） */
    if (!stream_) {
        stream_ = new StreamProcessWorker(model_, license_, auth_, module_, this);
        connect(stream_, &StreamProcessWorker::frameProcessed,
                this, &MainWindow::onProcessedFrame);
        connect(stream_, &StreamProcessWorker::failed, this, [this](const QString& e) {
            statusBar()->showMessage(e, 6000);
            resultLabel_->setText(QStringLiteral("Processing failed: %1").arg(e));
            if (recordButton_->isChecked()) recordButton_->setChecked(false);   /* 触发停止 */
        });
    }

    recBuf_.clear();
    lastOut_.clear();
    inputView_.clear();
    outView_.clear();
    recFrames_ = 0;
    recPeak_ = 0;
    recTimer_.restart();
    stopPlayback();   /* 采集期间不允许回放 */

    /* 起流式处理线程（单麦降噪，无参考信号） */
    currentCfg_ = readConfigFromUi();
    recordButton_->setText(QStringLiteral("▶  Starting..."));
    resultLabel_->setText(QStringLiteral("Initializing engine..."));
    authLabel_->setText(QStringLiteral("Initializing..."));
    const QString err = stream_->start(currentCfg_);
    if (!err.isEmpty()) {
        resultLabel_->setText(QStringLiteral("Init failed: %1").arg(err));
        authLabel_->setText(QStringLiteral("Algorithm status: init failed"));
        statusBar()->showMessage(err, 6000);
        recordButton_->blockSignals(true);
        recordButton_->setChecked(false);
        recordButton_->blockSignals(false);
        recordButton_->setText(QStringLiteral("▶  Start Recording"));
        return;
    }

    recording_ = true;
    /* 起始瞬态抑制：丢弃开头 200ms，再淡入 160ms（避免起录时的巨大冲击） */
    startupSkipLeft_ = kStartupSkipFrames;
    fadeLeft_ = kFadeInFrames;
    recordButton_->setText(QStringLiteral("■  Stop"));
    levelBar_->setValue(0);
    levelLabel_->setText(QStringLiteral("0%"));
    resultLabel_->setText(QStringLiteral("Settling input..."));
    authLabel_->setText(QStringLiteral("Processing (streaming)"));

    capture_ = new AudioCapture(this);
    connect(capture_, &AudioCapture::frameCaptured, this, &MainWindow::onFrame);
    connect(capture_, &AudioCapture::captureError, this, [this](const QString& m) {
        statusBar()->showMessage(m, 6000);
        if (recordButton_->isChecked()) recordButton_->setChecked(false);   /* 触发停止 */
    });
    capture_->openDevice(capCombo_->currentData().toString());
    capture_->start();
    updatePlayButtons();
}

void MainWindow::onFrame(const audio::AudioFrame& f) {
    /* 停止后仍可能有排队中的采集帧投递过来：丢弃，避免落盘后再改写 recBuf_ */
    if (!recording_) return;

    /* 起始瞬态：开头 200ms 整段丢弃（ALSA 起振 / codec 上电冲击，与真实语音无关） */
    if (startupSkipLeft_ > 0) {
        --startupSkipLeft_;
        if (startupSkipLeft_ == 0 && resultLabel_)
            resultLabel_->setText(QStringLiteral("Streaming..."));
        return;
    }
    /* 随后 160ms 线性淡入，避免仍有残余直流跳变时产生“啪”声 */
    double fade = 1.0;
    if (fadeLeft_ > 0) {
        fade = 1.0 - double(fadeLeft_) / double(kFadeInFrames);
        --fadeLeft_;
    }
    const double gain = micGain_ * fade;

    /* 软件预增益 + 淡入（削波保护）：作用在进算法之前 */
    audio::AudioFrame g = f;
    if (gain != 1.0) {
        for (int16_t& s : g.samples) {
            int v = qRound(double(s) * gain);
            if (v > 32767) v = 32767;
            else if (v < -32768) v = -32768;
            s = int16_t(v);
        }
    }

    recBuf_.insert(recBuf_.end(), g.samples.begin(), g.samples.end());
    ++recFrames_;
    int peak = 0;
    for (int16_t s : g.samples) {
        int a = int(s); if (a < 0) a = -a;
        if (a > peak) peak = a;
    }
    if (peak > recPeak_) recPeak_ = peak;

    /* Captured Input 波形：滚动保留最近约 1s（16000 采样） */
    const size_t cap = audio::kSampleRate * 1;
    inputView_.insert(inputView_.end(), g.samples.begin(), g.samples.end());
    if (inputView_.size() > cap)
        inputView_.erase(inputView_.begin(), inputView_.end() - cap);
    inputWave_->setData(inputView_);

    if (stream_) stream_->submitFrame(g.samples.data(), g.samples.size());
}

/* 流式输出回调：只负责实时波形显示（完整结果由 worker 内部累计，停止时整段取走） */
void MainWindow::onProcessedFrame(const std::vector<int16_t>& out) {
    if (out.empty()) return;
    const size_t cap = audio::kSampleRate * 1;
    outView_.insert(outView_.end(), out.begin(), out.end());
    if (outView_.size() > cap)
        outView_.erase(outView_.begin(), outView_.end() - cap);
    if (processedWave_) processedWave_->setData(outView_);
}

void MainWindow::onConfigChanged() {
    currentCfg_ = readConfigFromUi();
    /* 麦克风增益：dB → 线性（10^(dB/20)），从下一帧采集开始生效 */
    if (micGainSlider_) {
        const int db = micGainSlider_->value();
        micGain_ = std::pow(10.0, db / 20.0);
        if (micGainValue_) micGainValue_->setText(QStringLiteral("%1 dB").arg(db));
    }
    /* 流式中参数从下一帧生效（引擎有状态，不回改已处理数据） */
    if (stream_ && stream_->isRunning()) stream_->setConfig(currentCfg_);
}

/* ---------------- 麦克风测试（仅采集展示，不进算法） ---------------- */

void MainWindow::stopMicTest() {
    micTestOn_ = false;
    if (micTestCapture_) {
        micTestCapture_->stop();
        delete micTestCapture_;
        micTestCapture_ = nullptr;
    }
    liveBuf_.clear();
    if (liveWave_) liveWave_->setData({});
    if (liveInfo_) liveInfo_->setText(QStringLiteral("monitor ~1 s"));
    if (micTestButton_) {
        if (micTestButton_->isChecked()) {
            micTestButton_->blockSignals(true);
            micTestButton_->setChecked(false);
            micTestButton_->blockSignals(false);
        }
        micTestButton_->setText(QStringLiteral("●  Mic Test"));
    }
    recordButton_->setEnabled(true);
    updatePlayButtons();
}

void MainWindow::onMicTestToggled(bool on) {
    if (!on) { stopMicTest(); return; }

    /* 与录制互斥：若在录制，先强制停止录制（停其采集，不批处理） */
    if (recording_) {
        recordButton_->blockSignals(true);
        recordButton_->setChecked(false);
        recordButton_->blockSignals(false);
        stopRecord(false);
    }

    micTestOn_ = true;
    liveSkipLeft_ = kStartupSkipFrames;   /* Mic Test 同样丢弃起振瞬态 */
    micTestButton_->setText(QStringLiteral("■  Stop Mic Test"));
    recordButton_->setEnabled(false);
    liveBuf_.clear();
    liveWave_->setData({});
    liveInfo_->setText(QStringLiteral("testing..."));

    micTestCapture_ = new AudioCapture(this);
    connect(micTestCapture_, &AudioCapture::frameCaptured, this, &MainWindow::onLiveFrame);
    connect(micTestCapture_, &AudioCapture::captureError, this, [this](const QString& m) {
        statusBar()->showMessage(m, 6000);
        if (micTestButton_->isChecked()) micTestButton_->setChecked(false);
    });
    micTestCapture_->openDevice(capCombo_->currentData().toString());
    micTestCapture_->start();
    stopPlayback();   /* 采集期间不允许回放 */
    updatePlayButtons();
}

void MainWindow::onLiveFrame(const audio::AudioFrame& f) {
    const size_t cap = audio::kSampleRate;   /* 滚动保留最近 1s */
    /* Mic Test 起始同样丢弃起振瞬态，避免波形上出现一次巨大冲击 */
    if (liveSkipLeft_ > 0) {
        --liveSkipLeft_;
        return;
    }
    /* Mic Test 同样走软件预增益，便于直观看到增益对采集电平的影响 */
    if (micGain_ != 1.0) {
        for (int16_t s : f.samples) {
            int v = qRound(double(s) * micGain_);
            if (v > 32767) v = 32767;
            else if (v < -32768) v = -32768;
            liveBuf_.push_back(int16_t(v));
        }
    } else {
        liveBuf_.insert(liveBuf_.end(), f.samples.begin(), f.samples.end());
    }
    if (liveBuf_.size() > cap)
        liveBuf_.erase(liveBuf_.begin(), liveBuf_.end() - cap);
    liveWave_->setData(liveBuf_);
}

/* ---------------- 波形音频回放 ---------------- */

void MainWindow::stopPlayback() {
    if (inputWave_) inputWave_->setPlaybackProgress(-1.0);
    if (processedWave_) processedWave_->setPlaybackProgress(-1.0);
    if (player_ && player_->isPlaying()) player_->stop();
    playTarget_ = 0;
    updatePlayButtons();
}

/* 按播放状态刷新按钮文案/可用性：播放中该按钮显示 Stop，另一个禁用；
 * 采集/麦克风测试期间全部禁用（避免与采集争用同一 codec）。 */
void MainWindow::updatePlayButtons() {
    if (!playInputButton_ || !playOutputButton_) return;
    const bool playing = player_ && player_->isPlaying();
    const bool busy = recording_ || micTestOn_;

    playInputButton_->setText(playing && playTarget_ == 1 ? QStringLiteral("Stop")
                                                          : QStringLiteral("Play Input"));
    playOutputButton_->setText(playing && playTarget_ == 2 ? QStringLiteral("Stop")
                                                           : QStringLiteral("Play Output"));
    playInputButton_->setEnabled(!busy && !recBuf_.empty() && (!playing || playTarget_ == 1));
    playOutputButton_->setEnabled(!busy && !lastOut_.empty() && (!playing || playTarget_ == 2));
}

void MainWindow::onPlayInput() {
    if (playTarget_ == 1 && player_ && player_->isPlaying()) { stopPlayback(); return; }
    if (!player_ || recBuf_.empty()) return;
    playTarget_ = 1;
    if (inputWave_) { inputWave_->setData(recBuf_); inputWave_->setPlaybackProgress(0.0); }
    player_->play(recBuf_, playDev_);
    updatePlayButtons();
}

void MainWindow::onPlayOutput() {
    if (playTarget_ == 2 && player_ && player_->isPlaying()) { stopPlayback(); return; }
    if (!player_ || lastOut_.empty()) return;
    playTarget_ = 2;
    if (processedWave_) { processedWave_->setData(lastOut_); processedWave_->setPlaybackProgress(0.0); }
    player_->play(lastOut_, playDev_);
    updatePlayButtons();
}

/* 播放进度：把已播样本数换算成 [0,1]，驱动对应波形游标同步前进 */
void MainWindow::onPlayProgress(qint64 samplesPlayed) {
    WaveformWidget* w = nullptr;
    size_t total = 0;
    if (playTarget_ == 1) { w = inputWave_;   total = recBuf_.size(); }
    else if (playTarget_ == 2) { w = processedWave_; total = lastOut_.size(); }
    if (!w || total == 0) return;
    double f = double(samplesPlayed) / double(total);
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    w->setPlaybackProgress(f);
}
