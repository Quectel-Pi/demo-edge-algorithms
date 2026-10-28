#include "main.h"

#include <QApplication>
#include <QMetaType>
#include <QStyleFactory>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "camera.h"
#include "main_window.h"
#include "posture_detector.h"

namespace appconfig {
const QString kDefaultModel = QStringLiteral(
    "/PeoplePosture_Demo/PeoplePostureDet_RK3576_Ubuntu/models/"
    "RK3576_Ubuntu_peoplekpt.engine");
const int kDefaultModule = 3;  /* RK3576 */

QString readEnv(const char* name, const QString& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? QString::fromLocal8Bit(v) : fallback;
}
}  // namespace appconfig

int main(int argc, char** argv) {
    /* 注册跨线程(queued)连接所需的元类型 */
    qRegisterMetaType<std::shared_ptr<cv::Mat>>("std::shared_ptr<cv::Mat>");
    qRegisterMetaType<PostureFrameResult>("PostureFrameResult");

    QApplication app(argc, argv);

    /* 强制 Fusion 风格，避免 Debian/eglfs 下系统风格(GTK)额外绘制控件焦点虚框 */
    app.setStyle(QStyleFactory::create(QLatin1String("Fusion")));

    /* 配置来源：环境变量（run.sh 注入）> 默认值；命令行参数可覆盖设备号 */
    const QString model   = appconfig::readEnv("POSTURE_MODEL",   appconfig::kDefaultModel);
    const QString license = appconfig::readEnv("PPD_LICENSE_FILE", QString());
    const QString auth    = appconfig::readEnv("PEOPLEKPT_AUTH_CODE", QString());
    const int     module  = appconfig::kDefaultModule;
    /* 设备号：默认 -1 = 自动检索第一个可用摄像头；可被 CAMERA_DEVICE 或 --device 覆盖 */
    int device = appconfig::readEnv("CAMERA_DEVICE", QStringLiteral("-1")).toInt();

    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--device") == 0)
            device = std::atoi(argv[i + 1]);
    }

    MainWindow win;
    win.setAlgorithmConfig(model, license, auth, module);
    win.startPipe(device);
    win.show();

    return app.exec();
}