#pragma once
/* settings_dialog.h — 高级设置对话框（参数修改页面）
 *
 * 仅依赖 AlgorithmConfig（纯工程层结构），不引入任何算法 SDK 类型。
 * 使用方式：主窗口直接调用静态 editConfig()，确认返回 true 并携带新配置；
 * 取消/关闭/返回则返回 false（不应用任何修改）。
 */
#include <QDialog>

#include "posture_detector.h"   /* AlgorithmConfig */

class QLineEdit;

class AdvancedSettingsDialog : public QDialog {
    Q_OBJECT
public:
    /* current：当前生效的配置（作为输入框初值，并标注默认值后缀）。
     * editConfig：模态编辑当前配置；确认 -> out 填为新配置并返回 true；取消/关闭 -> false。 */
    static bool editConfig(QWidget* parent, const AlgorithmConfig& current,
                           AlgorithmConfig& out);

private slots:
    void onRestoreDefaults();
    void onConfirmed();

private:
    explicit AdvancedSettingsDialog(const AlgorithmConfig& current, QWidget* parent = nullptr);

    /* 从各输入框解析并校验；合法则填 out 返回 true，非法弹窗提示并返回 false */
    bool collect(AlgorithmConfig& out);

    QLineEdit* detThEdit_ = nullptr;
    QLineEdit* kptThEdit_ = nullptr;
    QLineEdit* onlyMaxEdit_ = nullptr;
    QLineEdit* inferFpsEdit_ = nullptr;
    AlgorithmConfig current_;
};