#include "TempoDial.h"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kSweepDegrees = 280;
}

TempoDial::TempoDial(int minimum, int maximum, QWidget* parent)
    : QWidget(parent), minimum_(minimum), maximum_(maximum), value_(minimum) {
    setMinimumSize(120, 120);
    setCursor(Qt::OpenHandCursor);
}

void TempoDial::setValue(int value) {
    value = std::clamp(value, minimum_, maximum_);
    if (value == value_) return;
    value_ = value;
    update();
}

void TempoDial::edit(int value) {
    value = std::clamp(value, minimum_, maximum_);
    if (value == value_) return;
    value_ = value;
    update();
    emit valueEdited(value_);
}

void TempoDial::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double radius = std::min(width(), height()) / 2.0 - 4;
    p.translate(width() / 2.0, height() / 2.0);

    const double fraction = double(value_ - minimum_) / (maximum_ - minimum_);
    const double startAngle = -kSweepDegrees / 2;

    // Tick ring: ticks up to the current tempo are lit.
    for (int i = 0; i <= 44; ++i) {
        const double t = i / 44.0;
        p.save();
        p.rotate(startAngle + t * kSweepDegrees);
        p.setPen(QPen(t <= fraction ? QColor("#e8a317") : QColor("#555a63"), i % 4 == 0 ? 2.4 : 1.2));
        p.drawLine(QPointF(0, -radius), QPointF(0, -radius + (i % 4 == 0 ? 9 : 5)));
        p.restore();
    }

    const double knob = radius - 14;
    QRadialGradient face(QPointF(-knob * 0.3, -knob * 0.4), knob * 1.6);
    face.setColorAt(0, QColor("#4a4f58"));
    face.setColorAt(1, QColor("#22252a"));
    p.setPen(QPen(QColor("#0f1012"), 1.5));
    p.setBrush(face);
    p.drawEllipse(QPointF(0, 0), knob, knob);

    p.save();
    p.rotate(startAngle + fraction * kSweepDegrees);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#e8a317"));
    p.drawEllipse(QPointF(0, -knob + 10), 4, 4);
    p.restore();

    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(int(knob * 0.5));
    p.setFont(font);
    p.setPen(QColor("#f1f1f1"));
    p.drawText(QRectF(-knob, -knob, knob * 2, knob * 2), Qt::AlignCenter, QString::number(value_));
}

double TempoDial::pointerAngle(QPointF pos) const {
    const QPointF d = pos - QPointF(width() / 2.0, height() / 2.0);
    return qRadiansToDegrees(std::atan2(d.x(), -d.y()));
}

void TempoDial::mousePressEvent(QMouseEvent* event) {
    dragAngle_ = pointerAngle(event->position());
    dragRemainder_ = 0;
}

void TempoDial::mouseMoveEvent(QMouseEvent* event) {
    const double angle = pointerAngle(event->position());
    double delta = angle - dragAngle_;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
    dragAngle_ = angle;
    dragRemainder_ += delta * (maximum_ - minimum_) / kSweepDegrees;
    const int whole = int(dragRemainder_);
    if (whole != 0) {
        dragRemainder_ -= whole;
        edit(value_ + whole);
    }
}

void TempoDial::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() != 0) edit(value_ + (event->angleDelta().y() > 0 ? 1 : -1));
}
