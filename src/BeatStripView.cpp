#include "BeatStripView.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QWheelEvent>

namespace {
constexpr double kHeadroom = 44;  // Room above the first row for the square's arc.
constexpr double kRowHeight = 126;
constexpr double kBottomMargin = 6;
constexpr double kLabelWidth = 66;
constexpr double kRightMargin = 12;
constexpr double kNoteAreaHeight = 66;
constexpr double kArcApexY = 28;
constexpr double kSquareSize = 44;
constexpr double kNoteScale = 1.3;

const QColor kPaper("#f4efe2");
const QColor kInk("#111111");
const QColor kMutedInk("#a59f90");
const QColor kRed("#e0201b");
const QColor kAmber("#e8a317");
const QColor kControl("#5d564a");
const char* const kSoundShort[SoundCount] = {"Wood", "Click", "Beep", "Cow", "Hat"};

QColor mix(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QString noteName(int midi) {
    static const char* const names[12] = {"C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"};
    return QString::fromUtf8(names[midi % 12]) + QString::number(midi / 12 - 1);
}

// A note glyph with its visual center at the origin.
QPainterPath notePath(int denominator) {
    QPainterPath head;
    head.addEllipse(QPointF(0, 0), 7, 5);
    if (denominator <= 2) head.addEllipse(QPointF(0, 0), 4.6, 2.6);
    head.setFillRule(Qt::OddEvenFill);
    QPainterPath path = QTransform().rotate(-20).map(head);
    path.setFillRule(Qt::OddEvenFill);

    QPainterPath stem;
    stem.addRect(5.4, -30, 1.8, 29);
    path.addPath(stem);

    const int flags = denominator >= 16 ? 2 : denominator >= 8 ? 1 : 0;
    for (int i = 0; i < flags; ++i) {
        const double y = -30 + i * 7;
        QPainterPath flag;
        flag.moveTo(7, y);
        flag.cubicTo(8, y + 8, 17, y + 10, 13, y + 22);
        flag.cubicTo(15, y + 13, 10, y + 10, 7, y + 7);
        flag.closeSubpath();
        path.addPath(flag);
    }
    path.translate(-3, 13);
    return path;
}
}  // namespace

BeatStripView::BeatStripView(const FrameState* frame, const SequenceConfig* config, QWidget* parent)
    : QOpenGLWidget(parent), frame_(frame), config_(config) {}

int BeatStripView::heightForRows(int rows) { return int(kHeadroom + rows * kRowHeight + kBottomMargin); }

void BeatStripView::setSelected(int measure, int beat) {
    selectedMeasure_ = measure;
    selectedBeat_ = beat;
    update();
}

// While playing, the layout follows what is sounding, so edits appear when they take effect.
int BeatStripView::rows() const {
    const bool second = frame_->hasBeat ? frame_->event.second : config_->secondMeasure;
    return second ? 2 : 1;
}
int BeatStripView::beatsIn(int measure) const {
    return frame_->hasBeat ? frame_->event.beats[measure] : config_->measure[measure].beats;
}
int BeatStripView::denominatorOf(int measure) const {
    return frame_->hasBeat ? frame_->event.denominator[measure] : config_->measure[measure].denominator;
}
double BeatStripView::slotWidth() const {
    int widest = beatsIn(0);
    if (rows() == 2) widest = std::max(widest, beatsIn(1));
    return std::min(170.0, (width() - kLabelWidth - kRightMargin) / std::max(widest, 4));
}
double BeatStripView::rowTop(int measure) const { return kHeadroom + measure * kRowHeight; }

QRectF BeatStripView::slotRect(int measure, int beat) const {
    return QRectF(kLabelWidth + beat * slotWidth(), rowTop(measure), slotWidth(), kRowHeight);
}
QRectF BeatStripView::noteRect(int measure, int beat) const {
    const QRectF slot = slotRect(measure, beat);
    return QRectF(slot.left(), slot.top(), slot.width(), kNoteAreaHeight);
}
QRectF BeatStripView::pitchRect(int measure, int beat) const {
    const QRectF slot = slotRect(measure, beat);
    return QRectF(slot.left(), slot.top() + kNoteAreaHeight, slot.width(), 20);
}
QRectF BeatStripView::levelRect(int measure, int beat) const {
    const QRectF slot = slotRect(measure, beat);
    return QRectF(slot.center().x() - 24, slot.top() + kNoteAreaHeight + 21, 48, 16);
}
QRectF BeatStripView::soundRect(int measure, int beat) const {
    const QRectF slot = slotRect(measure, beat);
    return QRectF(slot.left() + 4, slot.top() + kNoteAreaHeight + 39, slot.width() - 8, 17);
}
QRectF BeatStripView::levelCell(int measure, int beat, int level) const {
    const QRectF area = levelRect(measure, beat);
    return QRectF(area.left() + level * 12, area.top(), 12, area.height());
}

void BeatStripView::drawControls(QPainter& p, int measure, int beat) {
    const BeatSettings& s = config_->measure[measure].beat[beat];
    const bool selected = measure == selectedMeasure_ && beat == selectedBeat_;
    if (selected) {
        p.setPen(QPen(QColor("#b5ab93"), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(slotRect(measure, beat).adjusted(2, 1, -2, -3), 5, 5);
    }

    // Pitch: down arrow, name, up arrow.
    const QRectF pitch = pitchRect(measure, beat);
    p.setPen(kInk);
    QFont font = p.font();
    font.setPixelSize(12);
    font.setBold(true);
    p.setFont(font);
    p.drawText(pitch, Qt::AlignCenter, noteName(s.midiNote));
    const double midY = pitch.center().y();
    const double arrowOffset = std::min(pitch.width() / 2 - 6, 25.0);
    QPainterPath up, down;
    up.moveTo(pitch.center().x() + arrowOffset - 5, midY + 3);
    up.lineTo(pitch.center().x() + arrowOffset + 5, midY + 3);
    up.lineTo(pitch.center().x() + arrowOffset, midY - 4);
    up.closeSubpath();
    down.moveTo(pitch.center().x() - arrowOffset - 5, midY - 3);
    down.lineTo(pitch.center().x() - arrowOffset + 5, midY - 3);
    down.lineTo(pitch.center().x() - arrowOffset, midY + 4);
    down.closeSubpath();
    p.fillPath(up, kControl);
    p.fillPath(down, kControl);

    // Volume: an off cell, then three rising bars.
    const QRectF off = levelCell(measure, beat, 0).adjusted(2, 4, -2, -4);
    p.setPen(QPen(s.level == 0 ? kRed : kMutedInk, 1.6));
    p.drawLine(off.topLeft(), off.bottomRight());
    p.drawLine(off.topRight(), off.bottomLeft());
    p.setPen(Qt::NoPen);
    for (int level = 1; level <= 3; ++level) {
        const QRectF cell = levelCell(measure, beat, level);
        const double h = 4 + level * 4;
        p.setBrush(s.level >= level ? kInk : QColor("#d4cdbb"));
        p.drawRect(QRectF(cell.left() + 2, cell.bottom() - h, 8, h));
    }

    // Sound name.
    const QRectF sound = soundRect(measure, beat);
    p.setPen(QPen(QColor("#b5ab93"), 1));
    p.setBrush(QColor("#ebe4d2"));
    p.drawRoundedRect(sound, 3, 3);
    font.setPixelSize(11);
    font.setBold(false);
    p.setFont(font);
    p.setPen(kInk);
    p.drawText(sound, Qt::AlignCenter, QString::fromUtf8(kSoundShort[std::clamp(s.sound, 0, SoundCount - 1)]));
}

void BeatStripView::drawSubdivisions(QPainter& p, int measure) {
    const int subdivision = frame_->hasBeat ? frame_->event.subdivision : config_->subdivision;
    if (subdivision <= 1) return;
    p.setPen(Qt::NoPen);
    const int count = beatsIn(measure);
    for (int beat = 0; beat + 1 < count; ++beat) {
        const QPointF a = noteRect(measure, beat).center();
        const QPointF b = noteRect(measure, beat + 1).center();
        const bool active = frame_->hasBeat && frame_->event.measure == measure && frame_->event.beat == beat;
        for (int k = 1; k < subdivision; ++k) {
            const double fraction = double(k) / subdivision;
            // Sits where the square will be at the moment this subdivision sounds.
            const double along = anim::squareProgress(fraction);
            double pulse = 0;
            if (active) pulse = anim::flashPulse(frame_->sinceSec - fraction * frame_->beatSec, frame_->beatSec, 0.25, 0.12);
            p.setBrush(mix(kMutedInk, kRed, pulse));
            p.drawEllipse(QPointF(a.x() + (b.x() - a.x()) * along, a.y()), 3 + 3 * pulse, 3 + 3 * pulse);
        }
    }
}

void BeatStripView::drawSquare(QPainter& p) {
    QPointF center = noteRect(0, 0).center();
    double pulse = 0;
    if (frame_->hasBeat) {
        const BeatEvent& e = frame_->event;
        const int nextMeasure = std::min(e.nextMeasure, rows() - 1);
        const QPointF from = noteRect(e.measure, e.beat).center();
        const QPointF to = noteRect(nextMeasure, e.nextBeat).center();
        if (e.nextBeat == 0) {
            const anim::Point point = anim::arcPoint({from.x(), from.y()}, {to.x(), to.y()}, kArcApexY, frame_->phase);
            center = QPointF(point.x, point.y);
        } else {
            center = from + (to - from) * anim::squareProgress(frame_->phase);
        }
        pulse = anim::flashPulse(frame_->sinceSec, frame_->beatSec, 0.35, 0.18);
    }
    const double size = kSquareSize * (1.0 + 0.4 * pulse);
    QColor fill = kAmber;
    fill.setAlphaF(float(0.2 + 0.55 * pulse));
    p.setPen(QPen(kAmber.darker(100 + int(25 * (1 - pulse))), 2 + pulse));
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(center.x() - size / 2, center.y() - size / 2, size, size), 5, 5);
}

void BeatStripView::drawNote(QPainter& p, int measure, int beat) {
    double pulse = 0;
    if (frame_->hasBeat && frame_->event.measure == measure && frame_->event.beat == beat)
        pulse = anim::notePulse(frame_->sinceSec, frame_->beatSec);
    const bool muted = config_->measure[measure].beat[beat].level == 0;
    const QPointF center = noteRect(measure, beat).center();
    const double scale = kNoteScale * (1.0 + 0.5 * pulse);
    p.save();
    p.translate(center);
    p.scale(scale, scale);
    p.fillPath(notePath(denominatorOf(measure)), mix(muted ? kMutedInk : kInk, kRed, pulse));
    p.restore();
}

void BeatStripView::paintGL() {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), kPaper);

    const int rowCount = rows();
    for (int m = 0; m < rowCount; ++m) {
        if (m > 0) {
            p.setPen(QPen(QColor("#cfc7b2"), 1));
            p.drawLine(QPointF(10, rowTop(m) - 1), QPointF(width() - 10, rowTop(m) - 1));
        }
        QFont font = p.font();
        font.setPixelSize(20);
        font.setBold(true);
        p.setFont(font);
        p.setPen(kInk);
        const QRectF noteArea(8, rowTop(m), kLabelWidth - 12, kNoteAreaHeight);
        p.drawText(noteArea, Qt::AlignCenter, QString("%1/%2").arg(beatsIn(m)).arg(denominatorOf(m)));
        for (int b = 0; b < beatsIn(m); ++b) drawControls(p, m, b);
        drawSubdivisions(p, m);
    }
    drawSquare(p);
    for (int m = 0; m < rowCount; ++m)
        for (int b = 0; b < beatsIn(m); ++b) drawNote(p, m, b);
}

bool BeatStripView::hit(QPointF pos, int& measure, int& beat) const {
    for (int m = 0; m < rows(); ++m) {
        for (int b = 0; b < beatsIn(m); ++b) {
            if (slotRect(m, b).contains(pos)) {
                measure = m;
                beat = b;
                return true;
            }
        }
    }
    return false;
}

void BeatStripView::mousePressEvent(QMouseEvent* event) {
    int m, b;
    const QPointF pos = event->position();
    if (!hit(pos, m, b)) return;
    emit beatSelected(m, b);
    const QRectF pitch = pitchRect(m, b);
    if (pitch.contains(pos)) {
        emit pitchStepped(m, b, pos.x() < pitch.center().x() ? -1 : 1);
    } else if (levelRect(m, b).contains(pos)) {
        for (int level = 0; level <= 3; ++level)
            if (levelCell(m, b, level).contains(pos)) emit levelSet(m, b, level);
    } else if (soundRect(m, b).contains(pos)) {
        emit soundClicked(m, b, event->globalPosition().toPoint());
    }
}

void BeatStripView::wheelEvent(QWheelEvent* event) {
    int m, b;
    const QPointF pos = event->position();
    if (hit(pos, m, b) && pitchRect(m, b).contains(pos) && event->angleDelta().y() != 0)
        emit pitchStepped(m, b, event->angleDelta().y() > 0 ? 1 : -1);
}
