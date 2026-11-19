#include "GaugeView.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

namespace {
const QPointF kPivot(150, 372);
constexpr double kArmLength = 276;
constexpr double kMaxArmDegrees = 16;
const QPointF kDesignCenter(150, 220);
const QRectF kControlsButton(214, 12, 74, 26);

double weightRadius(int bpm) {
    // Like a real metronome, slower tempos have the weight further from the pivot.
    const double t = double(bpm - kMinBpm) / (kMaxBpm - kMinBpm);
    return 248 - t * (248 - 72);
}

QPainterPath trapezoid(double topY, double topHalf, double bottomY, double bottomHalf) {
    QPainterPath path;
    path.moveTo(150 - topHalf, topY);
    path.lineTo(150 + topHalf, topY);
    path.lineTo(150 + bottomHalf, bottomY);
    path.lineTo(150 - bottomHalf, bottomY);
    path.closeSubpath();
    return path;
}
}  // namespace

GaugeView::GaugeView(const FrameState* frame, QWidget* parent) : QOpenGLWidget(parent), frame_(frame) {
    setMinimumSize(220, 300);
    setMouseTracking(true);
}

void GaugeView::setTempo(int bpm, const QString& name) {
    bpm_ = bpm;
    tempoName_ = name;
    update();
}

void GaugeView::setControlsOpen(bool open) {
    controlsOpen_ = open;
    update();
}

void GaugeView::setTrainerStatus(const QString& status) { trainerStatus_ = status; }

void GaugeView::setNoAudio(bool noAudio) {
    if (noAudio == noAudio_) return;
    noAudio_ = noAudio;
    update();
}

// All drawing happens in a 284x428 design space around kDesignCenter, scaled to fit.
QTransform GaugeView::designTransform() const {
    const double scale = std::min(width() / 284.0, height() / 428.0);
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.scale(scale, scale);
    t.translate(-kDesignCenter.x(), -kDesignCenter.y());
    return t;
}

QRect GaugeView::controlsButtonRect() const { return designTransform().mapRect(kControlsButton).toAlignedRect(); }

const QPixmap& GaugeView::caseLayer() {
    const qreal ratio = devicePixelRatioF();
    const QSize pixels = size() * ratio;
    if (caseLayer_.size() == pixels) return caseLayer_;

    caseLayer_ = QPixmap(pixels);
    caseLayer_.setDevicePixelRatio(ratio);
    QPainter p(&caseLayer_);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), QColor("#1d1f23"));
    p.setTransform(designTransform());

    // Case.
    QLinearGradient wood(0, 0, 300, 0);
    wood.setColorAt(0.0, QColor("#4a2a14"));
    wood.setColorAt(0.5, QColor("#8a5428"));
    wood.setColorAt(1.0, QColor("#4a2a14"));
    p.setPen(QPen(QColor("#2b170a"), 2));
    p.setBrush(wood);
    p.drawPath(trapezoid(68, 92, 410, 128));
    p.setBrush(QColor("#3a2110"));
    p.drawRoundedRect(QRectF(10, 408, 280, 22), 4, 4);

    // Scale plate with tempo markings.
    QLinearGradient plate(0, 80, 0, 390);
    plate.setColorAt(0, QColor("#e4d6b0"));
    plate.setColorAt(1, QColor("#c8b687"));
    p.setPen(QPen(QColor("#6d5428"), 1.5));
    p.setBrush(plate);
    p.drawPath(trapezoid(82, 78, 392, 104));

    QFont font = p.font();
    font.setPixelSize(10);
    p.setFont(font);
    p.setPen(QPen(QColor("#4a3a18"), 1));
    for (int bpm = 40; bpm <= kMaxBpm; bpm += 30) {
        const double y = kPivot.y() - weightRadius(bpm);
        p.drawLine(QPointF(112, y), QPointF(128, y));
        p.drawLine(QPointF(172, y), QPointF(188, y));
        p.drawText(QRectF(80, y - 7, 30, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(bpm));
    }
    return caseLayer_;
}

void GaugeView::drawCorners(QPainter& p) {
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(22);
    p.setFont(font);
    p.setPen(QColor("#f1f1f1"));
    p.drawText(QRectF(12, 10, 104, 28), Qt::AlignLeft | Qt::AlignVCenter, QString("%1 BPM").arg(bpm_));
    font.setBold(false);
    font.setPixelSize(13);
    p.setFont(font);
    p.setPen(QColor("#e8a317"));
    p.drawText(QRectF(12, 38, 104, 18), Qt::AlignLeft | Qt::AlignVCenter, tempoName_);
    if (!trainerStatus_.isEmpty()) {
        font.setPixelSize(10);
        p.setFont(font);
        p.setPen(QColor("#9aa0aa"));
        p.drawText(QRectF(12, 54, 170, 14), Qt::AlignLeft | Qt::AlignVCenter, trainerStatus_);
    }

    p.setPen(QPen(controlsOpen_ ? QColor("#e8a317") : QColor("#454a53"), 1));
    p.setBrush(controlsOpen_ ? QColor("#e8a317") : QColor("#30343b"));
    p.drawRoundedRect(kControlsButton, 4, 4);
    font.setPixelSize(12);
    font.setBold(controlsOpen_);
    p.setFont(font);
    p.setPen(controlsOpen_ ? QColor("#16171a") : QColor("#e6e6e6"));
    p.drawText(kControlsButton, Qt::AlignCenter, "Controls");

    if (noAudio_) {
        font.setPixelSize(10);
        font.setBold(false);
        p.setFont(font);
        p.setPen(QColor("#ff7a70"));
        p.drawText(QRectF(188, 42, 100, 16), Qt::AlignRight | Qt::AlignVCenter, "No audio output");
    }
}

void GaugeView::mouseMoveEvent(QMouseEvent* event) {
    setCursor(controlsButtonRect().contains(event->position().toPoint()) ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void GaugeView::mousePressEvent(QMouseEvent* event) {
    if (controlsButtonRect().contains(event->position().toPoint())) emit controlsButtonClicked();
}

void GaugeView::paintGL() {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.drawPixmap(0, 0, caseLayer());
    p.setTransform(designTransform());

    drawCorners(p);

    // Arm and sliding weight.
    p.save();
    p.translate(kPivot);
    p.rotate(frame_->armPosition * kMaxArmDegrees);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#2a2d33"));
    p.drawRoundedRect(QRectF(-2.5, -kArmLength, 5, kArmLength + 8), 2, 2);
    const double weightY = -weightRadius(frame_->bpm);
    QLinearGradient brass(-20, 0, 20, 0);
    brass.setColorAt(0, QColor("#8c6a1f"));
    brass.setColorAt(0.5, QColor("#f0cf6a"));
    brass.setColorAt(1, QColor("#8c6a1f"));
    p.setPen(QPen(QColor("#5c4514"), 1));
    p.setBrush(brass);
    QPainterPath weight;
    weight.moveTo(-14, weightY - 13);
    weight.lineTo(14, weightY - 13);
    weight.lineTo(21, weightY + 13);
    weight.lineTo(-21, weightY + 13);
    weight.closeSubpath();
    p.drawPath(weight);
    p.restore();

    p.setPen(QPen(QColor("#5c4514"), 1));
    p.setBrush(QColor("#d9b856"));
    p.drawEllipse(kPivot, 9, 9);

    // Red beat light, instantly on at the beat.
    const QPointF light(150, 36);
    const double glow = frame_->hasBeat ? anim::flashPulse(frame_->sinceSec, frame_->beatSec, 0.6, 0.22) : 0.0;
    if (glow > 0) {
        QRadialGradient halo(light, 34);
        QColor core(255, 40, 30, int(190 * glow));
        QColor edge(255, 40, 30, 0);
        halo.setColorAt(0.3, core);
        halo.setColorAt(1, edge);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(light, 34, 34);
    }
    QColor lamp;
    lamp.setRgbF(0.28 + 0.72 * glow, 0.04 + 0.2 * glow, 0.04 + 0.14 * glow);
    p.setPen(QPen(QColor("#111"), 2));
    p.setBrush(lamp);
    p.drawEllipse(light, 16, 16);
}
