#pragma once
#include "Animation.h"
#include <QOpenGLWidget>
#include <QPoint>

// The sheet-music panel: notes, the moving square and per-beat pitch, volume and sound controls.
class BeatStripView : public QOpenGLWidget {
    Q_OBJECT
public:
    BeatStripView(const FrameState* frame, const SequenceConfig* config, QWidget* parent = nullptr);

    void setSelected(int measure, int beat);
    static int heightForRows(int rows);

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
    int rows() const;
    int beatsIn(int measure) const;
    int denominatorOf(int measure) const;
    double slotWidth() const;
    double rowTop(int measure) const;
    QRectF slotRect(int measure, int beat) const;
    QRectF levelCell(int measure, int beat, int level) const;
    bool hit(QPointF pos, int& measure, int& beat) const;

    void drawControls(QPainter& p, int measure, int beat);
    void drawSubdivisions(QPainter& p, int measure);
    void drawSquare(QPainter& p);
    void drawNote(QPainter& p, int measure, int beat);

    const FrameState* frame_;
    const SequenceConfig* config_;
    int selectedMeasure_ = 0, selectedBeat_ = 0;
};
