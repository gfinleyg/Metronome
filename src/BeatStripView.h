#pragma once
#include "Animation.h"
#include <QOpenGLWidget>
#include <QPixmap>
#include <QPoint>

// The sheet-music panel: notes, the moving square and per-beat pitch, volume and sound controls.
class BeatStripView : public QOpenGLWidget {
    Q_OBJECT
public:
    BeatStripView(const FrameState* frame, const SequenceConfig* config, QWidget* parent = nullptr);

    void setSelected(int measure, int beat);

    QRectF noteRect(int measure, int beat) const;
    QRectF pitchRect(int measure, int beat) const;
    QRectF levelRect(int measure, int beat) const;
    QRectF soundRect(int measure, int beat) const;

signals:
    void beatSelected(int measure, int beat);
    void pitchStepped(int measure, int beat, int delta);
    void levelSet(int measure, int beat, int level);
    void soundClicked(int measure, int beat, QPoint globalPos);

protected:
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // Sizes for the current panel and measures. Notes grow with the panel, and their controls grow more slowly.
    struct Metrics {
        double slot;          // Width of one beat.
        double layerSlot;     // Width of one second-measure beat, which differs when it is spaced in time.
        double noteScale;     // Size of the note glyph.
        double controlScale;  // Size of the per-beat controls.
        double noteArea;      // Height of the note area.
        double lineHeight;
        double left, top;  // Where the first slot of the centered block starts.
    };
    // What the note layer was drawn from. A change to any of it redraws the layer.
    struct LayerKey {
        int width = 0, height = 0;
        int rows = 0;
        int overlap = 0;
        int beats[2] = {0, 0};
        int denominator[2] = {0, 0};
        int selectedMeasure = 0, selectedBeat = 0;
        BeatSettings settings[2][kMaxBeats];
        bool operator==(const LayerKey&) const = default;
    };

    Metrics metrics() const;
    Metrics metricsFor(int lines, double widest) const;
    int overlapKind() const;
    int rows() const;
    int beatsIn(int measure) const;
    int denominatorOf(int measure) const;
    int lineOf(int measure, int beat) const;
    QRectF slotRect(int measure, int beat) const;
    QRectF levelCell(int measure, int beat, int level) const;
    bool hit(QPointF pos, int& measure, int& beat) const;
    bool crossesLine(int measure, int beat, int nextMeasure, int nextBeat) const;
    QPointF arcBetween(QPointF from, QPointF to, double phase) const;

    const QPixmap& noteLayer();
    void drawControls(QPainter& p, int measure, int beat);
    void drawSubdivisions(QPainter& p, int measure);
    bool overlapping() const;
    QPointF squareCenter(const BeatEvent& e, double phase) const;
    void drawSquare(QPainter& p, QPointF center, double pulse);
    void drawNote(QPainter& p, int measure, int beat);

    const FrameState* frame_;
    const SequenceConfig* config_;
    int selectedMeasure_ = 0, selectedBeat_ = 0;
    QPixmap noteLayer_;  // Paper, labels and controls, so a frame only redraws what moves.
    LayerKey noteLayerKey_;
};
