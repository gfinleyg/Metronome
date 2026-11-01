#include "GaugeView.h"
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

namespace {
const QPointF kPivot(150, 372);
constexpr double kArmLength = 276;
constexpr double kMaxArmDegrees = 16;

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
}

void GaugeView::paintGL() {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), QColor("#1d1f23"));

    // All drawing happens in a 300x440 design space, scaled to fit.
    const double scale = std::min(width() / 300.0, height() / 440.0);
    p.translate(width() / 2.0, height() / 2.0);
    p.scale(scale, scale);
    p.translate(-150, -220);

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
