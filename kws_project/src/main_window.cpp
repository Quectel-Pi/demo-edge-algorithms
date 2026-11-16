

#include "main_window.h"

#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPolygonF>
#include <QPointF>
#include <QPushButton>
#include <QRadialGradient>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "audio_capture.h"
#include "kws_pipeline.h"
#include "main.h"

namespace {
const QString kText  = QStringLiteral("#132a4a");
const QString kMuted = QStringLiteral("#58709b");
const QString kBlue  = QStringLiteral("#2f5bff");
const QString kGreen = QStringLiteral("#138a4e");
const QString kRed   = QStringLiteral("#c81e1e");
const QString kAmber = QStringLiteral("#b45309");

void place(QWidget* w, int x, int y, int wd, int ht, double s) {
    w->setProperty("dx", x);
    w->setProperty("dy", y);
    w->setProperty("dw", wd);
    w->setProperty("dh", ht);
    w->setGeometry(int(x * s), int(y * s), int(wd * s), int(ht * s));
}

std::vector<int16_t> buildTrace(const std::vector<int16_t>& v, size_t count, int w) {
    std::vector<int16_t> out;
    if (v.empty() || w <= 0) return out;
    const size_t end = v.size();
    const size_t start = end > count ? end - count : 0;
    const size_t n = end - start;
    if (n == 0) return out;

    out.resize(std::min<size_t>(size_t(w), n));
    const size_t bins = std::max<size_t>(1, n / out.size());
    for (size_t x = 0; x < out.size(); ++x) {
        const size_t i0 = start + x * bins;
        const size_t i1 = std::min(end - 1, i0 + bins - 1);
        int16_t best = v[i0];
        for (size_t i = i0; i <= i1; ++i) {
            if (std::abs(int(v[i])) > std::abs(int(best))) best = v[i];
        }
        out[x] = best;
    }
    return out;
}

QString formatSeconds(double sec) {
    if (!(sec > 0.0)) sec = 0.0;
    const int total = int(sec);
    const int tenths = int((sec - total) * 10.0);
    return QStringLiteral("%1:%2.%3")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'))
        .arg(tenths);
}

QStringList keywordTokens(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QString def = line.section(QLatin1Char('@'), 0, 0);
        return def.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }
    return {};
}

QString labelFromTokens(const QString& line) {
    const int at = line.indexOf(QLatin1Char('@'));
    const QString tokenPart = at >= 0 ? line.left(at) : line;
    const QStringList toks = tokenPart.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString py;
    for (const QString& t : toks) py += t;
    static const QString kToned =
        QStringLiteral("āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜü");
    static const QString kPlain = QStringLiteral("aaaaeeeeiiiioooouuuuvvvvv");
    for (int i = 0; i < py.size(); ++i) {
        const int idx = kToned.indexOf(py.at(i));
        if (idx >= 0) py[i] = kPlain.at(idx);
    }
    return py.toUpper();
}

QStringList keywordChineseWords(const QString& path) {
    QStringList out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const int at = line.indexOf(QLatin1Char('@'));
        if (at >= 0) out << line.mid(at + 1).trimmed();
    }
    return out;
}

void loadKeywordLabels(const QString& path, QStringList* labels,
                       QHash<QString, QString>* byChinese) {
    if (labels) labels->clear();
    if (byChinese) byChinese->clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QString label = labelFromTokens(line);
        if (labels) labels->append(label);
        const int at = line.indexOf(QLatin1Char('@'));
        if (byChinese && at >= 0) (*byChinese)[line.mid(at + 1).trimmed()] = label;
    }
}

QSet<QString> readVocabulary(const QString& path) {
    QSet<QString> vocab;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return vocab;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QString tok = line.simplified().section(QLatin1Char(' '), 0, 0);
        if (!tok.isEmpty()) vocab.insert(tok);
    }
    return vocab;
}

QChar tonedVowel(QChar base, int tone) {
    if (tone < 1 || tone > 4) return base;
    switch (base.unicode()) {
    case 'a': return QStringLiteral("āáǎà").at(tone - 1);
    case 'e': return QStringLiteral("ēéěè").at(tone - 1);
    case 'i': return QStringLiteral("īíǐì").at(tone - 1);
    case 'o': return QStringLiteral("ōóǒò").at(tone - 1);
    case 'u': return QStringLiteral("ūúǔù").at(tone - 1);
    case 0x00FC: return QStringLiteral("ǖǘǚǜ").at(tone - 1);
    default: return base;
    }
}

QString markTone(QString syl, int tone) {
    syl.replace(QStringLiteral("v"), QString(QChar(0x00FC)));
    syl.replace(QStringLiteral("u:"), QString(QChar(0x00FC)));
    if (tone < 1 || tone > 4) return syl;

    const QChar a = QLatin1Char('a'), o = QLatin1Char('o'), e = QLatin1Char('e');
    int idx = -1;
    if (syl.contains(a)) {
        idx = syl.indexOf(a);
    } else if (syl.contains(o)) {
        idx = syl.indexOf(o);
    } else if (syl.contains(e)) {
        idx = syl.indexOf(e);
    } else {
        const int iu = syl.indexOf(QStringLiteral("iu"));
        if (iu >= 0) idx = iu + 1;
        const int ui = syl.indexOf(QStringLiteral("ui"));
        if (ui >= 0) idx = ui + 1;
        if (idx < 0) {
            for (int i = syl.size() - 1; i >= 0; --i) {
                const QChar c = syl.at(i);
                if (c == QLatin1Char('i') || c == QLatin1Char('u') ||
                    c.unicode() == 0x00FC) {
                    idx = i;
                    break;
                }
            }
        }
    }
    if (idx < 0) return syl;
    syl.replace(idx, 1, QString(tonedVowel(syl.at(idx), tone)));
    return syl;
}

int pinyinInitialLength(const QString& syl) {
    static const char kInits[][3] = {"zh", "ch", "sh", "b", "p", "m", "f", "d", "t",
                                     "n",  "l",  "g",  "k", "h", "j", "q", "x", "r",
                                     "z",  "c",  "s",  "y", "w"};
    for (const auto& ini : kInits) {
        const QString s = QString::fromLatin1(ini);
        if (syl.startsWith(s)) return int(s.size());
    }
    return 0;
}

struct NumberSyllable {
    QString letters;
    int tone = 0;
};

bool convertNumberTone(const QString& text, QStringList* tokens, QString* label,
                       QString* error) {
    std::vector<NumberSyllable> list;
    QString letters;
    for (const QChar c : text) {
        const int u = c.unicode();
        if (c.isSpace() || c == QLatin1Char('-') || c == QLatin1Char('\'')) continue;
        if (u >= '1' && u <= '5') {
            if (letters.isEmpty()) {
                *error = QStringLiteral("Tone digit must follow a syllable.");
                return false;
            }
            list.push_back({letters, u - '0'});
            letters.clear();
            continue;
        }
        if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == 0x00FC ||
            c == QLatin1Char(':')) {
            letters += c.toLower();
            continue;
        }
        *error = QStringLiteral("Unsupported character '%1'; use ASCII pinyin like "
                                "xiao3 yuan3 tong2 xue2.")
                     .arg(c);
        return false;
    }
    if (!letters.isEmpty()) {
        *error = QStringLiteral("Each syllable needs a tone digit (1-4).");
        return false;
    }
    if (list.empty()) {
        *error = QStringLiteral("Enter the wake word, e.g. xiao3 yuan3 tong2 xue2.");
        return false;
    }

    QStringList out;
    QStringList name;
    for (const auto& s : list) {
        QString syl = markTone(s.letters, s.tone);
        const int n = pinyinInitialLength(syl);
        if (n > 0) out << syl.left(n);
        const QString fin = syl.mid(n);
        if (fin.isEmpty()) {
            *error = QStringLiteral("Cannot split syllable '%1%2'.").arg(s.letters).arg(s.tone);
            return false;
        }
        out << fin;

        QString plain = s.letters;
        plain.replace(QStringLiteral("u:"), QStringLiteral("v"));
        plain.replace(QStringLiteral("ü"), QStringLiteral("v"));
        name << plain.toUpper();
    }
    *tokens = out;
    *label = name.join(QLatin1Char(' '));
    return true;
}

QHash<QString, QStringList> loadPinyinDictionary(const QString& path) {
    QHash<QString, QStringList> dict;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return dict;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const int tab = line.indexOf(QLatin1Char('\t'));
        if (tab <= 0) continue;
        dict.insert(line.left(tab), line.mid(tab + 1).split(QLatin1Char(',')));
    }
    return dict;
}

const QHash<QString, QStringList>& pinyinDictionary(const QString& path) {
    static QHash<QString, QStringList> dict;
    static QString loadedPath;
    if (loadedPath != path) {
        dict = loadPinyinDictionary(path);
        loadedPath = path;
    }
    return dict;
}

const QSet<QString>& cachedVocabulary(const QString& path) {
    static QSet<QString> vocab;
    static QString loadedPath;
    if (loadedPath != path) {
        vocab = readVocabulary(path);
        loadedPath = path;
    }
    return vocab;
}

QString tonelessAscii(QString s) {
    static const QString kToned =
        QStringLiteral("āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜü");
    static const QString kPlain = QStringLiteral("aaaaeeeeiiiioooouuuuvvvvv");
    for (int i = 0; i < kToned.size(); ++i)
        s.replace(kToned.at(i), QChar(kPlain.at(i)));
    return s;
}

bool convertChinese(const QString& text, const QString& pinyinPath, const QString& tokensPath,
                    QStringList* tokens, QString* label, QString* error) {
    const QHash<QString, QStringList>& dict = pinyinDictionary(pinyinPath);
    if (dict.isEmpty()) {
        *error = QStringLiteral("Chinese dictionary not found: %1").arg(pinyinPath);
        return false;
    }
    const QSet<QString>& vocab = cachedVocabulary(tokensPath);

    QStringList out;
    QStringList name;
    for (const QChar c : text) {
        if (c.isSpace()) continue;
        const int u = c.unicode();
        if (u < 0x4E00 || u > 0x9FFF) {
            *error = QStringLiteral("Unsupported character '%1'; use Chinese or pinyin.")
                         .arg(QString(c));
            return false;
        }
        const QStringList readings = dict.value(QString(c));
        if (readings.isEmpty()) {
            *error = QStringLiteral("No pinyin for '%1' in %2.").arg(QString(c), pinyinPath);
            return false;
        }

        bool mapped = false;
        for (const QString& raw : readings) {
            const QString r = raw.trimmed();
            if (r.isEmpty()) continue;
            const int n = pinyinInitialLength(r);
            const QString ini = r.left(n);
            const QString fin = r.mid(n);
            if (fin.isEmpty() || !vocab.contains(fin)) continue;
            if (!ini.isEmpty() && !vocab.contains(ini)) continue;
            if (!ini.isEmpty()) out << ini;
            out << fin;
            name << tonelessAscii(r).toUpper();
            mapped = true;
            break;
        }
        if (!mapped) {
            *error = QStringLiteral("'%1' (%2) cannot be mapped to tokens in %3.")
                         .arg(QString(c), readings.join(QLatin1Char('/')), tokensPath);
            return false;
        }
    }
    if (out.isEmpty()) {
        *error = QStringLiteral("Enter the wake word.");
        return false;
    }
    *tokens = out;
    *label = name.join(QLatin1Char(' '));
    return true;
}

bool parseWakeInput(const QString& raw, const QString& tokensPath, const QString& pinyinPath,
                    QStringList* tokens, QString* label, QString* error) {
    const QString text = raw.trimmed();
    if (text.isEmpty()) {
        *error = QStringLiteral("Enter the wake word.");
        return false;
    }

    bool hasDigit = false, hasNonAscii = false, hasCjk = false;
    for (const QChar c : text) {
        const int u = c.unicode();
        if (u >= '0' && u <= '9') hasDigit = true;
        if (u > 127) hasNonAscii = true;
        if (u >= 0x4E00 && u <= 0x9FFF) hasCjk = true;
    }

    if (hasCjk) return convertChinese(text, pinyinPath, tokensPath, tokens, label, error);
    if (hasDigit) return convertNumberTone(text, tokens, label, error);
    if (hasNonAscii) {
        *tokens = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        *label = QString();
        return true;
    }
    *error = QStringLiteral("Add a tone digit to each syllable (1-4), e.g. xiao3 yuan3 tong2 xue2.");
    return false;
}

QPixmap makeBrandIcon(int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient g(0, 0, size, size);
    g.setColorAt(0.0, QColor("#4f80ff"));
    g.setColorAt(1.0, QColor("#2a54e8"));
    QPainterPath box;
    box.addRoundedRect(QRectF(0, 0, size, size), size * 0.28, size * 0.28);
    p.fillPath(box, g);

    const double heights[5] = {0.30, 0.52, 0.74, 0.52, 0.30};
    const double bw = size * 0.075;
    const double gap = size * 0.055;
    const double totalW = 5 * bw + 4 * gap;
    double x = (size - totalW) / 2.0;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 235));
    for (int i = 0; i < 5; ++i) {
        const double h = heights[i] * size;
        p.drawRoundedRect(QRectF(x, (size - h) / 2.0, bw, h), bw / 2.0, bw / 2.0);
        x += bw + gap;
    }
    return pm;
}

QPixmap makeGlyph(const char* kind, const QColor& color, int px) {
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double s = px / 24.0;
    p.scale(s, s);
    p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QString k = QString::fromLatin1(kind);

    if (k == QStringLiteral("exit")) {
        p.drawLine(QPointF(5, 4), QPointF(11, 4));
        p.drawLine(QPointF(5, 20), QPointF(11, 20));
        p.drawLine(QPointF(5, 4), QPointF(5, 20));
        p.drawLine(QPointF(20, 12), QPointF(9, 12));
        p.drawLine(QPointF(15, 8), QPointF(20, 12));
        p.drawLine(QPointF(20, 12), QPointF(15, 16));
    } else if (k == QStringLiteral("mic")) {
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(9, 2, 6, 11), 3, 3);
        p.drawArc(QRectF(6, 6, 12, 10), 180 * 16, 180 * 16);
        p.drawLine(QPointF(12, 16), QPointF(12, 20));
        p.drawLine(QPointF(8, 20), QPointF(16, 20));
    } else if (k == QStringLiteral("target")) {
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(2, 2, 20, 20));
        p.drawEllipse(QRectF(6, 6, 12, 12));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QRectF(10, 10, 4, 4));
    } else if (k == QStringLiteral("pulse")) {
        QPainterPath path;
        path.moveTo(2, 12); path.lineTo(6, 12); path.lineTo(9, 3);
        path.lineTo(15, 21); path.lineTo(18, 12); path.lineTo(22, 12);
        p.drawPath(path);
    } else if (k == QStringLiteral("sliders")) {
        p.drawLine(QPointF(21, 4), QPointF(14, 4)); p.drawLine(QPointF(10, 4), QPointF(3, 4));
        p.drawLine(QPointF(21, 12), QPointF(12, 12)); p.drawLine(QPointF(8, 12), QPointF(3, 12));
        p.drawLine(QPointF(21, 20), QPointF(16, 20)); p.drawLine(QPointF(12, 20), QPointF(3, 20));
        p.drawLine(QPointF(14, 2), QPointF(14, 6));
        p.drawLine(QPointF(8, 10), QPointF(8, 14));
        p.drawLine(QPointF(16, 18), QPointF(16, 22));
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QRectF(3, 5, 2.4, 2.4));
        p.drawEllipse(QRectF(3, 11, 2.4, 2.4));
        p.drawEllipse(QRectF(3, 17, 2.4, 2.4));
        p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(QPointF(9, 6), QPointF(21, 6));
        p.drawLine(QPointF(9, 12), QPointF(21, 12));
        p.drawLine(QPointF(9, 18), QPointF(21, 18));
    }
    p.end();
    return pm;
}

QIcon makeGlyphIcon(const char* kind, const QColor& color, int px) {
    return QIcon(makeGlyph(kind, color, px));
}

QPixmap makeSectionIcon(const char* kind, double scale) {
    return makeGlyph(kind, QColor("#2f5bff"), int(16 * scale));
}

QIcon makeTransportIcon(bool stop) {
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#ffffff"));
    if (stop) {
        p.drawRoundedRect(QRectF(6, 6, 8, 8), 1.5, 1.5);
    } else {
        QPolygonF tri;
        tri << QPointF(5.5, 4.0) << QPointF(16.0, 10.0) << QPointF(5.5, 16.0);
        p.drawPolygon(tri);
    }
    return QIcon(pm);
}

}

class WaveformWidget : public QWidget {
public:
    explicit WaveformWidget(QColor color, QWidget* parent = nullptr)
        : QWidget(parent), color_(std::move(color)) {}

    void setData(const std::vector<int16_t>& d) {
        data_ = d;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath bg;
        bg.addRoundedRect(box, 8, 8);
        p.fillPath(bg, QColor("#fbfcff"));
        p.setPen(QPen(QColor("#e3eaf7"), 1));
        p.drawPath(bg);
        p.drawLine(QPointF(0, height() / 2.0), QPointF(width(), height() / 2.0));

        const int w = width();
        const int h = height();
        if (data_.empty() || w <= 0) return;

        double peak = 0.0;
        for (int16_t v : data_) {
            const double a = std::abs(double(v));
            if (a > peak) peak = a;
        }
        double k = peak > 1.0 ? (0.9 * 32768.0) / peak : 1.0;
        if (k < 1.0) k = 1.0;
        if (k > 64.0) k = 64.0;

        const double mid = h / 2.0;
        const double half = h / 2.0 - 4.0;
        const int nPts = std::min(w, int(data_.size()));

        p.setPen(QPen(color_, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPolygonF poly;
        poly.reserve(nPts);
        double lastY = -1.0;
        for (int x = 0; x < nPts; ++x) {
            double y = mid - (double(data_[x]) / 32768.0) * half * k;
            if (y < 1.0) y = 1.0;
            if (y > h - 2.0) y = h - 2.0;

            if (lastY >= 0.0) y = 0.7 * y + 0.3 * lastY;
            lastY = y;
            poly << QPointF(double(x) + 0.5, y);
        }
        p.drawPolyline(poly);
    }

private:
    std::vector<int16_t> data_;
    QColor color_;
};

class StageWidget : public QWidget {
public:
    explicit StageWidget(QWidget* parent = nullptr) : QWidget(parent) {
        timer_.setInterval(50);
        connect(&timer_, &QTimer::timeout, this, [this] {
            phase_ += 0.9f;
            smooth_ += (level_ - smooth_) * 0.22f;
            if (hitFlash_ > 0.0f) hitFlash_ = std::max(0.0f, hitFlash_ - 0.055f);
            update();
        });
        timer_.start();
    }

    void setLevel(float level) {
        level_ = std::clamp(level, 0.0f, 1.0f);
    }

    void setListening(bool on) {
        if (listening_ != on) {
            listening_ = on;
            update();
        }
    }

    void triggerHit() {
        hitFlash_ = 1.0f;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(r, 14, 14);
        p.setClipPath(clip);

        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0.00, QColor(0x10, 0x19, 0x2B));
        bg.setColorAt(0.55, QColor(0x15, 0x22, 0x3C));
        bg.setColorAt(1.00, QColor(0x1A, 0x29, 0x48));
        p.fillRect(rect(), bg);

        const QColor accent = hitFlash_ > 0.02f ? QColor(60, 225, 150) : QColor(90, 140, 255);
        QRadialGradient glow(QPointF(width() / 2.0, height() * 0.46), width() * 0.55);
        QColor core = accent;
        core.setAlpha(listening_ ? int(40 + 70 * smooth_ + 60 * hitFlash_) : 16);
        QColor edge = accent;
        edge.setAlpha(0);
        glow.setColorAt(0.0, core);
        glow.setColorAt(1.0, edge);
        p.fillRect(rect(), glow);

        p.setPen(QPen(QColor(255, 255, 255, 10), 1));
        for (int x = 42; x < width(); x += 42) p.drawLine(x, 0, x, height());
        for (int y = 42; y < height(); y += 42) p.drawLine(0, y, width(), y);

        const double midY = height() * 0.76;
        for (int k = 0; k < 2; ++k) {
            const double amp = 5.0 + 13.0 * smooth_ + 9.0 * hitFlash_;
            const double freq = 0.012 + 0.004 * k;
            const double ph = phase_ * (0.05 + 0.02 * k) + k * 1.7;
            QPainterPath path;
            for (int x = 0; x <= width(); x += 4) {
                const double y = midY + amp * std::sin(x * freq + ph) * std::cos(x * freq * 0.6 + ph);
                if (x == 0) path.moveTo(x, y);
                else        path.lineTo(x, y);
            }
            QColor lc = accent;
            lc.setAlpha(k == 0 ? 70 : 38);
            p.setPen(QPen(lc, 1.3));
            p.drawPath(path);
        }

        const QRectF bar(24.0, height() - 13.0, width() - 48.0, 5.0);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 26));
        p.drawRoundedRect(bar, 2.5, 2.5);
        QColor fill = accent;
        fill.setAlpha(175);
        QRectF on = bar;
        on.setWidth(bar.width() * std::clamp(double(smooth_), 0.0, 1.0));
        p.setBrush(fill);
        p.drawRoundedRect(on, 2.5, 2.5);
    }

private:
    QTimer timer_;
    float level_ = 0.0f;
    float smooth_ = 0.0f;
    float phase_ = 0.0f;
    float hitFlash_ = 0.0f;
    bool  listening_ = false;
};

class LevelBar : public QWidget {
public:
    explicit LevelBar(QWidget* parent = nullptr) : QWidget(parent) {}

    void setValue(float v) {
        const float nv = std::clamp(v, 0.0f, 1.0f);
        if (std::abs(nv - value_) < 0.002f) return;
        value_ = nv;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.0, 0.0, 0.0, 0.0);
        const double rad = r.height() / 2.0;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#e6ecf8"));
        p.drawRoundedRect(r, rad, rad);

        if (value_ <= 0.01f) return;
        QLinearGradient g(r.topLeft(), r.topRight());
        g.setColorAt(0.0, QColor("#4f80ff"));
        g.setColorAt(0.7, QColor("#2f5bff"));
        g.setColorAt(1.0, QColor("#13a4a4"));
        QRectF on = r;
        on.setWidth(r.width() * value_);
        p.setBrush(g);
        p.drawRoundedRect(on, rad, rad);
    }

private:
    float value_ = 0.0f;
};

MainWindow::MainWindow(kws::SpotterFactory factory, appconfig::AppConfig config, QWidget* parent)
    : QMainWindow(parent), cfg_(std::move(config)), factory_(std::move(factory)) {
    threshold_ = cfg_.threshold;
    micGainDb_ = cfg_.micGainDb;
    cfg_.wakeTokens = keywordTokens(cfg_.keywordsFile).join(QLatin1Char(' '));
    loadKeywordLabels(cfg_.keywordsFile, &wakeLabels_, &wakeLabelByChinese_);
    wakeWordsChinese_ = keywordChineseWords(cfg_.keywordsFile);
    if (!cfg_.wakeLabel.isEmpty()) {

        wakeLabels_ = QStringList{cfg_.wakeLabel};
    } else if (!wakeLabels_.isEmpty()) {
        cfg_.wakeLabel = wakeLabels_.first();
    } else {
        cfg_.wakeLabel = appconfig::kDefaultWakeLabel;
        wakeLabels_ << cfg_.wakeLabel;
    }
    buildUi();
    refreshWakeLabelText();

    pipeline_ = new KwsPipeline(factory_, this);
    connect(pipeline_, &KwsPipeline::ready, this, &MainWindow::onPipelineReady);
    connect(pipeline_, &KwsPipeline::failed, this, &MainWindow::onPipelineFailed);
    connect(pipeline_, &KwsPipeline::wakeDetected, this, &MainWindow::onWake);
    connect(pipeline_, &KwsPipeline::feedError, this, &MainWindow::onFeedError);

    capture_ = new AlsaAudioSource(this);
    connect(capture_, &AlsaAudioSource::errorOccurred, this, &MainWindow::onCaptureError);

    uiTimer_ = new QTimer(this);
    uiTimer_->setInterval(500);
    connect(uiTimer_, &QTimer::timeout, this, &MainWindow::onUiTimer);
    uiTimer_->start();

    waveTimer_ = new QTimer(this);
    waveTimer_->setInterval(16);
    connect(waveTimer_, &QTimer::timeout, this, &MainWindow::onWaveTimer);
    waveTimer_->start();

    wakeUiTimer_ = new QTimer(this);
    wakeUiTimer_->setSingleShot(true);
    wakeUiTimer_->setInterval(2500);
    connect(wakeUiTimer_, &QTimer::timeout, this, &MainWindow::onWakeUiTimeout);

    updateModelPill(QStringLiteral("● Model Loading"), kAmber);
    updateHeaderState(QStringLiteral("● LOADING"), kAmber);
    updateStageChip(QStringLiteral("●  STOPPED"), QStringLiteral("rgba(110,165,255,30)"),
                    QStringLiteral("#8fb2ff"), QStringLiteral("rgba(140,180,255,80)"));
    setControlEnabled(false);
    resetStage();
    preloadEngine();
}

MainWindow::~MainWindow() {
    if (capture_) capture_->stop();
    if (pipeline_) pipeline_->stop();
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("Quectel PI M2 · Keyword Spotting Demo"));

    const QString style = QString::fromUtf8(R"CSS(
QWidget { font-family: "DejaVu Sans"; color: #132a4a; font-size: 13px; }
#page { background: #e6edf9; }
#header { background: #f4f8ff; border: 1px solid #e3eaf7; border-radius: 10px; }
#card { background: #ffffff; border: 1px solid #e3eaf7; border-radius: 10px; }
#title { font-size: 17px; font-weight: 700; }
#cardTitle { font-size: 12px; font-weight: 700; color: #8fa5ce; letter-spacing: 1px; }
#muted { font-size: 12px; color: #58709b; }
#body { font-size: 13px; font-weight: 700; }
#pill { font-size: 12px; font-weight: 700; background: transparent; border: none; padding: 0; }
#chip { font-size: 12px; color: #58709b; background: transparent; border: none; }
#wakeLabel { font-size: 34px; font-weight: 700; color: #ffffff; }
#stageHint { font-size: 13px; color: #b9ccec; }
#levelCaption { font-size: 11px; font-weight: 700; color: #8fa5ce; letter-spacing: 1px; }
#levelValue { font-size: 12px; font-weight: 700; color: #58709b; }
#startButton { border: none; border-radius: 10px; color: #ffffff; font-size: 15px; font-weight: 700;
               background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #4f80ff, stop:1 #2a54e8); }
#startButton:disabled { background: #c3cfe6; }
#exitButton { background: #fff1f1; border: 1px solid #f3c7c7; border-radius: 10px;
              color: #c81e1e; font-size: 16px; font-weight: 700; }
#exitButton:hover { background: #ffe4e4; }
#tile { background: #fbfcff; border: 1px solid #e3eaf7; border-radius: 8px; }
#tileLabel { font-size: 10px; font-weight: 700; color: #8fa5ce; letter-spacing: 1px; }
#tileValue { font-size: 20px; font-weight: 700; color: #132a4a; }
#sliderLabel { font-size: 13px; font-weight: 700; }
#sliderValue { font-size: 12px; color: #58709b; }
#keywordBox { background: transparent; border: none; }
#keywordCaption { font-size: 10px; font-weight: 700; color: #8fa5ce; letter-spacing: 1px; }
#keywordValue { font-size: 12px; font-weight: 700; color: #2f5bff; }
#editKeyword { background: transparent; border: none; border-radius: 0;
               color: #2f5bff; font-size: 11px; font-weight: 700; }
#editKeyword:hover { color: #1d3fd0; text-decoration: underline; }
#editKeyword:disabled { color: #a9b8d4; }
#inputField { background: #fbfcff; border: 1px solid #d3ddf0; border-radius: 8px;
              padding: 6px 8px; font-size: 12px; }
#inputField:focus { border-color: #2f5bff; }
#item { background: #ffffff; border: 1px solid #d3ddf0; border-left: 3px solid #2f5bff; border-radius: 8px; }
#idxChip { background: #e9efff; color: #2f5bff; font-size: 11px; font-weight: 700; border-radius: 5px; }
#badgeHit { background: #e7f8ee; color: #138a4e; font-size: 10px; font-weight: 700; border-radius: 4px; padding: 0 7px; }
#itemScore { font-size: 11px; font-weight: 700; color: #2f5bff; }
#itemText { font-size: 13px; font-weight: 600; }
#itemTime { font-size: 11px; color: #8fa5ce; }
QScrollArea { border: none; background: transparent; }
#scrollBody { background: transparent; }
QSlider::groove:horizontal { height: 4px; background: #e3eaf7; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #2f5bff; border-radius: 2px; }
QSlider::handle:horizontal { width: 16px; height: 16px; margin: -6px 0; border-radius: 8px; background: #2f5bff; }
QSlider::sub-page:horizontal:disabled { background: #cbd6e8; }
QSlider::handle:horizontal:disabled { background: #cbd6e8; }
QDialog { background: #ffffff; }
)CSS");
    setStyleSheet(style);

    double s = 1.0;
    if (QScreen* scr = QGuiApplication::primaryScreen()) {
        const QRect avail = scr->availableGeometry();
        s = std::min((avail.width() * 0.92) / kPageW, (avail.height() * 0.92) / kPageH);
        s = std::min(1.0, s);
    }
    s = std::max(0.6, s);
    uiScale_ = s;

    page_ = new QWidget;
    page_->setObjectName(QStringLiteral("page"));
    page_->setFixedSize(int(kPageW * s), int(kPageH * s));

    auto* central = new QWidget;
    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->addWidget(page_);
    setCentralWidget(central);
    setFixedSize(int(kPageW * s) + 32, int(kPageH * s) + 32);

    auto card = [this](const QString& name) {
        auto* f = new QFrame(page_);
        f->setObjectName(QStringLiteral("card"));
        f->setToolTip(name);
        return f;
    };
    auto cardTitle = [this](QWidget* parent, const QString& text, const char* kind) {
        auto* box = new QWidget(parent);
        auto* lay = new QHBoxLayout(box);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(int(6 * uiScale_));
        auto* ic = new QLabel(box);
        ic->setPixmap(makeSectionIcon(kind, uiScale_));
        ic->setFixedSize(int(16 * uiScale_), int(16 * uiScale_));
        auto* l = new QLabel(text, box);
        l->setObjectName(QStringLiteral("cardTitle"));
        l->setAlignment(Qt::AlignVCenter);
        lay->addWidget(ic);
        lay->addWidget(l);
        lay->addStretch(1);
        return box;
    };
    auto makeChip = [this](QWidget* parent, const QString& text, int width) {
        auto* l = new QLabel(text, parent);
        l->setObjectName(QStringLiteral("chip"));
        l->setAlignment(Qt::AlignCenter);
        l->setFixedWidth(int(width * uiScale_));
        return l;
    };

    header_ = new QFrame(page_);
    header_->setObjectName(QStringLiteral("header"));
    brandIcon_ = new QLabel(header_);
    brandIcon_->setPixmap(makeBrandIcon(int(32 * uiScale_)));
    brandIcon_->setScaledContents(true);
    auto* title = new QLabel(QStringLiteral("Quectel PI M2 · Keyword Spotting Demo"), header_);
    title->setObjectName(QStringLiteral("title"));
    modelPill_ = new QLabel(QStringLiteral("● Model Loading"), header_);
    modelPill_->setObjectName(QStringLiteral("pill"));
    modelPill_->setAlignment(Qt::AlignCenter);
    statePill_ = new QLabel(QStringLiteral("● LOADING"), header_);
    statePill_->setObjectName(QStringLiteral("pill"));
    statePill_->setAlignment(Qt::AlignCenter);

    wakeCard_ = card(QStringLiteral("wake word"));
    QWidget* wakeTitle = cardTitle(wakeCard_, QStringLiteral("WAKE WORD"), "target");

    stage_ = new StageWidget(wakeCard_);
    liveChip_ = new QLabel(QStringLiteral("●  STOPPED"), stage_);
    liveChip_->setAlignment(Qt::AlignCenter);
    scoreChip_ = new QLabel(QStringLiteral("SCORE -  ·  THR %1").arg(threshold_, 0, 'f', 2),
                            stage_);
    scoreChip_->setAlignment(Qt::AlignCenter);
    scoreChip_->setStyleSheet(
        QStringLiteral("font-size:11px;font-weight:700;color:#cfe0ff;"
                       "background:rgba(110,165,255,26);border:1px solid rgba(140,180,255,70);"
                       "border-radius:11px;"));
    wakeLabelText_ = new QLabel(cfg_.wakeLabel, stage_);
    wakeLabelText_->setObjectName(QStringLiteral("wakeLabel"));
    wakeLabelText_->setAlignment(Qt::AlignCenter);
    wakeLabelText_->setWordWrap(false);
    wakeLabelText_->setToolTip(
        QStringLiteral("English label for the wake phrase (click Edit in CONTROL to change)"));
    stageHint_ = new QLabel(stage_);
    stageHint_->setObjectName(QStringLiteral("stageHint"));
    stageHint_->setAlignment(Qt::AlignCenter);
    stageHint_->setWordWrap(false);

    hintText_ = new QLabel(wakeCard_);
    hintText_->setObjectName(QStringLiteral("muted"));
    hintText_->setText(QStringLiteral("Open-vocabulary KWS · streaming"));
    QLabel* chip16k = makeChip(wakeCard_, QStringLiteral("16 kHz"), 60);
    QLabel* chipMono = makeChip(wakeCard_, QStringLiteral("Mono"), 52);
    QLabel* chip10ms = makeChip(wakeCard_, QStringLiteral("10 ms"), 62);
    QLabel* chipStream = makeChip(wakeCard_, QStringLiteral("Streaming"), 92);

    waveCard_ = card(QStringLiteral("microphone input"));
    QWidget* waveTitle = cardTitle(waveCard_, QStringLiteral("MICROPHONE INPUT"), "mic");
    waveTag_ = new QLabel(QStringLiteral("10 ms frames"), waveCard_);
    waveTag_->setObjectName(QStringLiteral("chip"));
    waveTag_->setAlignment(Qt::AlignCenter);
    inputWave_ = new WaveformWidget(QColor(kBlue), waveCard_);
    QLabel* levelCaption = new QLabel(QStringLiteral("LEVEL"), waveCard_);
    levelCaption->setObjectName(QStringLiteral("levelCaption"));
    levelBar_ = new LevelBar(waveCard_);
    levelValue_ = new QLabel(QStringLiteral("0%"), waveCard_);
    levelValue_->setObjectName(QStringLiteral("levelValue"));
    levelValue_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    waveNote_ = new QLabel(
        QStringLiteral("16 kHz · mono · 160-sample frames (10 ms) streamed to the engine"),
        waveCard_);
    waveNote_->setObjectName(QStringLiteral("muted"));

    statsCard_ = card(QStringLiteral("session statistics"));
    QWidget* statsTitle = cardTitle(statsCard_, QStringLiteral("SESSION"), "pulse");
    auto makeTile = [this](QWidget* parent, int x, const QString& caption) -> QLabel* {
        auto* tile = new QFrame(parent);
        tile->setObjectName(QStringLiteral("tile"));
        auto* cap = new QLabel(caption, tile);
        cap->setObjectName(QStringLiteral("tileLabel"));
        auto* val = new QLabel(QStringLiteral("-"), tile);
        val->setObjectName(QStringLiteral("tileValue"));
        val->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        place(tile, x, 44, 163, 54, uiScale_);
        place(cap, 12, 7, 139, 14, uiScale_);
        place(val, 12, 23, 139, 26, uiScale_);
        return val;
    };
    framesValue_ = makeTile(statsCard_, 16, QStringLiteral("FRAMES"));
    framesValue_->setText(QStringLiteral("0"));
    audioValue_ = makeTile(statsCard_, 191, QStringLiteral("AUDIO"));
    audioValue_->setText(QStringLiteral("00:00.0"));
    latencyValue_ = makeTile(statsCard_, 366, QStringLiteral("AVG LATENCY"));
    latencyValue_->setText(QStringLiteral("n/a"));
    rtfValue_ = makeTile(statsCard_, 541, QStringLiteral("RTF"));
    rtfValue_->setText(QStringLiteral("n/a"));
    engineLabel_ = new QLabel(QStringLiteral("AIVOX OVKWS · loading..."), statsCard_);
    engineLabel_->setObjectName(QStringLiteral("muted"));

    controlCard_ = card(QStringLiteral("control"));
    QWidget* controlTitle = cardTitle(controlCard_, QStringLiteral("CONTROL"), "sliders");
    listenButton_ = new QPushButton(QStringLiteral("Start Listening"), controlCard_);
    listenButton_->setObjectName(QStringLiteral("startButton"));
    listenButton_->setCursor(Qt::PointingHandCursor);
    listenButton_->setIcon(makeTransportIcon(false));
    listenButton_->setIconSize(QSize(int(18 * uiScale_), int(18 * uiScale_)));

    auto makeSliderRow = [this](QWidget* parent, const QString& label, int max, int value,
                                QSlider** outSlider, QLabel** outValue) -> QWidget* {
        auto* row = new QWidget(parent);
        row->setAttribute(Qt::WA_TranslucentBackground);
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(10);
        auto* l = new QLabel(label, row);
        l->setObjectName(QStringLiteral("sliderLabel"));
        l->setFixedWidth(int(118 * uiScale_));
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, max);
        slider->setValue(value);
        slider->setPageStep(5);
        slider->setCursor(Qt::PointingHandCursor);
        auto* v = new QLabel(row);
        v->setObjectName(QStringLiteral("sliderValue"));
        v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        v->setFixedWidth(int(52 * uiScale_));
        lay->addWidget(l);
        lay->addWidget(slider, 1);
        lay->addWidget(v);
        *outSlider = slider;
        *outValue = v;
        return row;
    };
    QWidget* thresholdRow = makeSliderRow(
        controlCard_, QStringLiteral("Threshold:"), 100, int(std::lround(threshold_ * 100.0)),
        &thresholdSlider_, &thresholdValue_);
    thresholdValue_->setText(QString::number(threshold_, 'f', 2));
    QWidget* gainRow = makeSliderRow(
        controlCard_, QStringLiteral("Mic gain:"), 30, int(std::lround(micGainDb_)),
        &gainSlider_, &gainValue_);
    gainValue_->setText(QStringLiteral("%1 dB").arg(int(std::lround(micGainDb_))));

    auto* keywordBox = new QFrame(controlCard_);
    keywordBox->setObjectName(QStringLiteral("keywordBox"));
    auto* kwCap = new QLabel(QStringLiteral("WAKE WORD"), keywordBox);
    kwCap->setObjectName(QStringLiteral("keywordCaption"));
    keywordValue_ = new QLabel(cfg_.wakeTokens.isEmpty() ? QStringLiteral("(not set)")
                                                         : cfg_.wakeTokens,
                               keywordBox);
    keywordValue_->setObjectName(QStringLiteral("keywordValue"));
    keywordValue_->setWordWrap(false);
    keywordValue_->setToolTip(cfg_.keywordsFile + QStringLiteral("\n") + cfg_.wakeLabel);
    editKeywordButton_ = new QPushButton(QStringLiteral("Edit"), keywordBox);
    editKeywordButton_->setObjectName(QStringLiteral("editKeyword"));
    editKeywordButton_->setCursor(Qt::PointingHandCursor);
    editKeywordButton_->setToolTip(
        QStringLiteral("Edit the wake word and write it back to keywords.txt"));
    place(kwCap, 12, 5, 120, 12, uiScale_);
    place(keywordValue_, 12, 19, 344, 16, uiScale_);
    place(editKeywordButton_, 368, 9, 76, 22, uiScale_);

    cpuChip_ = makeChip(controlCard_, QStringLiteral("CPU -"), 150);
    memChip_ = makeChip(controlCard_, QStringLiteral("MEM -"), 150);
    licenseLabel_ = new QLabel(controlCard_);
    licenseLabel_->setObjectName(QStringLiteral("muted"));
    licenseLabel_->setText(QStringLiteral("License: ") + cfg_.licenseDir);
    licenseLabel_->setToolTip(cfg_.licenseDir);

    historyCard_ = card(QStringLiteral("wake-up history"));
    QWidget* historyTitle = cardTitle(historyCard_, QStringLiteral("WAKE-UP HISTORY"), "list");
    historyScroll_ = new QScrollArea(historyCard_);
    historyScroll_->setWidgetResizable(true);
    historyScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    historyBody_ = new QWidget(historyScroll_);
    historyBody_->setObjectName(QStringLiteral("scrollBody"));
    historyList_ = new QVBoxLayout(historyBody_);
    historyList_->setContentsMargins(0, 0, 0, 0);
    historyList_->setSpacing(8);
    historyList_->addStretch(1);
    historyScroll_->setWidget(historyBody_);

    exitButton_ = new QPushButton(QStringLiteral("Exit"), page_);
    exitButton_->setObjectName(QStringLiteral("exitButton"));
    exitButton_->setCursor(Qt::PointingHandCursor);
    exitButton_->setIcon(makeGlyphIcon("exit", QColor(kRed), int(16 * uiScale_)));
    exitButton_->setIconSize(QSize(int(16 * uiScale_), int(16 * uiScale_)));

    place(header_, 0, 0, 1240, 52, s);
    place(brandIcon_, 16, 10, 32, 32, s);
    place(title, 60, 0, 700, 52, s);
    place(modelPill_, 886, 13, 170, 26, s);
    place(statePill_, 1064, 13, 160, 26, s);

    place(wakeCard_, 0, 64, 720, 300, s);
    place(wakeTitle, 16, 16, 300, 18, s);
    place(stage_, 16, 42, 688, 206, s);
    place(liveChip_, 16, 14, 118, 22, s);
    place(scoreChip_, 500, 14, 172, 22, s);
    place(wakeLabelText_, 32, 50, 624, 58, s);
    place(stageHint_, 32, 110, 624, 24, s);
    place(hintText_, 16, 256, 320, 24, s);
    place(chip16k, 352, 256, 60, 22, s);
    place(chipMono, 420, 256, 52, 22, s);
    place(chip10ms, 480, 256, 62, 22, s);
    place(chipStream, 550, 256, 92, 22, s);

    place(waveCard_, 0, 376, 720, 220, s);
    place(waveTitle, 16, 16, 300, 18, s);
    place(waveTag_, 600, 14, 104, 20, s);
    place(inputWave_, 16, 46, 688, 120, s);
    place(levelCaption, 16, 174, 40, 16, s);
    place(levelBar_, 60, 176, 470, 12, s);
    place(levelValue_, 540, 170, 164, 20, s);
    place(waveNote_, 16, 196, 688, 16, s);

    place(statsCard_, 0, 608, 720, 138, s);
    place(statsTitle, 16, 16, 300, 18, s);
    place(engineLabel_, 16, 108, 688, 18, s);

    place(controlCard_, 736, 64, 488, 284, s);
    place(controlTitle, 16, 16, 300, 18, s);
    place(listenButton_, 16, 46, 456, 52, s);
    place(thresholdRow, 16, 110, 456, 26, s);
    place(gainRow, 16, 146, 456, 26, s);
    place(keywordBox, 16, 184, 456, 40, s);
    place(cpuChip_, 16, 236, 150, 26, s);
    place(memChip_, 174, 236, 150, 26, s);
    place(licenseLabel_, 16, 266, 456, 14, s);

    place(historyCard_, 736, 360, 488, 322, s);
    place(historyTitle, 16, 16, 300, 18, s);
    place(historyScroll_, 16, 46, 456, 260, s);

    place(exitButton_, 736, 694, 488, 52, s);

    applyLayout(s);

    connect(listenButton_, &QPushButton::clicked, this,
            [this] { onToggleListening(!listening_); });
    connect(thresholdSlider_, &QSlider::valueChanged, this, &MainWindow::onThresholdChanged);
    connect(gainSlider_, &QSlider::valueChanged, this, &MainWindow::onGainChanged);
    connect(editKeywordButton_, &QPushButton::clicked, this, &MainWindow::onEditWakeWord);
    connect(exitButton_, &QPushButton::clicked, this, [this] { close(); });
}

void MainWindow::applyLayout(double s) {
    uiScale_ = s;
    for (QWidget* w : page_->findChildren<QWidget*>()) {
        const QVariant dx = w->property("dx");
        if (!dx.isValid()) continue;
        w->setGeometry(int(dx.toInt() * s), int(w->property("dy").toInt() * s),
                       int(w->property("dw").toInt() * s), int(w->property("dh").toInt() * s));
    }
}

void MainWindow::preloadEngine() {
    kws::EngineConfig ec;
    ec.tokensFile = cfg_.tokensFile;
    ec.keywordsFile = cfg_.keywordsFile;
    ec.licenseDir = cfg_.licenseDir;
    ec.authCode = cfg_.authCode;
    ec.boardModule = cfg_.boardModule;
    ec.deviceNode = cfg_.deviceNode;
    ec.atIndex = cfg_.atIndex;
    ec.threshold = threshold_;
    pipeline_->start(ec);
}

void MainWindow::onToggleListening(bool on) {
    if (on) startListening();
    else    stopListening();
}

void MainWindow::startListening() {
    if (listening_ || !pipelineReady_ || !capture_) return;

    inputView_.clear();
    levelDisplay_ = 0.0f;
    inputWave_->setData({});
    levelBar_->setValue(0.0f);
    levelValue_->setText(QStringLiteral("0%"));

    capture_->stop();
    QString error;
    if (!capture_->open(cfg_.captureDevice, &error)) {
        onCaptureError(error);
        return;
    }
    if (!capture_->start(
            [this](const kws::AudioFrame& f) {
                QMetaObject::invokeMethod(
                    this, [this, f] { onFrame(f); }, Qt::QueuedConnection);
            },
            &error)) {
        onCaptureError(error);
        return;
    }

    listening_ = true;
    pipeline_->requestReset();
    listenButton_->setText(QStringLiteral("Stop Listening"));
    listenButton_->setIcon(makeTransportIcon(true));
    updateHeaderState(QStringLiteral("● LISTENING"), kBlue);
    updateStageChip(QStringLiteral("●  LISTENING"), QStringLiteral("rgba(110,165,255,36)"),
                    QStringLiteral("#cfe0ff"), QStringLiteral("rgba(140,180,255,90)"));
    scoreChip_->setText(QStringLiteral("SCORE -  ·  THR %1").arg(threshold_, 0, 'f', 2));
    waveNote_->setText(QStringLiteral(
        "Listening · 16 kHz mono · 160-sample frames (10 ms) streamed to the engine"));
    wakeLabelText_->setStyleSheet(QString());
    resetStage();
}

void MainWindow::stopListening() {
    if (capture_) capture_->stop();
    listening_ = false;
    if (wakeUiTimer_) wakeUiTimer_->stop();

    listenButton_->setText(QStringLiteral("Start Listening"));
    listenButton_->setIcon(makeTransportIcon(false));
    updateHeaderState(QStringLiteral("● READY"), kGreen);
    updateStageChip(QStringLiteral("●  STOPPED"), QStringLiteral("rgba(110,165,255,30)"),
                    QStringLiteral("#8fb2ff"), QStringLiteral("rgba(140,180,255,80)"));
    scoreChip_->setText(QStringLiteral("SCORE -  ·  THR %1").arg(threshold_, 0, 'f', 2));
    waveNote_->setText(QStringLiteral(
        "Idle · 16 kHz mono · 160-sample frames (10 ms) streamed to the engine"));
    wakeLabelText_->setStyleSheet(QString());
    levelDisplay_ = 0.0f;
    levelBar_->setValue(0.0f);
    levelValue_->setText(QStringLiteral("0%"));
    resetStage();
}

void MainWindow::onFrame(const kws::AudioFrame& frame) {
    if (!listening_ || !pipeline_) return;

    std::vector<int16_t> s = frame.samples;

    const double g = std::pow(10.0, micGainDb_ / 20.0);
    if (g != 1.0) {
        for (auto& v : s) {
            int x = int(std::lround(double(v) * g));
            if (x > 32767) x = 32767;
            else if (x < -32768) x = -32768;
            v = int16_t(x);
        }
    }

    int peak = 0;
    for (int16_t v : s) peak = std::max(peak, std::abs(int(v)));
    const float target = std::min(1.0f, float(peak) / 32768.0f * 2.5f);
    levelDisplay_ = levelDisplay_ * 0.8f + target * 0.2f;
    stage_->setLevel(levelDisplay_);

    const size_t cap = size_t(kws::kSampleRate);
    inputView_.insert(inputView_.end(), s.begin(), s.end());
    if (inputView_.size() > cap)
        inputView_.erase(inputView_.begin(), inputView_.begin() + (inputView_.size() - cap));

    kws::AudioFrame out;
    out.samples = std::move(s);
    pipeline_->submit(out);
}

void MainWindow::onPipelineReady(const QString& engineVersion) {
    pipelineReady_ = true;
    setControlEnabled(true);
    updateModelPill(QStringLiteral("● Model Ready"), kGreen);
    updateHeaderState(QStringLiteral("● READY"), kGreen);
    engineLabel_->setText(QStringLiteral("AIVOX OVKWS · %1").arg(
        engineVersion.isEmpty() ? QStringLiteral("ready") : engineVersion));
    engineLabel_->setToolTip(engineVersion);
    resetStage();

    if (resumeAfterReload_) {
        resumeAfterReload_ = false;
        startListening();
    }
}

void MainWindow::onPipelineFailed(const QString& error) {
    resumeAfterReload_ = false;
    pipelineReady_ = false;
    setControlEnabled(false);
    if (listening_ || (capture_ && capture_->isRunning())) stopListening();
    updateModelPill(QStringLiteral("● Model Error"), kRed);
    updateHeaderState(QStringLiteral("● FAILED"), kRed);
    engineLabel_->setText(QStringLiteral("KWS engine unavailable"));
    engineLabel_->setToolTip(error);
    stageHint_->setText(error);
    stageHint_->setStyleSheet(QStringLiteral("font-size:13px;color:#f08a8a;"));
    stageHint_->setToolTip(error);
}

void MainWindow::onFeedError(const QString& error) {
    if (error.isEmpty()) return;
    stageHint_->setText(QStringLiteral("Engine feed error: ") + error);
    stageHint_->setStyleSheet(QStringLiteral("font-size:13px;color:#f0c27a;"));
    stageHint_->setToolTip(error);
}

void MainWindow::onCaptureError(const QString& error) {
    const bool wasListening = listening_;
    if (capture_) capture_->stop();
    listening_ = false;
    if (wasListening) {
        listenButton_->setText(QStringLiteral("Start Listening"));
        listenButton_->setIcon(makeTransportIcon(false));
    }
    updateHeaderState(QStringLiteral("● CAPTURE ERROR"), kRed);
    updateStageChip(QStringLiteral("●  STOPPED"), QStringLiteral("rgba(255,120,120,30)"),
                    QStringLiteral("#ffb0b0"), QStringLiteral("rgba(255,140,140,90)"));
    stage_->setListening(false);
    stageHint_->setText(QStringLiteral("Audio capture failed: ") + error);
    stageHint_->setStyleSheet(QStringLiteral("font-size:13px;color:#f08a8a;"));
    stageHint_->setToolTip(error);
    waveNote_->setText(QStringLiteral("Capture stopped · check the ALSA device (--cap)"));
}

void MainWindow::onWake(const kws::WakeEvent& event) {

    if (!listening_) return;

    const QString when = event.detectedAt.isValid()
                             ? event.detectedAt.toString(QStringLiteral("HH:mm:ss"))
                             : QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));

    scoreChip_->setText(QStringLiteral("SCORE %1  ·  THR %2")
                            .arg(event.score, 0, 'f', 2)
                            .arg(event.threshold, 0, 'f', 2));
    updateStageChip(QStringLiteral("●  DETECTED"), QStringLiteral("rgba(60,225,150,38)"),
                    QStringLiteral("#8dffc4"), QStringLiteral("rgba(80,240,170,100)"));
    wakeLabelText_->setStyleSheet(wakeLabelStyle(QStringLiteral("#8dffc4")));
    stageHint_->setText(QStringLiteral("Detected at %1 · score %2")
                            .arg(when)
                            .arg(event.score, 0, 'f', 2));
    stageHint_->setStyleSheet(QStringLiteral("font-size:13px;color:#8dffc4;"));
    stageHint_->setToolTip(event.keyword);

    if (!event.keyword.trimmed().isEmpty())
        showWakeLabel(QStringList() << event.keyword.trimmed(), QStringLiteral("#8dffc4"));
    stage_->triggerHit();

    kws::WakeEvent shown = event;
    shown.displayName = wakeLabelByChinese_.value(event.keyword, cfg_.wakeLabel);
    appendHistory(shown);

    if (listening_ && wakeUiTimer_) wakeUiTimer_->start();
}

void MainWindow::onWakeUiTimeout() {
    if (!listening_) {
        updateStageChip(QStringLiteral("●  STOPPED"), QStringLiteral("rgba(110,165,255,30)"),
                        QStringLiteral("#8fb2ff"), QStringLiteral("rgba(140,180,255,80)"));
        resetStage();
        return;
    }
    updateStageChip(QStringLiteral("●  LISTENING"), QStringLiteral("rgba(110,165,255,36)"),
                    QStringLiteral("#cfe0ff"), QStringLiteral("rgba(140,180,255,90)"));
    wakeLabelText_->setStyleSheet(QString());
    refreshWakeLabelText();
    scoreChip_->setText(QStringLiteral("SCORE -  ·  THR %1").arg(threshold_, 0, 'f', 2));
    resetStage();
}

void MainWindow::onThresholdChanged(int value) {
    threshold_ = float(value) / 100.0f;
    thresholdValue_->setText(QString::number(threshold_, 'f', 2));
    scoreChip_->setText(QStringLiteral("SCORE -  ·  THR %1").arg(threshold_, 0, 'f', 2));
    if (pipeline_) {
        QString error;
        if (!pipeline_->setThreshold(threshold_, &error) && !error.isEmpty())
            stageHint_->setToolTip(error);
    }
}

void MainWindow::onGainChanged(int value) {
    micGainDb_ = double(value);
    gainValue_->setText(QStringLiteral("%1 dB").arg(value));
}

void MainWindow::refreshWakeLabelText() {
    if (!wakeLabelText_) return;
    QStringList words = wakeWordsChinese_;
    if (words.isEmpty()) words = wakeLabels_;
    if (words.isEmpty()) words << cfg_.wakeLabel;
    showWakeLabel(words, QStringLiteral("#ffffff"));
    wakeLabelText_->setToolTip(words.size() > 1
                                   ? QStringLiteral("Wake words:\n") + words.join(QLatin1Char('\n'))
                                   : QString());
}

void MainWindow::showWakeLabel(const QStringList& words, const QString& color) {
    if (!wakeLabelText_) return;
    const QString text = words.join(QStringLiteral("  ·  "));
    int size = 34;
    if (words.size() >= 2) size = 24;
    if (words.size() >= 4) size = 18;
    if (words.size() >= 6) size = 14;
    const int maxWidth = int(624 * uiScale_);
    auto estimatedWidth = [&](int px) {
        double w = 0.0;
        for (const QChar c : text) w += (c.unicode() > 0x2E80 ? 1.0 : 0.62) * px;
        return int(w);
    };
    while (size > 11 && estimatedWidth(size) > maxWidth) --size;
    wakeLabelFontPx_ = size;
    wakeLabelText_->setWordWrap(true);
    wakeLabelText_->setText(text);
    wakeLabelText_->setStyleSheet(wakeLabelStyle(color));
}

QString MainWindow::wakeLabelStyle(const QString& color) const {
    return QStringLiteral("font-size:%1px;font-weight:700;color:%2;")
        .arg(wakeLabelFontPx_)
        .arg(color);
}

bool MainWindow::applyWakeWords(const QString& text, QString* error) {
    const QSet<QString> vocab = readVocabulary(cfg_.tokensFile);
    if (vocab.isEmpty()) {
        *error = QStringLiteral("Cannot read the vocabulary: ") + cfg_.tokensFile;
        return false;
    }

    QStringList tokenLines, labelLines, chineseLines;
    const QStringList rawLines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < rawLines.size(); ++i) {
        const QString raw = rawLines.at(i).trimmed();
        if (raw.isEmpty() || raw.startsWith(QLatin1Char('#'))) continue;

        for (const QChar c : raw) {
            const int u = c.unicode();
            if (u < 0x4E00 || u > 0x9FFF) {
                *error = QStringLiteral("Line %1: only Chinese characters are supported.")
                             .arg(i + 1);
                return false;
            }
        }

        QStringList toks;
        QString sug, err;
        if (!parseWakeInput(raw, cfg_.tokensFile, cfg_.pinyinFile, &toks, &sug, &err)) {
            *error = QStringLiteral("Line %1: %2").arg(i + 1).arg(err);
            return false;
        }

        QStringList missing;
        for (const QString& t : toks) {
            if (!vocab.contains(t) && !missing.contains(t)) missing << t;
        }
        if (!missing.isEmpty()) {
            *error = QStringLiteral("Line %1: token(s) not in tokens.txt: %2")
                         .arg(i + 1)
                         .arg(missing.join(QLatin1Char(' ')));
            return false;
        }
        if (sug.isEmpty()) {
            *error = QStringLiteral("Line %1: cannot build the display label.").arg(i + 1);
            return false;
        }

        tokenLines << toks.join(QLatin1Char(' '));
        labelLines << sug;
        chineseLines << raw;
    }
    if (tokenLines.isEmpty()) {
        *error = QStringLiteral("Enter at least one wake word.");
        return false;
    }

    QString content;
    for (int i = 0; i < tokenLines.size(); ++i) {

        content += tokenLines.at(i) + QStringLiteral(" @") + chineseLines.at(i) + QLatin1Char('\n');
    }

    QFile f(cfg_.keywordsFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        *error = QStringLiteral("Cannot write ") + cfg_.keywordsFile + QStringLiteral(": ") +
                 f.errorString();
        return false;
    }
    f.write(content.toUtf8());
    f.close();

    cfg_.wakeTokens = tokenLines.join(QLatin1Char('\n'));
    cfg_.wakeLabel = labelLines.first();
    wakeLabels_ = labelLines;
    wakeWordsChinese_ = chineseLines;
    wakeLabelByChinese_.clear();
    for (int i = 0; i < chineseLines.size(); ++i)
        wakeLabelByChinese_[chineseLines.at(i)] = labelLines.at(i);
    refreshWakeLabelText();

    keywordValue_->setText(tokenLines.size() > 1
                               ? tokenLines.first() +
                                     QStringLiteral("  (+%1)").arg(tokenLines.size() - 1)
                               : tokenLines.first());
    keywordValue_->setToolTip(cfg_.keywordsFile + QStringLiteral("\n") + content.trimmed());
    wakeLabelText_->setToolTip(chineseLines.size() > 1
                                   ? QStringLiteral("Wake words:\n") + chineseLines.join(QLatin1Char('\n'))
                                   : QString());
    return true;
}

void MainWindow::onEditWakeWord() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Edit Wake Words"));
    dlg.setModal(true);

    auto* form = new QFormLayout(&dlg);
    form->setContentsMargins(18, 18, 18, 14);
    form->setSpacing(10);

    auto* wordsEdit = new QPlainTextEdit(&dlg);
    wordsEdit->setObjectName(QStringLiteral("inputField"));
    wordsEdit->setPlaceholderText(QStringLiteral("小远同学\n小远你好"));
    wordsEdit->setMinimumSize(int(480 * uiScale_), int(108 * uiScale_));

    QSettings store(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Quectel"),
                    QStringLiteral("KwsDemo"));
    const QString savedInput = store.value(QStringLiteral("wake_words_input")).toString();
    if (!savedInput.isEmpty()) wordsEdit->setPlainText(savedInput);
    QObject::connect(&dlg, &QDialog::finished, &dlg, [&] {
        store.setValue(QStringLiteral("wake_words_input"), wordsEdit->toPlainText());
    });

    auto* preview = new QLabel(&dlg);
    preview->setWordWrap(true);
    preview->setStyleSheet(QStringLiteral("font-size:12px;color:#138a4e;"));

    auto* errorLabel = new QLabel(&dlg);
    errorLabel->setWordWrap(true);
    errorLabel->setStyleSheet(QStringLiteral("font-size:12px;color:#c81e1e;"));

    form->addRow(QStringLiteral("Wake words (Chinese, one per line):"), wordsEdit);
    form->addRow(QStringLiteral("Preview:"), preview);
    form->addRow(errorLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancel"));
    form->addRow(buttons);

    QObject::connect(wordsEdit, &QPlainTextEdit::textChanged, &dlg, [&] {
        errorLabel->setText(QString());
        QStringList lines;
        QString err;
        const QStringList rawLines = wordsEdit->toPlainText().split(QLatin1Char('\n'));
        for (int i = 0; i < rawLines.size(); ++i) {
            const QString raw = rawLines.at(i).trimmed();
            if (raw.isEmpty() || raw.startsWith(QLatin1Char('#'))) continue;
            for (const QChar c : raw) {
                const int u = c.unicode();
                if (u < 0x4E00 || u > 0x9FFF) {
                    preview->setText(QStringLiteral("Line %1: only Chinese characters are supported.")
                                         .arg(i + 1));
                    preview->setStyleSheet(QStringLiteral("font-size:12px;color:#c81e1e;"));
                    return;
                }
            }
            QStringList toks;
            QString sug;
            if (!parseWakeInput(raw, cfg_.tokensFile, cfg_.pinyinFile, &toks, &sug, &err)) {
                preview->setText(QStringLiteral("Line %1: %2").arg(i + 1).arg(err));
                preview->setStyleSheet(QStringLiteral("font-size:12px;color:#c81e1e;"));
                return;
            }
            lines << toks.join(QLatin1Char(' '));
        }
        preview->setText(QStringLiteral("Tokens:\n") + lines.join(QLatin1Char('\n')));
        preview->setStyleSheet(QStringLiteral("font-size:12px;color:#138a4e;"));
    });

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&] {
        QString err;
        if (!applyWakeWords(wordsEdit->toPlainText(), &err)) {
            errorLabel->setText(err);
            return;
        }
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    const bool wasListening = listening_;
    if (wasListening) stopListening();
    resumeAfterReload_ = wasListening;
    pipelineReady_ = false;
    setControlEnabled(false);
    updateModelPill(QStringLiteral("● Model Loading"), kAmber);
    updateHeaderState(QStringLiteral("● LOADING"), kAmber);
    engineLabel_->setText(QStringLiteral("AIVOX OVKWS · reloading..."));
    preloadEngine();
}

void MainWindow::updateHeaderState(const QString& text, const QString& color) {
    statePill_->setText(text);
    statePill_->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;"
                       "font-size:12px;font-weight:700;padding:0;")
            .arg(color));
}

void MainWindow::updateModelPill(const QString& text, const QString& color) {
    modelPill_->setText(text);
    modelPill_->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;"
                       "font-size:12px;font-weight:700;padding:0;")
            .arg(color));
}

void MainWindow::updateStageChip(const QString& text, const QString& bg, const QString& fg,
                                 const QString& border) {
    liveChip_->setText(text);
    liveChip_->setStyleSheet(
        QStringLiteral("font-size:11px;font-weight:700;color:%1;background:%2;"
                       "border:1px solid %3;border-radius:11px;")
            .arg(fg, bg, border));
}

void MainWindow::resetStage() {
    if (!pipelineReady_) {
        stageHint_->setText(QStringLiteral("Engine loading, please wait..."));
        stage_->setListening(false);
    } else if (!listening_) {
        stageHint_->setText(QStringLiteral("Press Start, then say the wake word."));
        stage_->setListening(false);
    } else {
        stageHint_->setText(QStringLiteral("Listening... say the wake word."));
        stage_->setListening(true);
    }
    stageHint_->setStyleSheet(QStringLiteral("font-size:13px;color:#b9ccec;"));
    stageHint_->setToolTip(QString());
    wakeLabelText_->setStyleSheet(wakeLabelStyle(QStringLiteral("#ffffff")));
}

void MainWindow::setControlEnabled(bool on) {
    listenButton_->setEnabled(on);
    thresholdSlider_->setEnabled(on);
    gainSlider_->setEnabled(on);
    editKeywordButton_->setEnabled(on);
}

void MainWindow::appendHistory(const kws::WakeEvent& event) {
    ++historyCount_;
    const int itemH = int(54 * uiScale_);

    auto* item = new QFrame(historyBody_);
    item->setObjectName(QStringLiteral("item"));
    item->setFixedHeight(itemH);
    auto* lay = new QVBoxLayout(item);
    lay->setContentsMargins(10, 7, 10, 7);
    lay->setSpacing(2);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    auto* idx = new QLabel(QString::number(historyCount_).rightJustified(2, QLatin1Char('0')),
                           item);
    idx->setObjectName(QStringLiteral("idxChip"));
    idx->setAlignment(Qt::AlignCenter);
    idx->setFixedSize(int(26 * uiScale_), int(18 * uiScale_));
    auto* badge = new QLabel(QStringLiteral("DETECTED"), item);
    badge->setObjectName(QStringLiteral("badgeHit"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedHeight(int(18 * uiScale_));
    auto* score = new QLabel(QStringLiteral("score %1").arg(event.score, 0, 'f', 2), item);
    score->setObjectName(QStringLiteral("itemScore"));
    auto* time = new QLabel(event.detectedAt.isValid()
                                ? event.detectedAt.toString(QStringLiteral("HH:mm:ss"))
                                : QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                            item);
    time->setObjectName(QStringLiteral("itemTime"));
    time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(idx);
    row->addWidget(badge);
    row->addWidget(score);
    row->addStretch(1);
    row->addWidget(time);
    lay->addLayout(row);

    auto* text = new QLabel(
        event.displayName.isEmpty() ? event.keyword : event.displayName, item);
    text->setObjectName(QStringLiteral("itemText"));
    text->setContentsMargins(int(34 * uiScale_), 0, 0, 0);
    text->setWordWrap(false);
    text->setToolTip(event.keyword);
    lay->addWidget(text);

    historyList_->insertWidget(0, item);
    historyItems_.push_front(item);
    if (historyItems_.size() > 20) {
        auto* old = historyItems_.back();
        historyItems_.pop_back();
        historyList_->removeWidget(old);
        old->deleteLater();
    }
    historyScroll_->verticalScrollBar()->setValue(0);
}

void MainWindow::onUiTimer() {
    updateSessionStats();
    updateResources();
}

void MainWindow::onWaveTimer() {
    if (!inputWave_) return;
    const int w = std::max(1, inputWave_->width());
    inputWave_->setData(buildTrace(inputView_, size_t(kws::kSampleRate), w));
    levelBar_->setValue(levelDisplay_);
    levelValue_->setText(QStringLiteral("%1%").arg(int(levelDisplay_ * 100.0f)));
}

void MainWindow::updateSessionStats() {
    const kws::EngineStats st = pipeline_ ? pipeline_->stats() : kws::EngineStats{};

    framesValue_->setText(QString::number(st.frames));
    audioValue_->setText(formatSeconds(st.audioSec()));
    latencyValue_->setText(st.frames > 0 ? QStringLiteral("%1 ms").arg(st.avgMs(), 0, 'f', 1)
                                         : QStringLiteral("n/a"));
    rtfValue_->setText(st.frames > 0 ? QString::number(st.rtf(), 'f', 2)
                                     : QStringLiteral("n/a"));
}

void MainWindow::updateResources() {

    std::ifstream stat(QStringLiteral("/proc/stat").toStdString());
    std::string line;
    if (std::getline(stat, line)) {
        unsigned long long v[8] = {0};
        std::sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                    &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
        const unsigned long long idle = v[3] + v[4];
        const unsigned long long total = v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7];
        if (cpuLastTotal_ != 0 && total > cpuLastTotal_) {
            const double dTotal = double(total - cpuLastTotal_);
            const double dIdle = double(idle - cpuLastIdle_);
            const int pct = int(100.0 * (1.0 - dIdle / dTotal));
            cpuChip_->setText(QStringLiteral("CPU %1%").arg(pct));
        }
        cpuLastTotal_ = total;
        cpuLastIdle_ = idle;
    }

    std::ifstream mem(QStringLiteral("/proc/meminfo").toStdString());
    unsigned long long totalKb = 0, availKb = 0;
    while (std::getline(mem, line)) {
        if (line.rfind("MemTotal:", 0) == 0)
            std::sscanf(line.c_str(), "MemTotal: %llu", &totalKb);
        if (line.rfind("MemAvailable:", 0) == 0)
            std::sscanf(line.c_str(), "MemAvailable: %llu", &availKb);
    }
    if (totalKb > 0) {
        memChip_->setText(
            QStringLiteral("MEM %1 MB").arg((totalKb - availKb) / 1024));
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (listening_ || (pipeline_ && pipeline_->isReady())) stopListening();
    if (pipeline_) pipeline_->stop();
    QMainWindow::closeEvent(event);
}
