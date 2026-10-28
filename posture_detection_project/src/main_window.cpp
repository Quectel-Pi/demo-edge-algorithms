#include "main_window.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QGuiApplication>
#include <QImage>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygon>
#include <QPushButton>
#include <QScreen>
#include <QSize>
#include <QStatusBar>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QtGlobal>
#include <QtMath>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <utility>

#include "camera.h"
#include "settings_dialog.h"

/* 全局 UI 缩放系数：buildUi 里按屏幕高度设置；供 runtime 更新的标签（姿态/结果/待机）
 * 与图标绘制按此等比放大（设计基准 S=1.0 即 1280x720）。 */
static double gUiScale = 1.0;

/* 用纯 QPainter 自绘标准线性图标（Lucide 风格，stroke 2px），不依赖 QtSvg。
 * kind: play / exit / gear / cam / target / pulse / sliders。仅本文件内部使用。
 * 逻辑坐标系 24x24，绘制时缩放到 px 像素。 */
static QPixmap makeGlyph(const char* kind, const QColor& color, int px) {
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    qreal s = px / 24.0;
    p.scale(s, s);
    p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    if (std::strcmp(kind, "play") == 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        QPolygonF tri;
        tri << QPointF(6, 3) << QPointF(20, 12) << QPointF(6, 21);
        p.drawPolygon(tri);
    } else if (std::strcmp(kind, "exit") == 0) {     /* log-out：左侧开口框 + 向右穿出的箭头 */
        p.drawLine(QPointF(5, 4), QPointF(11, 4));   /* 框顶 */
        p.drawLine(QPointF(5, 20), QPointF(11, 20));/* 框底 */
        p.drawLine(QPointF(5, 4), QPointF(5, 20));  /* 框左 */
        p.drawLine(QPointF(20, 12), QPointF(9, 12));/* 箭头杆 */
        p.drawLine(QPointF(15, 8), QPointF(20, 12));p.drawLine(QPointF(20, 12), QPointF(15, 16)); /* 箭头 */
    } else if (std::strcmp(kind, "gear") == 0) {     /* settings：外圈齿 + 内圆 */
        p.setBrush(Qt::NoBrush);
        const double step = 2 * M_PI / 8;
        for (int i = 0; i < 8; ++i) {
            const double a = i * step;
            p.drawLine(QPointF(12 + 9.2 * qCos(a), 12 + 9.2 * qSin(a)),
                       QPointF(12 + 6.4 * qCos(a), 12 + 6.4 * qSin(a)));
        }
        p.drawEllipse(QRectF(12 - 3.4, 12 - 3.4, 6.8, 6.8));
    } else if (std::strcmp(kind, "cam") == 0) {
        p.setBrush(Qt::NoBrush);
        QPolygonF body;
        body << QPointF(9.5, 4) << QPointF(14.5, 4) << QPointF(17.5, 7)
             << QPointF(20, 7) << QPointF(20, 18) << QPointF(4, 18)
             << QPointF(4, 9) << QPointF(7, 7);
        p.drawPolyline(body);
        p.drawEllipse(QRectF(9, 10, 6, 6));          /* 镜头 */
    } else if (std::strcmp(kind, "target") == 0) {   /* 靶心 */
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(2, 2, 20, 20));         /* 外环 r10 */
        p.drawEllipse(QRectF(6, 6, 12, 12));         /* 中环 r6 */
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QRectF(10, 10, 4, 4));         /* 中心 */
    } else if (std::strcmp(kind, "pulse") == 0) {    /* activity 脉搏线 */
        QPainterPath path;
        path.moveTo(2, 12); path.lineTo(6, 12); path.lineTo(9, 3);
        path.lineTo(15, 21); path.lineTo(18, 12); path.lineTo(22, 12);
        p.drawPath(path);
    } else {                                          /* sliders-horizontal */
        p.drawLine(QPointF(21, 4), QPointF(14, 4)); p.drawLine(QPointF(10, 4), QPointF(3, 4));
        p.drawLine(QPointF(21, 12), QPointF(12, 12)); p.drawLine(QPointF(8, 12), QPointF(3, 12));
        p.drawLine(QPointF(21, 20), QPointF(16, 20)); p.drawLine(QPointF(12, 20), QPointF(3, 20));
        p.drawLine(QPointF(14, 2), QPointF(14, 6));
        p.drawLine(QPointF(8, 10), QPointF(8, 14));
        p.drawLine(QPointF(16, 18), QPointF(16, 22));
    }
    p.end();
    return pm;
}

/* 按钮图标(18px)，颜色单独给定 */
static QIcon makeButtonIcon(const QString& kind, QColor color) {
    return QIcon(makeGlyph(kind.toLatin1().constData(), color, 18));
}

/* 分区标题图标(16px)，统一主蓝色 */
static QPixmap makeSectionIcon(const char* kind) {
    return makeGlyph(kind, QColor(0x2f, 0x5b, 0xff), 16);
}

/* 按姿态序号(0未知/1托腮/2低头/3高低肩)切换 Posture 徽章的文字与区分色 */
static void setPosturePill(QLabel* l, int pose) {
    if (!l) return;
    struct P { const char* txt; const char* bg; const char* fg; };
    static const P k[4] = {
        {"unknown", "#eef1f6", "#64748b"},
        {"Cheek Support", "#fdeecb", "#b45309"},
        {"Head Bowing", "#fde3e1", "#dc2626"},
        {"Shoulder Imbalance", "#efe9fc", "#7c3aed"},
    };
    l->setText(QLatin1String(k[pose].txt));
    l->setStyleSheet(QStringLiteral(
        "color:%1;background:%2;padding:%3px %4px;border-radius:%5px;font-size:%6px;font-weight:bold;")
        .arg(QLatin1String(k[pose].fg), QLatin1String(k[pose].bg))
        .arg(qRound(2 * gUiScale)).arg(qRound(6 * gUiScale))
        .arg(qRound(9 * gUiScale)).arg(qRound(15 * gUiScale)));
}

/* 有无检到人的染色：Detected 绿 / None 灰 */
static void setDetectLabel(QLabel* l, bool person) {
    if (!l) return;
    l->setText(person ? QStringLiteral("Detected") : QStringLiteral("None"));
    l->setStyleSheet(person ? QStringLiteral("color:#16a34a;font-size:%1px;font-weight:bold;").arg(qRound(16 * gUiScale))
                            : QStringLiteral("color:#94a3bd;font-size:%1px;font-weight:bold;").arg(qRound(16 * gUiScale)));
}

/* 待机状态统一显示 NONE（灰字），用于未检测/关闭检测/切换摄像头等空闲情形 */
static void setResultStandby(QLabel* det, QLabel* pose, QLabel* conf) {
    const QString noneCss = QStringLiteral("color:#94a3bd;font-size:%1px;font-weight:bold;")
                                .arg(qRound(13 * gUiScale));
    if (det) {
        det->setText(QStringLiteral("NONE"));
        det->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        det->setStyleSheet(noneCss);
    }
    if (pose) {
        pose->setText(QStringLiteral("NONE"));
        pose->setStyleSheet(noneCss);
    }
    if (conf) {
        conf->setText(QStringLiteral("NONE"));
        conf->setStyleSheet(noneCss);
    }
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
}

MainWindow::~MainWindow() {
    /* 线程若未在 closeEvent 中停过（例如程序被系统强制结束），这里再兜底 */
    stopPipeline();
    delete camera_;
    camera_ = nullptr;
    delete detector_;
    detector_ = nullptr;
}

/* 关闭窗口：先停采集线程，再关算法线程并释放 SDK，最后才放行关闭。
 * 确保窗口消失前所有后台线程已 join，进程干净退出、不残留线程。 */
void MainWindow::closeEvent(QCloseEvent* event) {
    stopPipeline();
    event->accept();
}

/* 统一停线程入口：幂等，可被 closeEvent 与析构重复调用 */
void MainWindow::stopPipeline() {
    if (camera_) {
        camera_->stop();               /* join 摄像头采集线程 */
    }
    if (detector_) {
        detector_->setDetectionEnabled(false);
        detector_->shutdown();         /* join 算法线程，并 uninit 释放 SDK */
    }
    hub_.reset();                      /* 释放共享最新帧缓存 */
    cameraRunning_ = false;
}

void MainWindow::buildUi() {
    /* ===== 全局等比缩放 =====
       设计基准 1280x720（S=1.0）。按屏幕高度算出 S，下面所有固定像素/字号都乘 P(v)，
       使全屏时各卡片、边距、字体等比例同步放大。 */
    QScreen* scr = QGuiApplication::primaryScreen();
    double S = scr ? (double)scr->geometry().height() / 720.0 : 1.0;
    if (S < 0.8) S = 0.8;   /* 可读性下限 */
    if (S > 2.4) S = 2.4;   /* 上限，避免极端放大 */
    auto P = [S](double v) { return qRound(v * S); };
    gUiScale = S;   /* 供 runtime 标签/图标等比放大 */

    /* ===== 蓝白高级配色主题 =====
       边/底 : #E6EDF9  页面渐变 #F5F8FE->#E9EFFC
       卡片 #FFFFFF 线 #E3EAF7  主蓝 #2F5BFF  文字 #132A4A/#58709B/#8FA5CE */
    setStyleSheet(QStringLiteral(
        "QMainWindow { background:#e6edf9; }"   /* 边缘色，透过圆角页面四角露出 */
        "QLabel { background:transparent; color:#58709b; font-size:%1px; }"
        "QComboBox { border:1px solid #dbe4f2; border-radius:%2px; padding:0 %3px;"
        "    background:#fbfcff; color:#132a4a; font-size:%4px; outline:none; }"
        "QComboBox::drop-down { border:none; width:%5px; outline:none; }"   /* 去除Linux系统框 */
        "QComboBox::down-arrow { image:none; border:%6px solid transparent;"
        "    border-top-color:#58709b; margin-right:%7px; }"
        "QComboBox:focus { outline:none; border:1px solid #c3d2ef; }"   /* 抑制焦点虚框 */
        "QComboBox:on { outline:none; border:1px solid #c3d2ef; }"      /* 下拉展开态也不闪框 */
        "QComboBox:hover { border-color:#c3d2ef; outline:none; }"
        "QComboBox:disabled { background:#f3f6fc; color:#a4b4d2; outline:none; }"
        "QPushButton { border:none; border-radius:%8px; font-size:%9px; font-weight:bold; outline:none; }"
        "QPushButton:disabled { background:#eef2f9; color:#a4b4d2; outline:none; }"
        "QPushButton:focus { outline:none; }")   /* 抑制键盘导航时的焦点虚框 */
        .arg(P(14)).arg(P(11)).arg(P(12)).arg(P(15))
        .arg(P(22)).arg(P(6)).arg(P(8)).arg(P(12)).arg(P(15)));

    /* 状态栏若不隐藏会占用底部高度，导致页面无法 1:1 贴合 692px 预览，故直接隐藏 */
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setVisible(false);

    /* ---------- 顶部导航栏：logo + 品牌 + 引擎状态，与预览 topbar 一致（高58px） ---------- */
    auto* header = new QFrame;
    header->setFixedHeight(P(58));
    header->setStyleSheet("QFrame{background:#f4f8ff;border:1px solid #e3eaf7;border-radius:16px;}");
    auto* hLay = new QHBoxLayout(header);
    hLay->setContentsMargins(P(14), P(9), P(16), P(9));
    hLay->setSpacing(P(12));
    auto* logo = new QLabel(QStringLiteral("P"));
    logo->setFixedSize(P(34), P(34));
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                        "stop:0 #2f5bff, stop:1 #5a84ff); color:#ffffff;"
                        "border-radius:10px; font-size:16px; font-weight:800;");
    hLay->addWidget(logo);
    auto* brand = new QVBoxLayout;
    brand->setSpacing(0);
    auto* bTitle = new QLabel(QStringLiteral("Posture Detection"));
    bTitle->setStyleSheet(QStringLiteral("color:#132a4a; font-size:%1px; font-weight:700;").arg(P(15)));
    auto* bSub = new QLabel(QStringLiteral("Quectel PI · RK3576 NPU"));
    bSub->setStyleSheet(QStringLiteral("color:#58709b; font-size:%1px;").arg(P(12)));
    brand->addWidget(bTitle);
    brand->addWidget(bSub);
    hLay->addLayout(brand);
    hLay->addStretch(1);
    auto* pill = new QLabel(QStringLiteral("\u25CF  Engine Ready"));
    pill->setStyleSheet(QStringLiteral("background:#ffffff;border:1px solid #e3eaf7;border-radius:999px;"
                        "padding:%1px %2px;color:#16a34a;font-size:%3px;font-weight:600;")
                        .arg(P(7)).arg(P(13)).arg(P(12)));
    hLay->addWidget(pill);

    auto* root = new QHBoxLayout;
    /* 栅格内边距：左/右16，上/下14（与预览 .grid padding:14px 16px 一致） */
    root->setContentsMargins(P(16), P(14), P(16), P(14));
    root->setSpacing(P(18));

    /* 卡片外观与分区标题的便捷工厂 */
    auto cardFrame = []() {
        auto* f = new QFrame;
        f->setStyleSheet(
            "QFrame{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "    stop:0 #ffffff, stop:0.80 #fbfdff, stop:1 #edf3fc);"
            "    border:1px solid #dfe8f6; border-bottom:2px solid #ccd8ec; border-radius:22px;}");
        return f;
    };
    auto secTitle = [P](const QString& text, const char* kind) {
        auto* box = new QWidget;
        auto* lay = new QHBoxLayout(box);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(P(6));
        auto* ic = new QLabel;
        ic->setPixmap(makeSectionIcon(kind));
        ic->setFixedSize(P(16), P(16));
        auto* t = new QLabel(text);
        t->setStyleSheet(QStringLiteral("color:#8fa5ce;font-size:%1px;font-weight:bold;"
                         "letter-spacing:1.2px;").arg(P(12)));
        lay->addWidget(ic);
        lay->addWidget(t);
        lay->addStretch(1);
        return box;
    };

    /* ---------- 左列：摄像头预览卡片（等比缩放，左上角叠加 fps） ---------- */
    auto* previewCard = cardFrame();
    previewCard->setFixedSize(P(842), P(606));   /* 左列：宽842 高606，严格贴合预览 */
    auto* pcLayout = new QVBoxLayout(previewCard);
    pcLayout->setContentsMargins(P(12), P(12), P(12), P(12));   /* 预览内边距12 */
    pcLayout->setSpacing(P(10));

    previewLabel_ = new QLabel(QStringLiteral("Camera Preview"));
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setStyleSheet(QStringLiteral("background:#131a26; color:#8fa5ce;"
                                 "border-radius:%1px; font-size:%2px;").arg(P(12)).arg(P(15)));
    previewLabel_->setScaledContents(false);
    previewLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewLabel_->setMinimumSize(P(480), P(300));

    fpsLabel_ = new QLabel(QStringLiteral("-- fps"));
    fpsLabel_->setStyleSheet(QStringLiteral("background:rgba(0,0,0,140); color:#ffffff;"
                             "border-radius:%1px; padding:%2px %3px; font-size:%4px;")
                             .arg(P(10)).arg(P(3)).arg(P(10)).arg(P(12)));

    auto* stageGrid = new QGridLayout;
    stageGrid->setContentsMargins(0, 0, 0, 0);
    stageGrid->setSpacing(0);
    stageGrid->addWidget(previewLabel_, 0, 0);
    stageGrid->setColumnStretch(0, 1);
    stageGrid->setRowStretch(0, 1);
    stageGrid->addWidget(fpsLabel_, 0, 0, Qt::AlignLeft | Qt::AlignTop);
    auto* stage = new QWidget;
    stage->setLayout(stageGrid);
    pcLayout->addWidget(stage, 1);

    auto* hintLabel = new QLabel(QStringLiteral("Detection box + keypoints overlay on live feed"));
    hintLabel->setStyleSheet(QStringLiteral("color:#58709b; font-size:%1px;").arg(P(12)));
    pcLayout->addWidget(hintLabel);
    root->addWidget(previewCard);   /* 固定宽842，不再用伸缩比例 */

    /* ---------- 右列：分组卡片，从上到下（宽360） ---------- */
    auto* right = new QVBoxLayout;
    right->setSpacing(0);                    /* 间距全部由卡片间的伸缩吸收，顶/底贴齐 */
    right->setContentsMargins(0, 0, 0, 0);
    /* ① Camera 卡 (360 x 98) */
    auto* camCard = cardFrame();
    camCard->setFixedSize(P(360), P(98));
    auto* camLay = new QVBoxLayout(camCard);
    camLay->setContentsMargins(P(16), P(12), P(16), P(12));
    camLay->setSpacing(P(10));
    camLay->addWidget(secTitle(QStringLiteral("CAMERA"), "cam"));
    cameraCombo_ = new QComboBox;
    cameraCombo_->setEnabled(false);
    cameraCombo_->setFocusPolicy(Qt::NoFocus);   /* 消除Linux点击后出现的外框 */
    cameraCombo_->setToolTip(QStringLiteral("Select capture device"));
    cameraCombo_->setFixedHeight(P(44));   /* 对齐预览 combo 高44 */
    connect(cameraCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onCameraChanged);
    camLay->addWidget(cameraCombo_);
    right->addWidget(camCard);   /* 首卡贴顶，上方无伸缩 */
    right->addStretch(1);        /* cam → det 等距间隙 */

    /* ② Detection 卡：检测开关 + Memory/CPU 单行胶囊 (360 x 152) */
    auto* detCard = cardFrame();
    detCard->setFixedSize(P(360), P(152));
    auto* detLay = new QVBoxLayout(detCard);
    detLay->setContentsMargins(P(16), P(12), P(16), P(12));
    detLay->setSpacing(P(10));
    detLay->addWidget(secTitle(QStringLiteral("DETECTION"), "target"));
    detectButton_ = new QPushButton(QStringLiteral("Start Detection"));
    detectButton_->setCheckable(true);
    detectButton_->setChecked(false);
    detectButton_->setFocusPolicy(Qt::NoFocus);   /* 消除Linux点击后出现的外框 */
    detectButton_->setFixedHeight(P(56));   /* 对齐预览 检测按钮高56 */
    detectButton_->setIcon(makeButtonIcon(QStringLiteral("play"), QColor(Qt::white)));
    detectButton_->setIconSize(QSize(P(17), P(17)));
    detectButton_->setStyleSheet(QStringLiteral(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #6a93ff, stop:0.5 #3b6dff, stop:1 #2a54e8); color:#ffffff;"
        "    border:none; border-bottom:%1px solid #1e44c4; border-radius:%2px;"
        "    font-size:%3px; font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #5584ff, stop:1 #2850e0);}"
        "QPushButton:pressed{background:#2450dd; border-bottom:1px solid #1e44c4;}"
        "QPushButton:checked{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #23b85e, stop:1 #17a34d); border-bottom:%1px solid #0e7d39;}"
        "QPushButton:focus{outline:none;}")
        .arg(P(3)).arg(P(12)).arg(P(15)));
    detLay->addWidget(detectButton_);

    statsLabel_ = new QLabel(QStringLiteral("Memory: -- MB"));
    cpuLabel_   = new QLabel(QStringLiteral("CPU: -- %"));
    const QString chip = QStringLiteral("background:#f0f5ff; border:1px solid #dbe6f7; border-radius:%1px;"
                         "padding:%2px %3px; color:#58709b; font-size:%4px;")
                         .arg(P(12)).arg(P(4)).arg(P(12)).arg(P(13));
    statsLabel_->setStyleSheet(chip);
    cpuLabel_->setStyleSheet(chip);
    auto* metaRow = new QHBoxLayout;
    metaRow->setSpacing(P(10));
    metaRow->addWidget(statsLabel_);
    metaRow->addWidget(cpuLabel_);
    metaRow->addStretch(1);
    detLay->addLayout(metaRow);
    right->addWidget(detCard);
    right->addStretch(1);        /* det → res 等距间隙 */

    /* ③ Live Result 卡：Person / Posture / Confidence 三栏 tile（tile高84，两行姿态完整显示）(360 x 138) */
    auto* resCard = cardFrame();
    resCard->setFixedSize(P(360), P(138));
    auto* resLay = new QVBoxLayout(resCard);
    resLay->setContentsMargins(P(16), P(12), P(16), P(12));
    resLay->setSpacing(P(10));
    resLay->addWidget(secTitle(QStringLiteral("LIVE RESULT"), "pulse"));

    auto tile = [P](const QString& cap, QLabel*& value, const QString& valueCss,
                   const QString& capColor, const QString& frameCss) {
        auto* t = new QFrame;
        t->setStyleSheet(frameCss);
        t->setFixedHeight(P(84));
        auto* tl = new QVBoxLayout(t);
        tl->setContentsMargins(P(7), P(6), P(7), P(6));
        tl->setSpacing(P(4));
        auto* c = new QLabel(cap);
        c->setStyleSheet(QStringLiteral("color:%1;font-size:%2px;font-weight:bold;"
                                        "letter-spacing:.6px;").arg(capColor).arg(P(11)));
        c->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        c->setWordWrap(true);
        value = new QLabel(QStringLiteral("NONE"));   /* 待机统一占位：NONE */
        value->setStyleSheet(valueCss);
        value->setWordWrap(true);   /* 姿态为长词时可换行显示两行，不裁剪 */
        value->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);   /* 值在标题下方剩余空间居中 */
        value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   /* 不强迫变宽，改内部换行 */
        tl->addWidget(c, 0, Qt::AlignHCenter);   /* 小标题在卡内左右居中 */
        tl->addStretch(1);
        tl->addWidget(value, 0, Qt::AlignHCenter);
        tl->addStretch(1);
        return t;
    };
    /* 三张结果小卡：纯白底 + 边框，与下方白色大卡区分；标题/文字保留各自强调色 */
    QFrame* detTile = tile(QStringLiteral("PERSON"), resultDetect_,
                           QStringLiteral("color:#138a4e;font-size:%1px;font-weight:bold;").arg(P(12)), "#138a4e",
                           "QFrame{background:#ffffff;border:1px solid #d3ddf0;"
                           "    border-bottom:2px solid #c3d0ea; border-radius:16px;}");
    QFrame* poseTile = tile(QStringLiteral("POSTURE"), resultPose_,
                            QStringLiteral("padding:%1px %2px;border-radius:%3px;font-size:%4px;font-weight:bold;"
                            "background:#fdeecb;color:#b45309;").arg(P(2)).arg(P(6)).arg(P(9)).arg(P(13)), "#6d5bd0",
                            "QFrame{background:#ffffff;border:1px solid #d3ddf0;"
                            "    border-bottom:2px solid #c3d0ea; border-radius:16px;}");
    QFrame* confTile = tile(QStringLiteral("CONFIDENCE"), resultConf_,
                            QStringLiteral("color:#2f5bff;font-size:%1px;font-weight:bold;").arg(P(12)), "#2f5bff",
                            "QFrame{background:#ffffff;border:1px solid #d3ddf0;"
                            "    border-bottom:2px solid #c3d0ea; border-radius:16px;}");
    auto* resRow = new QHBoxLayout;
    resRow->setSpacing(P(6));
    /* 对齐预览的 1fr:1.5fr:1fr —— Posture 栏比 Person/Confidence 宽 1.5 倍 */
    resRow->addWidget(detTile, 2);
    resRow->addWidget(poseTile, 3);
    resRow->addWidget(confTile, 2);
    resLay->addLayout(resRow);
    right->addWidget(resCard);
    right->addStretch(1);        /* res → act 等距间隙 */

    /* ④ Actions 卡：高级设置(次级主色) + 退出(实心) (360 x 106) */
    auto* actCard = cardFrame();
    actCard->setFixedSize(P(360), P(106));
    auto* actLay = new QVBoxLayout(actCard);
    actLay->setContentsMargins(P(16), P(12), P(16), P(12));
    actLay->setSpacing(P(10));
    actLay->addWidget(secTitle(QStringLiteral("ACTIONS"), "sliders"));
    settingsButton_ = new QPushButton(QStringLiteral("Advanced Settings"));
    settingsButton_->setEnabled(false);
    settingsButton_->setFocusPolicy(Qt::NoFocus);   /* 消除Linux点击后出现的外框 */
    settingsButton_->setFixedHeight(P(52));   /* 对齐预览 Actions 按钮高52 */
    settingsButton_->setIcon(makeButtonIcon(QStringLiteral("gear"), QColor("#2f5bff")));
    settingsButton_->setIconSize(QSize(P(17), P(17)));
    settingsButton_->setStyleSheet(QStringLiteral(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #ffffff, stop:1 #e8efff); color:#2f5bff;"
        "    border:1.5px solid #bcd0fb; border-bottom:%1px solid #a9c0f4; border-radius:%2px;"
        "    font-size:%3px; font-weight:bold;}"
        "QPushButton:hover{background:#eef3ff;}"
        "QPushButton:pressed{background:#dbe6ff; border-bottom:1px solid #a9c0f4;}"
        "QPushButton:disabled{background:#eef2f9;color:#a4b4d2;border-color:#e3eaf7;"
        "    border-bottom-color:#dbe3f0;}"
        "QPushButton:focus{outline:none;}")
        .arg(P(2)).arg(P(12)).arg(P(15)));
    connect(settingsButton_, &QPushButton::clicked, this, &MainWindow::onOpenSettings);
    auto* exitButton = new QPushButton(QStringLiteral("Exit"));
    exitButton->setFocusPolicy(Qt::NoFocus);   /* 消除Linux点击后出现的外框 */
    exitButton->setFixedHeight(P(52));   /* 对齐预览 Actions 按钮高52 */
    exitButton->setIcon(makeButtonIcon(QStringLiteral("exit"), QColor(Qt::white)));
    exitButton->setIconSize(QSize(P(17), P(17)));
    exitButton->setStyleSheet(QStringLiteral(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #4f80ff, stop:1 #2a54e8); color:#ffffff;"
        "    border:none; border-bottom:%1px solid #1e44c4; border-radius:%2px;"
        "    font-size:%3px; font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #3b6dff, stop:1 #2450dd);}"
        "QPushButton:pressed{background:#2450dd; border-bottom:1px solid #1e44c4;}"
        "QPushButton:focus{outline:none;}")
        .arg(P(3)).arg(P(12)).arg(P(15)));
    connect(exitButton, &QPushButton::clicked, this, &MainWindow::close);
    auto* actRow = new QHBoxLayout;
    actRow->setSpacing(P(10));
    actRow->addWidget(settingsButton_);
    actRow->addWidget(exitButton);
    actLay->addLayout(actRow);
    right->addWidget(actCard);   /* 末卡贴底，无尾部伸缩 */

    auto* rightPanel = new QWidget;
    rightPanel->setLayout(right);
    rightPanel->setFixedWidth(P(360));   /* 右列固定宽360，等比放大 */
    root->addWidget(rightPanel);

    /* ---------- 整体：圆角页面 + 顶部导航栏(58) + 下方 左(842):右(360) 网格（像素固定） ---------- */
    auto* body = new QWidget;
    body->setLayout(root);

    auto* mainLay = new QVBoxLayout;
    mainLay->setContentsMargins(0, 0, 0, 0);   /* 页面内部不留边，标题顶到头、内容到底 */
    mainLay->setSpacing(0);                    /* header 与网格间距由 root 上内边距(14px)承担 */
    mainLay->addWidget(header);
    mainLay->addWidget(body, 1);

    /* 圆角页面容器：整体在右上角均为圆角，QMainWindow 边缘色在四角露出 */
    auto* page = new QFrame;
    page->setObjectName(QLatin1String("page"));
    page->setStyleSheet(
        "QFrame#page{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "    stop:0 #f5f8fe, stop:1 #e9effc); border:1px solid #dce5f5; border-radius:18px;}");
    page->setLayout(mainLay);

    auto* central = new QWidget;
    central->setObjectName(QLatin1String("bg"));
    auto* cLay = new QVBoxLayout(central);
    cLay->setContentsMargins(P(14), P(14), P(14), P(14));   /* 页面与窗口边缘留白14，等比放大 */
    cLay->addWidget(page);
    central->setStyleSheet("QWidget#bg{background:#dfe4ef;}");
    setCentralWidget(central);
    setWindowTitle(QStringLiteral("Posture Detection Demo"));
    resize(P(1280), P(720));   /* 基准尺寸按比例放大 */
    showFullScreen();          /* 初始化即全屏 */

    connect(detectButton_, &QPushButton::toggled, this, &MainWindow::onDetectionToggled);
}

void MainWindow::setAlgorithmConfig(QString model, QString license, QString auth, int module) {
    model_    = std::move(model);
    license_  = std::move(license);
    auth_     = std::move(auth);
    module_   = module;
}

void MainWindow::startPipe(int deviceIndex) {
    /* --- 共享最新帧缓存：摄像头写入、算法读取 --- */
    hub_ = std::make_shared<FrameHub>();

    /* --- 算法 worker：自管线程，主动拉"最新帧"推理，绝不阻塞界面线程 --- */
    detector_ = new PostureDetector(hub_);
    connect(detector_, &PostureDetector::initializedChanged, this, &MainWindow::onDetectorStatus);
    connect(detector_, &PostureDetector::resultReady, this, &MainWindow::onDetectorResult);
    connect(detector_, &PostureDetector::detectorError, this,
            [this](const QString& m) { statusBar()->showMessage(m); });
    detector_->initialize(model_, license_, auth_, module_);   /* 提交初始化请求(异步) */
    detector_->start();

    /* --- 摄像头选择下拉：枚举全部可用设备；默认选第一个，有显式指定且存在时优先 --- */
    const std::vector<int> cams = listAvailableCameras();
    activeCamera_ = -1;
    if (!cams.empty()) {
        activeCamera_ = cams.front();
        if (deviceIndex >= 0 &&
            std::find(cams.begin(), cams.end(), deviceIndex) != cams.end())
            activeCamera_ = deviceIndex;
    }

    /* 填充下拉（屏蔽编辑过程触发的 onCameraChanged）；无设备则显示占位项 */
    isSwitchingCameraCombo_ = true;
    cameraCombo_->clear();
    for (const int idx : cams)
        cameraCombo_->addItem(QStringLiteral("/dev/video%1").arg(idx), idx);
    if (cams.empty())
        cameraCombo_->addItem(QStringLiteral("No camera found"), -1);
    if (activeCamera_ >= 0 && cameraCombo_->findData(activeCamera_) >= 0)
        cameraCombo_->setCurrentIndex(cameraCombo_->findData(activeCamera_));
    cameraCombo_->setEnabled(!cams.empty());
    isSwitchingCameraCombo_ = false;

    /* --- 摄像头采集：只把"最新一帧"写入 hub，供算法拉取；预览照发界面 --- */
    camera_ = new CameraWorker(activeCamera_, hub_, this);
    connect(camera_, &CameraWorker::previewReady, this, &MainWindow::onPreview);
    connect(camera_, &CameraWorker::captureError, this, &MainWindow::onCameraError);
    connect(camera_, &CameraWorker::deviceOpened, this,
            [this](int idx) {
                statusBar()->showMessage(QStringLiteral("Using camera /dev/video%1").arg(idx), 4000);
            });
    camera_->start();
    cameraRunning_ = true;
    settingsButton_->setEnabled(true);   /* 检测器已就绪，允许打开高级设置 */

    /* 周期性刷新界面统计（fps / 内存），不影响推理 */
    if (!uiTimer_) {
        uiTimer_ = new QTimer(this);
        connect(uiTimer_, &QTimer::timeout, this, &MainWindow::onUiTimer);
        uiTimer_->start(500);
    }

    statusBar()->showMessage(cams.empty()
                                 ? QStringLiteral("No camera found")
                                 : QStringLiteral("Using camera /dev/video%1").arg(activeCamera_),
                             4000);
}

void MainWindow::onDetectionToggled(bool on) {
    if (!cameraRunning_) return;
    detectionOn_ = on;
    detector_->setDetectionEnabled(on);
    if (detectButton_) {
        /* 按键文本随开关状态切换 */
        detectButton_->setText(on ? QStringLiteral("Stop Detection")
                                  : QStringLiteral("Start Detection"));
    }
    if (!on) {
        /* 停止检测：清空框/关键点、结果输出与滤波窗口，并重绘当前裸预览帧 */
        lastResult_ = PostureFrameResult{};
        hasResult_  = false;
        resetFilterBatch();
        shownPose_  = 0;
        setResultStandby(resultDetect_, resultPose_, resultConf_);
        if (!lastPreview_.isNull()) {
            previewLabel_->setPixmap(QPixmap::fromImage(lastPreview_).scaled(
                previewLabel_->size() * devicePixelRatioF(),
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    } else {
        if (resultPose_) {
            resultPose_->setText(QStringLiteral("detecting..."));
            resultPose_->setStyleSheet("color:#94a3bd;font-size:15px;font-weight:bold;");
        }
    }
}

void MainWindow::onPreview(const QImage& image) {
    /* 始终以最新摄像头帧刷新预览（与推理解耦，保证流畅）；
     * 若已有检测结果，则把框/关键点叠加到当前预览帧上。 */
    uiFrameCount_++;
    lastPreview_ = image;
    QImage toShow = image;
    if (hasResult_) toShow = drawOverlay(image, lastResult_);
    previewLabel_->setPixmap(QPixmap::fromImage(toShow).scaled(
        previewLabel_->size() * devicePixelRatioF(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::onDetectorResult(const PostureFrameResult& r) {
    /* 丢弃检测已关闭后仍迟到的结果信号，避免框/点又画回来 */
    if (!detectionOn_) return;
    lastResult_ = r;
    hasResult_  = true;

    /* 用最新检测结果重绘当前预览帧（只叠加框/关键点，不再叠加姿态与置信度文字） */
    if (!lastPreview_.isNull()) {
        previewLabel_->setPixmap(QPixmap::fromImage(
            drawOverlay(lastPreview_, r)).scaled(
            previewLabel_->size() * devicePixelRatioF(),
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    /* 输出结果经批处理滤波后显示，避免"每次检测都刷新"带来的高频抖动 */
    updateFilteredResult(r.has_person);
}

/* 输出结果"滚动 + 去抖"滤波：维护最近 kResultWindow(30) 个算法输出，每新来
 * kSlideStep(10) 个就滑动判定一次（丢最老、接最新，窗口持续重叠）；判定规则仍为
 * "三类不良坐姿(1托腮/2低头/3高低肩)中计数最多且 >1 次"，否则 unknown。配合去抖：
 * 判定结果发生变化后保持 kDebounce(1) 步，避免阈值边沿来回翻转。 */
void MainWindow::resetFilterBatch() {
    window_.clear();
    batchN_        = 0;
    holdRemain_    = 0;
    noPersonOngoing_ = false;   /* 一起复位无人计时状态 */
    batchTimer_.restart();   /* 开始计时一个新的滑动周期（调试用） */
}

void MainWindow::updateFilteredResult(bool hasPerson) {
    if (!hasPerson) {
        /* 镜头无人：需持续 kNoPersonClearMs(1s) 后才清空滚动窗口与状态；
         * 期间保留上次显示结果，避免瞬时丢到人造成误清。 */
        if (!noPersonOngoing_) {
            noPersonOngoing_ = true;
            noPersonTimer_.start();
        }
        if (noPersonTimer_.elapsed() < kNoPersonClearMs) return;

        noPersonOngoing_ = false;
        resetFilterBatch();
        if (resultDetect_) setDetectLabel(resultDetect_, false);
        if (resultPose_)   setPosturePill(resultPose_, 0);
        if (resultConf_)   resultConf_->setText(QStringLiteral("NONE"));
        shownPose_ = 0;
        return;
    }

    /* 有人：取消无人计时 */
    noPersonOngoing_ = false;

    setDetectLabel(resultDetect_, true);

    /* 滚动入窗：始终保留最近 kResultWindow 个算法输出 */
    window_.emplace_back(lastResult_.pose, lastResult_.confidence);
    if (static_cast<int>(window_.size()) > kResultWindow) window_.pop_front();

    /* 每累计 kSlideStep 个新输出，滑动判定一次 */
    if (++batchN_ < kSlideStep) return;
    batchN_ = 0;
    batchTimer_.restart();

    /* 统计窗口内三类不良坐姿出现次数 */
    int cnt[4] = {0, 0, 0, 0};
    for (const auto& [pose, conf] : window_)
        if (pose >= 1 && pose <= 3) ++cnt[pose];

    /* 去抖：若判定结果刚变化过，本步保持上次显示不变 */
    if (holdRemain_ > 0) {
        --holdRemain_;
    } else {
        int best = 0, bestN = 0;
        for (int p = 1; p <= 3; ++p)
            if (cnt[p] > bestN) { bestN = cnt[p]; best = p; }
        const int cand = (best != 0 && bestN > 1) ? best : 0;   /* 计数 >1 → 有效 */

        if (cand != shownPose_) {
            shownPose_  = cand;
            holdRemain_ = kDebounce;   /* 变更新结果后保持 1 步，防翻转 */
            if (cand == 0) {
                if (resultPose_) setPosturePill(resultPose_, 0);
                if (resultConf_) resultConf_->setText(QStringLiteral("--"));
            } else {
                float sum = 0.f;
                for (const auto& [pose, conf] : window_) if (pose == cand) sum += conf;
                if (resultPose_)   setPosturePill(resultPose_, cand);
                if (resultConf_)   resultConf_->setText(
                    QStringLiteral("%1").arg(sum / bestN, 0, 'f', 3));
            }
        }
    }
}

/* 在 GUI 线程把检测框与关键点叠加到预览帧上（坐标已是原始帧空间）。
 * 仅更新预览，不阻塞推理线程。 */
QImage MainWindow::drawOverlay(const QImage& base, const PostureFrameResult& r) {
    QImage out = base.copy();
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, false);

    QPen boxPen(QColor(0, 255, 0), 2);
    p.setPen(boxPen);
    for (const PostureBox& b : r.boxes) {
        p.drawRect(b.x1, b.y1, b.x2 - b.x1, b.y2 - b.y1);
    }

    QPen kpPen(QColor(255, 0, 0), 1);
    p.setPen(kpPen);
    p.setBrush(QColor(255, 0, 0));
    for (const auto& kps : r.keypoints) {
        for (const PostureKeyPoint& k : kps) {
            p.drawEllipse(QPointF(k.x, k.y), 3.0, 3.0);
        }
    }

    /* 在检测框左上角用"小字"叠加当前显示结果：姿态 + 置信度（与右侧 Live Result 同步） */
    static const char* poseText[4] = {"unknown", "Cheek Support",
                                      "Head Bowing", "Shoulder Imbalance"};
    const int shown = (shownPose_ >= 0 && shownPose_ < 4) ? shownPose_ : 0;
    const QString confTxt = (resultConf_ && resultConf_->text() != QLatin1String("NONE"))
                                ? resultConf_->text() : QStringLiteral("unknown");
    QFont tagFont = p.font();
    tagFont.setPixelSize(13);
    tagFont.setBold(false);
    p.setFont(tagFont);
    const QFontMetrics fm(tagFont);
    const QString tag = QStringLiteral("%1 %2")
                            .arg(QLatin1String(poseText[shown]), confTxt);
    const int tw = fm.horizontalAdvance(tag);
    const int th = fm.height();
    for (const PostureBox& b : r.boxes) {
        QRect tagR(b.x1, b.y1 - th - 6, tw + 8, th + 6);
        if (tagR.top() < 0) tagR.moveTop(b.y1 + 4);   /* 贴画布顶则移到框内下方，避免出界 */
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(tagR, 3, 3);
        p.setPen(Qt::white);
        p.setBrush(Qt::NoBrush);
        p.drawText(tagR, Qt::AlignCenter, tag);
    }
    p.end();
    return out;
}

/* 打开"高级设置"对话框。只有点击"确认"才把新参数下发给算法线程；
 * 取消/关闭/返回则丢弃输入、不生效。 */
void MainWindow::onOpenSettings() {
    if (!detector_) return;
    AlgorithmConfig current  = *detector_->config();
    AlgorithmConfig updated;
    if (AdvancedSettingsDialog::editConfig(this, current, updated)) {
        detector_->setConfig(std::make_shared<AlgorithmConfig>(updated));
        statusBar()->showMessage(QStringLiteral("Settings applied"), 4000);
    }
}

/* 周期刷新界面统计：预览 fps、进程内存占用(/proc/self/statm)与 CPU 占用率。
 * CPU% 通过差分两次 /proc/self/stat 的 utime+stime，再除以本次耗时换算。 */
void MainWindow::onUiTimer() {
    const int intervalMs = uiTimer_ ? uiTimer_->interval() : 500;
    const int fps = static_cast<int>(uiFrameCount_ * (1000.0 / intervalMs));
    uiFrameCount_ = 0;

    long total = 0, resident = 0;
    std::ifstream f("/proc/self/statm");
    if (f) f >> total >> resident;
    const double memMB = resident * 4.0 / 1024.0;   /* resident 页数 * 4KB(页) / 1024 */

    if (fpsLabel_) fpsLabel_->setText(QStringLiteral("%1 fps").arg(fps));
    statsLabel_->setText(QStringLiteral("Memory: %1 MB")
                             .arg(memMB, 0, 'f', 1));

    /* --- CPU 占用率（进程级），差分两次 utime+stime / 经过的时钟数 --- */
    long utime = -1, stime = -1;
    {
        std::ifstream s("/proc/self/stat");
        std::string line;
        if (s && std::getline(s, line)) {
            const auto closeParen = line.rfind(')');
            if (closeParen != std::string::npos && closeParen + 1 < line.size()) {
                std::istringstream rest(line.substr(closeParen + 1));
                char state;                       /* field 3: 进程状态字符 (R/S/D...) */
                rest >> state;
                long v;
                for (int i = 4; i <= 13; ++i) rest >> v;   /* 跳过 field 4..13 */
                rest >> utime >> stime;                    /* field 14=utime, 15=stime */
            }
        }
    }
    /* 系统总 CPU 时间(/proc/stat 首行 cpu 的 user+nice+system+idle+iowait+irq+softirq+steal) */
    long sysTotal = -1;
    {
        std::ifstream s("/proc/stat");
        std::string tag, line;
        if (s && (s >> tag) && tag == "cpu" && std::getline(s, line)) {
            std::istringstream rest(line);
            long v, sum = 0;
            for (int i = 0; i < 8 && (rest >> v); ++i) sum += v;
            sysTotal = sum;
        }
    }

    if (utime >= 0 && stime >= 0 && sysTotal >= 0 && cpuPrevUtime_ >= 0) {
        const long nowCpu = utime + stime;
        const long prevCpu = cpuPrevUtime_ + cpuPrevStime_;
        const long dProc = nowCpu - prevCpu;
        const long dSys = sysTotal - cpuPrevSysTotal_;
        /* 多核总占用率：进程消耗的 CPU 时间 / 系统所有核经过的 CPU 时间 */
        const double pct = (dSys > 0) ? (100.0 * dProc / static_cast<double>(dSys)) : 0.0;
        const double c = pct < 0 ? 0.0 : (pct > 100 ? 100.0 : pct);
        if (cpuLabel_) cpuLabel_->setText(QStringLiteral("CPU: %1 %").arg(c, 0, 'f', 1));
    }
    if (utime >= 0) cpuPrevUtime_ = utime;
    if (stime >= 0) cpuPrevStime_ = stime;
    if (sysTotal >= 0) cpuPrevSysTotal_ = sysTotal;
}

void MainWindow::onDetectorStatus(bool ok, const QString& info) {
    Q_UNUSED(info);
    if (!ok) statusBar()->showMessage(QStringLiteral("Algorithm init failed; camera preview only"), 6000);
}

void MainWindow::onCameraError(const QString& msg) {
    statusBar()->showMessage(msg);
}

/* 下拉切换摄像头：编程填充导致的变动（isSwitchingCameraCombo_ 真）一律忽略；
 * 用户手动选择则热切换到对应设备号。 */
void MainWindow::onCameraChanged(int index) {
    if (isSwitchingCameraCombo_) return;
    const QVariant data = cameraCombo_->itemData(index);
    if (!data.isValid()) return;
    const int idx = data.toInt();
    if (idx < 0 || idx == activeCamera_) return;
    applyCamera(idx);
}

/* 切换到指定设备号：停旧采集线程 -> 换号 -> 重启（复用同一 CameraWorker） */
void MainWindow::applyCamera(int deviceIndex) {
    if (!camera_) return;
    if (camera_->deviceIndex() == deviceIndex) return;

    camera_->stop();                     /* join 旧线程，避免新旧设备并发 */
    camera_->setDeviceIndex(deviceIndex);
    activeCamera_ = deviceIndex;

    /* 切换到新设备后清空旧设备的检测叠加，避免残留标注误导 */
    lastResult_ = PostureFrameResult{};
    hasResult_  = false;
    resetFilterBatch();
    shownPose_  = 0;
    setResultStandby(resultDetect_, resultPose_, resultConf_);

    camera_->start();
    statusBar()->showMessage(QStringLiteral("Camera switched to /dev/video%1").arg(deviceIndex), 4000);
}