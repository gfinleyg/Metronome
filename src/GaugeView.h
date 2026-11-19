#pragma once
#include "Animation.h"
#include <QOpenGLWidget>
#include <QPixmap>
#include <QString>
#include <QTransform>

// The physical metronome: wooden case, swinging arm, sliding weight and red beat light.
// Its top corners also hold the tempo readout, the controls button, the no-audio warning and the
// speed trainer countdown. The countdown changes every beat, so it is drawn here rather than in a
// widget, whose repaint would slow the frame that shows the beat.
class GaugeView : public QOpenGLWidget {
    Q_OBJECT
public:
    static constexpr double kAspect = 284.0 / 428.0;  // Width over height of the drawing.

    explicit GaugeView(const FrameState* frame, QWidget* parent = nullptr);

    void setTempo(int bpm, const QString& name);
    void setControlsOpen(bool open);
    void setNoAudio(bool noAudio);
    void setTrainerStatus(const QString& status);
    QRect controlsButtonRect() const;

signals:
    void controlsButtonClicked();

protected:
    void paintGL() override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QTransform designTransform() const;
    const QPixmap& caseLayer();
    void drawCorners(QPainter& p);

    const FrameState* frame_;
    QPixmap caseLayer_;  // Everything that does not move, so a frame only redraws what does.
    int bpm_ = 0;
    QString tempoName_;
    QString trainerStatus_;
    bool controlsOpen_ = false;
    bool noAudio_ = false;
};
