#include "BeatStripView.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QWheelEvent>

namespace {
constexpr double kMargin = 12;
constexpr double kLabelWidth = 66;
constexpr double kControlsHeight = 60;
constexpr double kBaseSlot = 130;  // Slot width at which the controls are drawn at scale 1.
constexpr double kMaxControlScale = 2.0;
constexpr double kMinTimedSlot = 60;  // Narrowest second-measure beat when it is spaced in time.
constexpr double kMinNoteScale = 1.3;
// The rest are per unit of note scale.
constexpr double kSlotWidth = 44;
constexpr double kNoteAreaHeight = 54;  // Fits the note at the height of its pulse.
constexpr double kHeadroom = 36;        // Room above the first line for the square's arc.
constexpr double kArcRise = 40;
constexpr double kSquareSize = 34;
constexpr double kDotRadius = 2.3;

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
QPainterPath buildNotePath(int denominator) {
    QPainterPath head;
    head.addEllipse(QPointF(0, 0), 7, 5);
    if (denominator <= 2) head.addEllipse(QPointF(0, 0), 4.6, 2.6);
    head.setFillRule(Qt::OddEvenFill);
    QPainterPath path = QTransform().rotate(-20).map(head);
    path.setFillRule(Qt::OddEvenFill);

    QPainterPath stem;
    stem.addRect(5.4, -30, 1.8, 29);
    path = path.united(stem);

    const int flags = denominator >= 16 ? 2 : denominator >= 8 ? 1 : 0;
    for (int i = 0; i < flags; ++i) {
        const double y = -30 + i * 7;
        QPainterPath flag;
        flag.moveTo(7, y);
        flag.cubicTo(8, y + 8, 17, y + 10, 13, y + 22);
        flag.cubicTo(15, y + 13, 10, y + 10, 7, y + 7);
        flag.closeSubpath();
        path = path.united(flag);
    }
    path.translate(-3, 13);
    return path;
}

// Shared, because the GL paint engine reuses its work for a path it has seen before.
const QPainterPath& notePath(int denominator) {
    static const QPainterPath half = buildNotePath(2), quarter = buildNotePath(4), eighth = buildNotePath(8), sixteenth = buildNotePath(16);
    return denominator <= 2 ? half : denominator >= 16 ? sixteenth : denominator >= 8 ? eighth : quarter;
}
}  // namespace

BeatStripView::BeatStripView(const FrameState* frame, const SequenceConfig* config, QWidget* parent)
    : QOpenGLWidget(parent), frame_(frame), config_(config) {}

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

BeatStripView::Metrics BeatStripView::metrics() const {
    int lines = 0, widest = 1;
    for (int m = 0; m < rows(); ++m) {
        lines += anim::linesFor(beatsIn(m));
        widest = std::max(widest, anim::beatsPerLine(beatsIn(m)));
    }
    // Over a bar of the same length, each second-measure beat sits under the moment in the first
    // measure's bar when it plays, so 3 against 4 can be seen as well as heard. That needs both
    // measures on one line, and beats still wide enough for their controls.
    if (overlapKind() == OverlapSameBar && lines == 2) {
        // A narrower last beat can reach up to half a beat past the first measure's end.
        Metrics r = metricsFor(lines, beatsIn(0) + 0.5);
        r.layerSlot = r.slot * beatsIn(0) / beatsIn(1);
        if (std::min(r.slot, r.layerSlot) >= kMinTimedSlot) return r;
    }
    Metrics r = metricsFor(lines, widest);
    r.layerSlot = r.slot;
    return r;
}

BeatStripView::Metrics BeatStripView::metricsFor(int lines, double widest) const {
    const double columns = std::max(widest, 4.0);
    const double usable = width() - 2 * kMargin;

    // The label grows with the controls, so the slot width takes a second pass to settle.
    const double control = std::clamp((usable - kLabelWidth) / columns / kBaseSlot, 1.0, kMaxControlScale);
    double slot = (usable - kLabelWidth * control) / columns;

    const double controlsHeight = kControlsHeight * control;
    const double fit = (height() - lines * controlsHeight) / (kHeadroom + lines * kNoteAreaHeight);
    const double note = std::max(std::min(slot / kSlotWidth, fit), kMinNoteScale);
    slot = std::min(slot, std::max(note * kSlotWidth, kBaseSlot));

    Metrics r;
    r.slot = slot;
    r.noteScale = note;
    r.controlScale = control;
    r.noteArea = kNoteAreaHeight * note;
    r.lineHeight = r.noteArea + controlsHeight;
    const double headroom = kHeadroom * note;
    const double label = kLabelWidth * control;
    r.left = (width() - label - widest * slot) / 2.0 + label;
    r.top = std::max(0.0, (height() - headroom - lines * r.lineHeight) / 2.0) + headroom;
    return r;
}

int BeatStripView::lineOf(int measure, int beat) const {
    return (measure == 1 ? anim::linesFor(beatsIn(0)) : 0) + beat / anim::beatsPerLine(beatsIn(measure));
}

QRectF BeatStripView::slotRect(int measure, int beat) const {
    const Metrics m = metrics();
    const int column = beat % anim::beatsPerLine(beatsIn(measure));
    const double top = m.top + lineOf(measure, beat) * m.lineHeight;
    if (measure == 1 && m.layerSlot != m.slot) {
        // Centered where this beat falls in the first measure's bar, and no wider than its beats.
        const double center = m.left + (column * m.layerSlot / m.slot + 0.5) * m.slot;
        const double width = std::min(m.slot, m.layerSlot);
        return QRectF(center - width / 2, top, width, m.lineHeight);
    }
    return QRectF(m.left + column * m.slot, top, m.slot, m.lineHeight);
}
QRectF BeatStripView::noteRect(int measure, int beat) const {
    const QRectF slot = slotRect(measure, beat);
    return QRectF(slot.left(), slot.top(), slot.width(), metrics().noteArea);
}
QRectF BeatStripView::pitchRect(int measure, int beat) const {
    const Metrics m = metrics();
    const QRectF slot = slotRect(measure, beat);
    return QRectF(slot.left(), slot.top() + m.noteArea, slot.width(), 20 * m.controlScale);
}
QRectF BeatStripView::levelRect(int measure, int beat) const {
    const Metrics m = metrics();
    const QRectF slot = slotRect(measure, beat);
    const double c = m.controlScale;
    return QRectF(slot.center().x() - 24 * c, slot.top() + m.noteArea + 21 * c, 48 * c, 16 * c);
}
QRectF BeatStripView::soundRect(int measure, int beat) const {
    const Metrics m = metrics();
    const QRectF slot = slotRect(measure, beat);
    const double c = m.controlScale;
    return QRectF(slot.left() + 4, slot.top() + m.noteArea + 39 * c, slot.width() - 8, 17 * c);
}
QRectF BeatStripView::levelCell(int measure, int beat, int level) const {
    const QRectF area = levelRect(measure, beat);
    const double cell = area.width() / 4;
    return QRectF(area.left() + level * cell, area.top(), cell, area.height());
}

void BeatStripView::drawControls(QPainter& p, int measure, int beat) {
    const BeatSettings& s = config_->measure[measure].beat[beat];
    const double c = metrics().controlScale;
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
    font.setPixelSize(int(12 * c));
    font.setBold(true);
    p.setFont(font);
    p.drawText(pitch, Qt::AlignCenter, noteName(s.midiNote));
    const double midY = pitch.center().y();
    const double arrowOffset = std::min(pitch.width() / 2 - 6 * c, 25 * c);
    QPainterPath up, down;
    up.moveTo(pitch.center().x() + arrowOffset - 5 * c, midY + 3 * c);
    up.lineTo(pitch.center().x() + arrowOffset + 5 * c, midY + 3 * c);
    up.lineTo(pitch.center().x() + arrowOffset, midY - 4 * c);
    up.closeSubpath();
    down.moveTo(pitch.center().x() - arrowOffset - 5 * c, midY - 3 * c);
    down.lineTo(pitch.center().x() - arrowOffset + 5 * c, midY - 3 * c);
    down.lineTo(pitch.center().x() - arrowOffset, midY + 4 * c);
    down.closeSubpath();
    p.fillPath(up, kControl);
    p.fillPath(down, kControl);

    // Volume: an off cell, then three rising bars.
    const QRectF off = levelCell(measure, beat, 0).adjusted(2 * c, 4 * c, -2 * c, -4 * c);
    p.setPen(QPen(s.level == 0 ? kRed : kMutedInk, 1.6 * c));
    p.drawLine(off.topLeft(), off.bottomRight());
    p.drawLine(off.topRight(), off.bottomLeft());
    p.setPen(Qt::NoPen);
    for (int level = 1; level <= 3; ++level) {
        const QRectF cell = levelCell(measure, beat, level);
        const double h = (4 + level * 4) * c;
        p.setBrush(s.level >= level ? kInk : QColor("#d4cdbb"));
        p.drawRect(QRectF(cell.left() + 2 * c, cell.bottom() - h, 8 * c, h));
    }

    // Sound name.
    const QRectF sound = soundRect(measure, beat);
    p.setPen(QPen(QColor("#b5ab93"), 1));
    p.setBrush(QColor("#ebe4d2"));
    p.drawRoundedRect(sound, 3, 3);
    font.setPixelSize(int(11 * c));
    font.setBold(false);
    p.setFont(font);
    p.setPen(kInk);
    p.drawText(sound, Qt::AlignCenter, QString::fromUtf8(kSoundShort[std::clamp(s.sound, 0, SoundCount - 1)]));
}

// The square arcs over the page at a line break and at the end of the measures.
bool BeatStripView::crossesLine(int measure, int beat, int nextMeasure, int nextBeat) const {
    return nextBeat == 0 || lineOf(measure, beat) != lineOf(nextMeasure, nextBeat);
}

QPointF BeatStripView::arcBetween(QPointF from, QPointF to, double phase) const {
    const double apex = std::min(from.y(), to.y()) - kArcRise * metrics().noteScale;
    const anim::Point point = anim::arcPoint({from.x(), from.y()}, {to.x(), to.y()}, apex, phase);
    return QPointF(point.x, point.y);
}

// Whether the second measure is playing over the first rather than after it.
bool BeatStripView::overlapping() const { return overlapKind() != OverlapOff; }

int BeatStripView::overlapKind() const {
    if (rows() != 2) return OverlapOff;
    return frame_->hasBeat ? frame_->event.overlap : config_->overlap;
}

void BeatStripView::drawSubdivisions(QPainter& p, int measure) {
    if (measure == 1 && overlapping()) return;  // Only the first measure subdivides when they overlap.
    const int subdivision = frame_->hasBeat ? frame_->event.subdivision : config_->subdivision;
    if (subdivision <= 1) return;
    p.setPen(Qt::NoPen);
    const double radius = kDotRadius * metrics().noteScale;
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
            const QPointF dot = crossesLine(measure, beat, measure, beat + 1) ? arcBetween(a, b, fraction) : QPointF(a.x() + (b.x() - a.x()) * along, a.y());
            p.drawEllipse(dot, radius * (1 + pulse), radius * (1 + pulse));
        }
    }
}

QPointF BeatStripView::squareCenter(const BeatEvent& e, double phase) const {
    const int nextMeasure = std::min(e.nextMeasure, rows() - 1);
    const QPointF from = noteRect(e.measure, e.beat).center();
    const QPointF to = noteRect(nextMeasure, e.nextBeat).center();
    if (crossesLine(e.measure, e.beat, nextMeasure, e.nextBeat)) return arcBetween(from, to, phase);
    return from + (to - from) * anim::squareProgress(phase);
}

void BeatStripView::drawSquare(QPainter& p, QPointF center, double pulse) {
    const double size = kSquareSize * metrics().noteScale * (1.0 + 0.4 * pulse);
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
    else if (frame_->hasLayerBeat && measure == 1 && frame_->layerEvent.beat == beat)
        pulse = anim::notePulse(frame_->layerSinceSec, frame_->layerBeatSec);
    const bool muted = config_->measure[measure].beat[beat].level == 0;
    const QPointF center = noteRect(measure, beat).center();
    const double scale = metrics().noteScale * (1.0 + 0.5 * pulse);
    p.save();
    p.translate(center);
    p.scale(scale, scale);
    p.fillPath(notePath(denominatorOf(measure)), mix(muted ? kMutedInk : kInk, kRed, pulse));
    p.restore();
}

const QPixmap& BeatStripView::noteLayer() {
    const qreal ratio = devicePixelRatioF();
    LayerKey key;
    key.width = int(width() * ratio);
    key.height = int(height() * ratio);
    key.rows = rows();
    key.overlap = overlapKind();
    key.selectedMeasure = selectedMeasure_;
    key.selectedBeat = selectedBeat_;
    for (int m = 0; m < key.rows; ++m) {
        key.beats[m] = beatsIn(m);
        key.denominator[m] = denominatorOf(m);
        for (int b = 0; b < key.beats[m]; ++b) key.settings[m][b] = config_->measure[m].beat[b];
    }
    if (key == noteLayerKey_ && !noteLayer_.isNull()) return noteLayer_;
    noteLayerKey_ = key;

    noteLayer_ = QPixmap(key.width, key.height);
    noteLayer_.setDevicePixelRatio(ratio);
    QPainter p(&noteLayer_);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), kPaper);

    const Metrics metric = metrics();
    const double label = kLabelWidth * metric.controlScale;
    for (int m = 0; m < key.rows; ++m) {
        const double top = slotRect(m, 0).top();
        if (m > 0) {
            p.setPen(QPen(QColor("#cfc7b2"), 1));
            p.drawLine(QPointF(10, top - 1), QPointF(width() - 10, top - 1));
        }
        QFont font = p.font();
        font.setPixelSize(int(20 * metric.controlScale));
        font.setBold(true);
        p.setFont(font);
        p.setPen(kInk);
        p.drawText(QRectF(metric.left - label, top, label - 6, metric.noteArea), Qt::AlignCenter, QString("%1/%2").arg(key.beats[m]).arg(key.denominator[m]));
        for (int b = 0; b < key.beats[m]; ++b) drawControls(p, m, b);
    }
    return noteLayer_;
}

void BeatStripView::paintGL() {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawPixmap(0, 0, noteLayer());

    const int rowCount = rows();
    for (int m = 0; m < rowCount; ++m) drawSubdivisions(p, m);
    // One square per measure that is playing, so overlapping measures each have their own.
    if (frame_->hasBeat) {
        drawSquare(p, squareCenter(frame_->event, frame_->phase), anim::flashPulse(frame_->sinceSec, frame_->beatSec, 0.35, 0.18));
        if (frame_->hasLayerBeat)
            drawSquare(p, squareCenter(frame_->layerEvent, frame_->layerPhase),
                       anim::flashPulse(frame_->layerSinceSec, frame_->layerBeatSec, 0.35, 0.18));
    } else {
        drawSquare(p, noteRect(0, 0).center(), 0);
        if (overlapping()) drawSquare(p, noteRect(1, 0).center(), 0);
    }
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
