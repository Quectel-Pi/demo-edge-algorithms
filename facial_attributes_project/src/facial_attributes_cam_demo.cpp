#include "facial_attributes_detection.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStorageInfo>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>
#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char* emotionNames[] = {"Neutral", "Happy", "Sad", "Surprise", "Fear", "Disgust", "Anger"};
static volatile std::sig_atomic_t interrupted = 0;
static void onSignal(int) { interrupted = 1; }

enum class Source { Idle, Image, Camera };
struct Command {
    Source source = Source::Image;
    std::string path = "images/input.jpg";
    float confidence = 0.25f;
    float iou = 0.20f;
    unsigned long revision = 1;
};
struct Frame {
    QImage image;
    std::vector<facial_info::FaceContext> faces;
    QString source;
    QString message = "Initializing SDK";
    double milliseconds = 0;
    double fps = 0;
    int auth = -1;
    bool ready = false;
    bool failed = false;
    unsigned long revision = 0;
    unsigned long sequence = 0;
};
struct Shared {
    std::mutex mutex;
    std::condition_variable wake;
    Command command;
    Frame frame;
    bool stop = false;
};

static QString emotionName(const facial_info::FaceContext& face) {
    const int index = static_cast<int>(face.emotion.emotion_type);
    return index >= 0 && index < 7 ? emotionNames[index] : "Unknown";
}
static QString genderName(const facial_info::FaceContext& face) {
    if (face.age_gender.gender_type == facial_info::GenderType::Female) return "Female";
    if (face.age_gender.gender_type == facial_info::GenderType::Male) return "Male";
    return "Unknown";
}
static QImage toImage(const cv::Mat& bgr) {
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}
static std::string findCamera() {
    std::vector<std::string> devices;
    if (DIR* directory = opendir("/dev")) {
        while (dirent* entry = readdir(directory)) {
            const std::string name(entry->d_name);
            if (name.size() > 5 && name.compare(0, 5, "video") == 0 &&
                std::all_of(name.begin() + 5, name.end(), [](unsigned char value) { return value >= '0' && value <= '9'; }))
                devices.push_back("/dev/" + name);
        }
        closedir(directory);
    }
    std::sort(devices.begin(), devices.end(), [](const std::string& first, const std::string& second) {
        return std::stoi(first.substr(10)) < std::stoi(second.substr(10));
    });
    for (const auto& device : devices) {
        const int descriptor = open(device.c_str(), O_RDWR | O_NONBLOCK);
        if (descriptor < 0) continue;
        v4l2_capability capability{};
        const bool queried = ioctl(descriptor, VIDIOC_QUERYCAP, &capability) == 0;
        close(descriptor);
        const unsigned capabilities = capability.capabilities & V4L2_CAP_DEVICE_CAPS ? capability.device_caps : capability.capabilities;
        if (queried && (capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
            std::strstr(reinterpret_cast<const char*>(capability.bus_info), "usb")) return device;
    }
    return {};
}

static void publish(Shared& shared, Frame frame) {
    std::lock_guard<std::mutex> lock(shared.mutex);
    frame.sequence = shared.frame.sequence + 1;
    shared.frame = std::move(frame);
}

static void worker(Shared& shared, const std::string& camera) {
    FacialAttributesDetection sdk;
    Frame frame;
    try {
        std::string detector = "models/RK3576_Ubuntu_widerface_last.engine";
        std::string classifier = "models/RK3576_Ubuntu_FER_best.engine";
        std::string license = std::getenv("FAD_LICENSE_FILE") ? std::getenv("FAD_LICENSE_FILE") : "license/fad_license.txt";
        std::string auth = std::getenv("FAD_AUTH_CODE") ? std::getenv("FAD_AUTH_CODE") : "";
        QDir().mkpath(QFileInfo(QString::fromStdString(license)).absolutePath());
        const int result = sdk.init(detector.data(), classifier.data(), license.data(), 3, auth.data());
        frame.auth = sdk.get_status(1);
        frame.ready = result == FACIAL_PROCESS_OK && sdk.get_status(0) == 0;
        frame.failed = !frame.ready;
        frame.message = frame.ready ? "Ready" : QString("SDK init failed: %1 / auth %2\nFAD_AUTH_CODE required; see README.zh.md").arg(result).arg(frame.auth);
        std::printf("[init] rc=%d auth=%d ready=%d\n", result, frame.auth, frame.ready);
        publish(shared, frame);
        if (!frame.ready) return;

        cv::VideoCapture capture;
        unsigned long activeRevision = 0;
        Source activeSource = Source::Idle;
        while (true) {
            Command command;
            {
                std::unique_lock<std::mutex> lock(shared.mutex);
                shared.wake.wait(lock, [&] { return shared.stop || shared.command.revision != activeRevision || activeSource == Source::Camera; });
                if (shared.stop) break;
                command = shared.command;
            }
            const auto cycleStart = std::chrono::steady_clock::now();
            const bool changed = command.revision != activeRevision;
            if (changed) {
                if (command.source != activeSource) capture.release();
                activeRevision = command.revision;
                activeSource = command.source;
                float confidence = command.confidence, iou = command.iou, detectionFilter = 0, classificationFilter = 0;
                sdk.set_param(confidence, iou, detectionFilter, classificationFilter);
            }
            frame.revision = activeRevision;
            frame.source = activeSource == Source::Camera ? QString::fromStdString(camera) : QString::fromStdString(command.path);
            frame.image = QImage();
            frame.faces.clear();
            frame.failed = false;
            frame.milliseconds = frame.fps = 0;
            if (activeSource == Source::Idle) {
                frame.message = "Stopped";
                publish(shared, frame);
                continue;
            }
            cv::Mat bgr;
            if (activeSource == Source::Image) {
                bgr = cv::imread(command.path, cv::IMREAD_COLOR);
                if (bgr.empty()) frame.message = "Cannot read image: " + frame.source;
            } else {
                if (!capture.isOpened()) {
                    const std::string device = camera == "auto" ? findCamera() : camera;
                    bool opened = false;
                    if (!device.empty()) {
                        const bool numeric = std::all_of(device.begin(), device.end(), [](unsigned char value) { return value >= '0' && value <= '9'; });
                        opened = numeric ? capture.open(std::stoi(device), cv::CAP_V4L2) : capture.open(device, cv::CAP_V4L2);
                    }
                    if (opened) {
                        capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
                        capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
                        capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
                        capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
                        frame.source = QString::fromStdString(device);
                    }
                }
                if (!capture.isOpened() || !capture.read(bgr) || bgr.empty()) {
                    frame.message = "Camera unavailable / disconnected";
                    capture.release();
                    activeSource = Source::Idle;
                }
            }
            if (bgr.empty()) {
                frame.failed = true;
                publish(shared, frame);
                continue;
            }
            if (!bgr.isContinuous()) bgr = bgr.clone();
            const auto inferenceStart = std::chrono::steady_clock::now();
            const int result = sdk.inference(bgr.data, bgr.rows, bgr.cols, bgr.channels(), frame.faces);
            frame.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inferenceStart).count();
            frame.auth = sdk.get_status(1);
            if (result != FACIAL_PROCESS_OK) {
                frame.faces.clear();
                frame.failed = true;
                frame.message = QString("Inference failed: %1 / auth %2").arg(result).arg(frame.auth);
                activeSource = Source::Idle;
            } else {
                frame.faces.erase(std::remove_if(frame.faces.begin(), frame.faces.end(), [](const facial_info::FaceContext& face) {
                    return !face.box || !std::isfinite(face.box->x) || !std::isfinite(face.box->y) ||
                        !std::isfinite(face.box->width) || !std::isfinite(face.box->height) || face.box->width <= 0 || face.box->height <= 0;
                }), frame.faces.end());
                frame.message = activeSource == Source::Camera ? "Live" : "Image complete";
            }
            frame.image = toImage(bgr);
            frame.fps = 1.0 / std::chrono::duration<double>(std::chrono::steady_clock::now() - cycleStart).count();
            if (activeSource != Source::Camera)
                std::printf("[frame] faces=%zu inference_ms=%.2f auth=%d\n", frame.faces.size(), frame.milliseconds, frame.auth);
            publish(shared, frame);
        }
        sdk.uinit();
    } catch (const std::exception& error) {
        frame.image = QImage();
        frame.faces.clear();
        frame.failed = true;
        frame.message = QString("SDK / capture error: %1").arg(error.what());
        publish(shared, frame);
    }
}

static QImage annotatedImage(const Frame& frame, bool attributes) {
    QImage image = frame.image.copy();
    if (image.isNull()) return image;
    QPainter painter(&image);
    painter.setPen(QPen(QColor("#35ec86"), 2));
    QFont font("DejaVu Sans", 11);
    font.setBold(true);
    painter.setFont(font);
    int number = 0;
    for (const auto& face : frame.faces) {
        ++number;
        const auto& box = *face.box;
        const QRect bounds = QRectF(box.x, box.y, box.width, box.height).toAlignedRect().intersected(image.rect());
        if (bounds.isEmpty()) continue;
        painter.drawRect(bounds.adjusted(0, 0, -1, -1));
        QString text = QString("#%1  %2").arg(number).arg(box.score, 0, 'f', 2);
        if (attributes) text += QString("  %1 | %2 | Age %3").arg(emotionName(face), genderName(face)).arg(face.age_gender.age);
        const QFontMetrics metrics(font);
        text = metrics.elidedText(text, Qt::ElideRight, std::max(1, image.width() - 8));
        const int width = metrics.horizontalAdvance(text) + 8;
        const int height = metrics.height() + 6;
        const int left = std::clamp(bounds.left(), 0, std::max(0, image.width() - width));
        const int top = std::clamp(bounds.top() - height, 0, std::max(0, image.height() - height));
        painter.fillRect(QRect(left, top, width, height), QColor(12, 24, 24, 215));
        painter.drawText(left + 4, top + metrics.ascent() + 3, text);
    }
    return image;
}

static QString saveSnapshot(const Frame& frame) {
    if (frame.image.isNull() || frame.failed) return {};
    if (!QDir().mkpath("outputs")) return {};
    const QString base = "outputs/" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + "_" + QString::number(frame.sequence);
    const QString imagePath = base + ".jpg";
    if (!annotatedImage(frame, true).save(imagePath, "JPG", 95)) return {};
    QJsonArray faces;
    for (const auto& face : frame.faces) {
        QJsonArray emotionScores, genderScores;
        for (int index = 0; index < 7; ++index) emotionScores.append(face.emotion.scores[index]);
        for (float score : face.age_gender.gender_scores) genderScores.append(score);
        const auto& box = *face.box;
        faces.append(QJsonObject{{"box", QJsonObject{{"x", box.x}, {"y", box.y}, {"width", box.width}, {"height", box.height}, {"score", box.score}}},
            {"emotion", emotionName(face)}, {"emotion_scores", emotionScores}, {"gender", genderName(face)},
            {"gender_scores", genderScores}, {"age_estimate", face.age_gender.age}});
    }
    QJsonObject record{{"schema_version", 1}, {"source", frame.source}, {"captured_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"width", frame.image.width()}, {"height", frame.image.height()}, {"inference_ms", frame.milliseconds},
        {"auth_status", frame.auth}, {"faces", faces}, {"image", QFileInfo(imagePath).fileName()}};
    QSaveFile file(base + ".json");
    const QByteArray bytes = QJsonDocument(record).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        QFile::remove(imagePath);
        return {};
    }
    std::printf("[export] %s.json\n", base.toUtf8().constData());
    return base;
}

static QLabel* label(const QString& text, QVBoxLayout* layout) {
    auto* widget = new QLabel(text);
    widget->setWordWrap(true);
    widget->setTextFormat(Qt::PlainText);
    layout->addWidget(widget);
    return widget;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setFont(QFont("DejaVu Sans", 10));
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    Shared shared;
    std::string camera = "auto";
    bool smoke = false;
    int quitAfter = 0;
    QString screenshot;
    for (int index = 1; index < argc; ++index) {
        const QString argument = argv[index];
        if (argument == "--image" && index + 1 < argc) shared.command.path = argv[++index];
        else if (argument == "--camera" && index + 1 < argc) { camera = argv[++index]; shared.command.source = Source::Camera; }
        else if (argument == "--smoke-test") smoke = true;
        else if (argument == "--quit-after" && index + 1 < argc) quitAfter = QString(argv[++index]).toInt();
        else if (argument == "--screenshot" && index + 1 < argc) screenshot = argv[++index];
        else {
            std::fprintf(stderr, "Usage: %s [--image file | --camera auto|/dev/videoN|N] [--smoke-test] [--screenshot file.png] [--quit-after milliseconds]\n", argv[0]);
            return 2;
        }
    }
    QWidget window;
    window.setWindowTitle("Facial Attributes - Quectel Pi M2");
    window.setStyleSheet(
        "QWidget{background:#1e1e2a;color:#eeeef5;} QLabel{background:transparent;}"
        "QFrame#sidebar,QFrame#details{background:#262636;}"
        "QPushButton{background:#3a3a4e;border:1px solid #55556b;border-radius:6px;padding:9px;}"
        "QPushButton:hover{background:#4a4a63;} QPushButton:checked{background:#28764a;}"
        "QPushButton:disabled{color:#777784;} QComboBox,QDoubleSpinBox{background:#353547;padding:5px;}"
        "QProgressBar{border:0;background:#15151f;height:12px;text-align:center;}"
        "QProgressBar::chunk{background:#299b77;} QScrollArea{border:0;}");
    auto* root = new QHBoxLayout(&window);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);
    auto* sidebar = new QFrame;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(185);
    auto* left = new QVBoxLayout(sidebar);
    auto* brand = label("AI Playground", left);
    brand->setStyleSheet("font-size:17px;font-weight:bold;");
    label("Quectel Pi M2\nRK3576 / Debian", left);
    label("Facial Attributes", left)->setStyleSheet("color:#59d69e;font-weight:bold;padding:10px 0;");
    left->addSpacing(16);
    label("Detection confidence", left);
    auto* confidence = new QDoubleSpinBox;
    confidence->setRange(0.05, 0.5);
    confidence->setSingleStep(0.05);
    confidence->setValue(0.25);
    left->addWidget(confidence);
    label("NMS IoU", left);
    auto* iou = new QDoubleSpinBox;
    iou->setRange(0.05, 0.9);
    iou->setSingleStep(0.05);
    iou->setValue(0.20);
    left->addWidget(iou);
    auto* overlays = new QCheckBox("Attribute labels");
    overlays->setChecked(true);
    left->addWidget(overlays);
    left->addStretch();
    auto* authorization = label("Authorization: pending", left);
    auto* deviceStatus = label("", left);
    root->addWidget(sidebar);

    auto* center = new QVBoxLayout;
    label("Facial Attributes", center)->setStyleSheet("font-size:22px;font-weight:bold;");
    auto* metrics = label("Faces: --   Inference: --   FPS: --", center);
    auto* view = new QLabel;
    view->setAlignment(Qt::AlignCenter);
    view->setMinimumSize(240, 200);
    view->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    view->setStyleSheet("background:#15151f;");
    center->addWidget(view, 1);
    auto* status = label("Initializing SDK", center);
    status->setMinimumHeight(38);
    auto* buttons = new QGridLayout;
    auto makeButton = [&](const QString& text, QStyle::StandardPixmap icon, int row, int column) {
        auto* button = new QPushButton(window.style()->standardIcon(icon), text);
        button->setToolTip(text);
        buttons->addWidget(button, row, column);
        return button;
    };
    auto* cameraButton = makeButton("Open Camera", QStyle::SP_MediaPlay, 0, 0);
    const bool cameraInitiallyActive = shared.command.source == Source::Camera;
    cameraButton->setCheckable(true);
    cameraButton->setChecked(cameraInitiallyActive);
    cameraButton->setText(cameraInitiallyActive ? "Close Camera" : "Open Camera");
    auto* imageButton = makeButton("Local Image", QStyle::SP_DirOpenIcon, 0, 1);
    auto* saveButton = makeButton("Save Result", QStyle::SP_DialogSaveButton, 0, 2);
    auto* stopButton = makeButton("Stop", QStyle::SP_MediaStop, 1, 0);
    auto* fullButton = makeButton("Full Screen", QStyle::SP_TitleBarMaxButton, 1, 1);
    auto* quitButton = makeButton("Quit", QStyle::SP_DialogCloseButton, 1, 2);
    center->addLayout(buttons);
    root->addLayout(center, 1);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFixedWidth(245);
    auto* details = new QFrame;
    details->setObjectName("details");
    scroll->setWidget(details);
    auto* right = new QVBoxLayout(details);
    label("Face Attributes", right)->setStyleSheet("font-size:17px;font-weight:bold;");
    auto* selection = new QComboBox;
    right->addWidget(selection);
    auto* faceSummary = label("No face", right);
    label("Emotion scores", right);
    QProgressBar* emotionBars[7];
    for (int index = 0; index < 7; ++index) {
        label(emotionNames[index], right);
        emotionBars[index] = new QProgressBar;
        emotionBars[index]->setRange(0, 100);
        emotionBars[index]->setFixedHeight(15);
        right->addWidget(emotionBars[index]);
    }
    label("Gender scores", right);
    QProgressBar* genderBars[2];
    for (int index = 0; index < 2; ++index) {
        label(index == 0 ? "Female" : "Male", right);
        genderBars[index] = new QProgressBar;
        genderBars[index]->setRange(0, 100);
        genderBars[index]->setFixedHeight(15);
        right->addWidget(genderBars[index]);
    }
    right->addStretch();
    root->addWidget(scroll);
    window.resize(1180, 760);
    window.setMinimumSize(860, 600);

    Frame displayed;
    unsigned long lastSequence = 0;
    QString exportMessage;
    auto updateDetails = [&] {
        const int selected = selection->currentIndex();
        const facial_info::FaceContext* face = selected >= 0 && selected < static_cast<int>(displayed.faces.size()) ? &displayed.faces[selected] : nullptr;
        faceSummary->setText(face ? QString("%1 | %2\nEstimated age: %3\nDetection: %4")
            .arg(emotionName(*face), genderName(*face)).arg(face->age_gender.age).arg(face->box->score, 0, 'f', 3) : "No face");
        for (int index = 0; index < 7; ++index)
            emotionBars[index]->setValue(face ? std::clamp(static_cast<int>(face->emotion.scores[index] * 100), 0, 100) : 0);
        for (int index = 0; index < 2; ++index)
            genderBars[index]->setValue(face ? std::clamp(static_cast<int>(face->age_gender.gender_scores[index] * 100), 0, 100) : 0);
    };
    auto request = [&](Source source, const std::string& path) {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.command.source = source;
        if (!path.empty()) shared.command.path = path;
        ++shared.command.revision;
        exportMessage.clear();
        displayed = Frame();
        shared.wake.notify_one();
    };
    QObject::connect(cameraButton, &QPushButton::toggled, [&](bool enabled) {
        cameraButton->setText(enabled ? "Close Camera" : "Open Camera");
        request(enabled ? Source::Camera : Source::Idle, {});
    });
    QObject::connect(imageButton, &QPushButton::clicked, [&] {
        const QString path = QFileDialog::getOpenFileName(&window, "Local Image", QDir::currentPath() + "/images", "Images (*.jpg *.jpeg *.png *.bmp)");
        if (path.isEmpty()) return;
        { QSignalBlocker blocker(cameraButton); cameraButton->setChecked(false); cameraButton->setText("Open Camera"); }
        request(Source::Image, path.toStdString());
    });
    QObject::connect(stopButton, &QPushButton::clicked, [&] {
        { QSignalBlocker blocker(cameraButton); cameraButton->setChecked(false); cameraButton->setText("Open Camera"); }
        request(Source::Idle, {});
    });
    auto parametersChanged = [&] {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.command.confidence = static_cast<float>(confidence->value());
        shared.command.iou = static_cast<float>(iou->value());
        ++shared.command.revision;
        shared.wake.notify_one();
    };
    QObject::connect(confidence, QOverload<double>::of(&QDoubleSpinBox::valueChanged), parametersChanged);
    QObject::connect(iou, QOverload<double>::of(&QDoubleSpinBox::valueChanged), parametersChanged);
    QObject::connect(selection, QOverload<int>::of(&QComboBox::currentIndexChanged), updateDetails);
    QObject::connect(saveButton, &QPushButton::clicked, [&] {
        const QString base = saveSnapshot(displayed);
        if (base.isEmpty()) QMessageBox::warning(&window, "Save failed", "Cannot write result files.");
        else exportMessage = "Saved: " + base;
    });
    auto toggleFull = [&] { if (window.isFullScreen()) window.showNormal(); else window.showFullScreen(); };
    QObject::connect(fullButton, &QPushButton::clicked, toggleFull);
    QObject::connect(quitButton, &QPushButton::clicked, &app, &QApplication::quit);
    QObject::connect(new QShortcut(QKeySequence("F"), &window), &QShortcut::activated, toggleFull);
    QObject::connect(new QShortcut(QKeySequence("Escape"), &window), &QShortcut::activated, &app, &QApplication::quit);
    QTimer timer;
    bool smokeFinished = false;
    int testExit = smoke ? 3 : 0;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        if (interrupted) { app.quit(); return; }
        Frame incoming;
        unsigned long revision;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            incoming = shared.frame;
            revision = shared.command.revision;
        }
        if (incoming.sequence != lastSequence && (incoming.revision == revision || !incoming.ready)) {
            lastSequence = incoming.sequence;
            displayed = std::move(incoming);
            QSignalBlocker blocker(selection);
            const int selected = selection->currentIndex();
            selection->clear();
            for (size_t index = 0; index < displayed.faces.size(); ++index) selection->addItem(QString("Face #%1").arg(index + 1));
            selection->setCurrentIndex(displayed.faces.empty() ? -1 : std::clamp(selected, 0, static_cast<int>(displayed.faces.size()) - 1));
            updateDetails();
        }
        authorization->setText(displayed.auth == 0 ? "Authorization: formal" : displayed.auth == 100010 ? "Authorization: trial (30 min)" : QString("Authorization: %1").arg(displayed.auth));
        metrics->setText(QString("Faces: %1   Inference: %2 ms   FPS: %3").arg(displayed.faces.size()).arg(displayed.milliseconds, 0, 'f', 1).arg(displayed.fps, 0, 'f', 1));
        status->setText(exportMessage.isEmpty() ? displayed.message : exportMessage);
        const bool available = displayed.ready && !displayed.failed;
        cameraButton->setEnabled(displayed.ready);
        imageButton->setEnabled(displayed.ready);
        confidence->setEnabled(displayed.ready);
        iou->setEnabled(displayed.ready);
        saveButton->setEnabled(available && !displayed.image.isNull());
        if (displayed.failed && cameraButton->isChecked()) {
            QSignalBlocker blocker(cameraButton);
            cameraButton->setChecked(false);
            cameraButton->setText("Open Camera");
        }
        if (displayed.image.isNull()) { view->setPixmap(QPixmap()); view->setText(displayed.message); }
        else view->setPixmap(QPixmap::fromImage(annotatedImage(displayed, overlays->isChecked())).scaled(view->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (smoke && !smokeFinished && (displayed.failed || !displayed.image.isNull())) {
            smokeFinished = true;
            testExit = available && !saveSnapshot(displayed).isEmpty() ? 0 : 1;
            QTimer::singleShot(250, [&] {
                if (!screenshot.isEmpty() && !window.grab().save(screenshot)) testExit = 1;
                app.quit();
            });
        }
    });
    QTimer systemTimer;
    unsigned long long previousTotal = 0, previousIdle = 0;
    QObject::connect(&systemTimer, &QTimer::timeout, [&] {
        std::ifstream memory("/proc/meminfo");
        std::string key, unit;
        unsigned long long value = 0, totalMemory = 0, availableMemory = 0;
        while (memory >> key >> value >> unit) {
            if (key == "MemTotal:") totalMemory = value;
            if (key == "MemAvailable:") availableMemory = value;
        }
        std::ifstream cpu("/proc/stat");
        unsigned long long user = 0, nice = 0, system = 0, idle = 0, wait = 0, irq = 0, softIrq = 0, steal = 0;
        cpu >> key >> user >> nice >> system >> idle >> wait >> irq >> softIrq >> steal;
        const auto total = user + nice + system + idle + wait + irq + softIrq + steal;
        const auto idleTime = idle + wait;
        const double usage = total > previousTotal ? 100.0 * (1.0 - double(idleTime - previousIdle) / double(total - previousTotal)) : 0;
        previousTotal = total;
        previousIdle = idleTime;
        const QStorageInfo disk(QDir::currentPath());
        deviceStatus->setText(QString("CPU: %1%\nRAM: %2 / %3 MB\nDisk free: %4 GB").arg(usage, 0, 'f', 0)
            .arg((totalMemory - availableMemory) / 1024).arg(totalMemory / 1024).arg(disk.bytesAvailable() / 1073741824.0, 0, 'f', 1));
    });
    std::thread inferenceThread(worker, std::ref(shared), camera);
    window.show();
    timer.start(80);
    systemTimer.start(1500);
    if (quitAfter > 0) QTimer::singleShot(quitAfter, &app, &QApplication::quit);
    else if (smoke) QTimer::singleShot(90000, &app, &QApplication::quit);
    app.exec();
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.stop = true;
        shared.wake.notify_one();
    }
    inferenceThread.join();
    return testExit;
}