#include "settings_dialog.h"

#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {

/* 严格解析 [0,1] 内的浮点 */
bool parseRangeFloat(const QString& s, float& out, float lo, float hi) {
    bool ok = false;
    const double v = s.toDouble(&ok);
    if (!ok || v < lo || v > hi) return false;
    out = static_cast<float>(v);
    return true;
}

/* 严格解析 1~60 整数 */
bool parseRangeInt(const QString& s, int& out, int lo, int hi) {
    bool ok = false;
    const int v = s.toInt(&ok);
    if (!ok || v < lo || v > hi) return false;
    out = v;
    return true;
}

/* true/false/1/0 均可接受 */
QString boolToText(bool b) { return b ? QStringLiteral("true") : QStringLiteral("false"); }
bool parseBool(const QString& s, bool& out) {
    const QString t = s.trimmed().toLower();
    if (t == QStringLiteral("true") || t == QStringLiteral("1")) { out = true; return true; }
    if (t == QStringLiteral("false") || t == QStringLiteral("0")) { out = false; return true; }
    return false;
}

}  // namespace

bool AdvancedSettingsDialog::editConfig(QWidget* parent,
                                        const AlgorithmConfig& current,
                                        AlgorithmConfig& out) {
    AdvancedSettingsDialog dlg(current, parent);
    if (dlg.exec() != QDialog::Accepted) return false;   /* 取消/关闭/返回：不应用 */
    return dlg.collect(out);                              /* 已确认，解析并回填 */
}

AdvancedSettingsDialog::AdvancedSettingsDialog(const AlgorithmConfig& current, QWidget* parent)
    : QDialog(parent), current_(current) {
    setWindowTitle(QStringLiteral("Advanced Settings"));
    setModal(true);
    resize(480, 280);

    auto* grid = new QGridLayout;

    /* 0. 检测置信度阈值 */
    {
        auto* label = new QLabel(QStringLiteral("Detection confidence threshold"), this);
        detThEdit_ = new QLineEdit(QString::number(current_.detConfTh, 'f', 2));
        auto* hint = new QLabel(QStringLiteral("(default 0.5)"), this);
        hint->setStyleSheet("color:#888;");
        detThEdit_->setValidator(new QDoubleValidator(0.0, 1.0, 3, detThEdit_));
        detThEdit_->setMinimumWidth(140);
        label->setBuddy(detThEdit_);
        grid->addWidget(label, 0, 0);
        grid->addWidget(detThEdit_, 0, 1);
        grid->addWidget(hint, 0, 2);
    }
    /* 1. 关键点置信度阈值 */
    {
        auto* label = new QLabel(QStringLiteral("Keypoint confidence threshold"), this);
        kptThEdit_ = new QLineEdit(QString::number(current_.kptConfTh, 'f', 2));
        auto* hint = new QLabel(QStringLiteral("(default 0.3)"), this);
        hint->setStyleSheet("color:#888;");
        kptThEdit_->setValidator(new QDoubleValidator(0.0, 1.0, 3, kptThEdit_));
        kptThEdit_->setMinimumWidth(140);
        label->setBuddy(kptThEdit_);
        grid->addWidget(label, 1, 0);
        grid->addWidget(kptThEdit_, 1, 1);
        grid->addWidget(hint, 1, 2);
    }
    /* 2. 单目标/多目标 */
    {
        auto* label = new QLabel(QStringLiteral("Single target / Multiple targets"), this);
        onlyMaxEdit_ = new QLineEdit(boolToText(current_.onlyMaxBox));
        auto* hint = new QLabel(QStringLiteral("(default true), accepts true/false/1/0"), this);
        hint->setStyleSheet("color:#888;");
        onlyMaxEdit_->setMinimumWidth(140);
        label->setBuddy(onlyMaxEdit_);
        grid->addWidget(label, 2, 0);
        grid->addWidget(onlyMaxEdit_, 2, 1);
        grid->addWidget(hint, 2, 2);
    }
    /* 3. 推理限频 fps */
    {
        auto* label = new QLabel(QStringLiteral("Inference rate limit (fps)"), this);
        inferFpsEdit_ = new QLineEdit(QString::number(current_.inferFps));
        auto* hint = new QLabel(QStringLiteral("(default 15), 1~60"), this);
        hint->setStyleSheet("color:#888;");
        inferFpsEdit_->setValidator(new QIntValidator(1, 60, inferFpsEdit_));
        inferFpsEdit_->setMinimumWidth(140);
        label->setBuddy(inferFpsEdit_);
        grid->addWidget(label, 3, 0);
        grid->addWidget(inferFpsEdit_, 3, 1);
        grid->addWidget(hint, 3, 2);
    }

    auto* restoreBtn = new QPushButton(QStringLiteral("Restore Defaults"), this);
    connect(restoreBtn, &QPushButton::clicked, this,
            &AdvancedSettingsDialog::onRestoreDefaults);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &AdvancedSettingsDialog::onConfirmed);
    connect(buttons, &QDialogButtonBox::rejected, this, &AdvancedSettingsDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(grid);
    layout->addWidget(restoreBtn, /*stretch=*/0, /*alignment=*/Qt::AlignLeft);
    layout->addWidget(buttons);
}

void AdvancedSettingsDialog::onRestoreDefaults() {
    const AlgorithmConfig d = AlgorithmConfig::defaults();
    detThEdit_->setText(QString::number(d.detConfTh, 'f', 2));
    kptThEdit_->setText(QString::number(d.kptConfTh, 'f', 2));
    onlyMaxEdit_->setText(boolToText(d.onlyMaxBox));
    inferFpsEdit_->setText(QString::number(d.inferFps));
}

void AdvancedSettingsDialog::onConfirmed() {
    AlgorithmConfig out;
    if (collect(out)) accept();      /* 合法才关闭；否则停留并提示 */
}

bool AdvancedSettingsDialog::collect(AlgorithmConfig& out) {
    float det = 0.f, kpt = 0.f;
    int fps = 0;
    bool onlyMax = false;

    if (!parseRangeFloat(detThEdit_->text(), det, 0.0f, 1.0f)) {
        QMessageBox::warning(this, QStringLiteral("Invalid input"),
                             QStringLiteral("Detection confidence threshold must be between 0 and 1"));
        return false;
    }
    if (!parseRangeFloat(kptThEdit_->text(), kpt, 0.0f, 1.0f)) {
        QMessageBox::warning(this, QStringLiteral("Invalid input"),
                             QStringLiteral("Keypoint confidence threshold must be between 0 and 1"));
        return false;
    }
    if (!parseBool(onlyMaxEdit_->text(), onlyMax)) {
        QMessageBox::warning(this, QStringLiteral("Invalid input"),
                             QStringLiteral("Single target must be true/false/1/0"));
        return false;
    }
    if (!parseRangeInt(inferFpsEdit_->text(), fps, 1, 60)) {
        QMessageBox::warning(this, QStringLiteral("Invalid input"),
                             QStringLiteral("Inference rate limit must be an integer between 1 and 60"));
        return false;
    }

    out = AlgorithmConfig::defaults();
    out.detConfTh  = det;
    out.kptConfTh  = kpt;
    out.onlyMaxBox = onlyMax;
    out.inferFps   = fps;
    return true;
}