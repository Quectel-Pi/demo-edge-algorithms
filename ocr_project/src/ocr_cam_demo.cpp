#include "deep_ocr.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <cstdlib>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdio>
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
    QString image = "images/111.png";
    QString camera = "auto";
    QString output = "outputs";
    float confidence = 0.2f;
    int rotation = 0;
    int frames = 1;
    int repeat = 1;
    int quitAfter = 0;
    bool headless = false;
    QString screenshot;
};
struct Frame {
    QImage image;
    std::vector<OCR_Rec_Result> results;
    QString source;
    QString message;
    double milliseconds = 0;
    int result = OCR_INIT_ERROR;
    int auth = -1;
    float confidence = 0.2f;
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
    cv::Mat pending;                     // latest live frame awaiting OCR (single slot)
    bool hasPending = false;
    std::vector<OCR_Rec_Result> overlay; // latest live OCR results, drawn on the preview
    double overlayMs = 0;
    int overlayAuth = -1;
    int overlayRc = OCR_PROCESS_OK;
    unsigned long overlayVersion = 0;
};

static QString errorText(int result) {
    switch (result) {
        case OCR_PROCESS_OK: return "Complete";
        case OCR_INIT_ERROR: return "SDK initialization failed";
        case OCR_UNINIT_ERROR: return "SDK cleanup failed";
        case OCR_ERR_INFERENCE_ERROR: return "SDK inference failed (null result / execution error)";
        case OCR_ERR_IMAGE_EMPTY: return "Image is empty / camera unavailable";
        case OCR_ERR_JSON_NULL: return "SDK returned null JSON";
        case OCR_ERR_JSON_PARSE: return "SDK JSON parsing failed";
        default: return "SDK / input error";
    }
}

static QStringList cameraDevices() {
    QStringList devices;
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
            std::strstr(reinterpret_cast<const char*>(capability.bus_info), "usb")) devices.append(path);
    }
    std::sort(devices.begin(), devices.end(), [](const QString& first, const QString& second) {
        return first.mid(10).toInt() < second.mid(10).toInt();
    });
    return devices;
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

class Engine {
    DeepOcr sdk;
    bool attempted = false;
public:
    ~Engine() { if (attempted) sdk.uinit(); }
    bool init(float confidence, Frame& frame) {
        std::string model = "models/sim.engine";
        std::string license = std::getenv("OCR_LICENSE_FILE") ? std::getenv("OCR_LICENSE_FILE") : "license/ocr.lic";
        std::string auth = std::getenv("OCR_AUTH_CODE") ? std::getenv("OCR_AUTH_CODE") : "";
        if (!QDir().mkpath(QFileInfo(QString::fromStdString(license)).absolutePath()))
            throw std::runtime_error("Cannot create license directory");
        sdk.set_param(confidence);
        attempted = true;
        frame.result = sdk.init(model.data(), license.data(), 3, auth.data());
        frame.auth = sdk.get_status(1);
        const int status = sdk.get_status(0);
        std::printf("[init] rc=%d status=%d auth=%d confidence=%.3f\n", frame.result, status, frame.auth, confidence);
        if (frame.result != OCR_PROCESS_OK || status != 0 || frame.auth != 0) {
            frame.result = OCR_INIT_ERROR;
            frame.message = QString("SDK initialization failed: model=%1, auth=%2").arg(status).arg(frame.auth);
            return false;
        }
        return true;
    }
    int run(const cv::Mat& src, std::vector<OCR_Rec_Result>& results, double& milliseconds, int& auth) {
        cv::Mat bgr = src.isContinuous() ? src : src.clone();
        results.clear();
        const auto start = std::chrono::steady_clock::now();
        int result = sdk.inference(bgr.data, bgr.rows, bgr.cols, bgr.channels(), results);
        milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        auth = sdk.get_status(1);
        if (auth != 0 && result == OCR_PROCESS_OK) result = OCR_ERR_INFERENCE_ERROR;
        if (result != OCR_PROCESS_OK) results.clear();
        return result;
    }
    void infer(cv::Mat& bgr, Frame& frame) {
        frame.result = run(bgr, frame.results, frame.milliseconds, frame.auth);
        frame.image = toImage(bgr);
        frame.complete = true;
        frame.message = errorText(frame.result);
        std::printf("[frame] rc=%d texts=%zu inference_ms=%.2f auth=%d width=%d height=%d\n",
            frame.result, frame.results.size(), frame.milliseconds, frame.auth, bgr.cols, bgr.rows);
        for (const auto& item : frame.results) std::printf("[text] %s\n", item.text.c_str());
    }
};

static QImage annotated(const Frame& frame, bool labels = true) {
    QImage image = frame.image.copy();
    if (image.isNull()) return image;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(QFont("Noto Sans CJK SC", 12));
    int index = 0;
    for (const auto& item : frame.results) {
        ++index;
        QPolygon points;
        for (const auto& point : item.box) if (point.size() >= 2) points.append(QPoint(point[0], point[1]));
        if (points.size() < 3) continue;
        painter.setPen(QPen(QColor("#119574"), 2));
        painter.setBrush(QColor(17, 149, 116, 25));
        painter.drawPolygon(points);
        if (!labels) continue;
        const QString title = QString("%1: %2").arg(index).arg(QString::fromStdString(item.text));
        const QString text = painter.fontMetrics().elidedText(title, Qt::ElideRight, std::max(1, image.width() - 12));
        const int width = std::min(image.width(), painter.fontMetrics().horizontalAdvance(text) + 8);
        const int height = painter.fontMetrics().height() + 6;
        const int left = std::clamp(points.boundingRect().left(), 0, std::max(0, image.width() - width));
        const int top = std::clamp(points.boundingRect().top() - height, 0, std::max(0, image.height() - height));
        painter.fillRect(QRect(left, top, width, height), QColor("#ffffff"));
        painter.setPen(QColor("#153f32"));
        painter.drawText(left + 4, top + painter.fontMetrics().ascent() + 3, text);
    }
    return image;
}

static QString plainText(const Frame& frame) {
    QStringList lines;
    for (const auto& item : frame.results) lines.append(QString::fromStdString(item.text));
    return lines.join('\n');
}

static bool writeFile(const QString& path, const QByteArray& data) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

static QString exportFrame(const Frame& frame, const QString& directory) {
    if (!frame.complete || frame.result != OCR_PROCESS_OK || frame.image.isNull()) return {};
    if (!QDir().mkpath(directory)) return {};
    const QString name = "ocr_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + "_" + QString::number(frame.sequence);
    QDir output(directory);
    QJsonArray records, shapes;
    int index = 0;
    for (const auto& item : frame.results) {
        QJsonArray points;
        for (const auto& point : item.box) if (point.size() >= 2) points.append(QJsonArray{point[0], point[1]});
        const QString text = QString::fromStdString(item.text);
        records.append(QJsonObject{{"index", ++index}, {"text", text}, {"box", points}});
        shapes.append(QJsonObject{{"label", text}, {"points", points}, {"group_id", QJsonValue::Null},
            {"shape_type", "polygon"}, {"flags", QJsonObject{}}});
    }
    QJsonObject result{{"source", frame.source}, {"result_code", frame.result}, {"auth_status", frame.auth},
        {"width", frame.image.width()}, {"height", frame.image.height()}, {"rotation", frame.rotation},
        {"detection_threshold", frame.confidence}, {"inference_ms", frame.milliseconds}, {"results", records}};
    QJsonObject labelme{{"version", "5.0.1"}, {"flags", QJsonObject{}}, {"shapes", shapes},
        {"imagePath", name + "_source.png"}, {"imageData", QJsonValue::Null},
        {"imageHeight", frame.image.height()}, {"imageWidth", frame.image.width()}};
    const QStringList paths{output.filePath(name + ".png"), output.filePath(name + "_source.png"),
        output.filePath(name + ".txt"), output.filePath(name + ".json"), output.filePath(name + ".labelme.json")};
    const bool saved = annotated(frame).save(paths[0], "PNG") && frame.image.save(paths[1], "PNG") &&
        writeFile(paths[2], plainText(frame).toUtf8()) && writeFile(paths[3], QJsonDocument(result).toJson()) &&
        writeFile(paths[4], QJsonDocument(labelme).toJson());
    if (!saved) {
        for (const auto& path : paths) QFile::remove(path);
        return {};
    }
    std::printf("[export] base=%s\n", output.filePath(name).toUtf8().constData());
    return output.filePath(name);
}

// Inference thread: owns the SDK; handles image/idle one-shots and asynchronous live OCR.
static void worker(Shared& shared) {
    unsigned long activeRevision = 0;
    std::unique_ptr<Engine> engine;
    float activeConfidence = -1;
    bool live = false;
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
        live = options.source == Source::Camera;
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
            if (!engine || activeConfidence != options.confidence) {
                engine.reset();
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
                if (!engine->init(options.confidence, frame)) {
                    engine.reset();
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    shared.liveStop = true;
                    frame.resultsVersion = ++shared.overlayVersion;
                    frame.sequence = shared.frame.sequence + 1;
                    shared.frame = std::move(frame);
                    shared.camWake.notify_all();
                    continue;
                }
                activeConfidence = options.confidence;
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.overlay.clear();
                shared.overlayMs = 0;
                shared.overlayAuth = 0;
                shared.overlayRc = OCR_PROCESS_OK;
            }
            if (live) {
                if (!haveJob) continue;
                std::vector<OCR_Rec_Result> results;
                double milliseconds = 0;
                int auth = -1;
                const int rc = engine->run(job, results, milliseconds, auth);
                std::printf("[frame] rc=%d texts=%zu inference_ms=%.2f auth=%d width=%d height=%d\n",
                    rc, results.size(), milliseconds, auth, job.cols, job.rows);
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.overlay = rc == OCR_PROCESS_OK ? std::move(results) : std::vector<OCR_Rec_Result>{};
                shared.overlayMs = milliseconds;
                shared.overlayAuth = auth;
                shared.overlayRc = rc;
                ++shared.overlayVersion;
                if (rc != OCR_PROCESS_OK) {
                    shared.liveStop = true;
                    Frame frame;
                    frame.revision = revision;
                    frame.source = options.camera;
                    frame.result = rc;
                    frame.complete = true;
                    frame.message = errorText(rc);
                    frame.resultsVersion = shared.overlayVersion;
                    frame.sequence = shared.frame.sequence + 1;
                    shared.frame = std::move(frame);
                    shared.camWake.notify_all();
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
            frame.result = OCR_ERR_INFERENCE_ERROR;
            frame.complete = true;
            frame.message = QString::fromLocal8Bit(error.what());
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.liveStop = true;
            frame.resultsVersion = ++shared.overlayVersion;
            frame.sequence = shared.frame.sequence + 1;
            shared.frame = std::move(frame);
            shared.camWake.notify_all();
        }
    }
}

// Capture thread: owns the camera; streams preview frames at camera rate and feeds the latest frame to OCR.
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
        if (!capture.isOpened() && !openCamera(capture, options.camera)) {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.liveStop = true;
            Frame frame;
            frame.revision = activeRevision;
            frame.source = options.camera;
            frame.result = OCR_ERR_IMAGE_EMPTY;
            frame.complete = true;
            frame.message = "No accessible USB camera; select /dev/videoN";
            frame.resultsVersion = ++shared.overlayVersion;
            frame.sequence = shared.frame.sequence + 1;
            shared.frame = std::move(frame);
            shared.wake.notify_all();
            continue;
        }
        cv::Mat bgr;
        if (!capture.read(bgr) || bgr.empty()) {
            capture.release();
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.liveStop = true;
            Frame frame;
            frame.revision = activeRevision;
            frame.source = options.camera;
            frame.result = OCR_ERR_IMAGE_EMPTY;
            frame.complete = true;
            frame.message = "Camera disconnected / capture failed";
            frame.resultsVersion = ++shared.overlayVersion;
            frame.sequence = shared.frame.sequence + 1;
            shared.frame = std::move(frame);
            shared.wake.notify_all();
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
        frame.rotation = options.rotation;
        frame.complete = true;
        frame.message = "Live";
        frame.resultsVersion = shared.overlayVersion;
        frame.sequence = shared.frame.sequence + 1;
        shared.frame = std::move(frame);
        shared.wake.notify_all();
    }
    capture.release();
}

class Window : public QMainWindow {
    Shared shared;
    std::thread thread;
    std::thread captureWorker;
    Frame current;
    QLabel* preview = new QLabel;
    QLabel* status = new QLabel("Initializing SDK");
    QLabel* metrics = new QLabel;
    QTableWidget* table = new QTableWidget(0, 2);
    QTextEdit* text = new QTextEdit;
    QDoubleSpinBox* confidence = new QDoubleSpinBox;
    QComboBox* rotation = new QComboBox;
    QComboBox* cameras = new QComboBox;
    QCheckBox* labels = new QCheckBox("Text labels");
    QPushButton* save = new QPushButton("Export");
    QPushButton* copy = new QPushButton("Copy text");
    QTimer timer;
    unsigned long sequence = 0;
    unsigned long shownResults = 0;
    QString shownMessage;
    void request(Source source) {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.options.source = source;
        shared.options.confidence = static_cast<float>(confidence->value());
        shared.options.rotation = rotation->currentData().toInt();
        shared.options.camera = cameras->currentText();
        shared.liveStop = false;
        shared.hasPending = false;
        shared.overlay.clear();
        shared.overlayMs = 0;
        shared.overlayAuth = 0;
        shared.overlayRc = OCR_PROCESS_OK;
        ++shared.revision;
        save->setEnabled(false);
        copy->setEnabled(false);
        status->setText(source == Source::Idle ? "Stopping" : "Processing");
        shared.wake.notify_all();
        shared.camWake.notify_all();
    }
    void redraw() {
        if (current.image.isNull()) { preview->clear(); return; }
        preview->setPixmap(QPixmap::fromImage(annotated(current, labels->isChecked()))
            .scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    void updateFrame() {
        if (interrupted) { close(); return; }
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            if (sequence == shared.frame.sequence) return;
            sequence = shared.frame.sequence;
            if (shared.frame.revision != shared.revision) return;
            current = shared.frame;
        }
        if (current.message != shownMessage) {
            shownMessage = current.message;
            status->setText(current.message);
            status->setStyleSheet(current.complete && current.result != 0 ? "color:#b52b35" : "color:#246b55");
        }
        redraw();
        if (shownResults == current.resultsVersion) return;
        shownResults = current.resultsVersion;
        metrics->setText(QString("%1 regions  |  %2 ms  |  auth %3  |  rc %4")
            .arg(current.results.size()).arg(current.milliseconds, 0, 'f', 1).arg(current.auth).arg(current.result));
        const bool valid = current.complete && current.result == OCR_PROCESS_OK;
        save->setEnabled(valid);
        copy->setEnabled(valid && !current.results.empty());
        table->setRowCount(static_cast<int>(current.results.size()));
        int index = 0;
        for (const auto& item : current.results) {
            table->setItem(index, 0, new QTableWidgetItem(QString::number(index + 1)));
            table->setItem(index, 1, new QTableWidgetItem(QString::fromStdString(item.text)));
            ++index;
        }
        text->setPlainText(plainText(current));
        if (current.complete && !shared.options.screenshot.isEmpty()) {
            QDir().mkpath(QFileInfo(shared.options.screenshot).absolutePath());
            grab().save(shared.options.screenshot);
        }
    }
protected:
    void resizeEvent(QResizeEvent* event) override { QMainWindow::resizeEvent(event); redraw(); }
public:
    explicit Window(const Options& options) {
        shared.options = options;
        setWindowTitle("Quectel Pi M2 - OCR Studio");
        resize(1100, 720);
        auto* center = new QWidget;
        auto* layout = new QVBoxLayout(center);
        auto* toolbar = new QHBoxLayout;
        auto* title = new QLabel("OCR / Quectel Pi M2");
        title->setStyleSheet("font-size:20px;font-weight:600;color:#153f32");
        auto* open = new QPushButton("Image");
        open->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
        auto* live = new QPushButton("Camera");
        live->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        auto* stop = new QPushButton;
        stop->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
        stop->setToolTip("Stop camera");
        auto* rerun = new QPushButton;
        rerun->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        rerun->setToolTip("Recognize again / apply settings");
        save->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
        copy->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
        toolbar->addWidget(title);
        toolbar->addStretch();
        for (auto* button : {open, live, stop, rerun, copy, save}) toolbar->addWidget(button);
        layout->addLayout(toolbar);
        auto* settings = new QHBoxLayout;
        cameras->setEditable(true);
        cameras->addItem("auto");
        cameras->addItems(cameraDevices());
        cameras->setCurrentText(options.camera);
        cameras->setMinimumContentsLength(12);
        confidence->setRange(0.01, 0.99);
        confidence->setSingleStep(0.05);
        confidence->setDecimals(2);
        confidence->setValue(options.confidence);
        for (const int angle : {0, 90, 180, 270}) rotation->addItem(QString::number(angle), angle);
        rotation->setCurrentIndex(rotation->findData(options.rotation));
        labels->setChecked(true);
        settings->addWidget(new QLabel("Camera"));
        settings->addWidget(cameras);
        settings->addWidget(new QLabel("Detection threshold"));
        settings->addWidget(confidence);
        settings->addWidget(new QLabel("Rotation"));
        settings->addWidget(rotation);
        settings->addWidget(labels);
        settings->addStretch();
        layout->addLayout(settings);
        auto* splitter = new QSplitter;
        preview->setMinimumSize(240, 200);
        preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        preview->setAlignment(Qt::AlignCenter);
        preview->setStyleSheet("background:#e5e9e7;border:1px solid #ced8d2");
        splitter->addWidget(preview);
        auto* panel = new QWidget;
        auto* resultLayout = new QVBoxLayout(panel);
        resultLayout->setContentsMargins(8, 0, 0, 0);
        resultLayout->addWidget(new QLabel("Recognized Text"));
        table->setHorizontalHeaderLabels({"#", "Text"});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->verticalHeader()->hide();
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        resultLayout->addWidget(table, 2);
        text->setReadOnly(true);
        resultLayout->addWidget(text, 1);
        splitter->addWidget(panel);
        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 2);
        splitter->setSizes({650, 420});
        layout->addWidget(splitter, 1);
        status->setWordWrap(true);
        layout->addWidget(status);
        layout->addWidget(metrics);
        setCentralWidget(center);
        setStyleSheet("QMainWindow{background:#f5f7f6} QWidget{font-family:'Noto Sans CJK SC';font-size:13px} "
            "QPushButton{padding:7px 10px} QTableWidget,QTextEdit{background:white;border:1px solid #ced8d2}");
        save->setEnabled(false);
        copy->setEnabled(false);
        connect(open, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(this, "Open image", "images", "Images (*.png *.jpg *.jpeg *.bmp)");
            if (path.isEmpty()) return;
            { std::lock_guard<std::mutex> lock(shared.mutex); shared.options.image = path; }
            request(Source::Image);
        });
        connect(live, &QPushButton::clicked, this, [this] { request(Source::Camera); });
        connect(stop, &QPushButton::clicked, this, [this] { request(Source::Idle); });
        connect(rerun, &QPushButton::clicked, this, [this] {
            request(shared.options.source == Source::Idle ? Source::Image : shared.options.source);
        });
        connect(labels, &QCheckBox::toggled, this, [this] { redraw(); });
        connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(plainText(current)); });
        connect(save, &QPushButton::clicked, this, [this] {
            const QString base = exportFrame(current, shared.options.output);
            if (base.isEmpty()) QMessageBox::warning(this, "Export failed", "Cannot write output files");
            else status->setText("Exported: " + base);
        });
        connect(&timer, &QTimer::timeout, this, [this] { updateFrame(); });
        timer.start(33);
        if (options.quitAfter) QTimer::singleShot(options.quitAfter * 1000, this, &QWidget::close);
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
    frame.result = OCR_PROCESS_OK;
    frame.complete = true;
    frame.auth = 0;
    frame.sequence = 1;
    OCR_Rec_Result item;
    item.box = {{20, 30}, {420, 30}, {420, 100}, {20, 100}};
    item.text = "OCR \"test\" \\ UTF-8: \xe4\xb8\xad\xe6\x96\x87";
    frame.results.push_back(item);
    const QString base = exportFrame(frame, QDir(output).filePath("selftest"));
    if (base.isEmpty()) return 5;
    QFile json(base + ".json"), labelme(base + ".labelme.json"), text(base + ".txt");
    if (!json.open(QIODevice::ReadOnly) || !labelme.open(QIODevice::ReadOnly) || !text.open(QIODevice::ReadOnly)) return 5;
    const auto document = QJsonDocument::fromJson(json.readAll()).object();
    const auto annotations = QJsonDocument::fromJson(labelme.readAll()).object();
    const auto record = document.value("results").toArray().at(0).toObject();
    if (record.value("text").toString().toUtf8() != QByteArray::fromStdString(item.text) ||
        record.value("box").toArray().size() != 4 || text.readAll() != QByteArray::fromStdString(item.text) ||
        annotations.value("shapes").toArray().size() != 1 ||
        QImage(base + "_source.png").size() != frame.image.size() || QImage(base + ".png").isNull()) return 5;
    frame.result = OCR_ERR_INFERENCE_ERROR;
    if (!exportFrame(frame, output).isEmpty()) return 5;
    std::puts("[selftest] PASS: synthetic PNG / UTF-8 TXT / JSON / LabelMe / failure guard; no SDK inference");
    return 0;
}

static int headless(const Options& options) {
    Frame frame;
    Engine engine;
    if (!engine.init(options.confidence, frame)) return 3;
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
        frame.rotation = options.rotation;
        applyRotation(bgr, options.rotation);
        engine.infer(bgr, frame);
        if (frame.result != OCR_PROCESS_OK) return 4;
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
    app.setApplicationName("ocr_cam_demo");
    QCommandLineParser parser;
    parser.setApplicationDescription("Quectel Pi M2 RK3576 OCR demo");
    parser.addHelpOption();
    parser.addOptions({{"headless", "Run without a window"},
        {"self-test", "Test export with synthetic data (no SDK inference)"},
        {"image", "Input image", "path", "images/111.png"}, {"camera", "USB camera device or auto", "device"},
        {"confidence", "Detection threshold (0.01-0.99)", "value", "0.2"},
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
    bool validConfidence, validRotation, validFrames, validRepeat, validQuit;
    options.confidence = parser.value("confidence").toFloat(&validConfidence);
    options.rotation = parser.value("rotate").toInt(&validRotation);
    options.frames = parser.value("frames").toInt(&validFrames);
    options.repeat = parser.value("repeat").toInt(&validRepeat);
    options.quitAfter = parser.value("quit-after").toInt(&validQuit);
    if (!validConfidence || !std::isfinite(options.confidence) || options.confidence < 0.01f || options.confidence > 0.99f ||
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
