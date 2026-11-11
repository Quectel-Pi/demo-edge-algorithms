#include "human_segmentation.h"
#include <QtWidgets>
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
#include <thread>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

static volatile std::sig_atomic_t interrupted = 0;
static void onSignal(int) { interrupted = 1; }
enum class Source { Idle, Image, Camera, Video };
enum class View { Contours, Cutout, Original };
struct Options {
    Source source = Source::Image;
    QString image = "images/input.jpg", camera = "auto", video, output = "outputs", screenshot;
    float confidence = 0.20f;
    int rotation = 0, frames = 1, repeat = 1, quitAfter = 0;
    bool headless = false;
};
struct Frame {
    QImage image;
    std::vector<SegRes> results;
    QString source, message = "Waiting";
    double milliseconds = 0;
    int result = HUMAN_SEGMENTATION_INIT_ERROR, sdkResult = HUMAN_SEGMENTATION_INIT_ERROR, auth = -1, rotation = 0;
    float confidence = 0.20f;
    unsigned long revision = 0, sequence = 0, version = 0;
    bool complete = false, ended = false;
};
struct Shared {
    std::mutex mutex;
    std::condition_variable wake;
    Options options;
    Frame frame, overlay;
    cv::Mat pending;
    unsigned long revision = 1, version = 0;
    bool stop = false, failed = false, hasPending = false, inputEnded = false, inFlight = false;
};
static bool isLive(Source source) {
    return source == Source::Camera || source == Source::Video;
}
static QString sourceName(const Options& options) {
    return options.source == Source::Video ? options.video : options.source == Source::Camera ? options.camera : options.image;
}
static bool isVideoPath(const QString& path) {
    return QStringList{"mp4", "avi", "mkv", "mov", "webm", "m4v"}.contains(QFileInfo(path).suffix().toLower());
}
static QString mediaFromMime(const QMimeData* mime) {
    if (!mime || !mime->hasUrls()) return {};
    for (const auto& url : mime->urls()) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty() && (isVideoPath(path) || QStringList{"jpg", "jpeg", "png", "bmp"}.contains(QFileInfo(path).suffix().toLower())))
            return path;
    }
    return {};
}
struct Camera { QString path, name; };
static std::vector<Camera> cameras() {
    std::vector<Camera> found;
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
            found.push_back({path, QString::fromUtf8(reinterpret_cast<const char*>(capability.card))});
    }
    std::sort(found.begin(), found.end(), [](const Camera& first, const Camera& second) {
        return first.path.mid(10).toInt() < second.path.mid(10).toInt();
    });
    return found;
}
static bool openCamera(cv::VideoCapture& capture, const QString& device) {
    std::vector<Camera> candidates = device == "auto" ? cameras() : std::vector<Camera>{{device, device}};
    for (const auto& candidate : candidates) {
        if (!capture.open(candidate.path.toStdString(), cv::CAP_V4L2)) continue;
        capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
        std::printf("[camera] %s\n", candidate.path.toUtf8().constData());
        return true;
    }
    return false;
}
static void rotate(cv::Mat& image, int angle) {
    if (angle == 90) cv::rotate(image, image, cv::ROTATE_90_CLOCKWISE);
    else if (angle == 180) cv::rotate(image, image, cv::ROTATE_180);
    else if (angle == 270) cv::rotate(image, image, cv::ROTATE_90_COUNTERCLOCKWISE);
}
static QImage toImage(const cv::Mat& bgr) {
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}
static QString errorText(int code) {
    switch (code) {
        case 0: return "Complete";
        case -1: return "SDK initialization failed";
        case -2: return "SDK uninitialization failed";
        case -3: return "SDK inference failed";
        case -4: return "Image empty / camera unavailable";
        case -5: return "SDK JSON is null";
        case -6: return "SDK JSON parse failed";
        case -7: return "SDK extraction failed";
        default: return QString("SDK error (%1)").arg(code);
    }
}
static QString authText(int auth) {
    return auth == 0 ? "Licensed" : auth == -1 ? "Authorization pending" : QString("Authorization status: %1").arg(auth);
}
static QPolygon polygon(const SegRes& item) {
    QPolygon points;
    for (const auto& point : item.contour) points.append(QPoint(point.x, point.y));
    return points;
}
static QColor colorFor(int index) {
    static const QColor colors[]{QColor("#4ddd9a"), QColor("#f5c451"), QColor("#5bc8ed"), QColor("#ef849c")};
    return colors[index % 4];
}
static void normalizeResults(std::vector<SegRes>& results) {
    for (auto& item : results) {
        item.label_id = 1;
        item.label = "human";
        item.score = -1;
    }
}
static void finishInference(Frame& frame, int sdkResult) {
    frame.sdkResult = sdkResult;
    frame.result = sdkResult == HUMAN_SEGMENTATION_ERR_EXTRACTION_ERROR && frame.results.empty() ? 0 : sdkResult;
    if (frame.result != 0) frame.results.clear();
    else normalizeResults(frame.results);
    frame.complete = true;
    frame.message = frame.result == 0 && frame.results.empty() ? "No foreground contour" : errorText(frame.result);
}
static QString scoreText(double score) {
    return score < 0 ? "N/A" : QString::number(score, 'f', 3);
}
static QRegion foregroundRegion(const Frame& frame) {
    QRegion region;
    for (const auto& item : frame.results)
        if (item.label_id == 1 && item.contour.size() >= 3) region |= QRegion(polygon(item));
    return region.intersected(QRegion(frame.image.rect()));
}
static double coverage(const Frame& frame) {
    if (frame.image.isNull()) return 0;
    const QRegion region = foregroundRegion(frame);
    qint64 pixels = 0;
    for (const auto& rectangle : region) pixels += qint64(rectangle.width()) * rectangle.height();
    return 100.0 * pixels / (qint64(frame.image.width()) * frame.image.height());
}
static QImage rendered(const Frame& frame, View view = View::Contours, bool labels = true, int highlight = -1) {
    if (frame.image.isNull()) return {};
    if (view == View::Original) return frame.image;
    QImage image = frame.image.convertToFormat(QImage::Format_RGB32);
    if (view == View::Cutout) {
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setClipRegion(foregroundRegion(frame));
        painter.drawImage(0, 0, frame.image);
        return image;
    }
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font;
    font.setPixelSize(std::max(12, std::min(image.width(), image.height()) / 40));
    painter.setFont(font);
    const int penWidth = std::max(2, std::min(image.width(), image.height()) / 240);
    for (size_t index = 0; index < frame.results.size(); ++index) {
        const auto& item = frame.results[index];
        if (item.contour.size() < 3) continue;
        QColor color = colorFor(static_cast<int>(index));
        painter.setPen(QPen(color, static_cast<int>(index) == highlight ? penWidth * 2 : penWidth));
        color.setAlpha(static_cast<int>(index) == highlight ? 80 : 24);
        painter.setBrush(color);
        painter.drawPolygon(polygon(item));
    }
    if (labels) for (size_t index = 0; index < frame.results.size(); ++index) {
        const auto& item = frame.results[index];
        if (item.contour.empty()) continue;
        const QString text = QString("#%1 %2").arg(index + 1).arg(QString::fromStdString(item.label));
        const int width = std::min(image.width(), painter.fontMetrics().horizontalAdvance(text) + 10);
        const int height = painter.fontMetrics().height() + 6;
        const QRect bounds = polygon(item).boundingRect();
        const int left = std::clamp(bounds.left(), 0, std::max(0, image.width() - width));
        const int top = std::clamp(bounds.top() - height, 0, std::max(0, image.height() - height));
        painter.fillRect(QRect(left, top, width, height), colorFor(static_cast<int>(index)));
        painter.setPen(QColor("#111817"));
        painter.drawText(QRect(left + 4, top, width - 8, height), Qt::AlignVCenter, text);
    }
    return image;
}
static QString summary(const Frame& frame) {
    QStringList lines{QString("Contours: %1\nForeground: %2%\nInference: %3 ms\n%4")
        .arg(frame.results.size()).arg(coverage(frame), 0, 'f', 2).arg(frame.milliseconds, 0, 'f', 2).arg(authText(frame.auth))};
    int index = 0;
    for (const auto& item : frame.results)
        lines.append(QString("%1. label_id=%2 label=%3 score=%4 area=%5 points=%6")
            .arg(++index).arg(item.label_id).arg(QString::fromStdString(item.label)).arg(scoreText(item.score)).arg(item.area).arg(item.contour.size()));
    return lines.join('\n');
}
static bool writeFile(const QString& path, const QByteArray& data) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
static QByteArray png(const QImage& image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return bytes;
}
static QString exportFrame(const Frame& frame, const QString& directory) {
    if (!frame.complete || frame.result != 0 || frame.image.isNull() || !QDir().mkpath(directory)) return {};
    const QString name = "human_segmentation_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + "_" + QString::number(frame.sequence);
    QJsonArray records, shapes;
    for (const auto& item : frame.results) {
        QJsonArray points;
        for (const auto& point : item.contour) points.append(QJsonArray{point.x, point.y});
        records.append(QJsonObject{{"label_id", item.label_id}, {"label", QString::fromStdString(item.label)},
            {"score", item.score < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(item.score)}, {"area", item.area}, {"contour", points}});
        if (item.contour.size() >= 3) shapes.append(QJsonObject{{"label", QString::fromStdString(item.label)},
            {"points", points}, {"shape_type", "polygon"}, {"group_id", QJsonValue::Null}, {"flags", QJsonObject{}}});
    }
    QJsonObject object{{"source", frame.source}, {"result_code", frame.result}, {"sdk_result_code", frame.sdkResult}, {"auth_status", frame.auth},
        {"width", frame.image.width()}, {"height", frame.image.height()}, {"rotation", frame.rotation},
        {"confidence_threshold", frame.confidence}, {"inference_ms", frame.milliseconds},
        {"foreground_percent", coverage(frame)}, {"results", records}};
    QJsonObject labelme{{"version", "5.0.1"}, {"flags", QJsonObject{}}, {"shapes", shapes},
        {"imagePath", name + "_source.png"}, {"imageData", QJsonValue::Null},
        {"imageHeight", frame.image.height()}, {"imageWidth", frame.image.width()}};
    const QString base = QDir(directory).filePath(name);
    const std::vector<std::pair<QString, QByteArray>> files{
        {base + ".png", png(rendered(frame))}, {base + "_source.png", png(frame.image)},
        {base + "_cutout.png", png(rendered(frame, View::Cutout))},
        {base + ".txt", summary(frame).toUtf8()}, {base + ".json", QJsonDocument(object).toJson()},
        {base + ".labelme.json", QJsonDocument(labelme).toJson()}};
    for (const auto& file : files) {
        if (!file.second.isEmpty() && writeFile(file.first, file.second)) continue;
        for (const auto& cleanup : files) QFile::remove(cleanup.first);
        return {};
    }
    std::printf("[export] %s\n", base.toUtf8().constData());
    return base;
}
class Engine {
    std::unique_ptr<HumanSegmentation> sdk;
    float threshold = -1;
public:
    ~Engine() { if (sdk) sdk->uinit(); }
    bool configure(float confidence, Frame& frame) {
        if (sdk && confidence == threshold) return true;
        if (sdk) { sdk->uinit(); sdk.reset(); }
        threshold = -1;
        sdk = std::make_unique<HumanSegmentation>();
        std::string model = "models/sim.engine";
        std::string license = std::getenv("HUMAN_SEG_LICENSE_FILE") ? std::getenv("HUMAN_SEG_LICENSE_FILE") : "license/human_segmentation.lic";
        std::string auth = std::getenv("HUMAN_SEG_AUTH_CODE") ? std::getenv("HUMAN_SEG_AUTH_CODE") : "";
        if (!QDir().mkpath(QFileInfo(QString::fromStdString(license)).absolutePath())) throw std::runtime_error("Cannot create license directory");
        sdk->set_param(confidence);
        frame.result = sdk->init(model.data(), license.data(), 3, auth.data());
        const int status = sdk->get_status(0);
        frame.auth = sdk->get_status(1);
        std::printf("[init] rc=%d status=%d auth=%d\n", frame.result, status, frame.auth);
        if (frame.result != 0 || status != 0) {
            frame.result = frame.result ? frame.result : HUMAN_SEGMENTATION_INIT_ERROR;
            frame.message = QString("SDK initialization failed: rc=%1, status=%2, auth=%3").arg(frame.result).arg(status).arg(frame.auth);
            return false;
        }
        sdk->set_param(confidence);
        threshold = confidence;
        return true;
    }
    void infer(const cv::Mat& input, Frame& frame) {
        cv::Mat bgr = input.isContinuous() ? input : input.clone();
        frame.results.clear();
        const auto start = std::chrono::steady_clock::now();
        const int sdkResult = sdk->inference(bgr.data, bgr.rows, bgr.cols, bgr.channels(), frame.results);
        frame.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        frame.auth = sdk->get_status(1);
        frame.image = toImage(bgr);
        finishInference(frame, sdkResult);
        std::printf("[frame] rc=%d sdk_rc=%d contours=%zu inference_ms=%.2f auth=%d\n", frame.result, frame.sdkResult, frame.results.size(), frame.milliseconds, frame.auth);
    }
};
static void failure(Shared& shared, Frame frame) {
    frame.complete = true;
    frame.results.clear();
    frame.version = ++shared.version;
    frame.sequence = shared.frame.sequence + 1;
    shared.failed = true;
    shared.frame = std::move(frame);
    shared.wake.notify_all();
}
static void finishVideo(Shared& shared) {
    if (!shared.inputEnded || shared.hasPending || shared.inFlight || !shared.overlay.complete) return;
    shared.overlay.ended = true;
    shared.overlay.message = "Video finished";
    shared.overlay.version = ++shared.version;
    shared.overlay.sequence = shared.frame.sequence + 1;
    shared.frame = shared.overlay;
    std::puts("[video] finished");
}
static void worker(Shared& shared) {
    Engine engine;
    unsigned long active = 0;
    while (true) {
        Options options;
        unsigned long revision;
        cv::Mat input;
        {
            std::unique_lock<std::mutex> lock(shared.mutex);
            shared.wake.wait(lock, [&] { return shared.stop || (!shared.failed &&
                (shared.revision != active || (isLive(shared.options.source) && shared.hasPending))); });
            if (shared.stop) return;
            options = shared.options;
            revision = shared.revision;
            active = revision;
            if (isLive(options.source) && shared.hasPending) {
                input = std::move(shared.pending);
                shared.hasPending = false;
                shared.inFlight = true;
            }
        }
        if (options.source == Source::Idle || (isLive(options.source) && input.empty())) continue;
        Frame frame;
        frame.revision = revision;
        frame.source = sourceName(options);
        frame.confidence = options.confidence;
        frame.rotation = options.rotation;
        try {
            if (options.source == Source::Image) {
                input = cv::imread(options.image.toStdString());
                if (input.empty()) throw std::runtime_error("Cannot read image");
                rotate(input, options.rotation);
            }
            if (engine.configure(options.confidence, frame)) engine.infer(input, frame);
        } catch (const std::exception& error) {
            frame.result = HUMAN_SEGMENTATION_ERR_IMAGE_EMPTY;
            frame.message = QString::fromLocal8Bit(error.what());
        }
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (revision != shared.revision) continue;
        shared.inFlight = false;
        if (shared.failed) continue;
        if (frame.result != 0) { failure(shared, std::move(frame)); continue; }
        frame.version = ++shared.version;
        frame.sequence = shared.frame.sequence + 1;
        shared.overlay = frame;
        shared.frame = std::move(frame);
        if (options.source == Source::Video) finishVideo(shared);
    }
}
static void captureThread(Shared& shared) {
    cv::VideoCapture capture;
    unsigned long active = 0;
    unsigned long framesRead = 0;
    auto nextFrame = std::chrono::steady_clock::now();
    std::chrono::steady_clock::duration interval{};
    while (true) {
        Options options;
        unsigned long revision;
        {
            std::unique_lock<std::mutex> lock(shared.mutex);
            if (shared.stop) return;
            if (!isLive(shared.options.source) || shared.failed || shared.inputEnded) {
                lock.unlock();
                capture.release();
                lock.lock();
                shared.wake.wait(lock, [&] { return shared.stop || (isLive(shared.options.source) && !shared.failed && !shared.inputEnded); });
            }
            if (shared.stop) return;
            options = shared.options;
            revision = shared.revision;
        }
        if (active != revision) { capture.release(); active = revision; framesRead = 0; }
        const bool video = options.source == Source::Video;
        cv::Mat bgr;
        bool opened = capture.isOpened();
        if (!opened) {
            opened = video ? capture.open(options.video.toStdString()) : openCamera(capture, options.camera);
            if (opened && video) {
                const double rate = capture.get(cv::CAP_PROP_FPS);
                interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(1.0 / (std::isfinite(rate) && rate > 0 && rate <= 240 ? rate : 25.0)));
                nextFrame = std::chrono::steady_clock::now();
                std::printf("[video] opened fps=%.2f\n", rate);
            }
        }
        if (!opened || !capture.read(bgr) || bgr.empty()) {
            capture.release();
            std::lock_guard<std::mutex> lock(shared.mutex);
            if (revision != shared.revision) continue;
            if (video && opened && framesRead > 0) {
                shared.inputEnded = true;
                finishVideo(shared);
                shared.wake.notify_all();
                continue;
            }
            Frame frame;
            frame.revision = revision;
            frame.result = HUMAN_SEGMENTATION_ERR_IMAGE_EMPTY;
            frame.source = sourceName(options);
            frame.message = video ? "Cannot open or decode video file" : "Camera unavailable; connect a USB camera and rescan";
            failure(shared, frame);
            continue;
        }
        ++framesRead;
        if (video) {
            std::unique_lock<std::mutex> lock(shared.mutex);
            if (shared.wake.wait_until(lock, nextFrame, [&] { return shared.stop || shared.failed || shared.revision != revision; })) continue;
            nextFrame = std::max(nextFrame + interval, std::chrono::steady_clock::now());
        }
        rotate(bgr, options.rotation);
        const QImage preview = toImage(bgr);
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (shared.stop || shared.failed || shared.revision != revision) continue;
        shared.pending = bgr;
        shared.hasPending = true;
        Frame frame = shared.overlay;
        frame.image = preview;
        frame.source = sourceName(options);
        frame.revision = revision;
        frame.sequence = shared.frame.sequence + 1;
        shared.frame = std::move(frame);
        shared.wake.notify_all();
    }
}
class Window : public QMainWindow {
    Shared shared;
    std::thread inferenceThread, cameraThread;
    Frame current;
    QLabel *preview = new QLabel, *banner = new QLabel, *badge = new QLabel, *source = new QLabel;
    QLabel *count = new QLabel("--"), *latency = new QLabel("--"), *fps = new QLabel("--"), *resolution = new QLabel("--");
    QLabel *details = new QLabel;
    QProgressBar* percent = new QProgressBar;
    QTableWidget* table = new QTableWidget(0, 4);
    QComboBox *cameraBox = new QComboBox, *rotationBox = new QComboBox;
    QDoubleSpinBox* confidence = new QDoubleSpinBox;
    QCheckBox* labels = new QCheckBox("Show contour labels");
    QPushButton *cameraButton = new QPushButton("Start Camera"), *save = new QPushButton("Export Result");
    QPushButton* videoButton = new QPushButton("Open Video");
    QPushButton *rerun = new QPushButton("Detect Again"), *copy = new QPushButton("Copy Summary");
    QTimer timer, debounce;
    QElapsedTimer clock;
    QTabBar* views = new QTabBar;
    int highlight = -1, previewCount = 0, inferenceCount = 0;
    unsigned long sequence = 0, version = 0;
    bool screenshotTaken = false;
    void inputButtons(Source selected, bool stopped = false) {
        const bool camera = selected == Source::Camera && !stopped;
        const bool video = selected == Source::Video && !stopped;
        cameraButton->setText(camera ? "Stop Camera" : "Start Camera");
        cameraButton->setIcon(style()->standardIcon(camera ? QStyle::SP_MediaStop : QStyle::SP_MediaPlay));
        videoButton->setText(video ? "Stop Video" : "Open Video");
        videoButton->setIcon(style()->standardIcon(video ? QStyle::SP_MediaStop : QStyle::SP_DirOpenIcon));
    }
    static void heading(QVBoxLayout* layout, const QString& title) {
        auto* label = new QLabel(title.toUpper());
        label->setStyleSheet("color:#9fa8ad;font-size:11px;font-weight:600;padding-top:12px;");
        layout->addWidget(label);
    }
    void rescan(const QString& wanted) {
        QSignalBlocker blocker(cameraBox);
        cameraBox->clear();
        cameraBox->addItem("Auto (USB camera)", "auto");
        for (const auto& camera : cameras()) cameraBox->addItem(camera.name + " (" + camera.path + ")", camera.path);
        int index = cameraBox->findData(wanted);
        if (index < 0) { cameraBox->addItem(wanted, wanted); index = cameraBox->count() - 1; }
        cameraBox->setCurrentIndex(index);
    }
    void request(Source selected) {
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.options.source = selected;
            shared.options.camera = cameraBox->currentData().toString();
            shared.options.confidence = static_cast<float>(confidence->value());
            shared.options.rotation = rotationBox->currentData().toInt();
            shared.failed = false;
            shared.hasPending = false;
            shared.inputEnded = false;
            shared.inFlight = false;
            shared.overlay = Frame{};
            shared.overlay.confidence = shared.options.confidence;
            shared.overlay.rotation = shared.options.rotation;
            ++shared.revision;
            shared.wake.notify_all();
        }
        current = Frame{};
        highlight = -1;
        save->setEnabled(false);
        copy->setEnabled(false);
        rerun->setEnabled(selected == Source::Image);
        table->setRowCount(0);
        count->setText("--");
        percent->setValue(0);
        details->clear();
        previewCount = inferenceCount = 0;
        clock.restart();
        inputButtons(selected);
        banner->setText(selected == Source::Idle ? "Stopped" : "Processing...");
        redraw();
    }
    void openImage(const QString& path) {
        { std::lock_guard<std::mutex> lock(shared.mutex); shared.options.image = path; }
        request(Source::Image);
    }
    void openMedia(const QString& path) {
        if (!isVideoPath(path)) { openImage(path); return; }
        { std::lock_guard<std::mutex> lock(shared.mutex); shared.options.video = path; }
        request(Source::Video);
    }
    void redraw() {
        if (current.image.isNull()) { preview->clear(); return; }
        Frame view = current;
        view.image = current.image.scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const double scale = double(view.image.width()) / current.image.width();
        for (auto& item : view.results) for (auto& point : item.contour) {
            point.x = qRound(point.x * scale);
            point.y = qRound(point.y * scale);
        }
        preview->setPixmap(QPixmap::fromImage(rendered(view, static_cast<View>(views->currentIndex()), labels->isChecked(), highlight)));
    }
    void screenshot() {
        const QString path = shared.options.screenshot;
        if (path.isEmpty()) return;
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (!grab().save(path)) banner->setText("Screenshot failed: " + path);
    }
    void updateFrame() {
        if (interrupted) { close(); return; }
        bool fresh = false;
        Source selected;
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            selected = shared.options.source;
            if (sequence != shared.frame.sequence) {
                sequence = shared.frame.sequence;
                if (shared.frame.revision == shared.revision) { current = shared.frame; fresh = true; }
            }
        }
        if (fresh && isLive(selected) && !current.ended) ++previewCount;
        const bool newResults = fresh && version != current.version;
        if (newResults && isLive(selected) && !current.ended) ++inferenceCount;
        if (clock.elapsed() >= 1000) {
            fps->setText(isLive(selected) && !current.ended ? QString("%1 / %2").arg(inferenceCount * 1000.0 / clock.elapsed(), 0, 'f', 1)
                .arg(previewCount * 1000.0 / clock.elapsed(), 0, 'f', 0) : "--");
            previewCount = inferenceCount = 0;
            clock.restart();
        }
        if (!fresh) return;
        redraw();
        if (!newResults) return;
        version = current.version;
        if (current.complete) inputButtons(selected, current.ended || current.result != 0);
        const bool valid = current.complete && current.result == 0 && !current.image.isNull();
        save->setEnabled(valid);
        copy->setEnabled(valid);
        source->setText(QFileInfo(current.source).fileName());
        badge->setText(authText(current.auth));
        badge->setStyleSheet(current.auth == 0 ? "color:#4ddd9a;" : "color:#f5c451;");
        banner->setText(current.message + (current.result != 0 ? " | Check input, models, license and SDK libraries." : " | " + authText(current.auth)));
        banner->setStyleSheet(current.result != 0 ? "color:#ff9797;padding:8px;" : "color:#80dfba;padding:8px;");
        count->setText(QString::number(current.results.size()));
        latency->setText(QString("%1 ms").arg(current.milliseconds, 0, 'f', 1));
        resolution->setText(QString("%1 x %2").arg(current.image.width()).arg(current.image.height()));
        percent->setValue(qRound(coverage(current)));
        details->setText(QString("%1 contours\n%2% foreground").arg(current.results.size()).arg(coverage(current), 0, 'f', 1));
        QSignalBlocker blocker(table);
        table->setRowCount(static_cast<int>(current.results.size()));
        for (size_t index = 0; index < current.results.size(); ++index) {
            const auto& item = current.results[index];
            const QStringList cells{QString::number(index + 1), QString::fromStdString(item.label),
                scoreText(item.score), QString::number(item.area, 'f', 0)};
            for (int column = 0; column < cells.size(); ++column) {
                auto* cell = new QTableWidgetItem(cells[column]);
                cell->setToolTip(QString("Label ID: %1 | Points: %2 | SDK area: %3").arg(item.label_id).arg(item.contour.size()).arg(item.area));
                table->setItem(static_cast<int>(index), column, cell);
            }
        }
        if (highlight >= table->rowCount()) highlight = -1;
        if (highlight >= 0) table->selectRow(highlight);
        if (current.complete && !shared.options.screenshot.isEmpty() && !shared.options.quitAfter && !screenshotTaken) {
            screenshotTaken = true;
            QTimer::singleShot(0, this, [this] { screenshot(); });
        }
    }
    QString chooseFile(const QString& title, const QString& directory, const QString& filter) {
        QFileDialog dialog(this, title, directory, filter);
        dialog.setOption(QFileDialog::DontUseNativeDialog);
        dialog.setFileMode(QFileDialog::ExistingFile);
        for (auto* button : dialog.findChildren<QToolButton*>()) {
            if (button->objectName() != "toParentButton") {
                button->hide();
                continue;
            }
            button->setText("Back");
            button->setToolTip("Go to parent directory");
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setFixedSize(button->fontMetrics().horizontalAdvance("Back") + 40, 36);
        }
        return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
    }
protected:
    void resizeEvent(QResizeEvent* event) override { QMainWindow::resizeEvent(event); redraw(); }
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (!mediaFromMime(event->mimeData()).isEmpty()) event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        const QString path = mediaFromMime(event->mimeData());
        if (!path.isEmpty()) openMedia(path);
    }
public:
    explicit Window(const Options& options) {
        shared.options = options;
        setWindowTitle("Human Segmentation - Quectel Pi M2");
        setAcceptDrops(true);
        resize(1360, 800);
        setMinimumSize(1080, 680);
        auto* root = new QWidget;
        auto* row = new QHBoxLayout(root);
        row->setContentsMargins(16, 16, 16, 16);
        row->setSpacing(18);
        auto* sidebar = new QWidget;
        sidebar->setFixedWidth(238);
        auto* left = new QVBoxLayout(sidebar);
        left->setContentsMargins(0, 0, 12, 0);
        auto* brand = new QLabel("AI Playground");
        brand->setStyleSheet("font-size:20px;font-weight:700;");
        left->addWidget(brand);
        left->addWidget(new QLabel("Quectel Pi M2 / RK3576"));
        heading(left, "Input");
        auto* openButton = new QPushButton("Open Image");
        openButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
        left->addWidget(openButton);
        videoButton->setToolTip("Open a local MP4 / AVI / MKV / MOV / WEBM / M4V video, or stop playback");
        left->addWidget(videoButton);
        auto* cameraRow = new QHBoxLayout;
        cameraBox->setMinimumContentsLength(8);
        cameraBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        cameraRow->addWidget(cameraBox, 1);
        auto* scan = new QToolButton;
        scan->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        scan->setToolTip("Rescan USB cameras");
        cameraRow->addWidget(scan);
        left->addLayout(cameraRow);
        left->addWidget(cameraButton);
        heading(left, "Confidence");
        auto* thresholdRow = new QHBoxLayout;
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(1, 99);
        confidence->setRange(0.01, 0.99);
        confidence->setSingleStep(0.05);
        confidence->setValue(options.confidence);
        slider->setValue(qRound(options.confidence * 100));
        thresholdRow->addWidget(slider, 1);
        thresholdRow->addWidget(confidence);
        left->addLayout(thresholdRow);
        heading(left, "Rotation");
        for (int angle : {0, 90, 180, 270}) rotationBox->addItem(QString::number(angle), angle);
        rotationBox->setCurrentIndex(rotationBox->findData(options.rotation));
        left->addWidget(rotationBox);
        labels->setChecked(true);
        left->addWidget(labels);
        heading(left, "Legend");
        left->addWidget(new QLabel("<font color='#4ddd9a'>Human foreground</font>"));
        left->addStretch();
        badge->setWordWrap(true);
        left->addWidget(badge);
        row->addWidget(sidebar);
        auto* center = new QVBoxLayout;
        auto* title = new QLabel("Human Segmentation");
        title->setStyleSheet("font-size:24px;font-weight:700;");
        center->addWidget(title);
        source->setMaximumHeight(24);
        center->addWidget(source);
        auto* metrics = new QHBoxLayout;
        const QStringList captions{"CONTOURS", "INFERENCE", "INFER / PREVIEW FPS", "RESOLUTION"};
        const QList<QLabel*> values{count, latency, fps, resolution};
        for (int index = 0; index < values.size(); ++index) {
            auto* metric = new QVBoxLayout;
            auto* caption = new QLabel(captions[index]);
            caption->setStyleSheet("color:#9fa8ad;font-size:10px;");
            values[index]->setStyleSheet("font-size:17px;font-weight:600;");
            metric->addWidget(caption);
            metric->addWidget(values[index]);
            metrics->addLayout(metric, 1);
        }
        center->addLayout(metrics);
        for (const auto& name : {"Contours", "White Background", "Original"}) views->addTab(name);
        views->setExpanding(true);
        center->addWidget(views);
        preview->setAlignment(Qt::AlignCenter);
        preview->setMinimumSize(320, 240);
        preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        preview->setStyleSheet("background:#121618;");
        center->addWidget(preview, 1);
        banner->setWordWrap(true);
        center->addWidget(banner);
        auto* buttons = new QHBoxLayout;
        auto* fullscreen = new QToolButton;
        fullscreen->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
        fullscreen->setToolTip("Full screen (F11)");
        rerun->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        save->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
        save->setToolTip("Export contours, source, cutout, TXT, JSON and LabelMe polygons");
        buttons->addWidget(rerun);
        buttons->addWidget(save);
        buttons->addWidget(fullscreen);
        center->addLayout(buttons);
        row->addLayout(center, 1);
        auto* rightWidget = new QWidget;
        rightWidget->setFixedWidth(292);
        auto* right = new QVBoxLayout(rightWidget);
        heading(right, "Segmentation results");
        details->setStyleSheet("font-size:19px;color:#4ddd9a;padding:12px 0;");
        right->addWidget(details);
        heading(right, "Foreground coverage");
        percent->setRange(0, 100);
        percent->setFormat("%p%");
        right->addWidget(percent);
        heading(right, "Contours");
        table->setHorizontalHeaderLabels({"#", "Label", "Score", "Area"});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->verticalHeader()->hide();
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setShowGrid(false);
        right->addWidget(table, 1);
        copy->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
        right->addWidget(copy);
        row->addWidget(rightWidget);
        setCentralWidget(root);
        setStyleSheet("QWidget{background:#24292c;color:#edf1f0;font-family:'DejaVu Sans';font-size:12px;}"
            "QLabel{background:transparent;} QPushButton,QToolButton{background:#363e41;border:1px solid #596468;border-radius:5px;padding:8px;}"
            "QPushButton:hover,QToolButton:hover{background:#465357;} QPushButton:disabled{color:#7b8588;}"
            "QComboBox,QDoubleSpinBox{background:#30383b;padding:5px;border:1px solid #596468;border-radius:4px;}"
            "QTableWidget{background:#1b2124;border:0;} QHeaderView::section{background:#363e41;border:0;padding:5px;}"
            "QTabBar::tab{background:#30383b;padding:8px;} QTabBar::tab:selected{color:#4ddd9a;border-bottom:2px solid #4ddd9a;}"
            "QProgressBar{border:0;background:#141a1d;text-align:center;min-height:20px;} QProgressBar::chunk{background:#278660;}"
            "QToolTip{background:#363e41;color:#edf1f0;border:1px solid #596468;}");
        rescan(options.camera);
        debounce.setSingleShot(true);
        debounce.setInterval(250);
        connect(openButton, &QPushButton::clicked, this, [this] {
            const QString path = chooseFile("Open Image", "images", "Images (*.jpg *.jpeg *.png *.bmp)");
            if (!path.isEmpty()) openImage(path);
        });
        connect(cameraButton, &QPushButton::clicked, this, [this] {
            bool running;
            { std::lock_guard<std::mutex> lock(shared.mutex); running = shared.options.source == Source::Camera && !shared.failed; }
            request(running ? Source::Idle : Source::Camera);
        });
        connect(videoButton, &QPushButton::clicked, this, [this] {
            bool running;
            { std::lock_guard<std::mutex> lock(shared.mutex); running = shared.options.source == Source::Video && !shared.failed && !shared.inputEnded; }
            if (running) { request(Source::Idle); return; }
            const QString path = chooseFile("Open Video", QString(), "Videos (*.mp4 *.avi *.mkv *.mov *.webm *.m4v)");
            if (!path.isEmpty()) openMedia(path);
        });
        connect(scan, &QToolButton::clicked, this, [this] { rescan(cameraBox->currentData().toString()); });
        connect(slider, &QSlider::valueChanged, confidence, [this](int value) { confidence->setValue(value / 100.0); });
        connect(confidence, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, slider](double value) {
            QSignalBlocker blocker(slider);
            slider->setValue(qRound(value * 100));
            debounce.start();
        });
        connect(rotationBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { debounce.start(); });
        connect(cameraBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { debounce.start(); });
        connect(&debounce, &QTimer::timeout, this, [this] {
            Source selected;
            { std::lock_guard<std::mutex> lock(shared.mutex); selected = shared.options.source; }
            request(selected);
        });
        connect(rerun, &QPushButton::clicked, this, [this] { request(Source::Image); });
        connect(views, &QTabBar::currentChanged, this, [this] { redraw(); });
        connect(labels, &QCheckBox::toggled, this, [this] { redraw(); });
        connect(table, &QTableWidget::itemSelectionChanged, this, [this] {
            highlight = table->currentRow();
            redraw();
        });
        connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(summary(current)); });
        connect(save, &QPushButton::clicked, this, [this] {
            Frame snapshot = current;
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                if (isLive(shared.options.source)) {
                    snapshot = shared.overlay;
                    if (shared.failed || snapshot.revision != shared.revision) snapshot.complete = false;
                }
            }
            const QString base = exportFrame(snapshot, shared.options.output);
            banner->setText(base.isEmpty() ? "Export failed: check output directory permissions" : "Saved: " + base);
        });
        const auto toggle = [this] { isFullScreen() ? showNormal() : showFullScreen(); };
        connect(fullscreen, &QToolButton::clicked, this, toggle);
        connect(new QShortcut(QKeySequence(Qt::Key_F11), this), &QShortcut::activated, this, toggle);
        connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, this, [this] { if (isFullScreen()) showNormal(); });
        connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, openButton, &QPushButton::click);
        connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, [this] { if (save->isEnabled()) save->click(); });
        connect(new QShortcut(QKeySequence(Qt::Key_Space), this), &QShortcut::activated, cameraButton, &QPushButton::click);
        connect(&timer, &QTimer::timeout, this, [this] { updateFrame(); });
        request(options.source);
        timer.start(33);
        clock.start();
        if (options.quitAfter) QTimer::singleShot(options.quitAfter * 1000, this, [this] { screenshot(); close(); });
        inferenceThread = std::thread(worker, std::ref(shared));
        cameraThread = std::thread(captureThread, std::ref(shared));
    }
    ~Window() override {
        { std::lock_guard<std::mutex> lock(shared.mutex); shared.stop = true; shared.wake.notify_all(); }
        inferenceThread.join();
        cameraThread.join();
    }
};
static int selfTest(const QString& output) {
    Frame frame;
    frame.image = QImage(100, 100, QImage::Format_RGB32);
    frame.image.fill(QColor("#bd3051"));
    frame.complete = true;
    frame.result = 0;
    frame.auth = 0;
    frame.source = "synthetic self-test; not SDK inference";
    frame.sequence = 1;
    frame.results.push_back({1, "human", 0.9, {{20,20}, {79,20}, {79,79}, {20,79}}, 3481});
    frame.results.push_back({0, "background", 0.8, {{0,0}, {10,0}, {10,10}, {0,10}}, 100});
    const auto region = foregroundRegion(frame);
    const auto cutout = rendered(frame, View::Cutout);
    if (!region.contains(QPoint(40,40)) || region.contains(QPoint(5,5)) ||
        cutout.pixelColor(5,5) != QColor(Qt::white) || cutout.pixelColor(40,40) != frame.image.pixelColor(40,40) ||
        std::abs(coverage(frame) - 34.81) > 0.02) return 6;
    Frame overlapping = frame;
    overlapping.results.push_back(frame.results.front());
    if (coverage(overlapping) != coverage(frame)) return 6;
    Frame clipped = frame;
    clipped.results = {{1, "human", 0.9, {{-10,-10}, {20,-10}, {20,20}, {-10,20}}, 900}};
    if (foregroundRegion(clipped).contains(QPoint(-1,-1)) || std::abs(coverage(clipped) - 4.0) > 0.02) return 6;
    const QString base = exportFrame(frame, QDir(output).filePath("selftest"));
    if (base.isEmpty()) return 5;
    QFile json(base + ".json"), labelme(base + ".labelme.json");
    if (!json.open(QIODevice::ReadOnly) || !labelme.open(QIODevice::ReadOnly)) return 5;
    const auto records = QJsonDocument::fromJson(json.readAll()).object().value("results").toArray();
    const auto shapes = QJsonDocument::fromJson(labelme.readAll()).object().value("shapes").toArray();
    if (records.size() != 2 || records.at(0).toObject().value("contour").toArray().size() != 4 ||
        shapes.size() != 2 || shapes.at(0).toObject().value("shape_type").toString() != "polygon" ||
        QImage(base + "_cutout.png").size() != frame.image.size() ||
        QDir(QFileInfo(base).absolutePath()).entryList({QFileInfo(base).fileName() + "*"}, QDir::Files).size() != 6) return 5;
    frame.result = -3;
    if (!exportFrame(frame, output).isEmpty()) return 5;
    frame.result = 0;
    frame.results.clear();
    if (coverage(frame) != 0 || !foregroundRegion(frame).isEmpty() ||
        rendered(frame, View::Cutout).pixelColor(40,40) != QColor(Qt::white)) return 6;
    finishInference(frame, HUMAN_SEGMENTATION_ERR_EXTRACTION_ERROR);
    if (frame.result != 0 || frame.sdkResult != -7 || frame.message != "No foreground contour") return 6;
    finishInference(frame, HUMAN_SEGMENTATION_ERR_INFERENCE_ERROR);
    if (frame.result != -3) return 6;
    frame.results.push_back({0, "", 0, {{20,20}, {79,20}, {79,79}, {20,79}}, 3481});
    normalizeResults(frame.results);
    if (frame.results[0].label_id != 1 || frame.results[0].label != "human" || frame.results[0].score != -1 || coverage(frame) < 30) return 6;
    finishInference(frame, HUMAN_SEGMENTATION_ERR_EXTRACTION_ERROR);
    if (frame.result != -7 || !frame.results.empty()) return 6;
    std::puts("[selftest] PASS: polygons, foreground union, clipping, cutout, six-file export, JSON, LabelMe, failure guard, empty results");
    return 0;
}
static int headless(const Options& options) {
    cv::VideoCapture capture;
    cv::Mat image;
    if (options.source == Source::Camera) { if (!openCamera(capture, options.camera)) return 2; }
    else if (options.source == Source::Video) { if (!capture.open(options.video.toStdString())) return 2; }
    else { image = cv::imread(options.image.toStdString()); if (image.empty()) return 2; }
    Engine engine;
    Frame frame;
    if (!engine.configure(options.confidence, frame)) return 3;
    const int total = options.source == Source::Camera ? options.frames : options.repeat;
    for (int index = 0; options.source == Source::Video || index < total; ++index) {
        if (interrupted) return 130;
        cv::Mat bgr;
        if (isLive(options.source)) capture.read(bgr);
        else bgr = image.clone();
        if (bgr.empty()) {
            if (options.source == Source::Video && index > 0) break;
            return 2;
        }
        rotate(bgr, options.rotation);
        frame.source = sourceName(options);
        frame.confidence = options.confidence;
        frame.rotation = options.rotation;
        frame.sequence = index + 1;
        engine.infer(bgr, frame);
        if (frame.result != 0) return 4;
        if ((options.source == Source::Camera || (options.source == Source::Image && index == total - 1)) && exportFrame(frame, options.output).isEmpty()) return 5;
    }
    if (options.source == Source::Video && exportFrame(frame, options.output).isEmpty()) return 5;
    return 0;
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    for (int index = 1; index < argc; ++index)
        if (!std::strcmp(argv[index], "--headless") || !std::strcmp(argv[index], "--self-test")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setApplicationName("human_segmentation_cam_demo");
    QCommandLineParser parser;
    parser.setApplicationDescription("Quectel Pi M2 human segmentation");
    parser.addHelpOption();
    parser.addOptions({{"headless", "Run without a window"}, {"self-test", "Synthetic rendering/export self-test (no SDK inference)"},
        {"image", "Input image", "path", "images/input.jpg"}, {"camera", "USB camera device or auto", "device"},
        {"video", "Local video file (headless processes all frames and exports the last)", "path"},
        {"confidence", "Confidence threshold (0.01-0.99)", "value", "0.20"},
        {"rotate", "Clockwise rotation: 0,90,180,270", "degrees", "0"},
        {"frames", "Headless camera frame count", "count", "1"}, {"repeat", "Image repetitions", "count", "1"},
        {"output", "Export directory", "path", "outputs"}, {"quit-after", "Close GUI after N seconds", "seconds", "0"},
        {"screenshot", "Save GUI screenshot", "path"}});
    if (!parser.parse(app.arguments())) { std::fprintf(stderr, "%s\n", parser.errorText().toUtf8().constData()); return 2; }
    if (parser.isSet("help")) parser.showHelp();
    Options options;
    options.image = parser.value("image");
    options.output = parser.value("output");
    options.screenshot = parser.value("screenshot");
    options.headless = parser.isSet("headless");
    if (parser.isSet("camera")) { options.source = Source::Camera; options.camera = parser.value("camera"); }
    if (parser.isSet("video")) { options.source = Source::Video; options.video = parser.value("video"); }
    bool validConfidence, validRotation, validFrames, validRepeat, validQuit;
    options.confidence = parser.value("confidence").toFloat(&validConfidence);
    options.rotation = parser.value("rotate").toInt(&validRotation);
    options.frames = parser.value("frames").toInt(&validFrames);
    options.repeat = parser.value("repeat").toInt(&validRepeat);
    options.quitAfter = parser.value("quit-after").toInt(&validQuit);
    if (!validConfidence || !std::isfinite(options.confidence) || options.confidence < 0.01f || options.confidence > 0.99f ||
        !validRotation || (options.rotation != 0 && options.rotation != 90 && options.rotation != 180 && options.rotation != 270) ||
        !validFrames || options.frames < 1 || options.frames > 10000 || !validRepeat || options.repeat < 1 || options.repeat > 10000 ||
        !validQuit || options.quitAfter < 0 || options.quitAfter > 86400 || options.output.isEmpty() ||
        (int(parser.isSet("image")) + int(parser.isSet("camera")) + int(parser.isSet("video")) > 1) || options.camera.isEmpty() ||
        (parser.isSet("video") && (options.video.isEmpty() || parser.isSet("frames") || parser.isSet("repeat"))) ||
        (options.headless && (!options.screenshot.isEmpty() || options.quitAfter))) return 2;
    try {
        if (parser.isSet("self-test")) return selfTest(options.output);
        if (options.headless) return headless(options);
        Window window(options);
        window.show();
        const int result = app.exec();
        return interrupted ? 130 : result;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[error] %s\n", error.what());
        return 6;
    }
}