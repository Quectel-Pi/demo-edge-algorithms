#include "MaskEngine.h"
#include <opencv2/opencv.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFrame>
#include <QMimeData>
#include <QProgressBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QUrl>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

static volatile std::sig_atomic_t interrupted = 0;
static void onSignal(int) { interrupted = 1; }
enum class Source { Idle, Image, Camera };

struct Options {
    Source source = Source::Image;
    QString image = "images/input.jpg";
    QString camera = "auto";
    QString output = "outputs";
    float confidence = 0.30f;
    float nms = 0.45f;
    int rotation = 0;
    int frames = 1;
    int repeat = 1;
    int quitAfter = 0;
    bool headless = false;
    QString screenshot;
};
struct Frame {
    QImage image;
    std::vector<MaskResults_t> results;
    QString source;
    QString message;
    double milliseconds = 0;
    int result = MASK_INIT_FAILED;
    int auth = -1;
    float confidence = 0.30f;
    float nms = 0.45f;
    int rotation = 0;
    unsigned long revision = 0;
    unsigned long sequence = 0;
    unsigned long resultsVersion = 0;
    bool complete = false;
};
struct Shared {
    std::mutex mutex;
    std::condition_variable wake;        // wakes the inference worker
    std::condition_variable camWake;     // wakes the capture thread
    Options options;
    Frame frame;                         // latest snapshot for the GUI
    unsigned long revision = 1;
    bool stop = false;
    bool liveStop = false;               // fatal live error: capture thread idles
    cv::Mat pending;                     // latest live frame awaiting inference (single slot)
    bool hasPending = false;
    std::vector<MaskResults_t> overlay;  // latest live results, drawn on the preview
    double overlayMs = 0;
    int overlayAuth = -1;
    int overlayRc = MASK_SUCCESS;
    unsigned long overlayVersion = 0;
};

struct ClassInfo { const char* key; const char* name; QColor color; };
static ClassInfo classInfo(int id) {
    switch (id) {
        case 0: return {"correctly_worn", "Correctly worn", QColor("#2fbf71")};
        case 1: return {"not_worn", "Not worn", QColor("#e5484d")};
        case 2: return {"incorrectly_worn", "Incorrectly worn", QColor("#f5a524")};
        default: return {"unknown", "Unknown", QColor("#808080")};
    }
}

static QString errorText(int result) {
    switch (result) {
        case MASK_SUCCESS: return "Complete";
        case MASK_UNKNOWN_ERROR: return "SDK unknown error";
        case MASK_INVALID_ARGUMENT: return "SDK invalid argument";
        case MASK_INIT_FAILED: return "SDK not initialized / initialization failed";
        case MASK_IMAGE_EMPTY: return "Image is empty / camera unavailable";
        case MASK_INFERENCE_FAILED: return "SDK model inference failed";
        case MASK_AUTH_FAILED: return "SDK authorization failed";
        case MASK_TRIAL_END: return "SDK trial period ended";
        default: return "SDK / input error";
    }
}

static QString authNote(int auth) {
    return auth == 0 ? QString() : QString(" (trial mode, formal authorization status %1)").arg(auth);
}

struct CameraDevice { QString path; QString name; };

static std::vector<CameraDevice> cameraList() {
    std::vector<CameraDevice> devices;
    for (const auto& entry : QDir("/dev").entryList({"video*"}, QDir::System)) {
        bool numeric = false;
        entry.mid(5).toInt(&numeric);
        if (!numeric) continue;
        const QString path = "/dev/" + entry;
        const int descriptor = open(path.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
        if (descriptor < 0) continue;
        v4l2_capability capability{};
        const bool queried = ioctl(descriptor, VIDIOC_QUERYCAP, &capability) == 0;
        close(descriptor);
        const auto flags = capability.capabilities & V4L2_CAP_DEVICE_CAPS ? capability.device_caps : capability.capabilities;
        if (queried && (flags & V4L2_CAP_VIDEO_CAPTURE) && (flags & V4L2_CAP_STREAMING) &&
            std::strstr(reinterpret_cast<const char*>(capability.bus_info), "usb"))
            devices.push_back({path, QString::fromUtf8(reinterpret_cast<const char*>(capability.card)).trimmed()});
    }
    std::sort(devices.begin(), devices.end(), [](const CameraDevice& first, const CameraDevice& second) {
        return first.path.mid(10).toInt() < second.path.mid(10).toInt();
    });
    return devices;
}

static QStringList cameraDevices() {
    QStringList paths;
    for (const auto& device : cameraList()) paths.append(device.path);
    return paths;
}

static bool openCamera(cv::VideoCapture& capture, const QString& device) {
    QStringList candidates = device == "auto" ? cameraDevices() : QStringList{device};
    for (const auto& candidate : candidates) {
        if (!capture.open(candidate.toStdString(), cv::CAP_V4L2)) continue;
        capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
        std::printf("[camera] device=%s\n", candidate.toUtf8().constData());
        return true;
    }
    return false;
}

static QImage toImage(const cv::Mat& bgr) {
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

static void applyRotation(cv::Mat& bgr, int rotation) {
    if (rotation == 90) cv::rotate(bgr, bgr, cv::ROTATE_90_CLOCKWISE);
    else if (rotation == 180) cv::rotate(bgr, bgr, cv::ROTATE_180);
    else if (rotation == 270) cv::rotate(bgr, bgr, cv::ROTATE_90_COUNTERCLOCKWISE);
}

static void printResults(const std::vector<MaskResults_t>& results) {
    for (const auto& item : results)
        std::printf("[mask] class=%s confidence=%.3f box=%d,%d,%d,%d\n", classInfo(item.class_id).key,
            item.confidence, item.x, item.y, item.width, item.height);
}

class Engine {
    MaskEngine sdk;
    bool attempted = false;
    float confidence = -1, nms = -1;
public:
    ~Engine() { if (attempted) sdk.uinit(); }
    // set_param takes effect dynamically per SDK docs; reapplied only when values change.
    void setParam(float newConfidence, float newNms) {
        if (newConfidence == confidence && newNms == nms) return;
        sdk.set_param(newConfidence, newNms);
        confidence = newConfidence;
        nms = newNms;
    }
    bool init(float conf, float nmsThreshold, Frame& frame) {
        std::string model = "models/mask_q_3576_enc.rknn";
        std::string license = std::getenv("MASK_LICENSE_FILE") ? std::getenv("MASK_LICENSE_FILE") : "license/mask_license.ini";
        std::string auth = std::getenv("MASK_AUTH_CODE") ? std::getenv("MASK_AUTH_CODE") : "";
        bool validAppId = false;
        const int appId = QString::fromLocal8Bit(std::getenv("MASK_APP_ID") ? std::getenv("MASK_APP_ID") : "147").toInt(&validAppId);
        if (!validAppId) throw std::runtime_error("Invalid MASK_APP_ID");
        if (!QDir().mkpath(QFileInfo(QString::fromStdString(license)).absolutePath()))
            throw std::runtime_error("Cannot create license directory");
        sdk.set_param(conf, nmsThreshold);
        attempted = true;
        frame.result = sdk.init(model.data(), license.data(), 3, auth.data(), appId);
        const int status = sdk.get_status(0);
        frame.auth = sdk.get_status(1);
        // Re-apply after init in case init() resets thresholds to its defaults.
        sdk.set_param(conf, nmsThreshold);
        confidence = conf;
        nms = nmsThreshold;
        std::printf("[init] rc=%d status=%d auth=%d confidence=%.3f nms=%.3f\n", frame.result, status, frame.auth, conf, nmsThreshold);
        // Non-zero auth means SDK trial mode (1 h); inference then reports MASK_AUTH_FAILED / MASK_TRIAL_END itself.
        if (frame.auth != 0) std::printf("[warn] formal authorization failed (auth=%d); SDK trial mode\n", frame.auth);
        if (frame.result != MASK_SUCCESS || status != 0) {
            frame.result = frame.result != MASK_SUCCESS ? frame.result : MASK_INIT_FAILED;
            frame.message = QString("SDK initialization failed: rc=%1, model=%2, auth=%3").arg(frame.result).arg(status).arg(frame.auth);
            return false;
        }
        return true;
    }
    int run(const cv::Mat& src, std::vector<MaskResults_t>& results, double& milliseconds, int& auth) {
        cv::Mat bgr = src.isContinuous() ? src : src.clone();
        results.clear();
        const auto start = std::chrono::steady_clock::now();
        int result = sdk.inference(bgr.data, bgr.rows, bgr.cols, bgr.channels(), results);
        milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        auth = sdk.get_status(1);
        if (result != MASK_SUCCESS) results.clear();
        return result;
    }
    void infer(cv::Mat& bgr, Frame& frame) {
        frame.result = run(bgr, frame.results, frame.milliseconds, frame.auth);
        frame.image = toImage(bgr);
        frame.complete = true;
        frame.message = errorText(frame.result) + authNote(frame.auth);
        std::printf("[frame] rc=%d masks=%zu inference_ms=%.2f auth=%d width=%d height=%d\n",
            frame.result, frame.results.size(), frame.milliseconds, frame.auth, bgr.cols, bgr.rows);
        printResults(frame.results);
    }
};

static QImage annotated(const Frame& frame, bool labels = true, int highlight = -1) {
    QImage image = frame.image.copy();
    if (image.isNull()) return image;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font;
    font.setPixelSize(std::max(12, std::min(image.width(), image.height()) / 40));
    painter.setFont(font);
    const int pen = std::max(2, std::min(image.width(), image.height()) / 240);
    int index = -1;
    for (const auto& item : frame.results) {
        ++index;
        const auto info = classInfo(item.class_id);
        const QRect box(item.x, item.y, item.width, item.height);
        QColor fill = info.color;
        fill.setAlpha(60);
        painter.setPen(QPen(info.color, index == highlight ? pen * 2 : pen));
        painter.setBrush(index == highlight ? QBrush(fill) : QBrush(Qt::NoBrush));
        painter.drawRect(box);
        if (!labels) continue;
        const QString text = QString("#%1 %2 %3%").arg(index + 1).arg(info.name).arg(qRound(item.confidence * 100));
        const int width = std::min(image.width(), painter.fontMetrics().horizontalAdvance(text) + 8);
        const int height = painter.fontMetrics().height() + 4;
        const int left = std::clamp(box.left(), 0, std::max(0, image.width() - width));
        const int top = std::clamp(box.top() - height, 0, std::max(0, image.height() - height));
        painter.fillRect(QRect(left, top, width, height), info.color);
        painter.setPen(Qt::white);
        painter.drawText(left + 4, top + painter.fontMetrics().ascent() + 2, text);
    }
    return image;
}

static QString summaryText(const Frame& frame) {
    int counts[4] = {0, 0, 0, 0};
    for (const auto& item : frame.results) ++counts[item.class_id >= 0 && item.class_id <= 2 ? item.class_id : 3];
    QStringList lines;
    lines.append(QString("Total: %1").arg(frame.results.size()));
    for (int id = 0; id <= 2; ++id) lines.append(QString("%1: %2").arg(classInfo(id).name).arg(counts[id]));
    if (counts[3]) lines.append(QString("Unknown: %1").arg(counts[3]));
    int index = 0;
    for (const auto& item : frame.results)
        lines.append(QString("%1. %2 %3 [%4,%5,%6,%7]").arg(++index).arg(classInfo(item.class_id).name)
            .arg(item.confidence, 0, 'f', 3).arg(item.x).arg(item.y).arg(item.width).arg(item.height));
    return lines.join('\n');
}

static bool writeFile(const QString& path, const QByteArray& data) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

static QString exportFrame(const Frame& frame, const QString& directory) {
    if (!frame.complete || frame.result != MASK_SUCCESS || frame.image.isNull()) return {};
    if (!QDir().mkpath(directory)) return {};
    const QString name = "maskdet_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + "_" + QString::number(frame.sequence);
    QDir output(directory);
    QJsonArray records, shapes;
    int index = 0;
    for (const auto& item : frame.results) {
        const auto info = classInfo(item.class_id);
        records.append(QJsonObject{{"index", ++index}, {"class_id", item.class_id}, {"class_name", info.key},
            {"confidence", item.confidence}, {"code", item.code},
            {"box", QJsonObject{{"x", item.x}, {"y", item.y}, {"width", item.width}, {"height", item.height}}}});
        shapes.append(QJsonObject{{"label", info.key},
            {"points", QJsonArray{QJsonArray{item.x, item.y}, QJsonArray{item.x + item.width, item.y + item.height}}},
            {"group_id", QJsonValue::Null}, {"shape_type", "rectangle"}, {"flags", QJsonObject{}}});
    }
    QJsonObject result{{"source", frame.source}, {"result_code", frame.result}, {"auth_status", frame.auth},
        {"width", frame.image.width()}, {"height", frame.image.height()}, {"rotation", frame.rotation},
        {"confidence_threshold", frame.confidence}, {"nms_threshold", frame.nms},
        {"inference_ms", frame.milliseconds}, {"results", records}};
    QJsonObject labelme{{"version", "5.0.1"}, {"flags", QJsonObject{}}, {"shapes", shapes},
        {"imagePath", name + "_source.png"}, {"imageData", QJsonValue::Null},
        {"imageHeight", frame.image.height()}, {"imageWidth", frame.image.width()}};
    const QStringList paths{output.filePath(name + ".png"), output.filePath(name + "_source.png"),
        output.filePath(name + ".txt"), output.filePath(name + ".json"), output.filePath(name + ".labelme.json")};
    const bool saved = annotated(frame).save(paths[0], "PNG") && frame.image.save(paths[1], "PNG") &&
        writeFile(paths[2], summaryText(frame).toUtf8()) && writeFile(paths[3], QJsonDocument(result).toJson()) &&
        writeFile(paths[4], QJsonDocument(labelme).toJson());
    if (!saved) {
        for (const auto& path : paths) QFile::remove(path);
        return {};
    }
    std::printf("[export] base=%s\n", output.filePath(name).toUtf8().constData());
    return output.filePath(name);
}

static void publishFailure(Shared& shared, Frame frame) {
    // Caller holds shared.mutex.
    shared.liveStop = true;
    frame.resultsVersion = ++shared.overlayVersion;
    frame.sequence = shared.frame.sequence + 1;
    shared.frame = std::move(frame);
    shared.wake.notify_all();
    shared.camWake.notify_all();
}

// Inference thread: owns the SDK; handles image/idle one-shots and asynchronous live detection.
static void worker(Shared& shared) {
    unsigned long activeRevision = 0;
    std::unique_ptr<Engine> engine;
    while (true) {
        Options options;
        unsigned long revision;
        cv::Mat job;
        bool haveJob = false;
        {
            std::unique_lock<std::mutex> lock(shared.mutex);
            shared.wake.wait(lock, [&] {
                return shared.stop || shared.revision != activeRevision ||
                    (shared.options.source == Source::Camera && !shared.liveStop && shared.hasPending);
            });
            if (shared.stop) break;
            options = shared.options;
            revision = shared.revision;
            if (options.source == Source::Camera && !shared.liveStop && shared.hasPending) {
                job = std::move(shared.pending);
                shared.hasPending = false;
                haveJob = true;
            }
        }
        const bool revisionChanged = revision != activeRevision;
        activeRevision = revision;
        const bool live = options.source == Source::Camera;
        try {
            if (options.source == Source::Idle) {
                if (revisionChanged) {
                    Frame frame;
                    frame.revision = revision;
                    frame.message = "Stopped";
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    frame.resultsVersion = ++shared.overlayVersion;
                    frame.sequence = shared.frame.sequence + 1;
                    shared.frame = std::move(frame);
                }
                continue;
            }
            if (!engine) {
                engine = std::make_unique<Engine>();
                Frame frame;
                frame.revision = revision;
                frame.source = live ? options.camera : options.image;
                frame.message = "Initializing SDK";
                {
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    frame.resultsVersion = ++shared.overlayVersion;
                    frame.sequence = shared.frame.sequence + 1;
                    shared.frame = frame;
                }
                if (!engine->init(options.confidence, options.nms, frame)) {
                    engine.reset();
                    frame.complete = true;
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    publishFailure(shared, std::move(frame));
                    continue;
                }
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.overlay.clear();
                shared.overlayMs = 0;
                shared.overlayAuth = frame.auth;
                shared.overlayRc = MASK_SUCCESS;
            }
            engine->setParam(options.confidence, options.nms);
            if (live) {
                if (!haveJob) continue;
                std::vector<MaskResults_t> results;
                double milliseconds = 0;
                int auth = -1;
                const int rc = engine->run(job, results, milliseconds, auth);
                std::printf("[frame] rc=%d masks=%zu inference_ms=%.2f auth=%d width=%d height=%d\n",
                    rc, results.size(), milliseconds, auth, job.cols, job.rows);
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.overlay = rc == MASK_SUCCESS ? std::move(results) : std::vector<MaskResults_t>{};
                shared.overlayMs = milliseconds;
                shared.overlayAuth = auth;
                shared.overlayRc = rc;
                ++shared.overlayVersion;
                if (rc != MASK_SUCCESS) {
                    Frame frame;
                    frame.revision = revision;
                    frame.source = options.camera;
                    frame.result = rc;
                    frame.auth = auth;
                    frame.complete = true;
                    frame.message = errorText(rc);
                    publishFailure(shared, std::move(frame));
                }
                continue;
            }
            cv::Mat bgr = cv::imread(options.image.toStdString(), cv::IMREAD_COLOR);
            if (bgr.empty()) throw std::runtime_error("Cannot read image");
            applyRotation(bgr, options.rotation);
            Frame frame;
            frame.revision = revision;
            frame.source = options.image;
            frame.confidence = options.confidence;
            frame.nms = options.nms;
            frame.rotation = options.rotation;
            engine->infer(bgr, frame);
            std::lock_guard<std::mutex> lock(shared.mutex);
            frame.resultsVersion = ++shared.overlayVersion;
            frame.sequence = shared.frame.sequence + 1;
            shared.frame = std::move(frame);
        } catch (const std::exception& error) {
            engine.reset();
            Frame frame;
            frame.revision = revision;
            frame.result = MASK_UNKNOWN_ERROR;
            frame.complete = true;
            frame.message = QString::fromLocal8Bit(error.what());
            std::lock_guard<std::mutex> lock(shared.mutex);
            publishFailure(shared, std::move(frame));
        }
    }
}

// Capture thread: owns the camera; streams preview frames at camera rate and feeds the latest frame to the worker.
static void captureThread(Shared& shared) {
    unsigned long activeRevision = 0;
    cv::VideoCapture capture;
    bool live = false;
    Options options;
    while (true) {
        bool reopen = false;
        {
            std::unique_lock<std::mutex> lock(shared.mutex);
            if (!live)
                shared.camWake.wait(lock, [&] {
                    return shared.stop || (shared.options.source == Source::Camera && !shared.liveStop);
                });
            if (shared.stop) break;
            if (shared.options.source != Source::Camera || shared.liveStop) {
                live = false;
            } else {
                if (!live || shared.revision != activeRevision) { reopen = true; activeRevision = shared.revision; }
                options = shared.options;
                live = true;
            }
        }
        if (!live) { capture.release(); continue; }
        if (reopen) capture.release();
        const bool opened = capture.isOpened() || openCamera(capture, options.camera);
        cv::Mat bgr;
        if (!opened || !capture.read(bgr) || bgr.empty()) {
            capture.release();
            Frame frame;
            frame.revision = activeRevision;
            frame.source = options.camera;
            frame.result = MASK_IMAGE_EMPTY;
            frame.complete = true;
            frame.message = opened ? "Camera disconnected / capture failed" : "No accessible USB camera; select /dev/videoN";
            std::lock_guard<std::mutex> lock(shared.mutex);
            publishFailure(shared, std::move(frame));
            live = false;
            continue;
        }
        applyRotation(bgr, options.rotation);
        if (!bgr.isContinuous()) bgr = bgr.clone();
        QImage preview = toImage(bgr);
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (shared.stop || shared.options.source != Source::Camera || shared.liveStop || shared.revision != activeRevision)
            continue;
        shared.pending = bgr;
        shared.hasPending = true;
        Frame frame;
        frame.revision = activeRevision;
        frame.source = options.camera;
        frame.image = std::move(preview);
        frame.results = shared.overlay;
        frame.milliseconds = shared.overlayMs;
        frame.auth = shared.overlayAuth;
        frame.result = shared.overlayRc;
        frame.confidence = options.confidence;
        frame.nms = options.nms;
        frame.rotation = options.rotation;
        frame.complete = true;
        frame.message = "Live" + authNote(shared.overlayAuth);
        frame.resultsVersion = shared.overlayVersion;
        frame.sequence = shared.frame.sequence + 1;
        shared.frame = std::move(frame);
        shared.wake.notify_all();
    }
    capture.release();
}

static QString hintFor(const Frame& frame) {
    if (frame.message.startsWith("SDK initialization failed"))
        return "Check MASK_AUTH_CODE in run.sh, network and system time; delete an empty license/mask_license.ini and retry.";
    if (frame.message.startsWith("No accessible USB camera")) return "Plug in a USB camera, click the rescan button and select it.";
    if (frame.message.startsWith("Camera disconnected")) return "Check the USB cable, then start the camera again.";
    if (frame.message.startsWith("Cannot read image")) return "Choose a readable JPG / PNG / BMP file.";
    if (frame.result == MASK_TRIAL_END) return "The 1-hour trial has ended; configure a formal authorization code.";
    if (frame.result == MASK_AUTH_FAILED) return "Check MASK_AUTH_CODE and the license file.";
    return {};
}

static QIcon dotIcon(const QColor& color) {
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(1, 1, 12, 12);
    return QIcon(pixmap);
}

static QString imageFromMime(const QMimeData* mime) {
    if (!mime || !mime->hasUrls()) return {};
    for (const auto& url : mime->urls()) {
        const QString path = url.toLocalFile();
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (!path.isEmpty() && QStringList{"jpg", "jpeg", "png", "bmp"}.contains(suffix)) return path;
    }
    return {};
}

class Window : public QMainWindow {
    Shared shared;
    std::thread thread;
    std::thread captureWorker;
    Frame current;
    QLabel* preview = new QLabel;
    QLabel* banner = new QLabel;
    QLabel* authBadge = new QLabel;
    QLabel* sourceLabel = new QLabel;
    QLabel* facesValue = nullptr;
    QLabel* latencyValue = nullptr;
    QLabel* fpsValue = nullptr;
    QLabel* sizeValue = nullptr;
    QLabel* classCount[3] = {nullptr, nullptr, nullptr};
    QProgressBar* compliance = new QProgressBar;
    QTableWidget* table = new QTableWidget(0, 3);
    QDoubleSpinBox* confidence = new QDoubleSpinBox;
    QDoubleSpinBox* nms = new QDoubleSpinBox;
    QComboBox* rotation = new QComboBox;
    QComboBox* cameras = new QComboBox;
    QCheckBox* labels = new QCheckBox("Show labels on image");
    QPushButton* imageButton = new QPushButton("Open Image");
    QPushButton* cameraButton = new QPushButton("Start Camera");
    QPushButton* rerunButton = new QPushButton("Detect Again");
    QPushButton* save = new QPushButton("Export Result");
    QPushButton* copy = new QPushButton("Copy Summary");
    QPushButton* fullButton = new QPushButton("Full Screen");
    QTimer timer;
    QTimer settingsTimer;
    QElapsedTimer fpsClock;
    int previewFrames = 0, detectFrames = 0;
    double previewFps = 0, detectFps = 0;
    unsigned long sequence = 0;
    unsigned long shownResults = 0;
    int lastAuth = -1;
    int highlight = -1;
    bool live = false;
    bool screenshotTaken = false;
    QString notice;

    static QLabel* section(QVBoxLayout* layout, const QString& text) {
        auto* label = new QLabel(text.toUpper());
        label->setObjectName("section");
        layout->addWidget(label);
        return label;
    }
    static QLabel* chip(QHBoxLayout* row, const QString& caption, const QString& tip) {
        auto* frame = new QFrame;
        frame->setObjectName("chip");
        frame->setToolTip(tip);
        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(12, 6, 12, 6);
        layout->setSpacing(0);
        auto* name = new QLabel(caption);
        name->setObjectName("chipCaption");
        auto* value = new QLabel("--");
        value->setObjectName("chipValue");
        layout->addWidget(name);
        layout->addWidget(value);
        row->addWidget(frame, 1);
        return value;
    }
    // Slider + spin box pair sharing one value in [0.01, 0.99].
    QWidget* thresholdRow(QDoubleSpinBox* box, double value, const QString& tip) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(1, 99);
        box->setRange(0.01, 0.99);
        box->setSingleStep(0.05);
        box->setDecimals(2);
        box->setValue(value);
        box->setFixedWidth(70);
        slider->setValue(qRound(value * 100));
        row->setToolTip(tip);
        layout->addWidget(slider, 1);
        layout->addWidget(box);
        connect(slider, &QSlider::valueChanged, box, [box](int v) { box->setValue(v / 100.0); });
        connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, slider](double v) {
            QSignalBlocker blocker(slider);
            slider->setValue(qRound(v * 100));
            settingsTimer.start();
        });
        return row;
    }
    void setBanner(const QString& text, const QString& kind) {
        static const QMap<QString, QString> styles{
            {"ok", "background:#1d3b30;color:#7fe0b5;"}, {"info", "background:#2a2a3d;color:#cfd0e0;"},
            {"warn", "background:#3d3320;color:#f0c674;"}, {"error", "background:#3d2024;color:#ff8a8a;"}};
        banner->setText(text);
        banner->setStyleSheet(styles.value(kind) + "border-radius:6px;padding:8px 12px;");
    }
    void refreshCameras(const QString& wanted) {
        QSignalBlocker blocker(cameras);
        cameras->clear();
        cameras->addItem("Auto (first USB camera)", "auto");
        for (const auto& device : cameraList())
            cameras->addItem(QString("%1  (%2)").arg(device.name.isEmpty() ? "USB camera" : device.name, device.path), device.path);
        int index = cameras->findData(wanted);
        if (index < 0 && !wanted.isEmpty() && wanted != "auto") {
            cameras->addItem(wanted, wanted);
            index = cameras->count() - 1;
        }
        cameras->setCurrentIndex(std::max(0, index));
    }
    void setCameraButton(bool on) {
        QSignalBlocker blocker(cameraButton);
        cameraButton->setChecked(on);
        cameraButton->setText(on ? "Stop Camera" : "Start Camera");
        cameraButton->setIcon(style()->standardIcon(on ? QStyle::SP_MediaStop : QStyle::SP_MediaPlay));
    }
    void request(Source source) {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.options.source = source;
        shared.options.confidence = static_cast<float>(confidence->value());
        shared.options.nms = static_cast<float>(nms->value());
        shared.options.rotation = rotation->currentData().toInt();
        shared.options.camera = cameras->currentData().toString();
        shared.liveStop = false;
        shared.hasPending = false;
        shared.overlay.clear();
        shared.overlayMs = 0;
        shared.overlayRc = MASK_SUCCESS;
        ++shared.revision;
        live = source == Source::Camera;
        notice.clear();
        highlight = -1;
        previewFrames = detectFrames = 0;
        previewFps = detectFps = 0;
        fpsClock.restart();
        save->setEnabled(false);
        copy->setEnabled(false);
        rerunButton->setEnabled(source == Source::Image);
        setBanner(source == Source::Idle ? "Camera stopped" : source == Source::Camera ? "Opening camera..." : "Detecting...", "info");
        shared.wake.notify_all();
        shared.camWake.notify_all();
    }
    // Live sources pick up thresholds/rotation on the next frame; still images are re-detected.
    void applySettings() {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.options.confidence = static_cast<float>(confidence->value());
        shared.options.nms = static_cast<float>(nms->value());
        shared.options.rotation = rotation->currentData().toInt();
        if (shared.options.source == Source::Image) {
            ++shared.revision;
            shared.wake.notify_all();
        }
    }
    void openImage(const QString& path) {
        { std::lock_guard<std::mutex> lock(shared.mutex); shared.options.image = path; }
        setCameraButton(false);
        request(Source::Image);
    }
    void redraw() {
        if (current.image.isNull()) {
            preview->setPixmap(QPixmap());
            preview->setText("Drop an image here, click \"Open Image\", or \"Start Camera\"\n\n"
                "Ctrl+O  open image     Space  camera on/off     Ctrl+S  export     F11  full screen");
            return;
        }
        // Draw at display resolution so boxes and labels stay legible for any input size.
        const double scale = std::min(double(preview->width()) / current.image.width(), double(preview->height()) / current.image.height());
        Frame view;
        view.image = current.image.scaled(std::max(1, qRound(current.image.width() * scale)),
            std::max(1, qRound(current.image.height() * scale)), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        view.results = current.results;
        for (auto& item : view.results) {
            item.x = qRound(item.x * scale);
            item.y = qRound(item.y * scale);
            item.width = qRound(item.width * scale);
            item.height = qRound(item.height * scale);
        }
        preview->setPixmap(QPixmap::fromImage(annotated(view, labels->isChecked(), highlight)));
    }
    void updateAuth() {
        if (current.auth != -1) lastAuth = current.auth;
        if (lastAuth == 0) {
            authBadge->setText("\xe2\x97\x8f  Licensed");
            authBadge->setStyleSheet("color:#7fe0b5;");
            authBadge->setToolTip("Formal authorization is active.");
        } else if (lastAuth == -1) {
            authBadge->setText("\xe2\x97\x8f  Authorization pending");
            authBadge->setStyleSheet("color:#9a9ab0;");
        } else {
            authBadge->setText(QString("\xe2\x97\x8f  Trial mode (%1)").arg(lastAuth));
            authBadge->setStyleSheet("color:#f0c674;");
            authBadge->setToolTip("Formal authorization failed; the SDK runs a 1-hour trial. Set MASK_AUTH_CODE in run.sh.");
        }
    }
    void updateStats() {
        int counts[3] = {0, 0, 0};
        for (const auto& item : current.results) if (item.class_id >= 0 && item.class_id <= 2) ++counts[item.class_id];
        for (int id = 0; id < 3; ++id) classCount[id]->setText(QString::number(counts[id]));
        const int total = static_cast<int>(current.results.size());
        compliance->setValue(total ? qRound(100.0 * counts[0] / total) : 0);
        compliance->setFormat(total ? "%p% wearing masks correctly" : "No face detected");
        facesValue->setText(QString::number(total));
        latencyValue->setText(current.milliseconds > 0 ? QString("%1 ms").arg(current.milliseconds, 0, 'f', 1) : "--");
        sizeValue->setText(current.image.isNull() ? "--" : QString("%1\xc3\x97%2").arg(current.image.width()).arg(current.image.height()));
        QSignalBlocker blocker(table);
        table->setRowCount(total);
        int index = 0;
        for (const auto& item : current.results) {
            const auto info = classInfo(item.class_id);
            auto* number = new QTableWidgetItem(QString::number(index + 1));
            number->setTextAlignment(Qt::AlignCenter);
            auto* name = new QTableWidgetItem(dotIcon(info.color), info.name);
            auto* score = new QTableWidgetItem(QString("%1%").arg(item.confidence * 100, 0, 'f', 1));
            score->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            const QString tip = QString("Box: x=%1, y=%2, %3\xc3\x97%4 px").arg(item.x).arg(item.y).arg(item.width).arg(item.height);
            for (auto* cell : {number, name, score}) cell->setToolTip(tip);
            table->setItem(index, 0, number);
            table->setItem(index, 1, name);
            table->setItem(index, 2, score);
            ++index;
        }
        if (highlight >= total) highlight = -1;
        if (highlight >= 0) table->selectRow(highlight);
        else table->clearSelection();
    }
    void updateFrame() {
        if (interrupted) { close(); return; }
        bool fresh = false;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            if (sequence != shared.frame.sequence) {
                sequence = shared.frame.sequence;
                if (shared.frame.revision == shared.revision) { current = shared.frame; fresh = true; }
            }
        }
        const bool newResults = fresh && shownResults != current.resultsVersion;
        if (live && fresh) ++previewFrames;
        if (live && newResults) ++detectFrames;
        if (fpsClock.elapsed() >= 1000) {
            previewFps = previewFrames * 1000.0 / fpsClock.elapsed();
            detectFps = detectFrames * 1000.0 / fpsClock.elapsed();
            previewFrames = detectFrames = 0;
            fpsClock.restart();
            fpsValue->setText(live ? QString("%1 / %2").arg(detectFps, 0, 'f', 1).arg(previewFps, 0, 'f', 0) : "--");
        }
        if (!fresh) return;
        const bool failed = current.complete && current.result != MASK_SUCCESS;
        if (failed && cameraButton->isChecked()) { setCameraButton(false); live = false; }
        const QString hint = failed ? hintFor(current) : QString();
        if (!notice.isEmpty()) setBanner(notice, "ok");
        else if (failed) setBanner(current.message + (hint.isEmpty() ? QString() : "\n" + hint), "error");
        else if (!current.complete) setBanner(current.message, "info");
        else if (current.auth != 0 && current.auth != -1) setBanner(current.message, "warn");
        else setBanner(live ? "Live detection running" : current.message == "Complete" ? "Detection complete" : current.message, "ok");
        sourceLabel->setText(current.source.isEmpty() ? QString() : live ? "Camera: " + current.source : "Image: " + QFileInfo(current.source).fileName());
        updateAuth();
        redraw();
        if (!newResults) return;
        shownResults = current.resultsVersion;
        const bool valid = current.complete && current.result == MASK_SUCCESS;
        save->setEnabled(valid);
        copy->setEnabled(valid);
        updateStats();
        // With --quit-after the screenshot is taken just before closing; otherwise once, after the first result.
        if (current.complete && !shared.options.screenshot.isEmpty() && !shared.options.quitAfter && !screenshotTaken) {
            screenshotTaken = true;
            saveScreenshot();
        }
    }
    void saveScreenshot() {
        QDir().mkpath(QFileInfo(shared.options.screenshot).absolutePath());
        grab().save(shared.options.screenshot);
    }
protected:
    void resizeEvent(QResizeEvent* event) override { QMainWindow::resizeEvent(event); redraw(); }
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (!imageFromMime(event->mimeData()).isEmpty()) event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        const QString path = imageFromMime(event->mimeData());
        if (!path.isEmpty()) openImage(path);
    }
public:
    explicit Window(const Options& options) {
        shared.options = options;
        live = options.source == Source::Camera;
        setWindowTitle("Mask Detection - Quectel Pi M2");
        setAcceptDrops(true);
        resize(1280, 800);
        setMinimumSize(1000, 640);
        auto* root = new QWidget;
        root->setObjectName("root");
        auto* rootLayout = new QHBoxLayout(root);
        rootLayout->setContentsMargins(12, 12, 12, 12);
        rootLayout->setSpacing(12);

        auto* sidebar = new QFrame;
        sidebar->setObjectName("panel");
        sidebar->setFixedWidth(260);
        auto* left = new QVBoxLayout(sidebar);
        left->setContentsMargins(16, 16, 16, 16);
        auto* brand = new QLabel("AI Playground");
        brand->setStyleSheet("font-size:18px;font-weight:700;");
        left->addWidget(brand);
        auto* platform = new QLabel("Quectel Pi M2 \xc2\xb7 RK3576 NPU");
        platform->setStyleSheet("color:#9a9ab0;");
        left->addWidget(platform);
        auto* algorithm = new QLabel("Mask Detection");
        algorithm->setStyleSheet("color:#59d69e;font-weight:600;padding-top:6px;");
        left->addWidget(algorithm);

        section(left, "Input");
        imageButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
        imageButton->setToolTip("Open a JPG / PNG / BMP image (Ctrl+O). You can also drag an image onto the window.");
        left->addWidget(imageButton);
        auto* cameraRow = new QHBoxLayout;
        cameras->setToolTip("USB camera to use. \"Auto\" picks the first USB capture device.");
        cameras->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        cameras->setMinimumContentsLength(10);
        auto* rescan = new QToolButton;
        rescan->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        rescan->setToolTip("Rescan USB cameras");
        cameraRow->addWidget(cameras, 1);
        cameraRow->addWidget(rescan);
        left->addLayout(cameraRow);
        cameraButton->setCheckable(true);
        cameraButton->setObjectName("primary");
        cameraButton->setToolTip("Start / stop live detection (Space)");
        left->addWidget(cameraButton);

        section(left, "Parameters");
        left->addWidget(new QLabel("Confidence threshold"));
        left->addWidget(thresholdRow(confidence, options.confidence,
            "Detections below this score are hidden. Raise it to reduce false alarms, lower it to find more faces."));
        left->addWidget(new QLabel("NMS IoU threshold"));
        left->addWidget(thresholdRow(nms, options.nms,
            "Overlap allowed between boxes. Lower it if one face gets several boxes."));
        auto* rotationRow = new QHBoxLayout;
        rotationRow->addWidget(new QLabel("Rotation"));
        for (const int angle : {0, 90, 180, 270}) rotation->addItem(QString("%1\xc2\xb0").arg(angle), angle);
        rotation->setCurrentIndex(rotation->findData(options.rotation));
        rotation->setToolTip("Rotate the input clockwise before detection");
        rotationRow->addWidget(rotation, 1);
        left->addLayout(rotationRow);
        labels->setChecked(true);
        left->addWidget(labels);
        auto* legend = new QLabel(QString("<span style='color:%1'>\xe2\x96\xa0</span> Correctly worn<br>"
            "<span style='color:%2'>\xe2\x96\xa0</span> Not worn<br><span style='color:%3'>\xe2\x96\xa0</span> Incorrectly worn")
            .arg(classInfo(0).color.name(), classInfo(1).color.name(), classInfo(2).color.name()));
        section(left, "Legend");
        left->addWidget(legend);
        left->addStretch();
        authBadge->setWordWrap(true);
        left->addWidget(authBadge);
        rootLayout->addWidget(sidebar);

        auto* center = new QVBoxLayout;
        center->setSpacing(10);
        auto* header = new QHBoxLayout;
        auto* title = new QLabel("Mask Detection");
        title->setStyleSheet("font-size:22px;font-weight:700;");
        header->addWidget(title);
        header->addStretch();
        sourceLabel->setStyleSheet("color:#9a9ab0;");
        header->addWidget(sourceLabel);
        center->addLayout(header);
        auto* chips = new QHBoxLayout;
        facesValue = chip(chips, "FACES", "Number of faces detected in the current frame");
        latencyValue = chip(chips, "INFERENCE", "Time spent in MaskEngine::inference()");
        fpsValue = chip(chips, "DETECT / PREVIEW FPS", "Live detection rate / camera preview rate");
        sizeValue = chip(chips, "RESOLUTION", "Size of the image sent to the SDK");
        center->addLayout(chips);
        preview->setMinimumSize(320, 240);
        preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        preview->setAlignment(Qt::AlignCenter);
        preview->setWordWrap(true);
        preview->setStyleSheet("background:#15151f;color:#7d7d92;border-radius:10px;font-size:14px;");
        center->addWidget(preview, 1);
        banner->setWordWrap(true);
        banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
        center->addWidget(banner);
        auto* buttons = new QHBoxLayout;
        rerunButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        rerunButton->setToolTip("Run detection on the current image again");
        save->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
        save->setToolTip(QString("Save annotated image, source image, TXT, JSON and LabelMe to \"%1\" (Ctrl+S)").arg(options.output));
        fullButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
        fullButton->setToolTip("Toggle full screen (F11, Esc to leave)");
        for (auto* button : {rerunButton, save, fullButton}) buttons->addWidget(button);
        center->addLayout(buttons);
        rootLayout->addLayout(center, 1);

        auto* results = new QFrame;
        results->setObjectName("panel");
        results->setFixedWidth(340);
        auto* right = new QVBoxLayout(results);
        right->setContentsMargins(16, 16, 16, 16);
        auto* resultsTitle = new QLabel("Results");
        resultsTitle->setStyleSheet("font-size:18px;font-weight:700;");
        right->addWidget(resultsTitle);
        auto* cards = new QHBoxLayout;
        for (int id = 0; id < 3; ++id) {
            const auto info = classInfo(id);
            auto* card = new QFrame;
            card->setObjectName("card");
            card->setStyleSheet(QString("QFrame#card{border-top:3px solid %1;}").arg(info.color.name()));
            auto* layout = new QVBoxLayout(card);
            layout->setContentsMargins(8, 6, 8, 6);
            layout->setSpacing(0);
            classCount[id] = new QLabel("0");
            classCount[id]->setStyleSheet(QString("font-size:24px;font-weight:700;color:%1;").arg(info.color.name()));
            classCount[id]->setAlignment(Qt::AlignCenter);
            auto* name = new QLabel(info.name);
            name->setWordWrap(true);
            name->setAlignment(Qt::AlignCenter);
            name->setStyleSheet("color:#9a9ab0;font-size:11px;");
            layout->addWidget(classCount[id]);
            layout->addWidget(name);
            cards->addWidget(card, 1);
        }
        right->addLayout(cards);
        section(right, "Mask compliance");
        compliance->setRange(0, 100);
        compliance->setValue(0);
        compliance->setFormat("No face detected");
        compliance->setFixedHeight(20);
        compliance->setToolTip("Share of detected faces wearing a mask correctly");
        right->addWidget(compliance);
        section(right, "Detections");
        table->setHorizontalHeaderLabels({"#", "Status", "Score"});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table->verticalHeader()->hide();
        table->setShowGrid(false);
        table->setAlternatingRowColors(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setToolTip("Click a row to highlight that face in the image; hover for box coordinates");
        right->addWidget(table, 1);
        copy->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
        copy->setToolTip("Copy the per-class summary and every detection as text");
        right->addWidget(copy);
        rootLayout->addWidget(results);
        setCentralWidget(root);

        setStyleSheet(
            "QWidget#root{background:#1e1e2a;}"
            "QWidget{color:#e8e8f0;font-size:13px;}"
            "QLabel{background:transparent;}"
            "QFrame#panel{background:#262636;border-radius:10px;}"
            "QFrame#chip,QFrame#card{background:#2f2f42;border-radius:8px;}"
            "QLabel#chipCaption{color:#9a9ab0;font-size:11px;}"
            "QLabel#chipValue{font-size:18px;font-weight:600;}"
            "QLabel#section{color:#8a8aa3;font-size:11px;font-weight:600;padding-top:12px;}"
            "QPushButton,QToolButton{background:#3a3a4e;border:1px solid #55556b;border-radius:6px;padding:8px 12px;}"
            "QPushButton:hover,QToolButton:hover{background:#4a4a63;}"
            "QPushButton:disabled{color:#6f6f80;background:#2c2c3c;border-color:#3a3a4e;}"
            "QPushButton#primary{background:#299b77;border-color:#35b98f;font-weight:600;}"
            "QPushButton#primary:hover{background:#31b08a;}"
            "QPushButton#primary:checked{background:#b5443f;border-color:#d0605a;}"
            "QComboBox,QDoubleSpinBox{background:#353547;border:1px solid #4a4a60;border-radius:5px;padding:4px 6px;}"
            "QComboBox QAbstractItemView{background:#353547;selection-background-color:#299b77;}"
            "QSlider::groove:horizontal{height:4px;background:#15151f;border-radius:2px;}"
            "QSlider::sub-page:horizontal{background:#299b77;border-radius:2px;}"
            "QSlider::handle:horizontal{background:#59d69e;width:14px;margin:-6px 0;border-radius:7px;}"
            "QTableWidget{background:#202030;alternate-background-color:#25253a;border:0;border-radius:6px;"
            "selection-background-color:#3b4f6b;}"
            "QHeaderView::section{background:#2f2f42;color:#9a9ab0;border:0;padding:5px;}"
            "QProgressBar{background:#15151f;border:0;border-radius:6px;text-align:center;color:#e8e8f0;}"
            "QProgressBar::chunk{background:#2fbf71;border-radius:6px;}"
            "QToolTip{background:#353547;color:#e8e8f0;border:1px solid #55556b;padding:4px;}");

        refreshCameras(options.camera);
        setCameraButton(live);
        rerunButton->setEnabled(options.source == Source::Image);
        save->setEnabled(false);
        copy->setEnabled(false);
        updateAuth();
        setBanner("Initializing SDK...", "info");
        redraw();
        settingsTimer.setSingleShot(true);
        settingsTimer.setInterval(250);

        connect(imageButton, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(this, "Open image", "images", "Images (*.png *.jpg *.jpeg *.bmp)");
            if (!path.isEmpty()) openImage(path);
        });
        connect(cameraButton, &QPushButton::toggled, this, [this](bool on) {
            setCameraButton(on);
            request(on ? Source::Camera : Source::Idle);
        });
        connect(rescan, &QToolButton::clicked, this, [this] {
            refreshCameras(cameras->currentData().toString());
            setBanner(QString("Found %1 USB camera(s)").arg(cameras->count() - 1), "info");
        });
        connect(cameras, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            if (cameraButton->isChecked()) request(Source::Camera);
        });
        connect(rotation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { settingsTimer.start(); });
        connect(&settingsTimer, &QTimer::timeout, this, [this] { applySettings(); });
        connect(rerunButton, &QPushButton::clicked, this, [this] { request(Source::Image); });
        connect(labels, &QCheckBox::toggled, this, [this] { redraw(); });
        connect(table, &QTableWidget::itemSelectionChanged, this, [this] {
            const auto rows = table->selectionModel()->selectedRows();
            highlight = rows.isEmpty() ? -1 : rows.first().row();
            redraw();
        });
        connect(copy, &QPushButton::clicked, this, [this] {
            QApplication::clipboard()->setText(summaryText(current));
            setBanner("Summary copied to clipboard", "ok");
        });
        connect(save, &QPushButton::clicked, this, [this] {
            const QString base = exportFrame(current, shared.options.output);
            if (base.isEmpty()) QMessageBox::warning(this, "Export failed", "Cannot write output files to " + shared.options.output);
            else {
                notice = "Saved: " + QFileInfo(base).absoluteFilePath() + ".{png, _source.png, txt, json, labelme.json}";
                setBanner(notice, "ok");
                QTimer::singleShot(4000, this, [this] { notice.clear(); });
            }
        });
        const auto toggleFull = [this] { isFullScreen() ? showNormal() : showFullScreen(); };
        connect(fullButton, &QPushButton::clicked, this, toggleFull);
        connect(new QShortcut(QKeySequence(Qt::Key_F11), this), &QShortcut::activated, this, toggleFull);
        connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, this, [this] { if (isFullScreen()) showNormal(); });
        connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, imageButton, &QPushButton::click);
        connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, [this] { if (save->isEnabled()) save->click(); });
        connect(new QShortcut(QKeySequence(Qt::Key_Space), this), &QShortcut::activated, cameraButton, &QPushButton::click);
        connect(&timer, &QTimer::timeout, this, [this] { updateFrame(); });
        timer.start(33);
        fpsClock.start();
        if (options.quitAfter) QTimer::singleShot(options.quitAfter * 1000, this, [this] {
            if (!shared.options.screenshot.isEmpty()) saveScreenshot();
            close();
        });
        thread = std::thread(worker, std::ref(shared));
        captureWorker = std::thread(captureThread, std::ref(shared));
    }
    ~Window() override {
        { std::lock_guard<std::mutex> lock(shared.mutex); shared.stop = true; }
        shared.wake.notify_all();
        shared.camWake.notify_all();
        if (thread.joinable()) thread.join();
        if (captureWorker.joinable()) captureWorker.join();
    }
};

static int selfTest(const QString& output) {
    Frame frame;
    frame.image = QImage(480, 240, QImage::Format_RGB888);
    frame.image.fill(Qt::white);
    frame.source = "synthetic-export-self-test (not SDK inference)";
    frame.result = MASK_SUCCESS;
    frame.complete = true;
    frame.auth = 0;
    frame.sequence = 1;
    frame.results.push_back({0, 1, 0.9f, 20, 30, 120, 140});
    frame.results.push_back({0, 2, 0.6f, 200, 40, 100, 120});
    const QString base = exportFrame(frame, QDir(output).filePath("selftest"));
    if (base.isEmpty()) return 5;
    QFile json(base + ".json"), labelme(base + ".labelme.json");
    if (!json.open(QIODevice::ReadOnly) || !labelme.open(QIODevice::ReadOnly)) return 5;
    const auto document = QJsonDocument::fromJson(json.readAll()).object();
    const auto annotations = QJsonDocument::fromJson(labelme.readAll()).object();
    const auto records = document.value("results").toArray();
    const auto box = records.at(0).toObject().value("box").toObject();
    if (records.size() != 2 || records.at(0).toObject().value("class_name").toString() != "not_worn" ||
        box.value("width").toInt() != 120 || annotations.value("shapes").toArray().size() != 2 ||
        QImage(base + "_source.png").size() != frame.image.size() || QImage(base + ".png").isNull() ||
        !QFileInfo(base + ".txt").size()) return 5;
    frame.result = MASK_INFERENCE_FAILED;
    if (!exportFrame(frame, output).isEmpty()) return 5;
    std::puts("[selftest] PASS: synthetic PNG / TXT / JSON / LabelMe / failure guard; no SDK inference");
    return 0;
}

static int headless(const Options& options) {
    Frame frame;
    Engine engine;
    if (!engine.init(options.confidence, options.nms, frame)) return 3;
    cv::VideoCapture capture;
    if (options.source == Source::Camera && !openCamera(capture, options.camera)) return 2;
    std::vector<double> timings;
    const int count = options.source == Source::Camera ? options.frames : options.repeat;
    for (int index = 0; index < count && !interrupted; ++index) {
        cv::Mat bgr;
        if (options.source == Source::Camera) capture.read(bgr);
        else bgr = cv::imread(options.image.toStdString(), cv::IMREAD_COLOR);
        if (bgr.empty()) return 2;
        frame.source = options.source == Source::Camera ? options.camera : options.image;
        frame.sequence = static_cast<unsigned long>(index + 1);
        frame.confidence = options.confidence;
        frame.nms = options.nms;
        frame.rotation = options.rotation;
        applyRotation(bgr, options.rotation);
        engine.infer(bgr, frame);
        if (frame.result != MASK_SUCCESS) return 4;
        timings.push_back(frame.milliseconds);
        if (options.source == Source::Camera || index == count - 1)
            if (exportFrame(frame, options.output).isEmpty()) return 5;
    }
    if (timings.empty()) return 130;
    double total = 0;
    for (const auto duration : timings) total += duration;
    std::printf("[benchmark] count=%zu mean_ms=%.2f min_ms=%.2f max_ms=%.2f\n", timings.size(), total / timings.size(),
        *std::min_element(timings.begin(), timings.end()), *std::max_element(timings.begin(), timings.end()));
    return interrupted ? 130 : 0;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    QApplication app(argc, argv);
    app.setApplicationName("maskdet_cam_demo");
    QCommandLineParser parser;
    parser.setApplicationDescription("Quectel Pi M2 RK3576 mask detection demo");
    parser.addHelpOption();
    parser.addOptions({{"headless", "Run without a window"},
        {"self-test", "Test export with synthetic data (no SDK inference)"},
        {"image", "Input image", "path", "images/input.jpg"}, {"camera", "USB camera device or auto", "device"},
        {"confidence", "Confidence threshold (0.01-0.99)", "value", "0.30"},
        {"nms", "NMS IoU threshold (0.01-0.99)", "value", "0.45"},
        {"rotate", "Clockwise rotation: 0,90,180,270", "degrees", "0"},
        {"frames", "Camera frame count in headless mode", "count", "1"},
        {"repeat", "Image benchmark repetitions", "count", "1"},
        {"output", "Result directory", "path", "outputs"},
        {"quit-after", "Close GUI after N seconds", "seconds", "0"},
        {"screenshot", "Save GUI screenshot after processing", "path"}});
    parser.process(app);
    Options options;
    options.image = parser.value("image");
    options.headless = parser.isSet("headless");
    options.output = parser.value("output");
    options.screenshot = parser.value("screenshot");
    if (parser.isSet("camera")) { options.source = Source::Camera; options.camera = parser.value("camera"); }
    bool validConfidence, validNms, validRotation, validFrames, validRepeat, validQuit;
    options.confidence = parser.value("confidence").toFloat(&validConfidence);
    options.nms = parser.value("nms").toFloat(&validNms);
    options.rotation = parser.value("rotate").toInt(&validRotation);
    options.frames = parser.value("frames").toInt(&validFrames);
    options.repeat = parser.value("repeat").toInt(&validRepeat);
    options.quitAfter = parser.value("quit-after").toInt(&validQuit);
    const auto inRange = [](bool valid, float value) { return valid && std::isfinite(value) && value >= 0.01f && value <= 0.99f; };
    if (!inRange(validConfidence, options.confidence) || !inRange(validNms, options.nms) ||
        !validRotation || (options.rotation != 0 && options.rotation != 90 && options.rotation != 180 && options.rotation != 270) ||
        !validFrames || options.frames < 1 || options.frames > 10000 || !validRepeat || options.repeat < 1 || options.repeat > 10000 ||
        !validQuit || options.quitAfter < 0 || options.quitAfter > 86400 || (parser.isSet("image") && parser.isSet("camera"))) {
        std::fprintf(stderr, "[error] Invalid options; use --help\n");
        return 2;
    }
    try {
        if (parser.isSet("self-test")) return selfTest(options.output);
        if (options.headless) return headless(options);
        Window window(options);
        window.show();
        return app.exec();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[error] %s\n", error.what());
        return 6;
    }
}
